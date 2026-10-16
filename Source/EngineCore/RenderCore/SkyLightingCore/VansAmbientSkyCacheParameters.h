#pragma once

#include <glm/glm.hpp>

namespace VansGraphics
{
    struct alignas(16) VansAmbientSkyCacheParameters
    {
        glm::vec4 originAndSpacing;
        glm::uvec4 gridAndQuery;
        // 逻辑原点对应的物理纹理偏移；相机重定位保持环形缓存。
        glm::ivec4 ringOffset;
    };
    static_assert(sizeof(VansAmbientSkyCacheParameters) == 48, "Ambient sky cache parameter layout must match GLSL");

    struct alignas(16) VansAmbientSkyCachePushConstants
    {
        // control.x 为更新偏移，control.y 为 0 时追踪、1 时清理。
        glm::uvec4 control{};
        glm::vec4 previousOriginAndSpacing{};
    };
    static_assert(sizeof(VansAmbientSkyCachePushConstants) == 32, "Ambient sky cache push constant layout must match GLSL");
}
