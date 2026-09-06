#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <cassert>

namespace VansGraphics
{
    // ============================================================
    // VansParticleInstanceData — 每粒子上传到 GPU 的实例数据
    // 对齐到 16 字节，在 Vertex Shader 中作为 per-instance 属性读取。
    // sizeof == 48 字节
    // ============================================================
    struct alignas(16) VansParticleInstanceData
    {
        glm::vec3 m_WorldPosition;  // 世界位置（12 字节）
        float     m_Size;           // Billboard 大小（4 字节）
        glm::vec4 m_Color;          // RGBA（16 字节）
        float     m_Rotation;       // 视图空间旋转角（弧度）（4 字节）
        float     m_FrameIndex;     // Sprite Sheet 帧索引（float，Shader 内取整）（4 字节）
        glm::vec2 m_Padding;        // 对齐填充（8 字节）
    };

    static_assert(sizeof(VansParticleInstanceData) == 48,
        "VansParticleInstanceData 大小应为 48 字节");
    static_assert(sizeof(VansParticleInstanceData) % 16 == 0,
        "VansParticleInstanceData 必须 16 字节对齐");

    // 体积粒子使用独立数据通道；未启用体积注入的 Emitter 不会生成该结构，
    // 因而不会改变现有 Billboard/Six-Way 实例内容或排序。
    struct alignas(16) VansVolumetricParticleInstanceData
    {
        // xyz: 世界位置，w: 世界空间球体半径（m）。
        glm::vec4 m_WorldPositionRadius{ 0.0f };
        // rgb: 单次散射反照率 × 粒子颜色，a: 基础消光系数（1/m）。
        glm::vec4 m_ScatteringAlbedoExtinction{ 0.0f };
        // rgb: 自发光源项（radiance/m）× 粒子颜色，a: 各向异性 g。
        glm::vec4 m_EmissiveAnisotropy{ 0.0f };
        // x: 直射光，y: 天空光，z: 软边，w: 是否接收云影。
        glm::vec4 m_LightingEdgeCloud{ 1.0f, 1.0f, 0.35f, 1.0f };
        // x: 最大注入距离（m）。
        glm::vec4 m_DistanceAndPadding{ 100.0f, 0.0f, 0.0f, 0.0f };
        // x: 生命周期内稳定 ID；y/z 由 RenderCore 写入当前视图的 Z Slice 范围；w: 优先级。
        glm::uvec4 m_Metadata{ 0u };
    };

    static_assert(sizeof(VansVolumetricParticleInstanceData) == 96,
        "VansVolumetricParticleInstanceData 大小应为 96 字节");
    static_assert(sizeof(VansVolumetricParticleInstanceData) % 16 == 0,
        "VansVolumetricParticleInstanceData 必须 16 字节对齐");

} // namespace VansGraphics
