#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <limits>

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

    struct AudioDistanceGainKey
    {
        float distance = 0, gain = 1, tangentIn = 0, tangentOut = 0;
    };

    inline float ComputeDistanceGain(float distance, const std::vector<AudioDistanceGainKey>& keys)
    {
        if (!std::isfinite(distance) || keys.empty() || keys.size()>32)
            return std::numeric_limits<float>::quiet_NaN();
        float previous=-1;
        for (const auto& key : keys)
        {
            if (!std::isfinite(key.distance) || key.distance<0 || key.distance<=previous
                || !std::isfinite(key.gain) || key.gain<0 || key.gain>4
                || !std::isfinite(key.tangentIn) || !std::isfinite(key.tangentOut))
                return std::numeric_limits<float>::quiet_NaN();
            previous=key.distance;
        }
        if (distance<=keys.front().distance) return keys.front().gain;
        for (std::size_t i=1;i<keys.size();++i)
        {
            const auto& a=keys[i-1]; const auto& b=keys[i];
            if (distance>b.distance) continue;
            const float width=b.distance-a.distance, t=(distance-a.distance)/width, t2=t*t, t3=t2*t;
            return std::clamp((2*t3-3*t2+1)*a.gain+(t3-2*t2+t)*width*a.tangentOut
                +(-2*t3+3*t2)*b.gain+(t3-t2)*width*b.tangentIn,0.f,4.f);
        }
        return keys.back().gain;
    }

    // 距离衰减的唯一计算入口；OpenAL 固定使用 AL_NONE，只负责空间声像。
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
