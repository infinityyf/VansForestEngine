#pragma once
#include "../VansParticleData.h"
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <string>

namespace VansGraphics
{
    // ============================================================
    // VansParticleModule — 粒子模块基类
    // 所有 Initialize / Update 模块均继承此类。
    // Initialize 模块在粒子生成时执行一次；
    // Update 模块每帧对所有存活粒子执行。
    // ============================================================
    class VansParticleModule
    {
    public:
        // 模块是否启用
        bool m_Enabled = true;

        virtual ~VansParticleModule() = default;

        // 执行模块逻辑
        // pool      — 粒子数据池（SoA）
        // deltaTime — 帧时间（秒）；Initialize 模块此值为 0
        // localToWorld — 发射器局部到世界变换矩阵
        virtual void Execute(VansParticlePool& pool, float deltaTime,
                             const glm::mat4& localToWorld) = 0;

        // 仅对新生成的粒子执行（Initialize 阶段）：
        // startIndex 到 endIndex（不含）之间的粒子刚被复活
        virtual void ExecuteInit(VansParticlePool& pool, uint32_t startIndex,
                                 uint32_t endIndex,
                                 const glm::mat4& localToWorld)
        {
            // 默认不做任何事；Update 模块重写 Execute 即可
            (void)pool; (void)startIndex; (void)endIndex; (void)localToWorld;
        }

        virtual nlohmann::json Serialize() const   = 0;
        virtual void Deserialize(const nlohmann::json& j) = 0;
    };

    // ── 公用 Curve 辅助结构 ────────────────────────────────────────────────

    // 单个曲线 Key 点
    struct CurveKey
    {
        float t     = 0.f;
        float value = 0.f;
    };

    // Float Curve 模式枚举
    enum class FloatCurveMode
    {
        Constant,             // 固定值
        RandomBetween,        // 两常量之间随机
        Curve,                // 按生命周期曲线
        RandomBetweenCurves,  // 两条曲线之间随机
    };

    // ── Float Curve ────────────────────────────────────────────────────────
    struct VansFloatCurve
    {
        FloatCurveMode        m_Mode    = FloatCurveMode::Constant;
        float                 m_Value   = 1.f;     // Constant
        float                 m_Min     = 0.f;     // RandomBetween
        float                 m_Max     = 1.f;     // RandomBetween
        std::vector<CurveKey> m_Keys;              // Curve
        std::vector<CurveKey> m_MinKeys;           // RandomBetweenCurves
        std::vector<CurveKey> m_MaxKeys;           // RandomBetweenCurves

        // 根据 normalizedT[0,1] 和随机数 r[0,1] 求值
        float Evaluate(float normalizedT, float r = 0.f) const;

        nlohmann::json Serialize() const;
        void Deserialize(const nlohmann::json& j);
    };

    // ── Color Gradient Key ─────────────────────────────────────────────────
    struct ColorGradientStop
    {
        float      t     = 0.f;
        glm::vec4  color = glm::vec4(1.f);
    };

    // ── Color Gradient ─────────────────────────────────────────────────────
    struct VansColorGradient
    {
        std::vector<ColorGradientStop> m_Stops;

        // 根据 normalizedT[0,1] 线性插值颜色
        glm::vec4 Evaluate(float normalizedT) const;

        nlohmann::json Serialize() const;
        void Deserialize(const nlohmann::json& j);
    };

    // ── 随机数工具：基于 xorshift32 ────────────────────────────────────────
    inline float RandFloat(uint32_t& seed)
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return static_cast<float>(seed) / static_cast<float>(0xFFFFFFFFu);
    }

    // 返回 [minVal, maxVal] 的随机浮点数
    inline float RandRange(uint32_t& seed, float minVal, float maxVal)
    {
        return minVal + RandFloat(seed) * (maxVal - minVal);
    }

} // namespace VansGraphics
