#include "VansAudioMixConfigJsonCodec.h"
#include "VansAudioMixValueCodec.h"

#include "../VansAudioMixConfig.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

bool DecodeBus(const std::string& name, const Json& root, VansAudioMixBusConfig& bus, std::string& error)
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

bool DecodeDevice(const Json& root, VansAudioDeviceConfig& config, std::string& error)
{
	if (!root.is_object())
	{
		error = "Audio mix 'device' must be an object";
		return false;
	}
	for (const char* key : { "hrtf", "outputDevice", "masterGain", "sourceLimit" })
	{
		if (!root.contains(key))
		{
			error = std::string("Audio mix 'device.") + key + "' is required";
			return false;
		}
	}
	if (!root["hrtf"].is_string() || !root["outputDevice"].is_string() ||
		!root["masterGain"].is_number() || !root["sourceLimit"].is_number_integer())
	{
		error = "Audio mix device fields have invalid types";
		return false;
	}

	const std::string hrtf = root["hrtf"].get<std::string>();
	if (hrtf == "disabled") config.m_HrtfMode = VansAudioHrtfMode::Disabled;
	else if (hrtf == "enabled") config.m_HrtfMode = VansAudioHrtfMode::Enabled;
	else if (hrtf == "required") config.m_HrtfMode = VansAudioHrtfMode::Required;
	else
	{
		error = "Audio mix 'device.hrtf' must be disabled, enabled, or required";
		return false;
	}

	config.m_OutputDevice = root["outputDevice"].get<std::string>();
	config.m_MasterGain = root["masterGain"].get<float>();
	const std::int64_t sourceLimit = root["sourceLimit"].get<std::int64_t>();
	if (!std::isfinite(config.m_MasterGain) || config.m_MasterGain < 0.0f || config.m_MasterGain > 1.0f)
	{
		error = "Audio mix 'device.masterGain' must be between 0 and 1";
		return false;
	}
	if (sourceLimit < 1 || sourceLimit > 256)
	{
		error = "Audio mix 'device.sourceLimit' must be between 1 and 256";
		return false;
	}
	config.m_SourceLimit = static_cast<std::size_t>(sourceLimit);
	return true;
}

}

bool VansAudioMixConfigJsonCodec::Decode(
	const nlohmann::json& root,
	VansAudioMixConfig& config,
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
		const auto device = root.find("device");
		if (device == root.end())
		{
			error = "Audio mix 'device' is required";
			return false;
		}
		if (!DecodeDevice(*device, config.device, error))
			return false;

		if (const auto buses = root.find("buses"); buses != root.end())
		{
			if (!buses->is_object())
			{
				error = "Audio mix 'buses' must be an object keyed by bus name";
				return false;
			}
			for (const auto& item : buses->items())
			{
				VansAudioMixBusConfig bus;
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
				AudioBusSnapshot snapshot;
				if (!VansAudioMixValueCodec::DecodeSnapshot(
					Vans::DecodeSerializedValueJson(item.value()), snapshot, error))
				{
					error = "Audio snapshot '" + item.key() + "': " + error;
					return false;
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
				AudioDuckingRule rule;
				if (!VansAudioMixValueCodec::DecodeDuckingRule(
					Vans::DecodeSerializedValueJson(item), rule, error))
				{
					return false;
				}
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

nlohmann::json VansAudioMixConfigJsonCodec::Encode(const VansAudioMixConfig& config)
{
	Json buses = Json::object();
	for (const VansAudioMixBusConfig& bus : config.buses)
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
		snapshots[name] = Vans::EncodeSerializedValueJson<Json>(
			VansAudioMixValueCodec::EncodeSnapshot(snapshot));

	Json ducking = Json::array();
	for (const AudioDuckingRule& rule : config.duckingRules)
		ducking.push_back(Vans::EncodeSerializedValueJson<Json>(
			VansAudioMixValueCodec::EncodeDuckingRule(rule)));

	return {
		{ "displayName", config.displayName },
		{ "device", {
			{ "hrtf", VansAudioHrtfModeName(config.device.m_HrtfMode) },
			{ "outputDevice", config.device.m_OutputDevice },
			{ "masterGain", config.device.m_MasterGain },
			{ "sourceLimit", config.device.m_SourceLimit }
		} },
		{ "defaultSnapshot", config.defaultSnapshot },
		{ "buses", std::move(buses) },
		{ "snapshots", std::move(snapshots) },
		{ "ducking", std::move(ducking) }
	};
}
}
