#pragma once

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <vector>

namespace VansEngine
{
    struct AudioVoiceBudgetSettings
    {
        std::size_t maxActiveVoices = 32;
        float minAudibleGain = 0.001f;
    };

    struct AudioVoiceCandidate
    {
        std::size_t stableIndex = 0;
        bool bound = false;
        bool objectActive = false;
        bool componentEnabled = false;
        bool playing = false;
        bool spatial = false;
        float listenerDistance = 0.0f;
        float maxDistance = 100.0f;
        float effectiveGain = 1.0f;
    };

    struct AudioVoiceSelection
    {
        std::vector<bool> active;
        std::size_t activeCount = 0;
        std::size_t virtualizedCount = 0;
    };

    inline float ComputeAudioVoicePriority(const AudioVoiceCandidate& candidate)
    {
        if (!candidate.bound || !candidate.objectActive || !candidate.componentEnabled || !candidate.playing)
            return -1.0f;
        if (candidate.effectiveGain <= 0.0f)
            return -1.0f;

        const float gainScore = std::clamp(candidate.effectiveGain, 0.0f, 4.0f) * 100.0f;
        if (!candidate.spatial)
            return 2000.0f + gainScore;

        const float safeMaxDistance = std::max(candidate.maxDistance, 0.01f);
        const float normalizedDistance =
            std::clamp(candidate.listenerDistance / safeMaxDistance, 0.0f, 1.0f);
        const float distanceScore = (1.0f - normalizedDistance) * 1000.0f;
        return distanceScore + gainScore;
    }

    inline AudioVoiceSelection SelectAudioVoices(
        const std::vector<AudioVoiceCandidate>& candidates,
        AudioVoiceBudgetSettings settings = {})
    {
        AudioVoiceSelection selection;
        selection.active.assign(candidates.size(), false);
        if (candidates.empty() || settings.maxActiveVoices == 0)
        {
            selection.virtualizedCount = candidates.size();
            return selection;
        }

        std::vector<std::size_t> order(candidates.size());
        std::iota(order.begin(), order.end(), std::size_t{ 0 });
        std::stable_sort(order.begin(), order.end(),
            [&candidates](std::size_t lhs, std::size_t rhs)
            {
                const float lhsPriority = ComputeAudioVoicePriority(candidates[lhs]);
                const float rhsPriority = ComputeAudioVoicePriority(candidates[rhs]);
                if (lhsPriority == rhsPriority)
                    return candidates[lhs].stableIndex < candidates[rhs].stableIndex;
                return lhsPriority > rhsPriority;
            });

        for (std::size_t index : order)
        {
            const float priority = ComputeAudioVoicePriority(candidates[index]);
            if (priority < 0.0f || candidates[index].effectiveGain < settings.minAudibleGain)
                continue;
            if (selection.activeCount >= settings.maxActiveVoices)
                break;
            selection.active[index] = true;
            ++selection.activeCount;
        }

        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            if (ComputeAudioVoicePriority(candidates[i]) >= 0.0f &&
                candidates[i].effectiveGain >= settings.minAudibleGain &&
                !selection.active[i])
                ++selection.virtualizedCount;
        }
        return selection;
    }
}
