#pragma once

#include <cstdint>

namespace VansGraphics
{
    // One CPU/GLSL push-constant ABI, shared by depth prefilter and horizon search.
    // Distances use the same metre units as GBuffer2.w; output is visibility.
    struct VansGTAOParameters
    {
        float radiusMeters = 0.5f;
        float falloffFraction = 0.6f;
        float sampleDistributionPower = 2.0f;
        float depthMipSamplingOffset = 3.15f;
        int32_t sliceCount = 2;
        int32_t stepsPerSide = 2;
        int32_t sourceMip = -1;
        int32_t mipCount = 1;

        static constexpr uint32_t MaximumDepthMipLevels = 5;
    };
    static_assert(sizeof(VansGTAOParameters) == 32, "GTAO push constants must match GTAOCommon.glsl");
}
