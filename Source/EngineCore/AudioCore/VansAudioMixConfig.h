#pragma once

#include "VansAudioBus.h"
#include "VansAudioDeviceConfig.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansEngine
{
    struct VansAudioMixBusConfig
    {
        std::string busName = "SFX";
        float gain = 1.0f;
        bool muted = false;
        bool soloed = false;
        float lowpassHighFrequencyGain = 1.0f;
    };

    struct VansAudioMixConfig
    {
        std::string displayName;
        VansAudioDeviceConfig device;
        std::vector<VansAudioMixBusConfig> buses;
        std::unordered_map<std::string, AudioBusSnapshot> snapshots;
        std::string defaultSnapshot;
        std::vector<AudioDuckingRule> duckingRules;
    };

    class VansAudioMixConfigStorage
    {
    public:
        static bool Load(
            const std::filesystem::path& path,
            VansAudioMixConfig& config,
            std::string& error);
		static bool SaveAtomic(
			const std::filesystem::path& path,
			const VansAudioMixConfig& config,
			std::string& error);
    };
}
