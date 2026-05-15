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
                                          const glm::mat4&)
    {
        for (uint32_t i = 0; i < pool.m_AliveCount; ++i)
        {
            pool.m_Velocity[i]  += m_Gravity * deltaTime;
            pool.m_Position[i]  += pool.m_Velocity[i] * deltaTime;
        }
    }

    nlohmann::json VansUpdateGravityModule::Serialize() const
    {
        nlohmann::json j;
        j["module"]  = "UpdateGravity";
        j["gravity"] = { m_Gravity.x, m_Gravity.y, m_Gravity.z };
        return j;
    }

    void VansUpdateGravityModule::Deserialize(const nlohmann::json& j)
    {
        if (j.contains("gravity") && j["gravity"].is_array() && j["gravity"].size() >= 3)
        {
            m_Gravity = glm::vec3(
                j["gravity"][0].get<float>(),
                j["gravity"][1].get<float>(),
                j["gravity"][2].get<float>()
            );
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansUpdateColorOverLifetime
    // ──────────────────────────────────────────────────────────────────────

    void VansUpdateColorOverLifetime::Execute(VansParticlePool& pool, float,
                                              const glm::mat4&)
    {
        for (uint32_t i = 0; i < pool.m_AliveCount; ++i)
        {
            pool.m_Color[i] = m_Gradient.Evaluate(pool.m_NormalizedAge[i]);
        }
    }

    nlohmann::json VansUpdateColorOverLifetime::Serialize() const
    {
        nlohmann::json j;
        j["module"]   = "UpdateColorOverLifetime";
        j["gradient"] = m_Gradient.Serialize();
        return j;
    }

    void VansUpdateColorOverLifetime::Deserialize(const nlohmann::json& j)
    {
        if (j.contains("gradient"))
            m_Gradient.Deserialize(j["gradient"]);
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
                                             const glm::mat4&)
    {
        for (uint32_t i = 0; i < pool.m_AliveCount; ++i)
        {
            // Size 字段存储初始大小 × 曲线倍率
            float initSize   = pool.m_Size[i];
            float multiplier = EvalCurve(pool.m_NormalizedAge[i]);
            pool.m_Size[i]   = initSize * multiplier;
        }
    }

    nlohmann::json VansUpdateSizeOverLifetime::Serialize() const
    {
        nlohmann::json j;
        j["module"] = "UpdateSizeOverLifetime";
        auto curve = nlohmann::json::array();
        for (auto& k : m_Curve)
            curve.push_back({ {"t", k.t}, {"value", k.value} });
        j["curve"] = curve;
        return j;
    }

    void VansUpdateSizeOverLifetime::Deserialize(const nlohmann::json& j)
    {
        m_Curve.clear();
        if (j.contains("curve"))
        {
            for (auto& k : j["curve"])
                m_Curve.push_back({ k.value("t", 0.f), k.value("value", 1.f) });
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
                                                  const glm::mat4&)
    {
        for (uint32_t i = 0; i < pool.m_AliveCount; ++i)
        {
            // 阻力衰减：速度每秒按 drag 减少
            pool.m_Velocity[i] *= (1.f - m_Drag * deltaTime);

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
                pool.m_Velocity[i] += offset * m_TurbulenceStrength * deltaTime;
            }

            // 用更新后的速度推进位置（若 Gravity 模块未处理位置，则此处处理）
            // 注意：若同时存在 Gravity 模块，应避免重复积分位置。
            // 设计约定：Gravity 模块负责位置积分；此模块仅修改速度。
        }
    }

    nlohmann::json VansUpdateVelocityOverLifetime::Serialize() const
    {
        nlohmann::json j;
        j["module"] = "UpdateVelocityOverLifetime";
        j["drag"]   = m_Drag;
        j["turbulence"] = {
            {"enabled",     m_TurbulenceEnabled},
            {"strength",    m_TurbulenceStrength},
            {"frequency",   m_TurbulenceFrequency},
            {"scrollSpeed", m_TurbulenceScrollSpeed}
        };
        return j;
    }

    void VansUpdateVelocityOverLifetime::Deserialize(const nlohmann::json& j)
    {
        m_Drag = j.value("drag", 0.1f);
        if (j.contains("turbulence"))
        {
            auto& t = j["turbulence"];
            m_TurbulenceEnabled      = t.value("enabled",     false);
            m_TurbulenceStrength     = t.value("strength",    0.5f);
            m_TurbulenceFrequency    = t.value("frequency",   1.f);
            m_TurbulenceScrollSpeed  = t.value("scrollSpeed", 0.2f);
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansUpdateRotationOverLifetime
    // ──────────────────────────────────────────────────────────────────────

    void VansUpdateRotationOverLifetime::ExecuteInit(VansParticlePool& pool,
                                                      uint32_t startIndex,
                                                      uint32_t endIndex,
                                                      const glm::mat4&)
    {
        // 若角速度为随机模式，提前分配扩展数组并记录每粒子角速度
        if (m_AngularVelocity.m_Mode != FloatCurveMode::Constant)
        {
            pool.AllocAngularVelocity();
            for (uint32_t i = startIndex; i < endIndex; ++i)
            {
                uint32_t seed = static_cast<uint32_t>(i * 6364136223846793005ULL + 1442695040888963407ULL);
                float r = RandFloat(seed);
                pool.m_AngularVelocity[i] = m_AngularVelocity.Evaluate(0.f, r);
            }
        }
    }

    void VansUpdateRotationOverLifetime::Execute(VansParticlePool& pool,
                                                  float deltaTime,
                                                  const glm::mat4&)
    {
        if (m_AngularVelocity.m_Mode == FloatCurveMode::Constant)
        {
            // 常量角速度：所有粒子相同
            float delta = m_AngularVelocity.m_Value * deltaTime;
            for (uint32_t i = 0; i < pool.m_AliveCount; ++i)
                pool.m_Rotation[i] += delta;
        }
        else
        {
            // 每粒子独立角速度（存储在扩展数组中）
            if (!pool.m_AngularVelocity.empty())
            {
                for (uint32_t i = 0; i < pool.m_AliveCount; ++i)
                    pool.m_Rotation[i] += pool.m_AngularVelocity[i] * deltaTime;
            }
        }
    }

    nlohmann::json VansUpdateRotationOverLifetime::Serialize() const
    {
        nlohmann::json j;
        j["module"]          = "UpdateRotationOverLifetime";
        j["angularVelocity"] = m_AngularVelocity.Serialize();
        return j;
    }

    void VansUpdateRotationOverLifetime::Deserialize(const nlohmann::json& j)
    {
        if (j.contains("angularVelocity"))
            m_AngularVelocity.Deserialize(j["angularVelocity"]);
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansUpdateSpriteAnimModule
    // ──────────────────────────────────────────────────────────────────────

    void VansUpdateSpriteAnimModule::ExecuteInit(VansParticlePool& pool,
                                                  uint32_t startIndex,
                                                  uint32_t endIndex,
                                                  const glm::mat4&)
    {
        pool.AllocFrameIndex();
        for (uint32_t i = startIndex; i < endIndex; ++i)
            pool.m_FrameIndex[i] = 0.f;
    }

    void VansUpdateSpriteAnimModule::Execute(VansParticlePool& pool, float /*deltaTime*/,
                                              const glm::mat4&)
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

    nlohmann::json VansUpdateSpriteAnimModule::Serialize() const
    {
        nlohmann::json j;
        j["module"]  = "UpdateSpriteAnim";
        j["columns"] = m_Columns;
        j["rows"]    = m_Rows;
        j["fps"]     = m_FPS;
        return j;
    }

    void VansUpdateSpriteAnimModule::Deserialize(const nlohmann::json& j)
    {
        m_Columns = j.value("columns", 4);
        m_Rows    = j.value("rows",    4);
        m_FPS     = j.value("fps",     0.f);
    }

} // namespace VansGraphics
