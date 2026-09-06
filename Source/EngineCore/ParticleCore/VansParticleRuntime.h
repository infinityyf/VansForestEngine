#pragma once
#include "VansParticleAsset.h"
#include "VansParticleInstanceData.h"
#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <atomic>

namespace VansGraphics
{
    // ============================================================
    // VansParticleRuntime — 每个 VansScriptParticleComponent 持有一份
    //
    // 双缓冲设计：
    //   - 更新线程独占写 BackBuffer（不加锁）
    //   - 主线程独占读 FrontBuffer（上传到 GPU）
    //   - 帧边界处 SwapBuffers()
    // ============================================================
    class VansParticleRuntime
    {
    public:
        // 非拥有指针，生命周期由 VansScriptParticleComponent 管理
        VansParticleAsset* m_Asset = nullptr;

        // 发射器到世界的变换矩阵（每帧由 Component 写入）
        glm::mat4 m_LocalToWorld = glm::mat4(1.f);
        // 实例级发射锚点；共享 Asset 不包含所属模型的原点偏移。
        glm::vec3 m_EmitterPositionLocal{0.0f};
        void SetOwnerWorldTransform(const glm::mat4& ownerWorld);
        // 动态实例在当前帧中途出生；该帧已有的 delta 不属于它的寿命。
        void DeferFirstUpdate() { m_DeferFirstUpdate = true; }

        // ── 双缓冲 InstanceData（CPU 侧）────────────────────────
        static constexpr int BUFFER_COUNT = 2;
        std::array<std::vector<VansParticleInstanceData>, BUFFER_COUNT> m_InstanceBuffers;
        std::array<std::vector<VansVolumetricParticleInstanceData>, BUFFER_COUNT>
            m_VolumetricInstanceBuffers;

        // 更新线程写入的 Buffer 索引（0 或 1）
        std::atomic<int> m_BackBufferIdx { 1 };
        // 渲染线程读取的 Buffer 索引（0 或 1）
        std::atomic<int> m_FrontBufferIdx{ 0 };

        // 最近一次更新后的存活粒子总数（跨 Emitter 之和）
        std::atomic<uint32_t> m_AliveInstanceCount { 0 };

        // ── 播放状态 ─────────────────────────────────────────────
        bool  m_IsPlaying = false;
        float m_PlayTime  = 0.f;

        // ── 接口 ─────────────────────────────────────────────────

        // 由更新线程调用：推进所有 Emitter，填写 BackBuffer
        void Update(float deltaTime);
		void Play();
		void Pause() { m_IsPlaying = false; }
		void Stop();
		void Restart();
		void Seek(float seconds, float fixedStep = 1.0f / 60.0f);
		void SetRandomSeed(uint32_t seed);
		uint32_t GetRandomSeed() const { return m_RandomSeed; }
		void SetSimulationRate(float rate) { m_SimulationRate = rate > 0.0f ? rate : 0.0f; }
		float GetSimulationRate() const { return m_SimulationRate; }
		void Burst(uint32_t count = 1);
		bool IsPlaying() const { return m_IsPlaying; }
		float GetPlayTime() const { return m_PlayTime; }

        // 在更新完成 + 主线程空闲时调用：交换前后缓冲
        void SwapBuffers();

        // 渲染线程读取已就绪的 FrontBuffer
        const std::vector<VansParticleInstanceData>& GetRenderBuffer() const
        {
            return m_InstanceBuffers[m_FrontBufferIdx.load(std::memory_order_acquire)];
        }
        const std::vector<VansVolumetricParticleInstanceData>&
            GetVolumetricRenderBuffer() const
        {
            return m_VolumetricInstanceBuffers[
                m_FrontBufferIdx.load(std::memory_order_acquire)];
        }
        bool HasVolumetricInjectionEnabled() const;

	private:
		void Prewarm();
		bool m_StartInitialized = false;
		float m_DelayRemaining = 0.0f;
		bool m_DeferFirstUpdate = false;
		uint32_t m_RandomSeed = 0x9e3779b9u;
		float m_SimulationRate = 1.0f;
    };

} // namespace VansGraphics
