#include "VansSSGIParameters.h"
#include <algorithm>
#include <cmath>

VansGraphics::VansSSGIParameters VansGraphics::BuildSSGIParameters(
    const VansGISettings& gi,
    uint32_t renderWidth,
    uint32_t renderHeight)
{
    const float width = static_cast<float>(std::max(renderWidth, 1u));
    const float height = static_cast<float>(std::max(renderHeight, 1u));
    VansSSGIParameters data{};
    data.screenSize = glm::vec4(width, height, 1.0f / width, 1.0f / height);
    uint32_t regionCount = 0u;
    float maxTraceDistance = 1.0f;
    float fadeStartRatio = 0.75f;
    for (const GIProbeRegionDesc* desc : BuildActiveGIRegionOrder(gi))
    {
        if (regionCount >= kGIMaximumRegionCount)
            continue;
        const GIResolvedRegion region = ResolveGIRegion(*desc);
        const uint64_t probeCount64 =
            static_cast<uint64_t>(region.gridDimensions.x) *
            static_cast<uint64_t>(region.gridDimensions.y) *
            static_cast<uint64_t>(region.gridDimensions.z);
        const int totalProbeCount = static_cast<int>(
            std::max<uint64_t>(probeCount64, 1u));
        // 与 DDGI 图集分配一致；固定布局避免每个 Cache texel 重算尺寸。
        const int probesPerAtlasRow = std::max(1,
            static_cast<int>(std::ceil(std::sqrt(
                static_cast<float>(totalProbeCount)))));
        const int probesPerAtlasColumn =
            (totalProbeCount + probesPerAtlasRow - 1) / probesPerAtlasRow;
        VansSSGIRegionParameters& destination = data.regions[regionCount++];
        // 在共享的 std140 参数中发布图集固定行列数。
        destination.volumeMin = glm::vec4(
            region.volumeMin, static_cast<float>(probesPerAtlasRow));
        destination.volumeSizeAndBias = glm::vec4(region.volumeSize, region.normalBias);
        destination.traceParams = glm::vec4(
            region.trace.maxDistance, 0.75f, region.volumeFadeDistance,
            static_cast<float>(probesPerAtlasColumn));
        destination.gridDimensionsAndPriority = glm::vec4(glm::vec3(region.gridDimensions), region.priority);
        maxTraceDistance = std::max(maxTraceDistance, region.trace.maxDistance);
        fadeStartRatio = std::min(fadeStartRatio, destination.traceParams.y);
    }
    data.regionInfo = glm::vec4(
        static_cast<float>(regionCount), maxTraceDistance, fadeStartRatio, 0.0f);
    data.deferredProbeDebug = glm::vec4(
        gi.probeOnlyDeferredOutput ? 1.0f : 0.0f,
        gi.probeOnlyDeferredExposure,
        0.0f,
        0.0f);
    return data;
}
