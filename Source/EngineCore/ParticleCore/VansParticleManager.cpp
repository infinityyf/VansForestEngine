#include "VansParticleManager.h"

namespace VansGraphics
{
    // ── 单例 ──────────────────────────────────────────────────────────────

    VansParticleManager& VansParticleManager::Instance()
    {
        static VansParticleManager s_Instance;
        return s_Instance;
    }

    // ── 生命周期 ──────────────────────────────────────────────────────────

    void VansParticleManager::Initialize()
    {
        if (m_Running.load()) return;

        m_Running.store(true, std::memory_order_relaxed);
        m_UpdateThread = std::thread(&VansParticleManager::UpdateThreadLoop, this);
    }

    void VansParticleManager::Shutdown()
    {
        if (!m_Running.load()) return;

        // 通知更新线程退出
        {
            std::unique_lock<std::mutex> lock(m_TickMutex);
            m_Running.store(false, std::memory_order_relaxed);
            m_TickPending = true;
            m_TickCV.notify_one();
        }

        if (m_UpdateThread.joinable())
            m_UpdateThread.join();
    }

    // ── Runtime 注册/注销 ─────────────────────────────────────────────────

    void VansParticleManager::RegisterRuntime(VansParticleRuntime* runtime)
    {
        if (!runtime) return;
        std::lock_guard<std::mutex> lock(m_RuntimeListMutex);
        m_Runtimes.push_back(runtime);
    }

    void VansParticleManager::UnregisterRuntime(VansParticleRuntime* runtime)
    {
        if (!runtime) return;
        std::lock_guard<std::mutex> lock(m_RuntimeListMutex);
        auto it = std::find(m_Runtimes.begin(), m_Runtimes.end(), runtime);
        if (it != m_Runtimes.end())
            m_Runtimes.erase(it);
    }

    // ── 主线程帧接口 ──────────────────────────────────────────────────────

    void VansParticleManager::TickMainThread(float deltaTime)
    {
        std::unique_lock<std::mutex> lock(m_TickMutex);
        m_PendingDeltaTime = deltaTime;
        m_TickPending      = true;
        m_TickCV.notify_one();
    }

    void VansParticleManager::WaitForUpdateAndSwap()
    {
        // 等待更新线程完成本帧计算
        {
            std::unique_lock<std::mutex> lock(m_DoneMutex);
            m_DoneCV.wait(lock, [this] { return m_UpdateDone; });
            m_UpdateDone = false;
        }

        // 主线程交换所有 Runtime 双缓冲
        std::lock_guard<std::mutex> rLock(m_RuntimeListMutex);
        for (auto* rt : m_Runtimes)
        {
            if (rt) rt->SwapBuffers();
        }
    }

    // ── 更新线程主循环 ─────────────────────────────────────────────────────

    void VansParticleManager::UpdateThreadLoop()
    {
        while (true)
        {
            float deltaTime = 0.f;

            // 等待主线程触发一帧更新
            {
                std::unique_lock<std::mutex> lock(m_TickMutex);
                m_TickCV.wait(lock, [this] { return m_TickPending; });
                m_TickPending = false;

                if (!m_Running.load(std::memory_order_relaxed))
                    break;

                deltaTime = m_PendingDeltaTime;
            }

            // 遍历所有 Runtime 执行更新（无需锁，主线程在此期间不修改列表）
            {
                std::lock_guard<std::mutex> lock(m_RuntimeListMutex);
                for (auto* rt : m_Runtimes)
                {
                    if (rt) rt->Update(deltaTime);
                }
            }

            // 通知主线程更新完成
            {
                std::lock_guard<std::mutex> lock(m_DoneMutex);
                m_UpdateDone = true;
                m_DoneCV.notify_one();
            }
        }
    }

} // namespace VansGraphics
