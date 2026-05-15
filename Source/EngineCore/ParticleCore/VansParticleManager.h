#pragma once
#include "VansParticleRuntime.h"
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace VansGraphics
{
    // ============================================================
    // VansParticleManager — 粒子系统全局管理器（单例）
    //
    // 维护一个后台更新线程：
    //   主线程调用 TickMainThread(dt) → 触发更新
    //   主线程调用 UploadInstancesMainThread() → 将前台缓冲上传到 GPU
    //   主线程调用 WaitForUpdateAndSwap() → 等待更新完成后交换缓冲
    // ============================================================
    class VansParticleManager
    {
    public:
        // ── 单例访问 ─────────────────────────────────────────────
        static VansParticleManager& Instance();

        // ── 生命周期 ─────────────────────────────────────────────
        void Initialize();
        void Shutdown();

        // ── Runtime 注册/注销 ────────────────────────────────────
        void RegisterRuntime(VansParticleRuntime* runtime);
        void UnregisterRuntime(VansParticleRuntime* runtime);

        // ── 主线程帧更新接口 ──────────────────────────────────────

        // 触发一帧异步更新（在主线程帧开始时调用）
        void TickMainThread(float deltaTime);

        // 等待更新线程完成，然后交换所有 Runtime 双缓冲
        void WaitForUpdateAndSwap();

    private:
        VansParticleManager()  = default;
        ~VansParticleManager() = default;

        VansParticleManager(const VansParticleManager&)            = delete;
        VansParticleManager& operator=(const VansParticleManager&) = delete;

        // ── 更新线程逻辑 ─────────────────────────────────────────
        void UpdateThreadLoop();

        // ── 成员 ─────────────────────────────────────────────────
        std::thread             m_UpdateThread;
        std::atomic<bool>       m_Running     { false };

        // 主线程通过这两个变量向更新线程传递 deltaTime 并触发一帧更新
        std::mutex              m_TickMutex;
        std::condition_variable m_TickCV;
        bool                    m_TickPending = false;
        float                   m_PendingDeltaTime = 0.f;

        // 更新线程完成后通知主线程
        std::mutex              m_DoneMutex;
        std::condition_variable m_DoneCV;
        bool                    m_UpdateDone = false;

        // Runtime 列表（需要锁保护）
        std::mutex                        m_RuntimeListMutex;
        std::vector<VansParticleRuntime*> m_Runtimes;
    };

} // namespace VansGraphics
