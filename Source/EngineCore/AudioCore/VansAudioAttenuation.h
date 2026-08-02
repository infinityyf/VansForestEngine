#pragma once

#include <algorithm>
#include <cmath>
#include <string>

namespace VansEngine
{
    enum class AudioAttenuationMode
    {
        Linear,
        Inverse,
        Exponential
    };

    inline AudioAttenuationMode AudioAttenuationModeFromString(const std::string& mode)
    {
        if (mode == "inverse") return AudioAttenuationMode::Inverse;
        if (mode == "exponential") return AudioAttenuationMode::Exponential;
        return AudioAttenuationMode::Linear;
    }

    inline const char* AudioAttenuationModeToString(AudioAttenuationMode mode)
    {
        switch (mode)
        {
        case AudioAttenuationMode::Inverse: return "inverse";
        case AudioAttenuationMode::Exponential: return "exponential";
        case AudioAttenuationMode::Linear:
        default:
            return "linear";
        }
    }

    struct AudioAttenuationSettings
    {
        AudioAttenuationMode mode = AudioAttenuationMode::Linear;
        float referenceDistance = 1.0f;
        float maxDistance = 100.0f;
        float rolloff = 1.0f;

        void Normalize()
        {
            referenceDistance = std::max(referenceDistance, 0.01f);
            maxDistance = std::max(maxDistance, referenceDistance + 0.01f);
            rolloff = std::max(rolloff, 0.0f);
        }
    };

    inline float ComputeDistanceGain(float distance, AudioAttenuationSettings settings)
    {
        settings.Normalize();
        distance = std::max(distance, 0.0f);

        if (distance <= settings.referenceDistance || settings.rolloff <= 0.0f)
            return 1.0f;
        if (distance >= settings.maxDistance)
            return 0.0f;

        const float clampedDistance = std::clamp(distance, settings.referenceDistance, settings.maxDistance);
        switch (settings.mode)
        {
        case AudioAttenuationMode::Inverse:
        {
            const float denominator = settings.referenceDistance +
                settings.rolloff * (clampedDistance - settings.referenceDistance);
            return std::clamp(settings.referenceDistance / denominator, 0.0f, 1.0f);
        }
        case AudioAttenuationMode::Exponential:
        {
            const float ratio = clampedDistance / settings.referenceDistance;
            return std::clamp(std::pow(ratio, -settings.rolloff), 0.0f, 1.0f);
        }
        case AudioAttenuationMode::Linear:
        default:
        {
            const float range = settings.maxDistance - settings.referenceDistance;
            const float normalized = (clampedDistance - settings.referenceDistance) / range;
            return std::clamp(1.0f - normalized * settings.rolloff, 0.0f, 1.0f);
        }
        }
    }
}
