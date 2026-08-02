#include "VansAudioMixConfig.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace VansEngine
{
namespace
{
    using Json = nlohmann::json;

    float ClampFloat(float value, float minValue, float maxValue)
    {
        return std::clamp(value, minValue, maxValue);
    }

    AudioMixBusConfig DecodeBus(const std::string& name, const Json& json)
    {
        AudioMixBusConfig bus;
        bus.busName = NormalizeAudioBusName(json.value("bus", json.value("name", name)));
        bus.gain = ClampFloat(json.value("gain", bus.gain), 0.0f, 4.0f);
        bus.muted = json.value("muted", bus.muted);
        bus.soloed = json.value("soloed", bus.soloed);
        bus.lowpassHighFrequencyGain = ClampFloat(
            json.value("lowpassHighFrequencyGain", bus.lowpassHighFrequencyGain),
            0.0f,
            1.0f);
        return bus;
    }

    AudioBusSnapshotEntry DecodeSnapshotEntry(const std::string& name, const Json& json)
    {
        AudioBusSnapshotEntry entry;
        entry.busName = NormalizeAudioBusName(json.value("bus", json.value("name", name)));
        entry.gain = ClampFloat(json.value("gain", entry.gain), 0.0f, 4.0f);
        if (json.contains("muted"))
        {
            entry.overrideMuted = true;
            entry.muted = json.value("muted", entry.muted);
        }
        if (json.contains("soloed"))
        {
            entry.overrideSoloed = true;
            entry.soloed = json.value("soloed", entry.soloed);
        }
        if (json.contains("lowpassHighFrequencyGain"))
        {
            entry.overrideLowpassHighFrequencyGain = true;
            entry.lowpassHighFrequencyGain = ClampFloat(
                json.value("lowpassHighFrequencyGain", entry.lowpassHighFrequencyGain),
                0.0f,
                1.0f);
        }
        return entry;
    }

    AudioBusSnapshot DecodeSnapshot(const Json& json)
    {
        AudioBusSnapshot snapshot;
        snapshot.fadeSeconds = ClampFloat(json.value("fadeSeconds", snapshot.fadeSeconds), 0.0f, 10.0f);
        if (const auto busesIt = json.find("buses"); busesIt != json.end())
        {
            if (busesIt->is_array())
            {
                for (const Json& item : *busesIt)
                {
                    if (item.is_object())
                        snapshot.buses.push_back(DecodeSnapshotEntry({}, item));
                }
            }
            else if (busesIt->is_object())
            {
                for (const auto& item : busesIt->items())
                {
                    if (item.value().is_object())
                        snapshot.buses.push_back(DecodeSnapshotEntry(item.key(), item.value()));
                }
            }
        }
        return snapshot;
    }

    AudioDuckingRule DecodeDuckingRule(const Json& json)
    {
        AudioDuckingRule rule;
        rule.triggerBusName = json.value("triggerBus", json.value("trigger", rule.triggerBusName));
        rule.targetBusName = json.value("targetBus", json.value("target", rule.targetBusName));
        rule.targetGain = json.value("targetGain", json.value("gain", rule.targetGain));
        rule.attackSeconds = json.value("attackSeconds", json.value("attack", rule.attackSeconds));
        rule.releaseSeconds = json.value("releaseSeconds", json.value("release", rule.releaseSeconds));
        rule.enabled = json.value("enabled", rule.enabled);
        rule.Normalize();
        return rule;
    }
}

bool VansAudioMixConfigStorage::Load(
    const std::filesystem::path& path,
    AudioMixConfig& config,
    std::string& error)
{
    config = {};
    error.clear();
    try
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            error = "Cannot read audio mix config: " + path.string();
            return false;
        }

        Json root;
        file >> root;
        if (!root.is_object())
        {
            error = "Audio mix config root must be an object: " + path.string();
            return false;
        }

        config.displayName = root.value("displayName", root.value("name", ""));
        config.defaultSnapshot = root.value("defaultSnapshot", "");

        if (const auto busesIt = root.find("buses"); busesIt != root.end())
        {
            if (busesIt->is_array())
            {
                for (const Json& item : *busesIt)
                {
                    if (item.is_object())
                        config.buses.push_back(DecodeBus({}, item));
                }
            }
            else if (busesIt->is_object())
            {
                for (const auto& item : busesIt->items())
                {
                    if (item.value().is_object())
                        config.buses.push_back(DecodeBus(item.key(), item.value()));
                }
            }
        }

        if (const auto snapshotsIt = root.find("snapshots");
            snapshotsIt != root.end() && snapshotsIt->is_object())
        {
            for (const auto& item : snapshotsIt->items())
            {
                if (item.value().is_object())
                    config.snapshots.emplace(item.key(), DecodeSnapshot(item.value()));
            }
        }

        if (const auto duckingIt = root.find("ducking");
            duckingIt != root.end() && duckingIt->is_array())
        {
            for (const Json& item : *duckingIt)
            {
                if (item.is_object())
                    config.duckingRules.push_back(DecodeDuckingRule(item));
            }
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}
}
