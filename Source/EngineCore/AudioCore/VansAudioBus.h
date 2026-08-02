#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace VansEngine
{
    inline std::string NormalizeAudioBusName(const std::string& name)
    {
        auto begin = name.begin();
        while (begin != name.end() && std::isspace(static_cast<unsigned char>(*begin)))
            ++begin;
        auto end = name.end();
        while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1))))
            --end;

        std::string trimmed(begin, end);
        if (trimmed.empty())
            return "SFX";

        std::string lowered;
        lowered.reserve(trimmed.size());
        for (const char c : trimmed)
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

        if (lowered == "master") return "Master";
        if (lowered == "music") return "Music";
        if (lowered == "sfx") return "SFX";
        if (lowered == "ui") return "UI";
        if (lowered == "ambient") return "Ambient";
        if (lowered == "voice") return "Voice";
        if (lowered == "preview") return "Preview";
        return trimmed;
    }

    struct AudioBusState
    {
        float gain = 1.0f;
        float targetGain = 1.0f;
        float fadeStartGain = 1.0f;
        float fadeElapsed = 0.0f;
        float fadeDuration = 0.0f;
        float duckingGain = 1.0f;
        float duckingTargetGain = 1.0f;
        float duckingFadeStartGain = 1.0f;
        float duckingFadeElapsed = 0.0f;
        float duckingFadeDuration = 0.0f;
        float lowpassHighFrequencyGain = 1.0f;
        bool muted = false;
        bool soloed = false;

        void Normalize()
        {
            gain = std::clamp(gain, 0.0f, 4.0f);
            targetGain = std::clamp(targetGain, 0.0f, 4.0f);
            fadeStartGain = std::clamp(fadeStartGain, 0.0f, 4.0f);
            fadeElapsed = std::max(fadeElapsed, 0.0f);
            fadeDuration = std::max(fadeDuration, 0.0f);
            duckingGain = std::clamp(duckingGain, 0.0f, 1.0f);
            duckingTargetGain = std::clamp(duckingTargetGain, 0.0f, 1.0f);
            duckingFadeStartGain = std::clamp(duckingFadeStartGain, 0.0f, 1.0f);
            duckingFadeElapsed = std::max(duckingFadeElapsed, 0.0f);
            duckingFadeDuration = std::max(duckingFadeDuration, 0.0f);
            lowpassHighFrequencyGain = std::clamp(lowpassHighFrequencyGain, 0.0f, 1.0f);
        }
    };

    struct AudioBusSnapshotEntry
    {
        std::string busName;
        float gain = 1.0f;
        bool muted = false;
        bool soloed = false;
        bool overrideMuted = false;
        bool overrideSoloed = false;
        bool overrideLowpassHighFrequencyGain = false;
        float lowpassHighFrequencyGain = 1.0f;
    };

    struct AudioBusSnapshot
    {
        std::vector<AudioBusSnapshotEntry> buses;
        float fadeSeconds = 0.25f;
    };

    struct AudioDuckingRule
    {
        std::string triggerBusName = "Voice";
        std::string targetBusName = "Music";
        float targetGain = 0.35f;
        float attackSeconds = 0.08f;
        float releaseSeconds = 0.35f;
        bool enabled = true;

        void Normalize()
        {
            triggerBusName = NormalizeAudioBusName(triggerBusName);
            targetBusName = NormalizeAudioBusName(targetBusName);
            targetGain = std::clamp(targetGain, 0.0f, 1.0f);
            attackSeconds = std::clamp(attackSeconds, 0.0f, 10.0f);
            releaseSeconds = std::clamp(releaseSeconds, 0.0f, 10.0f);
        }
    };

    inline bool IsAudioBusFading(const AudioBusState& bus)
    {
        return bus.fadeDuration > 0.0f && bus.fadeElapsed < bus.fadeDuration;
    }

    inline bool IsAudioBusDuckingFading(const AudioBusState& bus)
    {
        return bus.duckingFadeDuration > 0.0f && bus.duckingFadeElapsed < bus.duckingFadeDuration;
    }

    inline void SetAudioBusGainImmediate(AudioBusState& bus, float gain)
    {
        const float clampedGain = std::clamp(gain, 0.0f, 4.0f);
        bus.gain = clampedGain;
        bus.targetGain = clampedGain;
        bus.fadeStartGain = clampedGain;
        bus.fadeElapsed = 0.0f;
        bus.fadeDuration = 0.0f;
    }

    inline void StartAudioBusGainFade(AudioBusState& bus, float targetGain, float fadeSeconds)
    {
        const float clampedTarget = std::clamp(targetGain, 0.0f, 4.0f);
        const float clampedFade = std::max(fadeSeconds, 0.0f);
        if (clampedFade <= 0.0f)
        {
            SetAudioBusGainImmediate(bus, clampedTarget);
            return;
        }

        bus.Normalize();
        bus.fadeStartGain = bus.gain;
        bus.targetGain = clampedTarget;
        bus.fadeElapsed = 0.0f;
        bus.fadeDuration = clampedFade;
    }

    inline void SetAudioBusDuckingGainImmediate(AudioBusState& bus, float gain)
    {
        const float clampedGain = std::clamp(gain, 0.0f, 1.0f);
        bus.duckingGain = clampedGain;
        bus.duckingTargetGain = clampedGain;
        bus.duckingFadeStartGain = clampedGain;
        bus.duckingFadeElapsed = 0.0f;
        bus.duckingFadeDuration = 0.0f;
    }

    inline void StartAudioBusDuckingFade(AudioBusState& bus, float targetGain, float fadeSeconds)
    {
        const float clampedTarget = std::clamp(targetGain, 0.0f, 1.0f);
        const float clampedFade = std::max(fadeSeconds, 0.0f);
        bus.Normalize();
        if (std::abs(bus.duckingTargetGain - clampedTarget) < 0.0005f)
            return;
        if (clampedFade <= 0.0f)
        {
            SetAudioBusDuckingGainImmediate(bus, clampedTarget);
            return;
        }

        bus.duckingFadeStartGain = bus.duckingGain;
        bus.duckingTargetGain = clampedTarget;
        bus.duckingFadeElapsed = 0.0f;
        bus.duckingFadeDuration = clampedFade;
    }

    inline void TickAudioBusFade(AudioBusState& bus, float deltaTime)
    {
        bus.Normalize();
        const float safeDeltaTime = std::max(deltaTime, 0.0f);
        if (IsAudioBusFading(bus))
        {
            bus.fadeElapsed = std::min(bus.fadeElapsed + safeDeltaTime, bus.fadeDuration);
            const float t = bus.fadeDuration > 0.0f ? bus.fadeElapsed / bus.fadeDuration : 1.0f;
            bus.gain = bus.fadeStartGain + (bus.targetGain - bus.fadeStartGain) * t;
            if (!IsAudioBusFading(bus))
                SetAudioBusGainImmediate(bus, bus.targetGain);
        }
        if (IsAudioBusDuckingFading(bus))
        {
            bus.duckingFadeElapsed = std::min(bus.duckingFadeElapsed + safeDeltaTime, bus.duckingFadeDuration);
            const float t = bus.duckingFadeDuration > 0.0f
                ? bus.duckingFadeElapsed / bus.duckingFadeDuration
                : 1.0f;
            bus.duckingGain =
                bus.duckingFadeStartGain + (bus.duckingTargetGain - bus.duckingFadeStartGain) * t;
            if (!IsAudioBusDuckingFading(bus))
                SetAudioBusDuckingGainImmediate(bus, bus.duckingTargetGain);
        }
    }

    inline float ComputeAudioBusEffectiveGain(
        AudioBusState master,
        AudioBusState bus,
        bool anyBusSoloed,
        bool busIsMaster)
    {
        master.Normalize();
        bus.Normalize();

        if (master.muted || bus.muted)
            return 0.0f;
        if (anyBusSoloed && !bus.soloed)
            return 0.0f;

        return std::clamp(
            master.gain *
                (busIsMaster ? 1.0f : bus.gain) *
                (busIsMaster ? 1.0f : bus.duckingGain),
            0.0f,
            4.0f);
    }
}
