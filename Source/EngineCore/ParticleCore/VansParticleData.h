#pragma once
#include <vector>
#include <cstdint>

#include <glm/glm.hpp>

namespace VansGraphics
{
    // ============================================================
    // VansParticlePool — CPU 粒子数据池，SoA 布局
    // SoA（Structure of Arrays）保证每个 Update 模块只访问
    // 一段连续内存，缓存友好、便于 SIMD 扩展。
    // ============================================================
    struct VansParticlePool
    {
        // ── 容量与存活数 ─────────────────────────────────────────
        uint32_t m_MaxCount   = 0;
        uint32_t m_AliveCount = 0;

        // ── 核心属性 ─────────────────────────────────────────────
        std::vector<glm::vec3> m_Position;      // 世界/局部位置
        std::vector<glm::vec3> m_Velocity;      // 速度
        std::vector<glm::vec4> m_Color;         // RGBA，[0,1]
        std::vector<float>     m_Size;          // 统一缩放大小
        std::vector<float>     m_Rotation;      // 绕视图轴旋转角（度）
        std::vector<float>     m_Age;           // 当前年龄（秒）
        std::vector<float>     m_LifeTime;      // 总生命周期（秒）
        std::vector<float>     m_NormalizedAge; // Age/LifeTime，[0,1]，预计算
        std::vector<uint32_t>  m_Flags;         // 位标记（alive/dead 等）

        // ── 可选扩展属性 ─────────────────────────────────────────
        // 仅在对应模块激活时分配，避免不必要的内存开销
        std::vector<float>     m_AngularVelocity;   // 旋转角速度（度/秒）
        std::vector<glm::vec3> m_InitialVelocity;   // 初始速度（拉伸 Billboard 用）
        std::vector<float>     m_FrameIndex;         // Sprite Sheet 帧索引
        std::vector<uint32_t>  m_SeedRandom;         // 每粒子随机种子

        // ── 粒子标记位定义 ───────────────────────────────────────
        static constexpr uint32_t FLAG_ALIVE = 1u << 0;

        // ── 接口 ─────────────────────────────────────────────────

        // 根据最大容量分配所有核心属性数组
        void Resize(uint32_t maxCount);

        // 分配可选扩展属性（首次激活对应模块时调用）
        void AllocAngularVelocity();
        void AllocInitialVelocity();
        void AllocFrameIndex();
        void AllocSeedRandom();

        // 粒子死亡：将最后一个存活粒子填入空缺槽位，O(1) 删除
        void SwapRemoveAt(uint32_t index);

        // 判断某粒子是否存活
        bool IsAlive(uint32_t index) const
        {
            return (m_Flags[index] & FLAG_ALIVE) != 0;
        }
    };

} // namespace VansGraphics
