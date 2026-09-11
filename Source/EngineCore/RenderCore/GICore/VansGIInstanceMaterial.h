#pragma once
#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <vulkan/vulkan_core.h>

namespace VansGraphics
{
// 与 GIInstanceMaterial.glsl 的 std430 数组一致；每个 TLAS 实例独立保存。
struct GIInstanceMaterialGPU
{
    uint32_t packedTextureIndex = 0xffffffffu;
    float alphaCutoff = 0.0f;

    static GIInstanceMaterialGPU Resolve(uint32_t textureIndex, bool alphaTest, float cutoff)
    {
        return {textureIndex, alphaTest ? std::clamp(cutoff, 0.0f, 1.0f) : 0.0f};
    }
    // cutoff 为零时没有任何 texel 被裁剪，可直接使用不透明快速路径。
    VkGeometryInstanceFlagsKHR InstanceFlags() const
    {
        return alphaCutoff > 0.0f ? VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR
                                 : VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
    }
};
static_assert(sizeof(GIInstanceMaterialGPU) == 8);
static_assert(std::is_trivially_copyable_v<GIInstanceMaterialGPU>);
}
