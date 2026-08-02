#pragma once

#include "VansAudioReverbPreset.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <string>
#include <vector>

namespace VansEngine
{
    enum class AudioReverbZoneShape
    {
        Sphere,
        Box
    };

    inline AudioReverbZoneShape AudioReverbZoneShapeFromString(const std::string& shape)
    {
        std::string lowered;
        lowered.reserve(shape.size());
        for (const char c : shape)
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        if (lowered == "box") return AudioReverbZoneShape::Box;
        return AudioReverbZoneShape::Sphere;
    }

    inline const char* AudioReverbZoneShapeToString(AudioReverbZoneShape shape)
    {
        switch (shape)
        {
        case AudioReverbZoneShape::Box: return "box";
        case AudioReverbZoneShape::Sphere:
        default:
            return "sphere";
        }
    }

    struct AudioReverbZoneState
    {
        AudioReverbZoneShape shape = AudioReverbZoneShape::Sphere;
        float centerX = 0.0f;
        float centerY = 0.0f;
        float centerZ = 0.0f;
        float rightX = 1.0f;
        float rightY = 0.0f;
        float rightZ = 0.0f;
        float upX = 0.0f;
        float upY = 1.0f;
        float upZ = 0.0f;
        float forwardX = 0.0f;
        float forwardY = 0.0f;
        float forwardZ = 1.0f;
        float radius = 8.0f;
        float halfExtentX = 4.0f;
        float halfExtentY = 4.0f;
        float halfExtentZ = 4.0f;
        float fadeDistance = 2.0f;
        float wetGain = 0.6f;
        int priority = 0;
        AudioReverbPreset preset = AudioReverbPreset::Generic;
        AudioReverbPresetParameters presetParameters;
        bool overridePresetParameters = false;

        void Normalize()
        {
            radius = std::max(radius, 0.01f);
            halfExtentX = std::max(halfExtentX, 0.01f);
            halfExtentY = std::max(halfExtentY, 0.01f);
            halfExtentZ = std::max(halfExtentZ, 0.01f);
            fadeDistance = std::max(fadeDistance, 0.0f);
            wetGain = std::clamp(wetGain, 0.0f, 1.0f);
            const auto normalizeAxis = [](float& x, float& y, float& z, float fallbackX, float fallbackY, float fallbackZ)
            {
                const float length = std::sqrt(x * x + y * y + z * z);
                if (length <= 0.0001f)
                {
                    x = fallbackX;
                    y = fallbackY;
                    z = fallbackZ;
                    return;
                }
                x /= length;
                y /= length;
                z /= length;
            };
            normalizeAxis(rightX, rightY, rightZ, 1.0f, 0.0f, 0.0f);
            normalizeAxis(upX, upY, upZ, 0.0f, 1.0f, 0.0f);
            normalizeAxis(forwardX, forwardY, forwardZ, 0.0f, 0.0f, 1.0f);
            presetParameters = overridePresetParameters
                ? NormalizeAudioReverbPresetParameters(presetParameters)
                : GetAudioReverbPresetParameters(preset);
        }
    };

    struct AudioReverbZoneEvaluation
    {
        bool affectsListener = false;
        int priority = 0;
        float blend = 0.0f;
        float wetGain = 0.0f;
        AudioReverbPreset preset = AudioReverbPreset::Generic;
        AudioReverbPresetParameters presetParameters;
    };

    struct AudioReverbEnvironmentEvaluation
    {
        bool affectsListener = false;
        int priority = 0;
        int contributingZoneCount = 0;
        float wetGain = 0.0f;
        AudioReverbPreset preset = AudioReverbPreset::Generic;
        AudioReverbPresetParameters presetParameters;
    };

    inline float ComputeReverbFadeBlend(float outsideDistance, float fadeDistance)
    {
        if (outsideDistance <= 0.0f)
            return 1.0f;
        if (fadeDistance <= 0.0f || outsideDistance >= fadeDistance)
            return 0.0f;
        return std::clamp(1.0f - outsideDistance / fadeDistance, 0.0f, 1.0f);
    }

    inline float ComputeReverbZoneBlend(
        float listenerX,
        float listenerY,
        float listenerZ,
        AudioReverbZoneState zone)
    {
        zone.Normalize();
        if (zone.shape == AudioReverbZoneShape::Box)
        {
            const float dx = listenerX - zone.centerX;
            const float dy = listenerY - zone.centerY;
            const float dz = listenerZ - zone.centerZ;
            const float localX = dx * zone.rightX + dy * zone.rightY + dz * zone.rightZ;
            const float localY = dx * zone.upX + dy * zone.upY + dz * zone.upZ;
            const float localZ = dx * zone.forwardX + dy * zone.forwardY + dz * zone.forwardZ;
            const float qx = std::fabs(localX) - zone.halfExtentX;
            const float qy = std::fabs(localY) - zone.halfExtentY;
            const float qz = std::fabs(localZ) - zone.halfExtentZ;
            const float outsideX = std::max(qx, 0.0f);
            const float outsideY = std::max(qy, 0.0f);
            const float outsideZ = std::max(qz, 0.0f);
            const float outsideDistance = std::sqrt(
                outsideX * outsideX + outsideY * outsideY + outsideZ * outsideZ);
            return ComputeReverbFadeBlend(outsideDistance, zone.fadeDistance);
        }

        const float dx = listenerX - zone.centerX;
        const float dy = listenerY - zone.centerY;
        const float dz = listenerZ - zone.centerZ;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        return ComputeReverbFadeBlend(distance - zone.radius, zone.fadeDistance);
    }

    inline AudioReverbZoneEvaluation EvaluateReverbZone(
        float listenerX,
        float listenerY,
        float listenerZ,
        AudioReverbZoneState zone)
    {
        zone.Normalize();
        const float blend = ComputeReverbZoneBlend(listenerX, listenerY, listenerZ, zone);
        AudioReverbZoneEvaluation evaluation;
        evaluation.affectsListener = blend > 0.0f;
        evaluation.priority = zone.priority;
        evaluation.blend = blend;
        evaluation.wetGain = zone.wetGain * blend;
        evaluation.preset = zone.preset;
        evaluation.presetParameters = zone.presetParameters;
        return evaluation;
    }

    inline bool ShouldSelectReverbZoneCandidate(
        const AudioReverbZoneEvaluation& current,
        const AudioReverbZoneEvaluation& candidate)
    {
        if (!candidate.affectsListener)
            return false;
        if (!current.affectsListener)
            return true;
        if (candidate.priority != current.priority)
            return candidate.priority > current.priority;
        return candidate.wetGain > current.wetGain;
    }

    inline AudioReverbEnvironmentEvaluation EvaluateReverbEnvironment(
        const std::vector<AudioReverbZoneEvaluation>& evaluations)
    {
        AudioReverbEnvironmentEvaluation environment;
        float strongestWetGain = -1.0f;
        for (const AudioReverbZoneEvaluation& evaluation : evaluations)
        {
            if (!evaluation.affectsListener)
                continue;
            if (!environment.affectsListener || evaluation.priority > environment.priority)
            {
                environment = {};
                environment.affectsListener = true;
                environment.priority = evaluation.priority;
                strongestWetGain = -1.0f;
            }
            if (evaluation.priority != environment.priority || evaluation.wetGain <= 0.0f)
                continue;

            environment.wetGain += evaluation.wetGain;
            environment.presetParameters.density +=
                evaluation.presetParameters.density * evaluation.wetGain;
            environment.presetParameters.diffusion +=
                evaluation.presetParameters.diffusion * evaluation.wetGain;
            environment.presetParameters.gain +=
                evaluation.presetParameters.gain * evaluation.wetGain;
            environment.presetParameters.gainHF +=
                evaluation.presetParameters.gainHF * evaluation.wetGain;
            environment.presetParameters.decayTime +=
                evaluation.presetParameters.decayTime * evaluation.wetGain;
            ++environment.contributingZoneCount;
            if (evaluation.wetGain > strongestWetGain)
            {
                strongestWetGain = evaluation.wetGain;
                environment.preset = evaluation.preset;
            }
        }

        if (!environment.affectsListener || environment.wetGain <= 0.0f)
            return environment;

        const float invWeight = 1.0f / environment.wetGain;
        environment.presetParameters.density *= invWeight;
        environment.presetParameters.diffusion *= invWeight;
        environment.presetParameters.gain *= invWeight;
        environment.presetParameters.gainHF *= invWeight;
        environment.presetParameters.decayTime *= invWeight;
        environment.presetParameters.Normalize();
        environment.wetGain = std::clamp(environment.wetGain, 0.0f, 1.0f);
        return environment;
    }

    inline float ComputeSmoothedReverbWetGain(float currentWetGain, float targetWetGain, float deltaTime)
    {
        const float smoothing = 1.0f - std::exp(-std::max(deltaTime, 0.0f) * 8.0f);
        return currentWetGain +
            (std::clamp(targetWetGain, 0.0f, 1.0f) - currentWetGain) *
            std::clamp(smoothing, 0.0f, 1.0f);
    }
}
