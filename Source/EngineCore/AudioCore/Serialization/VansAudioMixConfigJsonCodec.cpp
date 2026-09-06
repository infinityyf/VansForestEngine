#include "VansAudioMixConfigJsonCodec.h"

#include "../VansAudioMixConfig.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace VansEngine
{
namespace
{
using Json = nlohmann::json;

float ClampFloat(float value, float minimum, float maximum)
{
	return std::clamp(value, minimum, maximum);
}

bool DecodeBus(const std::string& name, const Json& root, AudioMixBusConfig& bus, std::string& error)
{
	if (!root.is_object())
	{
		error = "Audio mix bus '" + name + "' must be an object";
		return false;
	}
	bus = {};
	bus.busName = NormalizeAudioBusName(name);
	bus.gain = ClampFloat(root.value("gain", bus.gain), 0.0f, 4.0f);
	bus.muted = root.value("muted", bus.muted);
	bus.soloed = root.value("soloed", bus.soloed);
	bus.lowpassHighFrequencyGain = ClampFloat(
		root.value("lowpassHighFrequencyGain", bus.lowpassHighFrequencyGain), 0.0f, 1.0f);
	return !bus.busName.empty();
}

bool DecodeSnapshotEntry(
	const std::string& name,
	const Json& root,
	AudioBusSnapshotEntry& entry,
	std::string& error)
{
	if (!root.is_object())
	{
		error = "Audio snapshot bus '" + name + "' must be an object";
		return false;
	}
	entry = {};
	entry.busName = NormalizeAudioBusName(name);
	entry.gain = ClampFloat(root.value("gain", entry.gain), 0.0f, 4.0f);
	if (root.contains("muted"))
	{
		entry.overrideMuted = true;
		entry.muted = root.at("muted").get<bool>();
	}
	if (root.contains("soloed"))
	{
		entry.overrideSoloed = true;
		entry.soloed = root.at("soloed").get<bool>();
	}
	if (root.contains("lowpassHighFrequencyGain"))
	{
		entry.overrideLowpassHighFrequencyGain = true;
		entry.lowpassHighFrequencyGain = ClampFloat(
			root.at("lowpassHighFrequencyGain").get<float>(), 0.0f, 1.0f);
	}
	return !entry.busName.empty();
}

Json EncodeSnapshotEntry(const AudioBusSnapshotEntry& entry)
{
	Json root = { { "gain", entry.gain } };
	if (entry.overrideMuted) root["muted"] = entry.muted;
	if (entry.overrideSoloed) root["soloed"] = entry.soloed;
	if (entry.overrideLowpassHighFrequencyGain)
		root["lowpassHighFrequencyGain"] = entry.lowpassHighFrequencyGain;
	return root;
}
}

bool VansAudioMixConfigJsonCodec::Decode(
	const nlohmann::json& root,
	AudioMixConfig& config,
	std::string& error)
{
	config = {};
	error.clear();
	try
	{
		if (!root.is_object())
		{
			error = "Audio mix config root must be an object";
			return false;
		}
		config.displayName = root.value("displayName", "");
		config.defaultSnapshot = root.value("defaultSnapshot", "");

		if (const auto buses = root.find("buses"); buses != root.end())
		{
			if (!buses->is_object())
			{
				error = "Audio mix 'buses' must be an object keyed by bus name";
				return false;
			}
			for (const auto& item : buses->items())
			{
				AudioMixBusConfig bus;
				if (!DecodeBus(item.key(), item.value(), bus, error)) return false;
				config.buses.push_back(std::move(bus));
			}
		}

		if (const auto snapshots = root.find("snapshots"); snapshots != root.end())
		{
			if (!snapshots->is_object())
			{
				error = "Audio mix 'snapshots' must be an object";
				return false;
			}
			for (const auto& item : snapshots->items())
			{
				if (!item.value().is_object())
				{
					error = "Audio snapshot '" + item.key() + "' must be an object";
					return false;
				}
				AudioBusSnapshot snapshot;
				snapshot.fadeSeconds = ClampFloat(
					item.value().value("fadeSeconds", snapshot.fadeSeconds), 0.0f, 10.0f);
				const auto buses = item.value().find("buses");
				if (buses != item.value().end())
				{
					if (!buses->is_object())
					{
						error = "Audio snapshot '" + item.key() + "' buses must be an object";
						return false;
					}
					for (const auto& busItem : buses->items())
					{
						AudioBusSnapshotEntry entry;
						if (!DecodeSnapshotEntry(busItem.key(), busItem.value(), entry, error)) return false;
						snapshot.buses.push_back(std::move(entry));
					}
				}
				config.snapshots.emplace(item.key(), std::move(snapshot));
			}
		}

		if (const auto ducking = root.find("ducking"); ducking != root.end())
		{
			if (!ducking->is_array())
			{
				error = "Audio mix 'ducking' must be an array";
				return false;
			}
			for (const Json& item : *ducking)
			{
				if (!item.is_object())
				{
					error = "Audio ducking rule must be an object";
					return false;
				}
				AudioDuckingRule rule;
				rule.triggerBusName = item.value("triggerBus", "");
				rule.targetBusName = item.value("targetBus", "");
				rule.targetGain = item.value("targetGain", rule.targetGain);
				rule.attackSeconds = item.value("attackSeconds", rule.attackSeconds);
				rule.releaseSeconds = item.value("releaseSeconds", rule.releaseSeconds);
				rule.enabled = item.value("enabled", rule.enabled);
				if (rule.triggerBusName.empty() || rule.targetBusName.empty())
				{
					error = "Audio ducking rule requires triggerBus and targetBus";
					return false;
				}
				rule.Normalize();
				config.duckingRules.push_back(std::move(rule));
			}
		}
		return true;
	}
	catch (const std::exception& exception)
	{
		error = exception.what();
		config = {};
		return false;
	}
}

nlohmann::json VansAudioMixConfigJsonCodec::Encode(const AudioMixConfig& config)
{
	Json buses = Json::object();
	for (const AudioMixBusConfig& bus : config.buses)
	{
		buses[bus.busName] = {
			{ "gain", bus.gain },
			{ "muted", bus.muted },
			{ "soloed", bus.soloed },
			{ "lowpassHighFrequencyGain", bus.lowpassHighFrequencyGain }
		};
	}

	Json snapshots = Json::object();
	for (const auto& [name, snapshot] : config.snapshots)
	{
		Json snapshotBuses = Json::object();
		for (const AudioBusSnapshotEntry& entry : snapshot.buses)
			snapshotBuses[entry.busName] = EncodeSnapshotEntry(entry);
		snapshots[name] = {
			{ "fadeSeconds", snapshot.fadeSeconds },
			{ "buses", std::move(snapshotBuses) }
		};
	}

	Json ducking = Json::array();
	for (const AudioDuckingRule& rule : config.duckingRules)
	{
		ducking.push_back({
			{ "triggerBus", rule.triggerBusName },
			{ "targetBus", rule.targetBusName },
			{ "targetGain", rule.targetGain },
			{ "attackSeconds", rule.attackSeconds },
			{ "releaseSeconds", rule.releaseSeconds },
			{ "enabled", rule.enabled }
		});
	}

	return {
		{ "displayName", config.displayName },
		{ "defaultSnapshot", config.defaultSnapshot },
		{ "buses", std::move(buses) },
		{ "snapshots", std::move(snapshots) },
		{ "ducking", std::move(ducking) }
	};
}
}
