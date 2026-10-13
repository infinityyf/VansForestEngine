#pragma once
#include "VansParticleModule.h"
#include <glm/glm.hpp>
#include <string>

namespace VansGraphics
{
    // ============================================================
    // Update 模块系列
    // 这些模块每帧对所有存活粒子的 Execute() 被调用。
    // ============================================================

    // ── UpdateGravity — 重力加速 ───────────────────────────────────────────
    class VansUpdateGravityModule : public VansParticleModule
    {
    public:
        glm::vec3 m_Gravity = glm::vec3(0.f, -9.8f, 0.f);

        void Execute(VansParticlePool& pool, float deltaTime,
                     const glm::mat4& localToWorld) const override;

        void ExecuteInit(VansParticlePool&, uint32_t, uint32_t, const glm::mat4&) const override {}

    };

    // ── UpdateColorOverLifetime — 颜色随生命周期变化 ───────────────────────
    class VansUpdateColorOverLifetime : public VansParticleModule
    {
    public:
        VansColorGradient m_Gradient;
        bool m_ScaleInitialAlpha = false;

        VansUpdateColorOverLifetime()
        {
            // 默认：白色渐变透明
            m_Gradient.m_Stops = {
                { 0.f, glm::vec4(1.f, 1.f, 1.f, 1.f) },
                { 1.f, glm::vec4(1.f, 1.f, 1.f, 0.f) }
            };
        }

        void Execute(VansParticlePool& pool, float deltaTime,
                     const glm::mat4& localToWorld) const override;

        void ExecuteInit(VansParticlePool& pool, uint32_t startIndex,
                         uint32_t endIndex, const glm::mat4&) const override;

    };

    enum class VansParticleFadeTimeDomain { Lifetime, Seconds };
    enum class VansParticleFadeEasing { Linear, SmoothStep };

    struct VansParticleAlphaFade
    {
        bool enabled = false;
        float durationMin = 0.25f, durationMax = 0.25f;
        float randomExponent = 1.f;
        VansParticleFadeTimeDomain timeDomain = VansParticleFadeTimeDomain::Lifetime;
        VansParticleFadeEasing easing = VansParticleFadeEasing::SmoothStep;
    };

    // 仅在淡入/淡出区间从初始化 alpha 重算；其他区间保留当前值，RGB 不变。
    // 时长在出生时缓存，与每帧按稳定 ID 重算等价；不声称复现外部 RNG 表。
    class VansUpdateAlphaFadeModule : public VansParticleModule
    {
    public:
        VansParticleAlphaFade m_FadeIn, m_FadeOut;
        // 使用淡入的时间域；默认零延迟保持已有算子的写入区间。
        float m_FadeInDelay = 0.f;
        bool m_FadeInFirst = true;
        void Execute(VansParticlePool& pool, float deltaTime,
                     const glm::mat4& localToWorld) const override;
        void ExecuteInit(VansParticlePool& pool, uint32_t startIndex,
                         uint32_t endIndex, const glm::mat4& localToWorld) const override;
    };

    // ── UpdateSizeOverLifetime — 大小随生命周期变化 ────────────────────────
    class VansUpdateSizeOverLifetime : public VansParticleModule
    {
    public:
        // 曲线 key: t=normalizedAge, value=size 倍率（乘以初始 size）
        std::vector<CurveKey> m_Curve;

        VansUpdateSizeOverLifetime()
        {
            // 默认：从正常大小缩放到 0
            m_Curve = { {0.f, 1.f}, {0.5f, 1.3f}, {1.f, 0.f} };
        }

        void Execute(VansParticlePool& pool, float deltaTime,
                     const glm::mat4& localToWorld) const override;

        void ExecuteInit(VansParticlePool&, uint32_t, uint32_t, const glm::mat4&) const override {}


    private:
        float EvalCurve(float t) const;
    };

    // ── UpdateVelocityOverLifetime — 速度衰减 + 湍流 ──────────────────────
    class VansUpdateVelocityOverLifetime : public VansParticleModule
    {
    public:
        float m_Drag        = 0.1f;   // 阻力系数（速度每秒衰减比例）

        // 湍流噪波配置
        bool  m_TurbulenceEnabled = false;
        float m_TurbulenceStrength  = 0.5f;
        float m_TurbulenceFrequency = 1.f;
        float m_TurbulenceScrollSpeed = 0.2f;

        void Execute(VansParticlePool& pool, float deltaTime,
                     const glm::mat4& localToWorld) const override;

        void ExecuteInit(VansParticlePool&, uint32_t, uint32_t, const glm::mat4&) const override {}


    private:
        // 简单的 3D 值噪波（基于正弦叠加）
        float Noise3(glm::vec3 p) const;
    };

    // ── UpdateRotationOverLifetime — 旋转角速度 ────────────────────────────
    class VansUpdateRotationOverLifetime : public VansParticleModule
    {
    public:
        VansFloatCurve m_AngularVelocity;   // 角速度（度/秒）

        VansUpdateRotationOverLifetime()
        {
            m_AngularVelocity.m_Mode  = FloatCurveMode::Constant;
            m_AngularVelocity.m_Value = 45.f;
        }

        void Execute(VansParticlePool& pool, float deltaTime,
                     const glm::mat4& localToWorld) const override;

        // ExecuteInit：在粒子生成时初始化角速度扩展数组（若需要随机角速度）
        void ExecuteInit(VansParticlePool& pool, uint32_t startIndex,
                         uint32_t endIndex, const glm::mat4&) const override;

    };

    // ── UpdateSpriteAnim — Sprite Sheet 帧动画 ─────────────────────────────
    class VansUpdateSpriteAnimModule : public VansParticleModule
    {
    public:
        int   m_Columns  = 4;
        int   m_Rows     = 4;
        float m_FPS      = 0.f;   // 0 表示按生命周期平均分配帧

        void Execute(VansParticlePool& pool, float deltaTime,
                     const glm::mat4& localToWorld) const override;

        void ExecuteInit(VansParticlePool& pool, uint32_t startIndex,
                         uint32_t endIndex, const glm::mat4&) const override;

    };

} // namespace VansGraphics
