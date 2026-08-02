#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace VansEngine
{
    enum class AudioReverbPreset
    {
        Generic,
        Room,
        Hall,
        Cave,
        Underwater
    };

    struct AudioReverbPresetParameters
    {
        float density = 1.0f;
        float diffusion = 1.0f;
        float gain = 0.32f;
        float gainHF = 0.89f;
        float decayTime = 1.49f;

        void Normalize()
        {
            density = std::clamp(density, 0.0f, 1.0f);
            diffusion = std::clamp(diffusion, 0.0f, 1.0f);
            gain = std::clamp(gain, 0.0f, 1.0f);
            gainHF = std::clamp(gainHF, 0.0f, 1.0f);
            decayTime = std::clamp(decayTime, 0.1f, 20.0f);
        }
    };

    inline AudioReverbPresetParameters NormalizeAudioReverbPresetParameters(
        AudioReverbPresetParameters parameters)
    {
        parameters.Normalize();
        return parameters;
    }

    inline bool AudioReverbPresetParametersNearlyEqual(
        AudioReverbPresetParameters left,
        AudioReverbPresetParameters right,
        float tolerance = 0.0005f)
    {
        left.Normalize();
        right.Normalize();
        return std::fabs(left.density - right.density) <= tolerance &&
            std::fabs(left.diffusion - right.diffusion) <= tolerance &&
            std::fabs(left.gain - right.gain) <= tolerance &&
            std::fabs(left.gainHF - right.gainHF) <= tolerance &&
            std::fabs(left.decayTime - right.decayTime) <= tolerance;
    }

    inline std::string NormalizeAudioReverbPresetName(const std::string& preset)
    {
        std::string lowered;
        lowered.reserve(preset.size());
        for (const char c : preset)
        {
            if (!std::isspace(static_cast<unsigned char>(c)) &&
                c != '-' &&
                c != '_')
            {
                lowered.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c))));
            }
        }

        if (lowered == "room") return "room";
        if (lowered == "hall") return "hall";
        if (lowered == "cave") return "cave";
        if (lowered == "underwater") return "underwater";
        return "generic";
    }

    inline AudioReverbPreset AudioReverbPresetFromString(const std::string& preset)
    {
        const std::string normalized = NormalizeAudioReverbPresetName(preset);
        if (normalized == "room") return AudioReverbPreset::Room;
        if (normalized == "hall") return AudioReverbPreset::Hall;
        if (normalized == "cave") return AudioReverbPreset::Cave;
        if (normalized == "underwater") return AudioReverbPreset::Underwater;
        return AudioReverbPreset::Generic;
    }

    inline const char* AudioReverbPresetToString(AudioReverbPreset preset)
    {
        switch (preset)
        {
        case AudioReverbPreset::Room: return "room";
        case AudioReverbPreset::Hall: return "hall";
        case AudioReverbPreset::Cave: return "cave";
        case AudioReverbPreset::Underwater: return "underwater";
        case AudioReverbPreset::Generic:
        default:
            return "generic";
        }
    }

    inline AudioReverbPresetParameters GetAudioReverbPresetParameters(AudioReverbPreset preset)
    {
        switch (preset)
        {
        case AudioReverbPreset::Room:
            return AudioReverbPresetParameters{
                0.85f,
                0.78f,
                0.20f,
                0.75f,
                0.72f };
        case AudioReverbPreset::Hall:
            return AudioReverbPresetParameters{
                1.0f,
                1.0f,
                0.38f,
                0.82f,
                3.20f };
        case AudioReverbPreset::Cave:
            return AudioReverbPresetParameters{
                1.0f,
                0.92f,
                0.48f,
                0.62f,
                4.80f };
        case AudioReverbPreset::Underwater:
            return AudioReverbPresetParameters{
                0.70f,
                0.45f,
                0.24f,
                0.12f,
                1.60f };
        case AudioReverbPreset::Generic:
        default:
            return AudioReverbPresetParameters{};
        }
    }
}
