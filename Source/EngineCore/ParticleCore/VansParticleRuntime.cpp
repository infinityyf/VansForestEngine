#include "VansParticleRuntime.h"
#include <algorithm>
#include <cmath>

namespace VansGraphics
{
void VansParticleRuntime::SetAsset(std::shared_ptr<const VansParticleAsset> asset)
{
    Stop(); m_Emitters.clear(); m_Asset = std::move(asset);
    if (m_Asset) for (const auto& definition : m_Asset->m_Emitters)
        m_Emitters.push_back(definition ? std::make_unique<VansParticleEmitterRuntime>(*definition) : nullptr);
    SetRandomSeed(m_RandomSeed);
}
void VansParticleRuntime::SetOwnerWorldTransform(const glm::mat4& owner)
{
    glm::mat4 next = m_Asset && m_Asset->m_EmissionFrame == VansParticleEmissionFrame::World ? glm::mat4(1) : owner;
    next[3] = owner * glm::vec4(m_EmitterPositionLocal,1);
    if (m_OwnerInitialized)
    {
        for (int column=0; column<4; ++column)
            if (glm::length(next[column]-m_LocalToWorld[column]) > 1.0e-5f) m_HasMovingSource = true;
    }
    else { m_PreviousOwner = next; m_OwnerInitialized = true; }
    m_LocalToWorld = next;
    for (auto& emitter : m_Emitters) if (emitter) emitter->SetAnchor(next);
}
void VansParticleRuntime::Play()
{
    if (!m_Asset) return;
    if (m_State == VansParticlePlaybackState::Stopped || m_State == VansParticlePlaybackState::Finished)
    {
        Stop();
        m_PreviousOwner = m_LocalToWorld;
        for (auto& emitter : m_Emitters) if (emitter) emitter->SetAnchor(m_LocalToWorld);
        m_DelayRemaining = m_Asset->m_StartDelay;
        m_State = m_DelayRemaining > 0 ? VansParticlePlaybackState::Delayed : VansParticlePlaybackState::Emitting;
        if (m_State == VansParticlePlaybackState::Emitting) Prewarm();
    }
    m_Paused = false;
}
void VansParticleRuntime::Prewarm()
{
    if (m_Prewarmed) return;
    m_Prewarmed = true;
    if (!m_Asset->m_Prewarm) return;
    const float step = m_Asset->m_FixedStep > 0 ? m_Asset->m_FixedStep : 1.0f/60.0f;
    double remaining = m_Asset->m_Duration;
    while (remaining > 1.0e-8)
    { const auto dt = float(std::min<double>(step,remaining)); Advance(dt,m_LocalToWorld); remaining -= dt; }
    Capture(); m_Frames[m_Front] = m_Frames[m_Back];
}
void VansParticleRuntime::Stop()
{
    m_State = VansParticlePlaybackState::Stopped; m_Paused = false;
    m_Time = m_CycleTime = m_Accumulator = m_DrainTime = m_DelayRemaining = 0;
    m_Prewarmed = m_DeferFirstUpdate = false;
    for (auto& emitter : m_Emitters) if (emitter) emitter->ResetSimulation();
    m_AliveInstanceCount = 0;
    for (auto& frame : m_Frames) frame = {};
}
void VansParticleRuntime::StopEmitting(bool detach)
{
    if (IsFinished()) return;
    if (m_State != VansParticlePlaybackState::Draining) m_DrainTime = 0;
    m_State = VansParticlePlaybackState::Draining;
    for (auto& emitter : m_Emitters) if (emitter) emitter->StopEmitting(detach);
}
void VansParticleRuntime::Restart() { Stop(); Play(); }
bool VansParticleRuntime::RefreshEmission()
{
    if (!m_Asset || IsFinished()) return false;
    m_CycleTime = m_DrainTime = 0;
    if (m_State == VansParticlePlaybackState::Draining)
    {
        m_State = VansParticlePlaybackState::Emitting;
        for (auto& emitter : m_Emitters) if (emitter) emitter->ResumeEmission();
    }
    return true;
}
void VansParticleRuntime::SetRandomSeed(uint32_t seed)
{
    m_RandomSeed = seed ? seed : 0x9e3779b9u;
    uint32_t emitterSeed = m_RandomSeed;
    for (auto& emitter : m_Emitters)
    { if (emitter) emitter->SetRandomSeed(emitterSeed); emitterSeed = emitterSeed*1664525u+1013904223u; }
}
void VansParticleRuntime::SetSimulationRate(float rate)
{ if (std::isfinite(rate) && rate >= 0 && rate <= 16) m_SimulationRate = rate; }
void VansParticleRuntime::Burst(uint32_t count)
{
    if (!m_Asset || count == 0) return;
    count = std::min(count, 65536u);
    if (IsFinished()) { m_State = VansParticlePlaybackState::Draining; m_DrainTime = 0; }
    for (auto& emitter : m_Emitters) if (emitter && emitter->m_Enabled) emitter->EmitBurst(count,m_LocalToWorld);
    Capture(); m_Frames[m_Front] = m_Frames[m_Back];
}
bool VansParticleRuntime::Seek(float seconds, float step)
{
    if (!m_Asset || !CanSeek() || !std::isfinite(seconds) || seconds < 0 || seconds > 300
        || !std::isfinite(step) || step <= 0) return false;
    const bool paused = !IsPlaying();
    Stop(); m_Prewarmed = true; m_State = VansParticlePlaybackState::Emitting;
    for (auto& emitter : m_Emitters) if (emitter) emitter->SetAnchor(m_LocalToWorld);
    const float fixed = m_Asset->m_FixedStep > 0 ? m_Asset->m_FixedStep : std::max(step,1.0f/240.0f);
    double remaining = seconds;
    while (remaining > 1.0e-8)
    { const auto dt = float(std::min<double>(fixed,remaining)); Advance(dt,m_LocalToWorld); remaining -= dt; }
    m_Paused = paused; Capture(); m_Frames[m_Front] = m_Frames[m_Back];
    return true;
}
void VansParticleRuntime::Finish()
{
    for (auto& emitter : m_Emitters) if (emitter) emitter->ClearParticles();
    m_State = VansParticlePlaybackState::Finished;
    m_AliveInstanceCount = 0;
}
void VansParticleRuntime::Advance(float seconds, const glm::mat4& transform, bool skip)
{
    double remaining = seconds;
    while (remaining > 1.0e-8 && !IsFinished())
    {
        double dt = remaining;
        if (m_State == VansParticlePlaybackState::Emitting)
            dt = std::min(dt, double(m_Asset->m_Duration)-m_CycleTime);
        const bool emitting = m_State == VansParticlePlaybackState::Emitting;
        for (auto& emitter : m_Emitters) if (emitter)
        {
            if (skip) emitter->Skip(float(dt),transform,emitting);
            else emitter->Update(float(dt),transform,emitting);
        }
        m_Time += dt; remaining -= dt;
        if (emitting)
        {
            m_CycleTime += dt;
            if (m_CycleTime >= m_Asset->m_Duration-1.0e-7)
            {
                m_CycleTime = 0;
                if (m_Asset->m_Loop)
                { for (auto& emitter : m_Emitters) if (emitter) emitter->ResetEmission(); }
                else StopEmitting();
            }
        }
        else if (m_State == VansParticlePlaybackState::Draining)
        {
            m_DrainTime += dt;
            uint32_t alive = 0;
            for (const auto& emitter : m_Emitters) if (emitter) alive += emitter->m_ParticlePool.m_AliveCount;
            if (!alive || (m_Asset->m_DrainFade > 0 && m_DrainTime >= m_Asset->m_DrainFade)) Finish();
        }
    }
}
void VansParticleRuntime::Update(float deltaTime)
{
    if (!m_Asset) return;
    if (!std::isfinite(deltaTime) || deltaTime < 0 || !m_EffectiveEnabled || !IsPlaying()) { Capture(); return; }
    if (m_DeferFirstUpdate) { m_DeferFirstUpdate = false; Capture(); return; }
    const double scaled = std::min(double(deltaTime)*m_SimulationRate,300.0);
    double dt = scaled;
    if (m_State == VansParticlePlaybackState::Delayed)
    {
        const double consumed = std::min(m_DelayRemaining,dt);
        m_DelayRemaining -= consumed; dt -= consumed;
        if (m_DelayRemaining > 1.0e-7)
        {
            m_PreviousOwner = m_LocalToWorld;
            for (auto& emitter : m_Emitters) if (emitter) emitter->ResetAnchor(m_LocalToWorld);
            Capture(); return;
        }
        // 延迟期间的运动不是烟带历史；首个出生从延迟结束的子帧姿态开始。
        glm::mat4 start;
        const float alpha = scaled > 0 ? float(consumed/scaled) : 1.0f;
        for (int column=0; column<4; ++column) start[column] = glm::mix(m_PreviousOwner[column],m_LocalToWorld[column],alpha);
        for (auto& emitter : m_Emitters) if (emitter) emitter->ResetAnchor(start);
        m_State = VansParticlePlaybackState::Emitting; Prewarm();
    }
    const auto sample = [&](double t) {
        const float alpha = scaled > 0 ? std::clamp(float(t/scaled),0.0f,1.0f) : 1.0f;
        glm::mat4 result;
        for (int column=0; column<4; ++column) result[column] = glm::mix(m_PreviousOwner[column],m_LocalToWorld[column],alpha);
        return result;
    };
    if (m_Asset->m_FixedStep > 0)
    {
        const double step = m_Asset->m_FixedStep;
        const double oldAccumulator = m_Accumulator;
        m_Accumulator += dt;
        const auto dueSteps = static_cast<uint64_t>(std::floor((m_Accumulator+1.0e-8)/step));
        const double skipped = dueSteps > m_Asset->m_MaxSubsteps
            ? (dueSteps-m_Asset->m_MaxSubsteps)*step : 0;
        // 预算不足时先舍弃旧历史，再模拟最近的区间；否则每帧最后一次 Skip
        // 都会移除刚建立的根部，使持续低帧率下的 FollowSource 永远不可见。
        if (skipped > 0)
        {
            Advance(float(skipped),sample(scaled-dt+skipped-oldAccumulator),true);
            m_Accumulator = std::max(0.0,m_Accumulator-skipped); ++m_SubstepOverruns;
        }
        uint32_t steps = 0;
        while (m_Accumulator+1.0e-8 >= step && steps < m_Asset->m_MaxSubsteps)
        {
            ++steps;
            Advance(float(step),sample(scaled-dt+skipped+steps*step-oldAccumulator));
            m_Accumulator = std::max(0.0,m_Accumulator-step);
        }
    }
    else if (dt > 0) Advance(float(dt),m_LocalToWorld);
    const glm::vec3 root(m_LocalToWorld[3]);
    if (std::isfinite(root.x) && std::isfinite(root.y) && std::isfinite(root.z))
        for (auto& emitter : m_Emitters) if (emitter) emitter->SetRenderAnchor(root);
    m_PreviousOwner = m_LocalToWorld;
    Capture();
}
void VansParticleRuntime::Capture()
{
    auto& frame = m_Frames[m_Back];
    frame.instances.clear(); frame.medium.clear(); frame.ribbons.clear(); frame.emitters.clear();
    uint32_t alive = 0;
    float alpha = 1;
    if (m_State == VansParticlePlaybackState::Draining && m_Asset->m_DrainFade > 0)
        alpha = std::clamp(1.0f-float(m_DrainTime/m_Asset->m_DrainFade),0.0f,1.0f);
    for (uint32_t index=0; index<m_Emitters.size(); ++index)
    {
        const auto& emitter = m_Emitters[index];
        if (!emitter) continue;
        alive += emitter->m_ParticlePool.m_AliveCount;
        if (!m_EffectiveEnabled || !emitter->m_Enabled) continue;
        VansParticleEmitterRange range;
        range.emitterIndex = index;
        range.surfaceFirst = uint32_t(frame.instances.size());
        range.mediumFirst = uint32_t(frame.medium.size());
        range.ribbonFirst = uint32_t(frame.ribbons.size());
        const auto& renderer = emitter->Definition().m_RendererConfig;
        if (renderer.m_Type != VansParticleRendererType::None)
        {
            if (renderer.m_Type == VansParticleRendererType::Billboard) emitter->FillInstanceData(frame.instances);
            if (renderer.m_Type == VansParticleRendererType::Ribbon) emitter->FillRibbonData(frame.ribbons);
        }
        if (renderer.m_Volumetric.m_Enabled) emitter->FillVolumetricInstanceData(frame.medium);
        range.surfaceCount = uint32_t(frame.instances.size())-range.surfaceFirst;
        range.mediumCount = uint32_t(frame.medium.size())-range.mediumFirst;
        range.ribbonCount = uint32_t(frame.ribbons.size())-range.ribbonFirst;
        for (uint32_t i=range.surfaceFirst; i<frame.instances.size(); ++i) frame.instances[i].m_Color.a *= alpha;
        for (uint32_t i=range.mediumFirst; i<frame.medium.size(); ++i) frame.medium[i].m_ScatteringAlbedoExtinction.a *= alpha;
        for (uint32_t i=range.ribbonFirst; i<frame.ribbons.size(); ++i)
            for (auto& point : frame.ribbons[i].points) point.color.a *= alpha;
        frame.emitters.push_back(range);
    }
    m_AliveInstanceCount.store(alive,std::memory_order_release);
}
void VansParticleRuntime::SwapBuffers() { std::swap(m_Front,m_Back); }
bool VansParticleRuntime::HasVolumetricInjectionEnabled() const
{
    return m_EffectiveEnabled && std::any_of(m_Emitters.begin(),m_Emitters.end(),[](const auto& emitter) {
        return emitter && emitter->m_Enabled && emitter->Definition().m_RendererConfig.m_Volumetric.m_Enabled;
    });
}
bool VansParticleRuntime::HasRibbon() const
{
    return std::any_of(m_Emitters.begin(),m_Emitters.end(),[](const auto& emitter) {
        return emitter && emitter->Definition().m_RendererConfig.m_Type == VansParticleRendererType::Ribbon;
    });
}
}
