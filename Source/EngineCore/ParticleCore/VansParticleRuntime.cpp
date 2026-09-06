#include "VansParticleRuntime.h"

#include <algorithm>

namespace VansGraphics
{
	void VansParticleRuntime::SetOwnerWorldTransform(const glm::mat4& ownerWorld)
	{
		m_LocalToWorld = m_Asset && m_Asset->m_WorldAligned ? glm::mat4(1.0f) : ownerWorld;
		m_LocalToWorld[3] = ownerWorld * glm::vec4(m_EmitterPositionLocal, 1.0f);
	}

	void VansParticleRuntime::Play()
	{
		if (m_IsPlaying)
			return;
		if (!m_StartInitialized)
		{
			m_StartInitialized = true;
			m_DelayRemaining = m_Asset ? std::max(0.0f, m_Asset->m_StartDelay) : 0.0f;
			if (m_DelayRemaining <= 0.0f) Prewarm();
		}
		m_IsPlaying = true;
	}

	void VansParticleRuntime::Prewarm()
	{
		if (m_Asset && m_Asset->m_Prewarm && m_PlayTime <= 0.0f &&
			m_Asset->m_Duration > 0.0f)
		{
			Seek(m_Asset->m_Duration);
			const int back = m_BackBufferIdx.load(std::memory_order_relaxed);
			const int front = m_FrontBufferIdx.load(std::memory_order_relaxed);
			m_InstanceBuffers[front] = m_InstanceBuffers[back];
			m_VolumetricInstanceBuffers[front] = m_VolumetricInstanceBuffers[back];
		}
	}

	void VansParticleRuntime::Stop()
	{
		m_IsPlaying = false;
		m_PlayTime = 0.0f;
		m_StartInitialized = false;
		m_DelayRemaining = 0.0f;
		m_DeferFirstUpdate = false;
		if (m_Asset)
			for (auto& emitter : m_Asset->m_Emitters)
				if (emitter) emitter->ResetSimulation();
		m_AliveInstanceCount.store(0, std::memory_order_release);
		for (auto& buffer : m_InstanceBuffers) buffer.clear();
		for (auto& buffer : m_VolumetricInstanceBuffers) buffer.clear();
	}

	void VansParticleRuntime::SetRandomSeed(uint32_t seed)
	{
		m_RandomSeed = seed != 0 ? seed : 0x9e3779b9u;
		if (!m_Asset) return;
		uint32_t emitterSeed = m_RandomSeed;
		for (auto& emitter : m_Asset->m_Emitters)
		{
			if (emitter) emitter->SetRandomSeed(emitterSeed);
			emitterSeed = emitterSeed * 1664525u + 1013904223u;
		}
	}

	void VansParticleRuntime::Burst(uint32_t count)
	{
		if (!m_Asset || count == 0) return;
		for (auto& emitter : m_Asset->m_Emitters)
			if (emitter) emitter->EmitBurst(count, m_LocalToWorld);
	}

	void VansParticleRuntime::Restart()
	{
		Stop();
		Play();
	}

	void VansParticleRuntime::Seek(float seconds, float fixedStep)
	{
		const bool wasPlaying = m_IsPlaying;
		Stop();
		m_IsPlaying = true;
		// Seek 定位发射后的模拟时间，不重放启动倒计时。
		m_StartInitialized = true;
		float remaining = std::max(0.0f, seconds);
		const float step = std::max(1.0f / 240.0f, fixedStep);
		while (remaining > 0.0f)
		{
			const float delta = std::min(step, remaining);
			Update(delta);
			remaining -= delta;
		}
		m_IsPlaying = wasPlaying;
	}

    void VansParticleRuntime::Update(float deltaTime)
    {
        if (!m_Asset || !m_IsPlaying) return;
		if (m_DeferFirstUpdate) { m_DeferFirstUpdate = false; return; }

		if (m_DelayRemaining > 0.0f)
		{
			const float elapsed = std::min(std::max(0.0f, deltaTime), m_DelayRemaining);
			m_DelayRemaining -= elapsed;
			deltaTime -= elapsed;
			if (m_DelayRemaining > 0.0f) return;
			Prewarm();
		}

		deltaTime *= m_SimulationRate;
		if (deltaTime <= 0.0f) return;
        m_PlayTime += deltaTime;

        // 获取后台缓冲索引
        int backIdx = m_BackBufferIdx.load(std::memory_order_relaxed);
        auto& backBuffer = m_InstanceBuffers[backIdx];
        auto& volumetricBackBuffer = m_VolumetricInstanceBuffers[backIdx];
        backBuffer.clear();
        volumetricBackBuffer.clear();
		std::uint32_t aliveParticleCount = 0u;

        // 推进所有 Emitter 并收集实例数据
        for (auto& emitter : m_Asset->m_Emitters)
        {
            if (!emitter) continue;
            emitter->Update(deltaTime, m_LocalToWorld);
			aliveParticleCount += emitter->m_ParticlePool.m_AliveCount;
            const auto& volumetric = emitter->m_RendererConfig.m_Volumetric;
            if (!volumetric.m_Enabled || volumetric.m_KeepSurfaceRenderer)
                emitter->FillInstanceData(backBuffer);
            if (volumetric.m_Enabled)
                emitter->FillVolumetricInstanceData(volumetricBackBuffer);
        }

        m_AliveInstanceCount.store(
			aliveParticleCount,
            std::memory_order_release);
    }

    bool VansParticleRuntime::HasVolumetricInjectionEnabled() const
    {
        if (!m_Asset)
            return false;
        return std::any_of(m_Asset->m_Emitters.begin(), m_Asset->m_Emitters.end(),
            [](const auto& emitter)
            {
                return emitter && emitter->m_Enabled &&
                    emitter->m_RendererConfig.m_Volumetric.m_Enabled;
            });
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
