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

} // namespace VansGraphics
