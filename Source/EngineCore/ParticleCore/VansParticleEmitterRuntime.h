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
    bool m_Enabled = true;
    VansParticlePool m_ParticlePool;
    uint64_t m_DroppedSpawns = 0;
    uint64_t m_BreakCount = 0;
    void ResetSimulation();
    void ResetEmission();
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
    void FillInstanceData(std::vector<VansParticleInstanceData>& outBuffer) const;
    void FillVolumetricInstanceData(std::vector<VansVolumetricParticleInstanceData>& outBuffer) const;
    void FillRibbonData(std::vector<VansParticleRibbonStrip>& strips) const;
private:
    uint32_t SpawnParticles(uint32_t count, const glm::mat4& transform, float remainingTime, bool detachedRoot = false);
    void DetachRoot();
    uint32_t NextRandomSeed();
    bool IsRibbon() const { return m_Definition.m_RendererConfig.m_Type == VansParticleRendererType::Ribbon; }
    const VansParticleEmitter& m_Definition;
    double m_SpawnAccum = 0;
    double m_EmissionTime = 0;
    struct BurstState { uint64_t cyclesDone = 0; };
    std::vector<BurstState> m_BurstStates;
    uint32_t m_RandomSeed = 0x9e3779b9u, m_RandomState = 0x9e3779b9u;
    uint64_t m_RibbonId = 1, m_Sequence = 0;
    glm::mat4 m_Anchor{1.0f};
    glm::vec3 m_RenderRootPosition{0.0f};
    bool m_AnchorValid = false, m_RootAttached = false;
};
}
