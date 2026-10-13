#include "VansUpdateModules.h"
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>

namespace VansGraphics
{
    // ──────────────────────────────────────────────────────────────────────
    // VansUpdateGravityModule
    // ──────────────────────────────────────────────────────────────────────

    void VansUpdateGravityModule::Execute(VansParticlePool& pool, float deltaTime,
                                          const glm::mat4&) const
    {
        for (uint32_t i = 0; i < pool.m_AliveCount; ++i)
            pool.m_Velocity[i] += m_Gravity * pool.StepDelta(i, deltaTime);
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansUpdateColorOverLifetime
    // ──────────────────────────────────────────────────────────────────────

    void VansUpdateColorOverLifetime::Execute(VansParticlePool& pool, float,
                                              const glm::mat4&) const
    {
        for (uint32_t i = 0; i < pool.m_AliveCount; ++i)
        {
            pool.m_Color[i] = m_Gradient.Evaluate(pool.m_NormalizedAge[i]);
            if (m_ScaleInitialAlpha && !pool.m_InitialAlpha.empty())
                pool.m_Color[i].a *= pool.m_InitialAlpha[i];
        }
    }

    void VansUpdateColorOverLifetime::ExecuteInit(VansParticlePool& pool,
        uint32_t startIndex, uint32_t endIndex, const glm::mat4&) const
    {
        if (!m_ScaleInitialAlpha) return;
        pool.AllocInitialAlpha();
        for (uint32_t i=startIndex; i<endIndex; ++i) pool.m_InitialAlpha[i]=pool.m_Color[i].a;
    }

    namespace
    {
        float SampleFadeDuration(const VansParticleAlphaFade& fade, uint32_t& seed)
        {
            if (!fade.enabled) return 0.f;
            if (fade.durationMin == fade.durationMax) return fade.durationMin;
            const float u = RandFloat(seed);
            // 外部算子以四分之一为指数步长；区间方向保留，不排序端点。
            const float exponent = std::trunc(fade.randomExponent * 4.f) * 0.25f;
            const float shaped = exponent == 0.f ? 1.f : std::pow(u, exponent);
            return fade.durationMin + shaped * (fade.durationMax - fade.durationMin);
        }

        float EvaluateFade(float t, VansParticleFadeEasing easing)
        {
            t = std::clamp(t, 0.f, 1.f);
            return easing == VansParticleFadeEasing::SmoothStep ? t*t*(3.f-2.f*t) : t;
        }
    }

    void VansUpdateAlphaFadeModule::ExecuteInit(VansParticlePool& pool,
        uint32_t startIndex, uint32_t endIndex, const glm::mat4&) const
    {
        if (!m_FadeIn.enabled && !m_FadeOut.enabled) return;
        pool.AllocAlphaFade();
        for (uint32_t i = startIndex; i < endIndex; ++i)
        {
            // 独立局部种子不推进其他模块/图集使用的粒子随机流。
            uint32_t seed = pool.m_SeedRandom[i] ^ 0x9e3779b9u;
            if (seed == 0) seed = 1;
            const float fadeIn = SampleFadeDuration(m_FadeIn, seed);
            const float fadeOut = SampleFadeDuration(m_FadeOut, seed);
            pool.m_AlphaFade[i] = glm::vec3(pool.m_Color[i].a, fadeIn, fadeOut);
        }
    }

    void VansUpdateAlphaFadeModule::Execute(VansParticlePool& pool, float,
        const glm::mat4&) const
    {
        if ((!m_FadeIn.enabled && !m_FadeOut.enabled) || pool.m_AlphaFade.empty()) return;
        for (uint32_t i = 0; i < pool.m_AliveCount; ++i)
        {
            const auto& cached = pool.m_AlphaFade[i];
            float alpha = pool.m_Color[i].a;
            const auto fadeIn = [&]
            {
                if (!m_FadeIn.enabled) return;
                const float age = m_FadeIn.timeDomain == VansParticleFadeTimeDomain::Seconds
                    ? pool.m_Age[i] : pool.m_NormalizedAge[i];
                // 淡入结束后不重置为初始值，保留进入算子前的当前 alpha。
                if (std::max(age, 0.f) < m_FadeInDelay + cached.y)
                    alpha = cached.x * EvaluateFade((age-m_FadeInDelay)/cached.y, m_FadeIn.easing);
            };
            const auto fadeOut = [&]
            {
                if (!m_FadeOut.enabled) return;
                const bool seconds = m_FadeOut.timeDomain == VansParticleFadeTimeDomain::Seconds;
                const float age = seconds ? pool.m_Age[i] : pool.m_NormalizedAge[i];
                const float start = (seconds ? pool.m_LifeTime[i] : 1.f) - cached.z;
                // 严格进入末端区间才覆盖；时长大于寿命时不截短。
                if (age > start)
                    alpha = cached.x * (1.f-EvaluateFade((age-start)/cached.z, m_FadeOut.easing));
            };
            if (m_FadeInFirst) { fadeIn(); fadeOut(); }
            else { fadeOut(); fadeIn(); }
            pool.m_Color[i].a = alpha;
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansUpdateSizeOverLifetime
    // ──────────────────────────────────────────────────────────────────────

    float VansUpdateSizeOverLifetime::EvalCurve(float t) const
    {
        if (m_Curve.empty()) return 1.f;
        if (t <= m_Curve.front().t) return m_Curve.front().value;
        if (t >= m_Curve.back().t)  return m_Curve.back().value;

        for (size_t i = 0; i + 1 < m_Curve.size(); ++i)
        {
            const CurveKey& a = m_Curve[i];
            const CurveKey& b = m_Curve[i + 1];
            if (t >= a.t && t <= b.t)
            {
                float span = b.t - a.t;
                if (span < 1e-6f) return a.value;
                float alpha = (t - a.t) / span;
                return a.value + (b.value - a.value) * alpha;
            }
        }
        return m_Curve.back().value;
    }

    void VansUpdateSizeOverLifetime::Execute(VansParticlePool& pool, float,
                                             const glm::mat4&) const
    {
        for (uint32_t i = 0; i < pool.m_AliveCount; ++i)
        {
            // 曲线表示生成尺寸的倍率，不依赖帧率，也不会逐帧累乘。
            float initSize   = pool.m_InitialSize[i];
            float multiplier = EvalCurve(pool.m_NormalizedAge[i]);
            pool.m_Size[i]   = std::max(initSize * multiplier, 0.0f);
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansUpdateVelocityOverLifetime
    // ──────────────────────────────────────────────────────────────────────

    float VansUpdateVelocityOverLifetime::Noise3(glm::vec3 p) const
    {
        // 简单正弦叠加噪波，不需要纹理查找
        return std::sin(p.x * 1.1f + p.z * 0.7f)
             * std::cos(p.y * 0.9f + p.x * 0.5f)
             * std::sin(p.z * 1.3f + p.y * 0.8f);
    }

    void VansUpdateVelocityOverLifetime::Execute(VansParticlePool& pool,
                                                  float deltaTime,
                                                  const glm::mat4&) const
    {
        for (uint32_t i = 0; i < pool.m_AliveCount; ++i)
        {
            // 指数阻力在不同帧率和较大 deltaTime 下保持稳定，不会反向速度。
            pool.m_Velocity[i] *= std::exp(-std::max(m_Drag, 0.0f) * pool.StepDelta(i, deltaTime));

            // 湍流扰动
            if (m_TurbulenceEnabled)
            {
                glm::vec3 pos = pool.m_Position[i] * m_TurbulenceFrequency;
                float scrollOffset = pool.m_Age[i] * m_TurbulenceScrollSpeed;
                glm::vec3 offset(
                    Noise3(pos + glm::vec3(0.f,     scrollOffset, 0.f)),
                    Noise3(pos + glm::vec3(1.234f,  scrollOffset, 5.678f)),
                    Noise3(pos + glm::vec3(9.101f,  scrollOffset, 3.456f))
                );
                pool.m_Velocity[i] += offset * m_TurbulenceStrength * pool.StepDelta(i, deltaTime);
            }
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansUpdateRotationOverLifetime
    // ──────────────────────────────────────────────────────────────────────

    void VansUpdateRotationOverLifetime::ExecuteInit(VansParticlePool& pool,
                                                      uint32_t startIndex,
                                                      uint32_t endIndex,
                                                      const glm::mat4&) const
    {
        // 若角速度为随机模式，提前分配扩展数组并记录每粒子角速度
        if (m_AngularVelocity.m_Mode != FloatCurveMode::Constant)
        {
            pool.AllocAngularVelocity();
            for (uint32_t i = startIndex; i < endIndex; ++i)
            {
                uint32_t& seed = pool.m_SeedRandom[i];
                float r = RandFloat(seed);
                pool.m_AngularVelocity[i] = m_AngularVelocity.Evaluate(0.f, r);
            }
        }
    }

    void VansUpdateRotationOverLifetime::Execute(VansParticlePool& pool,
                                                  float deltaTime,
                                                  const glm::mat4&) const
    {
        if (m_AngularVelocity.m_Mode == FloatCurveMode::Constant)
        {
            // 常量角速度：所有粒子相同
            for (uint32_t i = 0; i < pool.m_AliveCount; ++i)
                pool.m_Rotation[i] += m_AngularVelocity.m_Value * pool.StepDelta(i, deltaTime);
        }
        else
        {
            // 每粒子独立角速度（存储在扩展数组中）
            if (!pool.m_AngularVelocity.empty())
            {
                for (uint32_t i = 0; i < pool.m_AliveCount; ++i)
                    pool.m_Rotation[i] += pool.m_AngularVelocity[i] * pool.StepDelta(i, deltaTime);
            }
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansUpdateSpriteAnimModule
    // ──────────────────────────────────────────────────────────────────────

    void VansUpdateSpriteAnimModule::ExecuteInit(VansParticlePool& pool,
                                                  uint32_t startIndex,
                                                  uint32_t endIndex,
                                                  const glm::mat4&) const
    {
        pool.AllocFrameIndex();
        for (uint32_t i = startIndex; i < endIndex; ++i)
            pool.m_FrameIndex[i] = 0.f;
    }

    void VansUpdateSpriteAnimModule::Execute(VansParticlePool& pool, float /*deltaTime*/,
                                              const glm::mat4&) const
    {
        if (pool.m_FrameIndex.empty()) return;

        int totalFrames = m_Columns * m_Rows;
        if (totalFrames <= 0) return;

        if (m_FPS <= 0.f)
        {
            // 按生命周期平均分配帧
            for (uint32_t i = 0; i < pool.m_AliveCount; ++i)
            {
                float t = pool.m_NormalizedAge[i];
                pool.m_FrameIndex[i] = std::min(
                    static_cast<float>(totalFrames - 1),
                    t * static_cast<float>(totalFrames));
            }
        }
        else
        {
            // 按固定 FPS 播放（基于年龄计算帧号）
            for (uint32_t i = 0; i < pool.m_AliveCount; ++i)
            {
                float frameF = pool.m_Age[i] * m_FPS;
                pool.m_FrameIndex[i] = std::fmod(frameF,
                                                  static_cast<float>(totalFrames));
            }
        }
    }

} // namespace VansGraphics
