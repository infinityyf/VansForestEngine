#include "VansParticleManager.h"
#include "../Util/VansProfiler.h"
#include <cassert>
#include <cmath>
#include <chrono>
#include <algorithm>

namespace VansGraphics
{
VansParticleManager::~VansParticleManager() { Shutdown(); }
VansParticleDebugSnapshot VansParticleManager::CaptureDebugSnapshot() const
{
    assert(!m_InFlight);
    VansParticleDebugSnapshot result;
    m_Runtimes.ForEach([&](auto handle, const auto& owned) {
        const auto& runtime = *owned;
        const auto& frame = runtime.GetFrameData();
        for (const auto& range : frame.emitters)
        {
            if (range.ribbonCount == 0) continue;
            ++result.totalEmitters;
            VansParticleDebugEmitter emitter;
            emitter.instance = handle;
            emitter.emitterIndex = range.emitterIndex;
            emitter.effectName = runtime.GetAsset()->m_Name;
            emitter.emitterName = runtime.GetAsset()->m_Emitters[range.emitterIndex]->m_Name;
            for (std::size_t i = range.ribbonFirst; i < std::size_t(range.ribbonFirst) + range.ribbonCount; ++i)
            {
                const auto& strip = frame.ribbons[i];
                result.totalPoints += strip.points.size();
                const auto count = result.emitters.size() < VansParticleDebugSnapshot::MaxEmitters
                    ? std::min(strip.points.size(), VansParticleDebugSnapshot::MaxPoints - result.capturedPoints) : 0;
                if (count == 0) continue;
                VansParticleRibbonStrip copy;
                copy.ribbonId = strip.ribbonId;
                copy.hasSourceRoot = strip.hasSourceRoot;
                copy.points.assign(strip.points.begin(), strip.points.begin() + count);
                emitter.ribbons.push_back(std::move(copy));
                result.capturedPoints += count;
            }
            if (!emitter.ribbons.empty()) result.emitters.push_back(std::move(emitter));
        }
    });
    result.truncated = result.capturedPoints != result.totalPoints;
    return result;
}
Vans::VansGenerationHandle VansParticleManager::Create(std::shared_ptr<const VansParticleAsset> asset)
{
    assert(!m_InFlight);
    if (!asset) return {};
    std::uint64_t capacity = 0;
    for (const auto& emitter : asset->m_Emitters) if (emitter) capacity += emitter->m_MaxParticles;
    if (m_Runtimes.ActiveCount() >= MaxInstances || capacity > MaxPointCapacity-m_PointCapacity)
    { ++m_RejectedInstances; return {}; }
    auto runtime = std::make_unique<VansParticleRuntime>();
    runtime->SetAsset(std::move(asset));
    const auto handle = m_Runtimes.Emplace(std::move(runtime));
    if (handle.IsValid()) m_PointCapacity += capacity;
    return handle;
}
bool VansParticleManager::Destroy(Vans::VansGenerationHandle handle)
{
    WaitForUpdateAndSwap();
    const auto* runtime = Resolve(handle);
    if (!runtime) return false;
    for (const auto& emitter : runtime->GetAsset()->m_Emitters) if (emitter) m_PointCapacity -= emitter->m_MaxParticles;
    return m_Runtimes.Release(handle);
}
VansParticleRuntime* VansParticleManager::Resolve(Vans::VansGenerationHandle handle)
{
    assert(!m_InFlight);
    const auto* runtime = m_Runtimes.Resolve(handle);
    return runtime ? runtime->get() : nullptr;
}
const VansParticleRuntime* VansParticleManager::Resolve(Vans::VansGenerationHandle handle) const
{
    assert(!m_InFlight);
    const auto* runtime = m_Runtimes.Resolve(handle);
    return runtime ? runtime->get() : nullptr;
}
bool VansParticleManager::Queue(Vans::VansGenerationHandle handle, VansParticleControl control, float value, uint32_t index)
{
    assert(!m_InFlight);
    if (!m_Runtimes.Contains(handle) || !std::isfinite(value)) return false;
    if (control == VansParticleControl::Seek && (!Resolve(handle)->CanSeek() || value < 0 || value > 300)) return false;
    m_Commands.push_back({handle, control, value, index});
    return true;
}
void VansParticleManager::Prepare()
{
    assert(!m_InFlight);
    for (const auto& command : m_Commands)
    {
        auto* runtime = Resolve(command.instance);
        if (!runtime) continue;
        switch (command.control)
        {
        case VansParticleControl::Play: runtime->Play(); break;
        case VansParticleControl::Pause: runtime->Pause(); break;
        case VansParticleControl::Stop: runtime->Stop(); break;
        case VansParticleControl::StopEmitting: runtime->StopEmitting(); break;
        case VansParticleControl::DetachAndDrain: runtime->StopEmitting(true); break;
        case VansParticleControl::Restart: runtime->Restart(); break;
        case VansParticleControl::RefreshEmission: runtime->RefreshEmission(); break;
        case VansParticleControl::Seek: runtime->Seek(command.value); break;
        case VansParticleControl::Seed: runtime->SetRandomSeed(command.index); break;
        case VansParticleControl::SimulationRate: runtime->SetSimulationRate(command.value); break;
        case VansParticleControl::Burst: runtime->Burst(command.index); break;
        case VansParticleControl::EmitterEnabled:
            if (auto* emitter = runtime->GetEmitter(command.index)) emitter->m_Enabled = command.value != 0;
            break;
        case VansParticleControl::EffectiveEnabled: runtime->SetEffectiveEnabled(command.value != 0); break;
        case VansParticleControl::DeferFirstUpdate: runtime->DeferFirstUpdate(); break;
        }
    }
    m_Commands.clear();
}
void VansParticleManager::TickMainThread(float deltaTime)
{
    VANS_PROFILE_SCOPE("Particle::TickMainThread", Vans::ProfileCategory::Particles);
    assert(!m_InFlight);
    Prepare();
    if (m_Runtimes.ActiveCount() == 0) { m_SimulationMilliseconds = 0; m_WaitMilliseconds = 0; return; }
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (!m_Running)
    {
        m_Running = true;
        m_UpdateThread = std::thread(&VansParticleManager::UpdateThreadLoop, this);
    }
    m_DeltaTime = deltaTime;
    m_UpdateDone = false;
    m_InFlight = true;
    m_TickPending = true;
    m_TickCV.notify_one();
}
void VansParticleManager::WaitForUpdateAndSwap()
{
    if (!m_InFlight) return;
    const auto started = std::chrono::steady_clock::now();
    {
        VANS_PROFILE_WAIT("Particle::WaitThreadDone");
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_DoneCV.wait(lock, [this] { return m_UpdateDone; });
    }
    m_WaitMilliseconds = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    m_Runtimes.ForEach([](auto, auto& runtime) { runtime->SwapBuffers(); });
    m_InFlight = false;
}
void VansParticleManager::Shutdown()
{
    WaitForUpdateAndSwap();
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Running = false;
        m_TickCV.notify_one();
    }
    if (m_UpdateThread.joinable()) m_UpdateThread.join();
    m_Commands.clear();
    m_Runtimes.Clear();
    m_PointCapacity = m_RejectedInstances = 0;
}
void VansParticleManager::UpdateThreadLoop()
{
    VANS_PROFILE_THREAD("Particle Thread");
    while (true)
    {
        float delta;
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_TickCV.wait(lock, [this] { return !m_Running || m_TickPending; });
            if (!m_Running) return;
            delta = m_DeltaTime;
            m_TickPending = false;
        }
        const auto started = std::chrono::steady_clock::now();
        m_Runtimes.ForEach([delta](auto, auto& runtime) { runtime->Update(delta); });
        m_SimulationMilliseconds = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_UpdateDone = true;
            m_DoneCV.notify_one();
        }
    }
}
}
