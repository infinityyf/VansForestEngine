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

    enum class VansParticleLifetimeAgeOperation { None, Multiply, Replace };

    // ── InitLifetime — 初始化粒子生命周期 ─────────────────────────────────
    class VansInitLifetimeModule : public VansParticleModule
    {
    public:
        VansFloatCurve m_Lifetime;  // 生命周期曲线（RandomBetween 最常用）
        VansParticleLifetimeAgeOperation m_CollectionAgeOperation = VansParticleLifetimeAgeOperation::None;
        VansFloatCurve m_CollectionAgeCurve;
        float m_CollectionAgeLoopPeriod = 0; // 0 为端点保持；正数为从零开始的秒制周期。

        VansInitLifetimeModule()
        {
            m_Lifetime.m_Mode  = FloatCurveMode::RandomBetween;
            m_Lifetime.m_Min   = 1.f;
            m_Lifetime.m_Max   = 2.f;
        }

        void Execute(VansParticlePool&, float, const glm::mat4&) const override {}

        void ExecuteInit(VansParticlePool& pool, uint32_t startIndex,
                         uint32_t endIndex, const glm::mat4&) const override;

    };

    // ── InitVelocity — 初始化粒子初速度 ───────────────────────────────────
    // 支持 Cone 圆锥发射、Random 全向随机与 Box 分轴速度范围。
    enum class VansInitVelocityMode { Cone, Random, Box };

    class VansInitVelocityModule : public VansParticleModule
    {
    public:
        VansInitVelocityMode m_VelocityMode = VansInitVelocityMode::Cone;
        float                m_ConeAngle    = 25.f;  // 圆锥半角（度）
        float                m_Speed        = 2.f;   // 初速度大小
        glm::vec3 m_MinVelocity{0}, m_MaxVelocity{0};

        void Execute(VansParticlePool&, float, const glm::mat4&) const override {}

        void ExecuteInit(VansParticlePool& pool, uint32_t startIndex,
                         uint32_t endIndex, const glm::mat4& localToWorld) const override;

    };

    // ── InitSize — 初始化粒子大小 ──────────────────────────────────────────
    class VansInitSizeModule : public VansParticleModule
    {
    public:
        VansFloatCurve m_Size;
        VansFloatCurve m_CollectionAgeScale; // 出生时按发射点的集合秒钟采样；默认倍率 1。

        VansInitSizeModule()
        {
            m_Size.m_Mode = FloatCurveMode::RandomBetween;
            m_Size.m_Min  = 0.05f;
            m_Size.m_Max  = 0.15f;
        }

        void Execute(VansParticlePool&, float, const glm::mat4&) const override {}

        void ExecuteInit(VansParticlePool& pool, uint32_t startIndex,
                         uint32_t endIndex, const glm::mat4&) const override;

    };

    // ── InitColor — 初始化粒子颜色 ─────────────────────────────────────────
    class VansInitColorModule : public VansParticleModule
    {
    public:
        glm::vec4 m_Color = glm::vec4(1.f);
        // 独立的初始化 alpha 倍率；默认空，不消耗随机数或分配粒子缓存。
        std::vector<VansFloatCurve> m_AlphaScales;

        void Execute(VansParticlePool&, float, const glm::mat4&) const override {}

        void ExecuteInit(VansParticlePool& pool, uint32_t startIndex,
                         uint32_t endIndex, const glm::mat4&) const override;

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

        void Execute(VansParticlePool&, float, const glm::mat4&) const override {}

        void ExecuteInit(VansParticlePool& pool, uint32_t startIndex,
                         uint32_t endIndex, const glm::mat4&) const override;

    };

    // ── InitPositionShape — 形状发射器（位置初始化）────────────────────────
    enum class VansEmitterShape { Sphere, Box, Cone, Disk, Edge };

    class VansInitPositionModule : public VansParticleModule
    {
    public:
        VansEmitterShape m_Shape  = VansEmitterShape::Cone;
        float            m_Radius = 0.2f;
        float            m_Arc    = 360.f;  // Cone/Disk 弧度范围（度）

        void Execute(VansParticlePool&, float, const glm::mat4&) const override {}

        void ExecuteInit(VansParticlePool& pool, uint32_t startIndex,
                         uint32_t endIndex, const glm::mat4& localToWorld) const override;

    };

    class VansInitPositionOffsetModule : public VansParticleModule
    {
    public:
        glm::vec3 m_MinOffset{0},m_MaxOffset{0};
        bool m_LocalCoordinates=true;
        void Execute(VansParticlePool&,float,const glm::mat4&) const override {}
        void ExecuteInit(VansParticlePool& pool,uint32_t startIndex,uint32_t endIndex,
                         const glm::mat4& localToWorld) const override;
    };

} // namespace VansGraphics
