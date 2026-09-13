#pragma once
#include "VansParticleRuntime.h"
#include "VansParticleDebugSnapshot.h"
#include "../RuntimeCore/VansGenerationPool.h"
#include <thread>
#include <mutex>
#include <condition_variable>

namespace VansGraphics
{
enum class VansParticleControl
{
    Play, Pause, Stop, StopEmitting, DetachAndDrain, Restart, Seek, Seed, SimulationRate, Burst, EmitterEnabled, EffectiveEnabled, DeferFirstUpdate, RefreshEmission
};
struct VansParticleCommand
{
    Vans::VansGenerationHandle instance;
    VansParticleControl control = VansParticleControl::Play;
    float value = 0.0f;
    uint32_t index = 0;
};
// 场景拥有实例与后台线程；调用者持有句柄，控制请求在模拟前消费。
class VansParticleManager
{
public:
    VansParticleManager() = default;
    ~VansParticleManager();
    VansParticleManager(const VansParticleManager&) = delete;
    VansParticleManager& operator=(const VansParticleManager&) = delete;
    Vans::VansGenerationHandle Create(std::shared_ptr<const VansParticleAsset> asset);
    bool Destroy(Vans::VansGenerationHandle handle);
    VansParticleRuntime* Resolve(Vans::VansGenerationHandle handle);
    const VansParticleRuntime* Resolve(Vans::VansGenerationHandle handle) const;
    bool Queue(Vans::VansGenerationHandle handle, VansParticleControl control, float value = 0.0f, uint32_t index = 0);
    void Prepare();
    void TickMainThread(float deltaTime);
    void WaitForUpdateAndSwap();
    void Shutdown();
    VansParticleDebugSnapshot CaptureDebugSnapshot() const;
    double SimulationMilliseconds() const { return m_SimulationMilliseconds.load(std::memory_order_relaxed); }
    double WaitMilliseconds() const { return m_WaitMilliseconds; }
    std::size_t ActiveCount() const { return m_Runtimes.ActiveCount(); }
    static constexpr std::size_t MaxInstances = 1024;
    static constexpr std::uint64_t MaxPointCapacity = 1048576;
    std::uint64_t PointCapacity() const { return m_PointCapacity; }
    std::uint64_t RejectedInstances() const { return m_RejectedInstances; }
    template<class Function> void ForEach(Function&& function)
    { m_Runtimes.ForEach([&](auto handle, auto& runtime) { function(handle, *runtime); }); }
private:
    void UpdateThreadLoop();
    Vans::VansGenerationPool<std::unique_ptr<VansParticleRuntime>> m_Runtimes;
    std::vector<VansParticleCommand> m_Commands;
    std::thread m_UpdateThread;
    std::mutex m_Mutex;
    std::condition_variable m_TickCV, m_DoneCV;
    bool m_Running = false;
    bool m_TickPending = false;
    bool m_UpdateDone = false;
    bool m_InFlight = false;
    float m_DeltaTime = 0.0f;
    std::atomic<double> m_SimulationMilliseconds{0};
    double m_WaitMilliseconds = 0;
    std::uint64_t m_PointCapacity = 0, m_RejectedInstances = 0;
};
}
