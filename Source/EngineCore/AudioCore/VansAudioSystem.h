#pragma once
#include <cstdint>

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

        // ── Listener 空间属性 ────────────────────────────────────────────────
        // 每帧由 VansAudioManager::TickAll 根据主摄像机 Transform 调用。
        // px/py/pz    : 监听者世界坐标
        // fx/fy/fz    : 注视方向（Forward，已归一化）
        // ux/uy/uz    : 上方向（Up，已归一化）
        void UpdateListener(float px, float py, float pz,
                            float fx, float fy, float fz,
                            float ux, float uy, float uz) const;

        // ── 主音量 ─────────────────────────────────────────────────────────
        // gain ∈ [0, 1]；0 = 静音，1 = 原始音量
        void  SetMasterVolume(float gain);
        float GetMasterVolume() const;

    private:
        VansAudioSystem()  = default;
        ~VansAudioSystem() = default;

        void*  m_Device      = nullptr;   // ALCdevice*
        void*  m_Context     = nullptr;   // ALCcontext*
        bool   m_Initialized = false;
        float  m_MasterVolume = 1.0f;
    };

} // namespace VansEngine
