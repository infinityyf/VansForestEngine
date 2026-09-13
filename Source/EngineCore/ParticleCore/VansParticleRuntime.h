#pragma once
#include "VansParticleAsset.h"
#include "VansParticleEmitterRuntime.h"
#include <array>
#include <atomic>

namespace VansGraphics
{
enum class VansParticlePlaybackState { Stopped, Delayed, Emitting, Draining, Finished };
class VansParticleRuntime
{
public:
    void SetAsset(std::shared_ptr<const VansParticleAsset> asset);
    const std::shared_ptr<const VansParticleAsset>& GetAsset() const { return m_Asset; }
    VansParticleEmitterRuntime* GetEmitter(std::size_t index)
    { return index < m_Emitters.size() ? m_Emitters[index].get() : nullptr; }
    const VansParticleEmitterRuntime* GetEmitter(std::size_t index) const
    { return index < m_Emitters.size() ? m_Emitters[index].get() : nullptr; }
    glm::mat4 m_LocalToWorld{1.0f};
    glm::vec3 m_EmitterPositionLocal{0.0f};
    std::atomic<uint32_t> m_AliveInstanceCount{0};
    uint64_t m_SubstepOverruns = 0;
    void SetOwnerWorldTransform(const glm::mat4& ownerWorld);
    void DeferFirstUpdate() { m_DeferFirstUpdate = true; }
    void Update(float deltaTime);
    void Play();
    void Pause() { m_Paused = true; }
    void Stop();
    void StopEmitting(bool detach = false);
    // 延后本轮发射结束时间，保留点、年龄、随机状态和累计播放时间。
    bool RefreshEmission();
    void Restart();
    bool Seek(float seconds, float step = 1.0f/60.0f);
    bool CanSeek() const { return !HasRibbon() || !m_HasMovingSource; }
    void SetRandomSeed(uint32_t seed);
    uint32_t GetRandomSeed() const { return m_RandomSeed; }
    void SetSimulationRate(float rate);
    float GetSimulationRate() const { return m_SimulationRate; }
    void Burst(uint32_t count = 1);
    void SetEffectiveEnabled(bool enabled) { m_EffectiveEnabled = enabled; }
    bool IsEffectivelyEnabled() const { return m_EffectiveEnabled; }
    bool IsPlaying() const { return !m_Paused && (m_State == VansParticlePlaybackState::Delayed
        || m_State == VansParticlePlaybackState::Emitting || m_State == VansParticlePlaybackState::Draining); }
    bool IsPaused() const { return m_Paused; }
    bool IsFinished() const { return m_State == VansParticlePlaybackState::Finished || m_State == VansParticlePlaybackState::Stopped; }
    VansParticlePlaybackState GetState() const { return m_State; }
    float GetPlayTime() const { return static_cast<float>(m_Time); }
    void SwapBuffers();
    const VansParticleFrameData& GetFrameData() const { return m_Frames[m_Front]; }
    const std::vector<VansParticleInstanceData>& GetRenderBuffer() const { return GetFrameData().instances; }
    const std::vector<VansVolumetricParticleInstanceData>& GetVolumetricRenderBuffer() const { return GetFrameData().medium; }
    bool HasVolumetricInjectionEnabled() const;
    bool HasRibbon() const;
private:
    void Prewarm();
    void Advance(float seconds, const glm::mat4& transform, bool skip = false);
    void Capture();
    void Finish();
    std::shared_ptr<const VansParticleAsset> m_Asset;
    std::vector<std::unique_ptr<VansParticleEmitterRuntime>> m_Emitters;
    std::array<VansParticleFrameData,2> m_Frames;
    uint32_t m_Front = 0, m_Back = 1;
    VansParticlePlaybackState m_State = VansParticlePlaybackState::Stopped;
    bool m_Paused = false, m_EffectiveEnabled = true, m_DeferFirstUpdate = false, m_Prewarmed = false;
    bool m_OwnerInitialized = false, m_HasMovingSource = false;
    glm::mat4 m_PreviousOwner{1.0f};
    double m_Time = 0, m_CycleTime = 0, m_Accumulator = 0, m_DrainTime = 0, m_DelayRemaining = 0;
    uint32_t m_RandomSeed = 0x9e3779b9u;
    float m_SimulationRate = 1;
};
}
