#pragma once

#include "VansGISettings.h"
#include <array>

namespace VansGraphics
{
    struct alignas(16) VansSSGIRegionParameters
    {
        // w 保存图集每行探针数；尺寸在区域资源创建后保持不变。
        glm::vec4 volumeMin;
        glm::vec4 volumeSizeAndBias;
        // w 保存图集探针行数。
        glm::vec4 traceParams;
        glm::vec4 gridDimensionsAndPriority;
    };
    static_assert(sizeof(VansSSGIRegionParameters) == 64, "SSGI region parameter layout must match GLSL");

    struct alignas(16) VansSSGIParameters
    {
        static constexpr uint32_t ProbeCacheTileSize = 4u;

        glm::vec4 screenSize;
        // x 区域数，y 最大追踪距离，z 最小淡出起始比例，w 稀疏布局标志。
        glm::vec4 regionInfo;
        std::array<VansSSGIRegionParameters, kGIMaximumRegionCount> regions{};
        // x 为 Deferred 直接显示探针辐照度，y 为诊断曝光。
        glm::vec4 deferredProbeDebug;
    };
    static_assert(sizeof(VansSSGIParameters) == 48 + 64 * kGIMaximumRegionCount, "SSGI parameter layout must match GLSL");

    VansSSGIParameters BuildSSGIParameters(const VansGISettings& gi, uint32_t renderWidth, uint32_t renderHeight);

    struct alignas(16) VansSSGIAtrousPushConstants
    {
        uint32_t stepWidth = 1;
        float planeToleranceScale = 1.0f;
        float normalPower = 32.0f;
        float materialWeight = 0.0f;
    };
    static_assert(sizeof(VansSSGIAtrousPushConstants) == 16, "SSGI A-Trous push constant layout must match GLSL");
}
