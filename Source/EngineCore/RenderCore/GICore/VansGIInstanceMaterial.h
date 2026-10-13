#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vulkan/vulkan_core.h>

namespace VansGraphics
{
// 与 GIInstanceMaterial.glsl 的 std430 数组一致；每个 TLAS 实例独立保存。
struct alignas(16) GIInstanceMaterialGPU
{
    static constexpr uint32_t TextureIndexMask = 0x0fffffffu;
    static constexpr uint32_t ClothFlag = 0x20000000u;
    static constexpr uint32_t ClothSingleSidedFlag = 0x10000000u;
    uint32_t packedTextureIndex = 0xffffffffu;
    float alphaCutoff = 0.0f;
    uint32_t clothMaterialIndex = 0xffffffffu;
    uint32_t padding = 0u;
    // Cloth 的 GI 漫反射近似；非 Cloth 保持既有纹理取值，不消费此字段。
    std::array<float, 4> clothDiffuseRoughness{1.0f, 1.0f, 1.0f, 1.0f};

    static GIInstanceMaterialGPU Resolve(uint32_t textureIndex, bool alphaTest, float cutoff)
    {
        return {textureIndex, alphaTest ? std::clamp(cutoff, 0.0f, 1.0f) : 0.0f};
    }
    static GIInstanceMaterialGPU ResolveCloth(uint32_t textureIndex, bool alphaTest,
        float cutoff, bool doubleSided, const std::array<float, 3>& diffuse, float roughness)
    {
        auto result = Resolve((textureIndex & TextureIndexMask) | ClothFlag |
            (doubleSided ? 0u : ClothSingleSidedFlag), alphaTest, cutoff);
        result.clothDiffuseRoughness = {diffuse[0], diffuse[1], diffuse[2], roughness};
        return result;
    }
    // Cloth 的覆盖/单双面允许实时编辑，始终执行轻量 any-hit，避免参数修改重建 TLAS。
    // 非 Cloth 保持原有不透明快速路径。
    VkGeometryInstanceFlagsKHR InstanceFlags() const
    {
        const bool cloth = packedTextureIndex != 0xffffffffu && (packedTextureIndex & ClothFlag) != 0u;
        return alphaCutoff > 0.0f || cloth ? VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR
                                 : VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
    }
};
static_assert(sizeof(GIInstanceMaterialGPU) == 32);
static_assert(offsetof(GIInstanceMaterialGPU, clothDiffuseRoughness) == 16);
static_assert(std::is_trivially_copyable_v<GIInstanceMaterialGPU>);
}
