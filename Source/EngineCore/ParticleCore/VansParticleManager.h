#pragma once
#include "VansParticleRuntime.h"
#include "VansParticleDebugSnapshot.h"
#include "../RuntimeCore/VansGenerationPool.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

namespace VansGraphics
{
enum class VansParticleControl
{
    Play, Pause, Stop, StopEmitting, DetachAndDrain, Restart, Seek, Seed, SimulationRate, Burst, EmitterEnabled, EffectiveEnabled, DeferFirstUpdate, RefreshEmission,
    SetOwnerWorldTransform, SetEmitterPositionLocal
};
struct VansParticleCommand
{
    Vans::VansGenerationHandle instance;
    VansParticleControl control = VansParticleControl::Play;
    float value = 0.0f;
    uint32_t index = 0;
    glm::mat4 ownerWorld{1.0f};
    glm::vec3 emitterPositionLocal{0.0f};
    bool hasOwnerWorld = false;
    bool hasEmitterPositionLocal = false;
};
// 场景拥有实例与后台线程；调用者持有句柄，控制请求在模拟前消费。
class VansParticleManager
{
public:
    VansParticleManager();
    ~VansParticleManager();
    VansParticleManager(const VansParticleManager&) = delete;
    VansParticleManager& operator=(const VansParticleManager&) = delete;
    Vans::VansGenerationHandle Create(std::shared_ptr<const VansParticleAsset> asset);
    bool Destroy(Vans::VansGenerationHandle handle);
    const VansParticleRuntime* Resolve(Vans::VansGenerationHandle handle) const;
    bool SetOwnerWorldTransform(Vans::VansGenerationHandle handle, const glm::mat4& ownerWorld);
    bool SetEmitterPositionLocal(Vans::VansGenerationHandle handle, const glm::vec3& position);
    bool Queue(Vans::VansGenerationHandle handle, VansParticleControl control, float value = 0.0f, uint32_t index = 0);
    bool QueueBatch(const std::vector<VansParticleCommand>& commands);
    bool SetSimulationFrozen(bool frozen);
    bool TickMainThread(float deltaTime);
    void WaitForUpdateAndSwap();
    void Shutdown();
    VansParticleDebugSnapshot CaptureDebugSnapshot() const;
    double SimulationMilliseconds() const { return m_SimulationMilliseconds.load(std::memory_order_relaxed); }
    double ResimulationMilliseconds() const { return m_ResimulationMilliseconds.load(std::memory_order_relaxed); }
    std::uint32_t ResimulationSteps() const { return m_ResimulationSteps.load(std::memory_order_relaxed); }
    std::uint32_t PendingResimulations() const { return m_PendingResimulations.load(std::memory_order_relaxed); }
    double MainThreadOverlapMilliseconds() const { return m_MainThreadOverlapMilliseconds; }
    double WaitMilliseconds() const { return m_WaitMilliseconds; }
    std::size_t ActiveCount() const { return m_Runtimes.ActiveCount(); }
    static constexpr std::size_t MaxInstances = 1024;
    static constexpr std::uint64_t MaxPointCapacity = 1048576;
    static constexpr std::uint32_t MaxResimulationStepsPerTick = 256;
    std::uint64_t PointCapacity() const { return m_PointCapacity; }
    std::uint64_t RejectedInstances() const { return m_RejectedInstances; }
    template<class Function> bool ForEachRuntime(Function&& function) const
    {
        if (!CanAccessMainState()) return false;
        m_Runtimes.ForEach([&](auto handle, const auto& runtime) {
            const VansParticleRuntime& readOnlyRuntime = *runtime;
            function(handle, readOnlyRuntime);
        });
        return true;
    }
private:
    bool IsOwnerThread() const { return std::this_thread::get_id() == m_OwnerThread; }
    bool CanAccessMainState() const
    { return IsOwnerThread() && !m_InFlight.load(std::memory_order_acquire); }
    bool CanQueue(const VansParticleCommand& command) const;
    void ApplyCommands(const std::vector<VansParticleCommand>& commands);
    void UpdateThreadLoop();
    Vans::VansGenerationPool<std::unique_ptr<VansParticleRuntime>> m_Runtimes;
    std::vector<VansParticleCommand> m_Commands;
    std::vector<VansParticleCommand> m_WorkerCommands;
    std::thread m_UpdateThread;
    std::mutex m_Mutex;
    std::condition_variable m_TickCV, m_DoneCV;
    bool m_Running = false;
    bool m_TickPending = false;
    bool m_UpdateDone = false;
    std::atomic_bool m_InFlight{false};
    bool m_SimulationFrozen = false;
    float m_DeltaTime = 0.0f;
    std::atomic<double> m_SimulationMilliseconds{0};
    std::atomic<double> m_ResimulationMilliseconds{0};
    std::atomic<std::uint32_t> m_ResimulationSteps{0};
    std::atomic<std::uint32_t> m_PendingResimulations{0};
    std::chrono::steady_clock::time_point m_DispatchedAt{};
    double m_MainThreadOverlapMilliseconds = 0, m_WaitMilliseconds = 0;
    std::size_t m_ResimulationCursor = 0;
    std::uint64_t m_PointCapacity = 0, m_RejectedInstances = 0;
    const std::thread::id m_OwnerThread;
};
}
