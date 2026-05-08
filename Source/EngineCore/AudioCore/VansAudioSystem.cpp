#include "VansAudioSystem.h"
#include "../Util/VansLog.h"

// OpenAL-Soft 头文件仅在此 .cpp 中引入，不暴露到其他模块
#include <AL/al.h>
#include <AL/alc.h>

namespace VansEngine
{

// ===========================================================================
// GetInstance — 单例访问器
// ===========================================================================
VansAudioSystem& VansAudioSystem::GetInstance()
{
    static VansAudioSystem s_Instance;
    return s_Instance;
}

// ===========================================================================
// Initialize — 打开默认设备，创建并激活上下文
// ===========================================================================
bool VansAudioSystem::Initialize()
{
    if (m_Initialized)
    {
        VANS_LOG_WARN("[VansAudioSystem] Initialize: 已经初始化，跳过");
        return true;
    }

    ALCdevice* device = alcOpenDevice(nullptr); // nullptr = 系统默认设备
    if (!device)
    {
        VANS_LOG_ERROR("[VansAudioSystem] alcOpenDevice 失败：无法打开默认音频设备");
        return false;
    }

    ALCcontext* ctx = alcCreateContext(device, nullptr);
    if (!ctx)
    {
        VANS_LOG_ERROR("[VansAudioSystem] alcCreateContext 失败");
        alcCloseDevice(device);
        return false;
    }

    if (!alcMakeContextCurrent(ctx))
    {
        VANS_LOG_ERROR("[VansAudioSystem] alcMakeContextCurrent 失败");
        alcDestroyContext(ctx);
        alcCloseDevice(device);
        return false;
    }

    m_Device      = static_cast<void*>(device);
    m_Context     = static_cast<void*>(ctx);
    m_Initialized = true;

    // 初始化 listener 默认值
    alListener3f(AL_POSITION,    0.0f, 0.0f,  0.0f);
    alListener3f(AL_VELOCITY,    0.0f, 0.0f,  0.0f);
    const float orientation[6] = { 0.0f, 0.0f, -1.0f,  0.0f, 1.0f, 0.0f };
    alListenerfv(AL_ORIENTATION, orientation);
    alListenerf (AL_GAIN, m_MasterVolume);

    VANS_LOG("[VansAudioSystem] 初始化成功");
    return true;
}

// ===========================================================================
// Shutdown — 释放上下文和设备
// ===========================================================================
void VansAudioSystem::Shutdown()
{
    if (!m_Initialized)
        return;

    alcMakeContextCurrent(nullptr);

    if (m_Context)
    {
        alcDestroyContext(static_cast<ALCcontext*>(m_Context));
        m_Context = nullptr;
    }
    if (m_Device)
    {
        alcCloseDevice(static_cast<ALCdevice*>(m_Device));
        m_Device = nullptr;
    }

    m_Initialized = false;
    VANS_LOG("[VansAudioSystem] 已关闭");
}

// ===========================================================================
// UpdateListener — 每帧更新监听者空间变换
// ===========================================================================
void VansAudioSystem::UpdateListener(float px, float py, float pz,
                                      float fx, float fy, float fz,
                                      float ux, float uy, float uz) const
{
    if (!m_Initialized) return;

    alListener3f(AL_POSITION, px, py, pz);

    // OpenAL orientation 格式：[forward(3), up(3)]
    const float orientation[6] = { fx, fy, fz, ux, uy, uz };
    alListenerfv(AL_ORIENTATION, orientation);
}

// ===========================================================================
// SetMasterVolume / GetMasterVolume
// ===========================================================================
void VansAudioSystem::SetMasterVolume(float gain)
{
    if (!m_Initialized) return;
    m_MasterVolume = gain;
    alListenerf(AL_GAIN, gain);
}

float VansAudioSystem::GetMasterVolume() const
{
    return m_MasterVolume;
}

} // namespace VansEngine
