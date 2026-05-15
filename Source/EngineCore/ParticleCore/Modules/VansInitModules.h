#pragma once
#include "VansParticleModule.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <string>

namespace VansGraphics
{
    // ============================================================
    // Initialize 模块系列
    // 这些模块在粒子刚生成时对指定槽位范围执行一次。
    // 所有模块重写 ExecuteInit；Execute 不做任何事。
    // ============================================================

    // ── InitLifetime — 初始化粒子生命周期 ─────────────────────────────────
    class VansInitLifetimeModule : public VansParticleModule
    {
    public:
        VansFloatCurve m_Lifetime;  // 生命周期曲线（RandomBetween 最常用）

        VansInitLifetimeModule()
        {
            m_Lifetime.m_Mode  = FloatCurveMode::RandomBetween;
            m_Lifetime.m_Min   = 1.f;
            m_Lifetime.m_Max   = 2.f;
        }

        void Execute(VansParticlePool&, float, const glm::mat4&) override {}

        void ExecuteInit(VansParticlePool& pool, uint32_t startIndex,
                         uint32_t endIndex, const glm::mat4&) override;

        nlohmann::json Serialize() const override;
        void Deserialize(const nlohmann::json& j) override;
    };

    // ── InitVelocity — 初始化粒子初速度 ───────────────────────────────────
    // 支持 Cone 圆锥发射和 Random 全向随机两种模式
    enum class VansInitVelocityMode { Cone, Random };

    class VansInitVelocityModule : public VansParticleModule
    {
    public:
        VansInitVelocityMode m_VelocityMode = VansInitVelocityMode::Cone;
        float                m_ConeAngle    = 25.f;  // 圆锥半角（度）
        float                m_Speed        = 2.f;   // 初速度大小

        void Execute(VansParticlePool&, float, const glm::mat4&) override {}

        void ExecuteInit(VansParticlePool& pool, uint32_t startIndex,
                         uint32_t endIndex, const glm::mat4& localToWorld) override;

        nlohmann::json Serialize() const override;
        void Deserialize(const nlohmann::json& j) override;
    };

    // ── InitSize — 初始化粒子大小 ──────────────────────────────────────────
    class VansInitSizeModule : public VansParticleModule
    {
    public:
        VansFloatCurve m_Size;

        VansInitSizeModule()
        {
            m_Size.m_Mode = FloatCurveMode::RandomBetween;
            m_Size.m_Min  = 0.05f;
            m_Size.m_Max  = 0.15f;
        }

        void Execute(VansParticlePool&, float, const glm::mat4&) override {}

        void ExecuteInit(VansParticlePool& pool, uint32_t startIndex,
                         uint32_t endIndex, const glm::mat4&) override;

        nlohmann::json Serialize() const override;
        void Deserialize(const nlohmann::json& j) override;
    };

    // ── InitColor — 初始化粒子颜色 ─────────────────────────────────────────
    class VansInitColorModule : public VansParticleModule
    {
    public:
        glm::vec4 m_Color = glm::vec4(1.f);

        void Execute(VansParticlePool&, float, const glm::mat4&) override {}

        void ExecuteInit(VansParticlePool& pool, uint32_t startIndex,
                         uint32_t endIndex, const glm::mat4&) override;

        nlohmann::json Serialize() const override;
        void Deserialize(const nlohmann::json& j) override;
    };

    // ── InitRotation — 初始化粒子旋转角 ───────────────────────────────────
    class VansInitRotationModule : public VansParticleModule
    {
    public:
        VansFloatCurve m_Angle;

        VansInitRotationModule()
        {
            m_Angle.m_Mode = FloatCurveMode::RandomBetween;
            m_Angle.m_Min  = 0.f;
            m_Angle.m_Max  = 360.f;
        }

        void Execute(VansParticlePool&, float, const glm::mat4&) override {}

        void ExecuteInit(VansParticlePool& pool, uint32_t startIndex,
                         uint32_t endIndex, const glm::mat4&) override;

        nlohmann::json Serialize() const override;
        void Deserialize(const nlohmann::json& j) override;
    };

    // ── InitPositionShape — 形状发射器（位置初始化）────────────────────────
    enum class VansEmitterShape { Sphere, Box, Cone, Disk, Edge };

    class VansInitPositionModule : public VansParticleModule
    {
    public:
        VansEmitterShape m_Shape  = VansEmitterShape::Cone;
        float            m_Radius = 0.2f;
        float            m_Arc    = 360.f;  // Cone/Disk 弧度范围（度）

        void Execute(VansParticlePool&, float, const glm::mat4&) override {}

        void ExecuteInit(VansParticlePool& pool, uint32_t startIndex,
                         uint32_t endIndex, const glm::mat4& localToWorld) override;

        nlohmann::json Serialize() const override;
        void Deserialize(const nlohmann::json& j) override;
    };

} // namespace VansGraphics
