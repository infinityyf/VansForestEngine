#pragma once
#include "VansParticleEmitter.h"
#include "VansParticleFrameData.h"

namespace VansGraphics
{
class VansParticleEmitterRuntime
{
public:
    explicit VansParticleEmitterRuntime(const VansParticleEmitter& definition);
    const VansParticleEmitter& Definition() const { return m_Definition; }
    bool IsEnabled() const { return m_Enabled; }
    const VansParticlePool& ParticlePool() const { return m_ParticlePool; }
    uint32_t AliveCount() const { return m_ParticlePool.m_AliveCount; }
    uint64_t DroppedSpawns() const { return m_DroppedSpawns; }
    uint64_t BreakCount() const { return m_BreakCount; }
    void ResetSimulation();
    void ResetEmission();
    void SetEmissionPoints(const std::vector<VansParticleEmissionPoint>& points);
    void SetRandomSeed(uint32_t seed);
    void EmitBurst(uint32_t count, const glm::mat4& localToWorld);
    void Update(float deltaTime, const glm::mat4& localToWorld, bool emitting = true);
    void Skip(float seconds, const glm::mat4& localToWorld, bool emitting);
    void StopEmitting(bool forceDetach = false);
    void ResumeEmission();
    void SetAnchor(const glm::mat4& transform);
    void ResetAnchor(const glm::mat4& transform);
    void SetRenderAnchor(const glm::vec3& position) { m_RenderRootPosition = position; }
    void ClearParticles() { m_ParticlePool.m_AliveCount = 0; m_RootAttached = false; }
    void FillInstanceData(std::vector<VansParticleInstanceData>& outBuffer,
        float effectAge = 0, float effectSeconds = 0) const;
    void FillVolumetricInstanceData(std::vector<VansVolumetricParticleInstanceData>& outBuffer) const;
    void FillRibbonData(std::vector<VansParticleRibbonStrip>& strips) const;
private:
    friend class VansParticleRuntime;
    uint32_t SpawnParticles(uint32_t count, const glm::mat4& transform, float remainingTime,
        bool detachedRoot = false, float scale = 1, float collectionBorn = 0, float collectionAge = 0);
    void DetachRoot();
    uint32_t NextRandomSeed();
    bool IsRibbon() const { return m_Definition.m_RendererConfig.m_Type == VansParticleRendererType::Ribbon; }
    const VansParticleEmitter& m_Definition;
    bool m_Enabled = true;
    VansParticlePool m_ParticlePool;
    uint64_t m_DroppedSpawns = 0;
    uint64_t m_BreakCount = 0;
    double m_EmissionTime = 0;
    double m_CollectionTime = 0; // Runtime 提供的本步起始模拟时间，不另行推进。
    struct BurstState { uint64_t cyclesDone = 0; };
    struct EmissionState { double accum = 0, age = 0; std::vector<BurstState> bursts; };
    struct PointState { VansParticleEmissionPoint point; EmissionState emission; };
    EmissionState m_DefaultEmission;
    std::vector<PointState> m_EmissionPoints;
    bool m_UsesEmissionPoints = false;
    void Schedule(EmissionState& state, const glm::mat4& from, const glm::mat4& to,
        float seconds, float offset, float fullStep, float scale, bool drop, float collectionBorn = 0);
    void ScheduleInputs(float seconds, const glm::mat4& transform, bool drop);
    uint32_t m_RandomSeed = 0x9e3779b9u, m_RandomState = 0x9e3779b9u;
    uint64_t m_RibbonId = 1, m_Sequence = 0;
    glm::mat4 m_Anchor{1.0f};
    glm::vec3 m_RenderRootPosition{0.0f};
    bool m_AnchorValid = false, m_RootAttached = false;
};
}
