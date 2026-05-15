#include "VansInitModules.h"
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <cmath>
#include <ctime>

namespace VansGraphics
{
    // ── 全局随机种子序列（线程不安全，仅用于粒子初始化）─────────────────
    static uint32_t s_GlobalSeed = static_cast<uint32_t>(std::time(nullptr));

    // ──────────────────────────────────────────────────────────────────────
    // VansInitLifetimeModule
    // ──────────────────────────────────────────────────────────────────────

    void VansInitLifetimeModule::ExecuteInit(VansParticlePool& pool,
                                             uint32_t startIndex,
                                             uint32_t endIndex,
                                             const glm::mat4&)
    {
        for (uint32_t i = startIndex; i < endIndex; ++i)
        {
            float r         = RandFloat(s_GlobalSeed);
            float lifetime  = m_Lifetime.Evaluate(0.f, r);
            if (lifetime < 0.01f) lifetime = 0.01f;  // 防止生命周期为零
            pool.m_LifeTime[i]   = lifetime;
            pool.m_Age[i]        = 0.f;
            pool.m_NormalizedAge[i] = 0.f;
        }
    }

    nlohmann::json VansInitLifetimeModule::Serialize() const
    {
        nlohmann::json j;
        j["module"]   = "InitLifetime";
        j["lifetime"] = m_Lifetime.Serialize();
        return j;
    }

    void VansInitLifetimeModule::Deserialize(const nlohmann::json& j)
    {
        if (j.contains("lifetime"))
            m_Lifetime.Deserialize(j["lifetime"]);
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansInitVelocityModule
    // ──────────────────────────────────────────────────────────────────────

    void VansInitVelocityModule::ExecuteInit(VansParticlePool& pool,
                                             uint32_t startIndex,
                                             uint32_t endIndex,
                                             const glm::mat4& localToWorld)
    {
        // 提取发射器的局部方向（默认向上 +Y 轴）
        glm::vec3 worldUp = glm::normalize(
            glm::vec3(localToWorld * glm::vec4(0.f, 1.f, 0.f, 0.f)));

        for (uint32_t i = startIndex; i < endIndex; ++i)
        {
            glm::vec3 velocity;
            if (m_VelocityMode == VansInitVelocityMode::Cone)
            {
                // 在圆锥内随机方向计算速度
                float angleRad    = glm::radians(m_ConeAngle);
                float cosMaxAngle = std::cos(angleRad);

                float cosTheta = cosMaxAngle + RandFloat(s_GlobalSeed) * (1.f - cosMaxAngle);
                float sinTheta = std::sqrt(1.f - cosTheta * cosTheta);
                float phi      = RandFloat(s_GlobalSeed) * 2.f * glm::pi<float>();

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
            else // Random 全向
            {
                float theta = RandFloat(s_GlobalSeed) * 2.f * glm::pi<float>();
                float phi   = std::acos(2.f * RandFloat(s_GlobalSeed) - 1.f);
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

    nlohmann::json VansInitVelocityModule::Serialize() const
    {
        nlohmann::json j;
        j["module"]    = "InitVelocity";
        j["mode"]      = (m_VelocityMode == VansInitVelocityMode::Cone) ? "Cone" : "Random";
        j["angle"]     = m_ConeAngle;
        j["speed"]     = m_Speed;
        return j;
    }

    void VansInitVelocityModule::Deserialize(const nlohmann::json& j)
    {
        std::string mode = j.value("mode", "Cone");
        m_VelocityMode = (mode == "Random") ? VansInitVelocityMode::Random
                                            : VansInitVelocityMode::Cone;
        m_ConeAngle = j.value("angle", 25.f);
        m_Speed     = j.value("speed", 2.f);
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansInitSizeModule
    // ──────────────────────────────────────────────────────────────────────

    void VansInitSizeModule::ExecuteInit(VansParticlePool& pool,
                                         uint32_t startIndex,
                                         uint32_t endIndex,
                                         const glm::mat4&)
    {
        for (uint32_t i = startIndex; i < endIndex; ++i)
        {
            float r         = RandFloat(s_GlobalSeed);
            pool.m_Size[i]  = m_Size.Evaluate(0.f, r);
        }
    }

    nlohmann::json VansInitSizeModule::Serialize() const
    {
        nlohmann::json j;
        j["module"] = "InitSize";
        j["size"]   = m_Size.Serialize();
        return j;
    }

    void VansInitSizeModule::Deserialize(const nlohmann::json& j)
    {
        if (j.contains("size"))
            m_Size.Deserialize(j["size"]);
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansInitColorModule
    // ──────────────────────────────────────────────────────────────────────

    void VansInitColorModule::ExecuteInit(VansParticlePool& pool,
                                          uint32_t startIndex,
                                          uint32_t endIndex,
                                          const glm::mat4&)
    {
        for (uint32_t i = startIndex; i < endIndex; ++i)
            pool.m_Color[i] = m_Color;
    }

    nlohmann::json VansInitColorModule::Serialize() const
    {
        nlohmann::json j;
        j["module"] = "InitColor";
        j["color"]  = { m_Color.r, m_Color.g, m_Color.b, m_Color.a };
        return j;
    }

    void VansInitColorModule::Deserialize(const nlohmann::json& j)
    {
        if (j.contains("color") && j["color"].is_array() && j["color"].size() >= 4)
        {
            m_Color = glm::vec4(
                j["color"][0].get<float>(),
                j["color"][1].get<float>(),
                j["color"][2].get<float>(),
                j["color"][3].get<float>()
            );
        }
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansInitRotationModule
    // ──────────────────────────────────────────────────────────────────────

    void VansInitRotationModule::ExecuteInit(VansParticlePool& pool,
                                              uint32_t startIndex,
                                              uint32_t endIndex,
                                              const glm::mat4&)
    {
        for (uint32_t i = startIndex; i < endIndex; ++i)
        {
            float r              = RandFloat(s_GlobalSeed);
            pool.m_Rotation[i]   = m_Angle.Evaluate(0.f, r);
        }
    }

    nlohmann::json VansInitRotationModule::Serialize() const
    {
        nlohmann::json j;
        j["module"] = "InitRotation";
        j["angle"]  = m_Angle.Serialize();
        return j;
    }

    void VansInitRotationModule::Deserialize(const nlohmann::json& j)
    {
        if (j.contains("angle"))
            m_Angle.Deserialize(j["angle"]);
    }

    // ──────────────────────────────────────────────────────────────────────
    // VansInitPositionModule
    // ──────────────────────────────────────────────────────────────────────

    void VansInitPositionModule::ExecuteInit(VansParticlePool& pool,
                                              uint32_t startIndex,
                                              uint32_t endIndex,
                                              const glm::mat4& localToWorld)
    {
        glm::vec3 origin = glm::vec3(localToWorld[3]);

        for (uint32_t i = startIndex; i < endIndex; ++i)
        {
            glm::vec3 localPos(0.f);

            switch (m_Shape)
            {
            case VansEmitterShape::Sphere:
            {
                // 球壳表面均匀随机点
                float theta = RandFloat(s_GlobalSeed) * 2.f * glm::pi<float>();
                float phi   = std::acos(2.f * RandFloat(s_GlobalSeed) - 1.f);
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
                    RandRange(s_GlobalSeed, -m_Radius, m_Radius),
                    RandRange(s_GlobalSeed, -m_Radius, m_Radius),
                    RandRange(s_GlobalSeed, -m_Radius, m_Radius));
                break;
            }
            case VansEmitterShape::Cone:
            case VansEmitterShape::Disk:
            {
                // 在圆盘范围内均匀随机
                float arcRad = glm::radians(m_Arc);
                float angle  = RandFloat(s_GlobalSeed) * arcRad;
                float dist   = m_Radius * std::sqrt(RandFloat(s_GlobalSeed));
                localPos = glm::vec3(dist * std::cos(angle), 0.f, dist * std::sin(angle));
                break;
            }
            case VansEmitterShape::Edge:
            {
                // 沿 X 轴线段均匀随机
                float t  = RandRange(s_GlobalSeed, -m_Radius, m_Radius);
                localPos = glm::vec3(t, 0.f, 0.f);
                break;
            }
            }

            // 将局部位置变换到世界空间
            pool.m_Position[i] = glm::vec3(localToWorld * glm::vec4(localPos, 1.f));
        }
    }

    nlohmann::json VansInitPositionModule::Serialize() const
    {
        auto shapeToStr = [](VansEmitterShape s) -> std::string {
            switch (s)
            {
            case VansEmitterShape::Sphere: return "Sphere";
            case VansEmitterShape::Box:    return "Box";
            case VansEmitterShape::Cone:   return "Cone";
            case VansEmitterShape::Disk:   return "Disk";
            case VansEmitterShape::Edge:   return "Edge";
            default:                       return "Cone";
            }
        };
        nlohmann::json j;
        j["module"] = "InitPositionShape";
        j["shape"]  = shapeToStr(m_Shape);
        j["radius"] = m_Radius;
        j["arc"]    = m_Arc;
        return j;
    }

    void VansInitPositionModule::Deserialize(const nlohmann::json& j)
    {
        std::string shapeStr = j.value("shape", "Cone");
        if      (shapeStr == "Sphere") m_Shape = VansEmitterShape::Sphere;
        else if (shapeStr == "Box")    m_Shape = VansEmitterShape::Box;
        else if (shapeStr == "Disk")   m_Shape = VansEmitterShape::Disk;
        else if (shapeStr == "Edge")   m_Shape = VansEmitterShape::Edge;
        else                           m_Shape = VansEmitterShape::Cone;

        m_Radius = j.value("radius", 0.2f);
        m_Arc    = j.value("arc", 360.f);
    }

} // namespace VansGraphics
