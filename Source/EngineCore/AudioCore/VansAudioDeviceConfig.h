#pragma once

#include <algorithm>
#include <cstddef>
#include <string>

namespace VansEngine
{
    enum class VansAudioHrtfMode
    {
        Disabled,
        Enabled,
        Required
    };

    inline const char* VansAudioHrtfModeName(VansAudioHrtfMode mode)
    {
        switch (mode)
        {
        case VansAudioHrtfMode::Disabled: return "disabled";
        case VansAudioHrtfMode::Enabled: return "enabled";
        case VansAudioHrtfMode::Required: return "required";
        default: return "enabled";
        }
    }

    struct VansAudioDeviceConfig
    {
        VansAudioHrtfMode m_HrtfMode = VansAudioHrtfMode::Enabled;
        std::string m_OutputDevice;
        float m_MasterGain = 1.0f;
        std::size_t m_SourceLimit = 128;
    };

    inline VansAudioDeviceConfig NormalizeAudioDeviceConfig(VansAudioDeviceConfig config)
    {
        config.m_MasterGain = std::clamp(config.m_MasterGain, 0.0f, 1.0f);
        config.m_SourceLimit = std::clamp<std::size_t>(config.m_SourceLimit, 1, 256);
        return config;
    }
}
