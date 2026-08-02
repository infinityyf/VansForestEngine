#pragma once

#include <algorithm>

namespace VansEngine
{
    struct AudioConeSettings
    {
        bool enabled = false;
        float innerAngleDegrees = 360.0f;
        float outerAngleDegrees = 360.0f;
        float outerGain = 1.0f;

        void Normalize()
        {
            innerAngleDegrees = std::clamp(innerAngleDegrees, 0.0f, 360.0f);
            outerAngleDegrees = std::clamp(outerAngleDegrees, innerAngleDegrees, 360.0f);
            outerGain = std::clamp(outerGain, 0.0f, 1.0f);
            if (!enabled)
            {
                innerAngleDegrees = 360.0f;
                outerAngleDegrees = 360.0f;
                outerGain = 1.0f;
            }
        }
    };

    inline AudioConeSettings NormalizeAudioConeSettings(AudioConeSettings settings)
    {
        settings.Normalize();
        return settings;
    }
}
