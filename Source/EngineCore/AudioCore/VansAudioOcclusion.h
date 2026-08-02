#pragma once

#include <algorithm>
#include <cmath>
#include <cctype>
#include <string>

namespace VansEngine
{
    inline std::string NormalizeAudioOcclusionMaterialName(std::string value)
    {
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c) {
            return !std::isspace(c);
        }));
        value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char c) {
            return !std::isspace(c);
        }).base(), value.end());
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (value == "thin" || value == "curtain" || value == "foliage") return "thin";
        if (value == "wood" || value == "door") return "wood";
        if (value == "stone" || value == "concrete" || value == "wall") return "stone";
        if (value == "metal") return "metal";
        if (value == "glass" || value == "window") return "glass";
        if (value == "fabric" || value == "cloth") return "fabric";
        return "custom";
    }

    struct AudioOcclusionMaterialProfile
    {
        float blockedGain = 0.45f;
        float blockedHighFrequencyGain = 0.35f;
    };

    inline AudioOcclusionMaterialProfile GetAudioOcclusionMaterialProfile(const std::string& material)
    {
        const std::string normalized = NormalizeAudioOcclusionMaterialName(material);
        if (normalized == "thin") return { 0.72f, 0.58f };
        if (normalized == "wood") return { 0.48f, 0.30f };
        if (normalized == "stone") return { 0.28f, 0.12f };
        if (normalized == "metal") return { 0.24f, 0.08f };
        if (normalized == "glass") return { 0.62f, 0.42f };
        if (normalized == "fabric") return { 0.56f, 0.24f };
        return {};
    }

    struct AudioOcclusionSettings
    {
        bool enabled = false;
        float blockedGain = 0.45f;
        float blockedHighFrequencyGain = 0.35f;
        std::string material = "custom";
        float materialThickness = 1.0f;
        float releaseSeconds = 0.18f;
        float attackSeconds = 0.08f;
        float queryIntervalSeconds = 0.12f;
        float maxQueryDistance = 100.0f;
        int maxQueriesPerFrame = 4;

        void Normalize()
        {
            blockedGain = std::clamp(blockedGain, 0.0f, 1.0f);
            blockedHighFrequencyGain = std::clamp(blockedHighFrequencyGain, 0.0f, 1.0f);
            material = NormalizeAudioOcclusionMaterialName(material);
            materialThickness = std::clamp(materialThickness, 0.0f, 4.0f);
            releaseSeconds = std::max(releaseSeconds, 0.001f);
            attackSeconds = std::max(attackSeconds, 0.001f);
            queryIntervalSeconds = std::max(queryIntervalSeconds, 0.016f);
            maxQueryDistance = std::max(maxQueryDistance, 0.01f);
            maxQueriesPerFrame = std::max(maxQueriesPerFrame, 1);
        }
    };

    struct AudioOcclusionState
    {
        float gain = 1.0f;
        float highFrequencyGain = 1.0f;
        float queryTimer = 0.0f;
        bool lastBlocked = false;
    };

    inline AudioOcclusionSettings ResolveAudioOcclusionMaterialSettings(AudioOcclusionSettings settings)
    {
        settings.Normalize();
        if (settings.material == "custom")
            return settings;

        const AudioOcclusionMaterialProfile profile =
            GetAudioOcclusionMaterialProfile(settings.material);
        settings.blockedGain = std::clamp(
            std::pow(profile.blockedGain, settings.materialThickness),
            0.0f,
            1.0f);
        settings.blockedHighFrequencyGain = std::clamp(
            std::pow(profile.blockedHighFrequencyGain, settings.materialThickness),
            0.0f,
            1.0f);
        return settings;
    }

    inline float SmoothAudioOcclusionValue(
        float current,
        float target,
        float deltaTime,
        float seconds)
    {
        if (deltaTime <= 0.0f)
            return current;

        const float normalizedSeconds = std::max(seconds, 0.001f);
        const float blend = 1.0f - std::exp(-deltaTime / normalizedSeconds);
        return current + (target - current) * std::clamp(blend, 0.0f, 1.0f);
    }

    inline AudioOcclusionState UpdateAudioOcclusionState(
        AudioOcclusionState state,
        AudioOcclusionSettings settings,
        bool blocked,
        float deltaTime)
    {
        settings = ResolveAudioOcclusionMaterialSettings(settings);
        state.lastBlocked = blocked;
        const float gainTarget = blocked ? settings.blockedGain : 1.0f;
        const float highFrequencyTarget = blocked ? settings.blockedHighFrequencyGain : 1.0f;
        const float smoothingSeconds = blocked ? settings.attackSeconds : settings.releaseSeconds;
        state.gain = std::clamp(
            SmoothAudioOcclusionValue(state.gain, gainTarget, deltaTime, smoothingSeconds),
            0.0f,
            1.0f);
        state.highFrequencyGain = std::clamp(
            SmoothAudioOcclusionValue(
                state.highFrequencyGain,
                highFrequencyTarget,
                deltaTime,
                smoothingSeconds),
            0.0f,
            1.0f);
        return state;
    }
}
