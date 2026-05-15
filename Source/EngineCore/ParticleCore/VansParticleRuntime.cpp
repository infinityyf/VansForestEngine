#include "VansParticleRuntime.h"

namespace VansGraphics
{
    void VansParticleRuntime::Update(float deltaTime)
    {
        if (!m_Asset || !m_IsPlaying) return;

        m_PlayTime += deltaTime;

        // 获取后台缓冲索引
        int backIdx = m_BackBufferIdx.load(std::memory_order_relaxed);
        auto& backBuffer = m_InstanceBuffers[backIdx];
        backBuffer.clear();

        // 推进所有 Emitter 并收集实例数据
        for (auto& emitter : m_Asset->m_Emitters)
        {
            if (!emitter) continue;
            emitter->Update(deltaTime, m_LocalToWorld);
            emitter->FillInstanceData(backBuffer);
        }

        m_AliveInstanceCount.store(
            static_cast<uint32_t>(backBuffer.size()),
            std::memory_order_release);
    }

    void VansParticleRuntime::SwapBuffers()
    {
        // 交换前后缓冲索引（通知主线程读取最新数据）
        int back  = m_BackBufferIdx.load(std::memory_order_relaxed);
        int front = m_FrontBufferIdx.load(std::memory_order_relaxed);
        m_FrontBufferIdx.store(back,  std::memory_order_release);
        m_BackBufferIdx.store(front, std::memory_order_relaxed);
    }

} // namespace VansGraphics
