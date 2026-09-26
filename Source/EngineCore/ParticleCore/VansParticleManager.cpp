#include "VansParticleManager.h"
#include "../Util/VansProfiler.h"
#include <cassert>
#include <cmath>
#include <chrono>
#include <algorithm>

namespace VansGraphics
{
VansParticleManager::VansParticleManager()
    : m_OwnerThread(std::this_thread::get_id())
{
}
VansParticleManager::~VansParticleManager() { Shutdown(); }
VansParticleDebugSnapshot VansParticleManager::CaptureDebugSnapshot() const
{
    VansParticleDebugSnapshot result;
    if (!CanAccessMainState()) return result;
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
    if (!CanAccessMainState() || !asset) return {};
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
    if (!IsOwnerThread()) return false;
    WaitForUpdateAndSwap();
    const auto* runtime = Resolve(handle);
    if (!runtime) return false;
    for (const auto& emitter : runtime->GetAsset()->m_Emitters) if (emitter) m_PointCapacity -= emitter->m_MaxParticles;
    return m_Runtimes.Release(handle);
}
const VansParticleRuntime* VansParticleManager::Resolve(Vans::VansGenerationHandle handle) const
{
    if (!CanAccessMainState()) return nullptr;
    const auto* runtime = m_Runtimes.Resolve(handle);
    return runtime ? runtime->get() : nullptr;
}
bool VansParticleManager::SetOwnerWorldTransform(Vans::VansGenerationHandle handle, const glm::mat4& ownerWorld)
{
    if (!CanAccessMainState()) return false;
    VansParticleCommand command{handle,VansParticleControl::SetOwnerWorldTransform};
    command.ownerWorld = ownerWorld;
    command.hasOwnerWorld = true;
    if (!CanQueue(command)) return false;
    m_Commands.push_back(std::move(command));
    return true;
}
bool VansParticleManager::SetEmitterPositionLocal(Vans::VansGenerationHandle handle, const glm::vec3& position)
{
    if (!CanAccessMainState()) return false;
    VansParticleCommand command{handle,VansParticleControl::SetEmitterPositionLocal};
    command.emitterPositionLocal = position;
    command.hasEmitterPositionLocal = true;
    if (!CanQueue(command)) return false;
    m_Commands.push_back(std::move(command));
    return true;
}
bool VansParticleManager::Queue(Vans::VansGenerationHandle handle, VansParticleControl control, float value, uint32_t index)
{
    if (!CanAccessMainState()) return false;
    VansParticleCommand command{handle, control, value, index};
    if (!CanQueue(command)) return false;
    m_Commands.push_back(command);
    return true;
}
bool VansParticleManager::QueueBatch(const std::vector<VansParticleCommand>& commands)
{
    if (!CanAccessMainState()) return false;
    if (!std::all_of(commands.begin(), commands.end(),
        [this](const VansParticleCommand& command) { return CanQueue(command); }))
    {
        return false;
    }
    m_Commands.insert(m_Commands.end(), commands.begin(), commands.end());
    return true;
}
bool VansParticleManager::CanQueue(const VansParticleCommand& command) const
{
    const auto* ownedRuntime = m_Runtimes.Resolve(command.instance);
    const VansParticleRuntime* runtime = ownedRuntime ? ownedRuntime->get() : nullptr;
    if (!runtime || !std::isfinite(command.value)) return false;
    if (command.control == VansParticleControl::SetOwnerWorldTransform)
    {
        if (!command.hasOwnerWorld) return false;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                if (!std::isfinite(command.ownerWorld[column][row])) return false;
        return true;
    }
    if (command.control == VansParticleControl::SetEmitterPositionLocal)
        return command.hasEmitterPositionLocal && std::isfinite(command.emitterPositionLocal.x)
            && std::isfinite(command.emitterPositionLocal.y) && std::isfinite(command.emitterPositionLocal.z);
    return command.control != VansParticleControl::Seek ||
        (runtime->CanSeek() && command.value >= 0.0f && command.value <= 300.0f);
}
void VansParticleManager::ApplyCommands(const std::vector<VansParticleCommand>& commands)
{
    for (const auto& command : commands)
    {
        auto* ownedRuntime = m_Runtimes.Resolve(command.instance);
        auto* runtime = ownedRuntime ? ownedRuntime->get() : nullptr;
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
            runtime->SetEmitterEnabled(command.index, command.value != 0);
            break;
        case VansParticleControl::EffectiveEnabled: runtime->SetEffectiveEnabled(command.value != 0); break;
        case VansParticleControl::DeferFirstUpdate: runtime->DeferFirstUpdate(); break;
        case VansParticleControl::SetOwnerWorldTransform: runtime->SetOwnerWorldTransform(command.ownerWorld); break;
        case VansParticleControl::SetEmitterPositionLocal: runtime->SetEmitterPositionLocal(command.emitterPositionLocal); break;
        }
    }
}
bool VansParticleManager::SetSimulationFrozen(bool frozen)
{
    if (!CanAccessMainState()) return false;
    m_SimulationFrozen = frozen;
    return true;
}
bool VansParticleManager::TickMainThread(float deltaTime)
{
    VANS_PROFILE_SCOPE("Particle::TickMainThread", Vans::ProfileCategory::Particles);
    if (!CanAccessMainState()) return false;
    if (m_Runtimes.ActiveCount() == 0)
    {
        m_Commands.clear();
        m_SimulationMilliseconds = m_ResimulationMilliseconds = 0;
        m_ResimulationSteps = m_PendingResimulations = 0;
        m_MainThreadOverlapMilliseconds = m_WaitMilliseconds = 0;
        return false;
    }
    bool needsWorker = !m_SimulationFrozen || !m_Commands.empty();
    if (!needsWorker)
        m_Runtimes.ForEach([&](auto, const auto& runtime) {
            needsWorker = needsWorker || runtime->NeedsWorkerTick();
        });
    if (!needsWorker)
    {
        m_SimulationMilliseconds = m_ResimulationMilliseconds = 0;
        m_ResimulationSteps = m_PendingResimulations = 0;
        m_MainThreadOverlapMilliseconds = m_WaitMilliseconds = 0;
        return false;
    }
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (!m_Running)
    {
        m_Running = true;
        m_UpdateThread = std::thread(&VansParticleManager::UpdateThreadLoop, this);
    }
    m_WorkerCommands = std::move(m_Commands);
    m_Commands.clear();
    m_DeltaTime = m_SimulationFrozen ? 0.0f : deltaTime;
    m_UpdateDone = false;
    m_InFlight.store(true, std::memory_order_release);
    m_TickPending = true;
    m_DispatchedAt = std::chrono::steady_clock::now();
    m_TickCV.notify_one();
    return true;
}
void VansParticleManager::WaitForUpdateAndSwap()
{
    if (!IsOwnerThread() || !m_InFlight.load(std::memory_order_acquire)) return;
    const auto started = std::chrono::steady_clock::now();
    m_MainThreadOverlapMilliseconds = std::chrono::duration<double,std::milli>(
        started-m_DispatchedAt).count();
    {
        VANS_PROFILE_WAIT("Particle::WaitThreadDone");
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_DoneCV.wait(lock, [this] { return m_UpdateDone; });
    }
    m_WaitMilliseconds = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    m_Runtimes.ForEach([](auto, auto& runtime) { runtime->SwapBuffers(); });
    m_InFlight.store(false, std::memory_order_release);
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
    m_WorkerCommands.clear();
    m_Runtimes.Clear();
    m_PointCapacity = m_RejectedInstances = 0;
    m_SimulationMilliseconds = m_ResimulationMilliseconds = 0;
    m_ResimulationSteps = m_PendingResimulations = 0;
    m_MainThreadOverlapMilliseconds = m_WaitMilliseconds = 0;
    m_ResimulationCursor = 0;
}
void VansParticleManager::UpdateThreadLoop()
{
    VANS_PROFILE_THREAD("Particle Thread");
    while (true)
    {
        float delta;
        std::vector<VansParticleCommand> commands;
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_TickCV.wait(lock, [this] { return !m_Running || m_TickPending; });
            if (!m_Running) return;
            delta = m_DeltaTime;
            commands = std::move(m_WorkerCommands);
            m_TickPending = false;
        }
        ApplyCommands(commands);
        std::uint32_t remainingBudget = MaxResimulationStepsPerTick;
        std::uint32_t resimulationSteps = 0;
        const auto resimulationStarted = std::chrono::steady_clock::now();
        std::vector<VansParticleRuntime*> pending;
        m_Runtimes.ForEach([&](auto, auto& runtime) {
            if (runtime->HasPendingResimulation()) pending.push_back(runtime.get());
        });
        const auto scheduledCount = std::min<std::size_t>(pending.size(),remainingBudget);
        const auto start = pending.empty() ? 0 : m_ResimulationCursor % pending.size();
        for (std::size_t scheduled = 0; scheduled < scheduledCount; ++scheduled)
        {
            const auto slots = scheduledCount - scheduled;
            const auto share = std::max<std::uint32_t>(1,remainingBudget / static_cast<std::uint32_t>(slots));
            const auto used = pending[(start+scheduled)%pending.size()]->AdvanceResimulation(share);
            remainingBudget -= std::min(remainingBudget,used);
            resimulationSteps += used;
        }
        if (!pending.empty()) m_ResimulationCursor = (start+scheduledCount)%pending.size();
        m_ResimulationMilliseconds = std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-resimulationStarted).count();
        m_ResimulationSteps = resimulationSteps;
        const auto started = std::chrono::steady_clock::now();
        m_Runtimes.ForEach([delta](auto, auto& runtime) { runtime->Update(delta); });
        m_SimulationMilliseconds = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        std::uint32_t pendingResimulations = 0;
        m_Runtimes.ForEach([&](auto, const auto& runtime) {
            if (runtime->HasPendingResimulation()) ++pendingResimulations;
        });
        m_PendingResimulations = pendingResimulations;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_UpdateDone = true;
            m_DoneCV.notify_one();
        }
    }
}
}
