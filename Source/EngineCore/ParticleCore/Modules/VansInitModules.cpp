#include "VansInitModules.h"
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <cmath>

namespace VansGraphics
{
    // ── 全局随机种子序列（线程不安全，仅用于粒子初始化）─────────────────
    // ──────────────────────────────────────────────────────────────────────
    // VansInitLifetimeModule
    // ──────────────────────────────────────────────────────────────────────

    void VansInitLifetimeModule::ExecuteInit(VansParticlePool& pool,
                                             uint32_t startIndex,
                                             uint32_t endIndex,
                                             const glm::mat4&) const
    {
        for (uint32_t i = startIndex; i < endIndex; ++i)
        {
			uint32_t& seed = pool.m_SeedRandom[i];
            float r         = RandFloat(seed);
            float lifetime  = m_Lifetime.Evaluate(0.f, r);
            if (m_CollectionAgeOperation != VansParticleLifetimeAgeOperation::None)
            {
                float time = pool.m_InitializationCollectionAge;
                if (m_CollectionAgeLoopPeriod > 0) time = std::fmod(time, m_CollectionAgeLoopPeriod);
                const bool random = m_CollectionAgeCurve.m_Mode == FloatCurveMode::RandomBetween
                    || m_CollectionAgeCurve.m_Mode == FloatCurveMode::RandomBetweenCurves;
                const float value = m_CollectionAgeCurve.Evaluate(time, random ? RandFloat(seed) : 0.f);
                lifetime = m_CollectionAgeOperation == VansParticleLifetimeAgeOperation::Replace
                    ? value : lifetime * value;
            }
            if (lifetime < 0.01f) lifetime = 0.01f;  // 防止生命周期为零
            pool.m_LifeTime[i]   = lifetime;
            pool.m_Age[i]        = 0.f;
            pool.m_NormalizedAge[i] = 0.f;
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansInitVelocityModule
    // ──────────────────────────────────────────────────────────────────────

    void VansInitVelocityModule::ExecuteInit(VansParticlePool& pool,
                                             uint32_t startIndex,
                                             uint32_t endIndex,
                                             const glm::mat4& localToWorld) const
    {
        // 提取发射器的局部方向（默认向上 +Y 轴）
        glm::vec3 worldUp = glm::normalize(
            glm::vec3(localToWorld * glm::vec4(0.f, 1.f, 0.f, 0.f)));

        for (uint32_t i = startIndex; i < endIndex; ++i)
        {
			uint32_t& seed = pool.m_SeedRandom[i];
            glm::vec3 velocity;
            if (m_VelocityMode == VansInitVelocityMode::Cone)
            {
                // 在圆锥内随机方向计算速度
                float angleRad    = glm::radians(m_ConeAngle);
                float cosMaxAngle = std::cos(angleRad);

                float cosTheta = cosMaxAngle + RandFloat(seed) * (1.f - cosMaxAngle);
                float sinTheta = std::sqrt(1.f - cosTheta * cosTheta);
                float phi      = RandFloat(seed) * 2.f * glm::pi<float>();

                // 构建局部圆锥方向向量（以 worldUp 为轴）
                glm::vec3 localDir(sinTheta * std::cos(phi),
                                   cosTheta,
                                   sinTheta * std::sin(phi));

                // 将向量旋转到 worldUp 方向
                glm::vec3 up    = glm::vec3(0.f, 1.f, 0.f);
                float dot       = glm::dot(up, worldUp);
                if (dot < -0.9999f)
                {
                    velocity = -localDir * m_Speed;
                }
                else
                {
                    glm::quat q = glm::rotation(up, worldUp);
                    velocity    = q * localDir * m_Speed;
                }
            }
            else if (m_VelocityMode == VansInitVelocityMode::Box)
            {
                glm::vec3 random;
                for (int axis=0;axis<3;++axis) random[axis]=RandFloat(seed);
                velocity=glm::vec3(localToWorld*glm::vec4(glm::mix(m_MinVelocity,m_MaxVelocity,random),0));
            }
            else // Random 全向
            {
                float theta = RandFloat(seed) * 2.f * glm::pi<float>();
                float phi   = std::acos(2.f * RandFloat(seed) - 1.f);
                velocity = glm::vec3(
                    std::sin(phi) * std::cos(theta),
                    std::cos(phi),
                    std::sin(phi) * std::sin(theta)) * m_Speed;
            }

            pool.m_Velocity[i] = velocity;

            // 若扩展数组已分配，同步写入初始速度（StretchedBillboard 需要）
            if (!pool.m_InitialVelocity.empty())
                pool.m_InitialVelocity[i] = velocity;
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansInitSizeModule
    // ──────────────────────────────────────────────────────────────────────

    void VansInitSizeModule::ExecuteInit(VansParticlePool& pool,
                                         uint32_t startIndex,
                                         uint32_t endIndex,
                                         const glm::mat4&) const
    {
        for (uint32_t i = startIndex; i < endIndex; ++i)
        {
			uint32_t& seed = pool.m_SeedRandom[i];
            float r         = RandFloat(seed);
            const bool random = m_CollectionAgeScale.m_Mode == FloatCurveMode::RandomBetween
                || m_CollectionAgeScale.m_Mode == FloatCurveMode::RandomBetweenCurves;
            pool.m_Size[i] = m_Size.Evaluate(0.f, r) * m_CollectionAgeScale.Evaluate(
                pool.m_InitializationCollectionAge, random ? RandFloat(seed) : 0.f);
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansInitColorModule
    // ──────────────────────────────────────────────────────────────────────

    void VansInitColorModule::ExecuteInit(VansParticlePool& pool,
                                          uint32_t startIndex,
                                          uint32_t endIndex,
                                          const glm::mat4&) const
    {
        for (uint32_t i = startIndex; i < endIndex; ++i)
        {
            pool.m_Color[i] = m_Color;
            for (const auto& scale : m_AlphaScales)
            {
                const bool random = scale.m_Mode == FloatCurveMode::RandomBetween
                    || scale.m_Mode == FloatCurveMode::RandomBetweenCurves;
                const float value = scale.Evaluate(0.f, random ? RandFloat(pool.m_SeedRandom[i]) : 0.f);
                pool.m_Color[i].a *= value;
            }
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansInitRotationModule
    // ──────────────────────────────────────────────────────────────────────

    void VansInitRotationModule::ExecuteInit(VansParticlePool& pool,
                                              uint32_t startIndex,
                                              uint32_t endIndex,
                                              const glm::mat4&) const
    {
        for (uint32_t i = startIndex; i < endIndex; ++i)
        {
			uint32_t& seed = pool.m_SeedRandom[i];
            float r              = RandFloat(seed);
            pool.m_Rotation[i]   = m_Angle.Evaluate(0.f, r);
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansInitPositionModule
    // ──────────────────────────────────────────────────────────────────────

    void VansInitPositionModule::ExecuteInit(VansParticlePool& pool,
                                              uint32_t startIndex,
                                              uint32_t endIndex,
                                              const glm::mat4& localToWorld) const
    {
        glm::vec3 origin = glm::vec3(localToWorld[3]);

        for (uint32_t i = startIndex; i < endIndex; ++i)
        {
			uint32_t& seed = pool.m_SeedRandom[i];
            glm::vec3 localPos(0.f);

            switch (m_Shape)
            {
            case VansEmitterShape::Sphere:
            {
                // 球壳表面均匀随机点
                float theta = RandFloat(seed) * 2.f * glm::pi<float>();
                float phi   = std::acos(2.f * RandFloat(seed) - 1.f);
                float r     = m_Radius;
                localPos = glm::vec3(
                    r * std::sin(phi) * std::cos(theta),
                    r * std::cos(phi),
                    r * std::sin(phi) * std::sin(theta));
                break;
            }
            case VansEmitterShape::Box:
            {
                localPos = glm::vec3(
                    RandRange(seed, -m_Radius, m_Radius),
                    RandRange(seed, -m_Radius, m_Radius),
                    RandRange(seed, -m_Radius, m_Radius));
                break;
            }
            case VansEmitterShape::Cone:
            case VansEmitterShape::Disk:
            {
                // 在圆盘范围内均匀随机
                float arcRad = glm::radians(m_Arc);
                float angle  = RandFloat(seed) * arcRad;
                float dist   = m_Radius * std::sqrt(RandFloat(seed));
                localPos = glm::vec3(dist * std::cos(angle), 0.f, dist * std::sin(angle));
                break;
            }
            case VansEmitterShape::Edge:
            {
                // 沿 X 轴线段均匀随机
                float t  = RandRange(seed, -m_Radius, m_Radius);
                localPos = glm::vec3(t, 0.f, 0.f);
                break;
            }
            }

            // 将局部位置变换到世界空间
            pool.m_Position[i] = glm::vec3(localToWorld * glm::vec4(localPos, 1.f));
        }
    }

    void VansInitPositionOffsetModule::ExecuteInit(VansParticlePool& pool,
        uint32_t startIndex,uint32_t endIndex,const glm::mat4& localToWorld) const
    {
        for (uint32_t i=startIndex;i<endIndex;++i) {
            auto& seed=pool.m_SeedRandom[i];
            glm::vec3 sample;
            for (int axis=0;axis<3;++axis) sample[axis]=RandFloat(seed);
            const auto offset=glm::mix(m_MinOffset,m_MaxOffset,sample);
            pool.m_Position[i]+=m_LocalCoordinates ? glm::vec3(localToWorld*glm::vec4(offset,0)) : offset;
        }
    }

} // namespace VansGraphics
