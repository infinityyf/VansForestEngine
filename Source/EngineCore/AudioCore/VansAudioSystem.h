#pragma once
#include "VansAudioReverbPreset.h"

#include <cstdint>
#include <string>
#include <vector>

// OpenAL 头文件仅在 .cpp 中引入，此处使用 void* 隔离
// ALCdevice* m_Device, ALCcontext* m_Context 均以 void* 存储

namespace VansEngine
{
    // ===========================================================================
    // VansAudioSystem — OpenAL-Soft 设备/上下文的单例封装
    //
    // 职责：
    //   - 初始化 / 关闭 OpenAL 设备与上下文（全局唯一）
    //   - 暴露 Listener 位置、朝向、主音量设置
    //
    // 生命周期：
    //   Initialize() 在引擎启动时调用（VansEngine::Init 之后）
    //   Shutdown()   在引擎关闭前调用
    //
    // 线程安全：
    //   Initialize/Shutdown 必须在主线程调用；
    //   UpdateListener / SetMasterVolume 也在主线程（帧循环）调用。
    // ===========================================================================
    class VansAudioSystem
    {
    public:
        VansAudioSystem(const VansAudioSystem&)            = delete;
        VansAudioSystem& operator=(const VansAudioSystem&) = delete;

        static VansAudioSystem& GetInstance();

        // ── 设备 / 上下文 ────────────────────────────────────────────────────
        bool Initialize();
        void Shutdown();

        bool IsInitialized() const { return m_Initialized; }
        bool IsEfxSupported() const { return m_EfxSupported; }
        std::uint32_t GetDefaultReverbEffectSlot() const { return m_DefaultReverbSlot; }
        std::uint32_t AcquireSource();
        void ReleaseSource(std::uint32_t& sourceId);
        std::size_t GetActiveSourceLeaseCount() const { return m_ActiveSourceLeases; }
        std::size_t GetPooledSourceCount() const { return m_PooledSources.size(); }
        bool ApplyDefaultReverbSend(std::uint32_t sourceId, float sendGain,
            std::uint32_t& sourceSendFilter) const;
        bool ApplySourceDirectLowpass(std::uint32_t sourceId, float highFrequencyGain,
            std::uint32_t& sourceFilter) const;
        void ReleaseSourceEffectFilter(std::uint32_t& sourceSendFilter) const;
        void SetDefaultReverbWetGain(float wetGain);
        float GetDefaultReverbWetGain() const { return m_DefaultReverbWetGain; }
        void SetDefaultReverbPreset(AudioReverbPreset preset);
        AudioReverbPreset GetDefaultReverbPreset() const { return m_DefaultReverbPreset; }
        void SetDefaultReverbParameters(
            AudioReverbPresetParameters parameters,
            const char* presetName = nullptr);
        const std::string& GetDefaultReverbPresetName() const { return m_DefaultReverbPresetName; }

        // ── Listener 空间属性 ────────────────────────────────────────────────
        // 每帧由 VansAudioManager::TickAll 根据主摄像机 Transform 调用。
        // px/py/pz    : 监听者世界坐标
        // fx/fy/fz    : 注视方向（Forward，已归一化）
        // ux/uy/uz    : 上方向（Up，已归一化）
        void UpdateListener(float px, float py, float pz,
                            float fx, float fy, float fz,
                            float ux, float uy, float uz,
                            float vx = 0.0f, float vy = 0.0f, float vz = 0.0f) const;

        // ── 主音量 ─────────────────────────────────────────────────────────
        // gain ∈ [0, 1]；0 = 静音，1 = 原始音量
        void  SetMasterVolume(float gain);
        float GetMasterVolume() const;

    private:
        VansAudioSystem()  = default;
        ~VansAudioSystem() = default;

        void InitializeEffects();
        void ShutdownEffects();
        void CommitDefaultReverbParameters();

        void*  m_Device      = nullptr;   // ALCdevice*
        void*  m_Context     = nullptr;   // ALCcontext*
        bool   m_Initialized = false;
        bool   m_EfxSupported = false;
        std::uint32_t m_DefaultReverbEffect = 0;
        std::uint32_t m_DefaultReverbSlot = 0;
        float  m_DefaultReverbWetGain = 1.0f;
        float  m_LastCommittedDefaultReverbWetGain = -1.0f;
        AudioReverbPreset m_DefaultReverbPreset = AudioReverbPreset::Generic;
        std::string m_DefaultReverbPresetName = "generic";
        AudioReverbPresetParameters m_DefaultReverbParameters;
        AudioReverbPresetParameters m_LastCommittedDefaultReverbParameters;
        bool m_HasCommittedDefaultReverbParameters = false;
        float  m_MasterVolume = 1.0f;
        std::vector<std::uint32_t> m_PooledSources;
        std::size_t m_ActiveSourceLeases = 0;
    };

} // namespace VansEngine
