#include "VansAudioSystem.h"
#include "VansAudioVoice.h"
#include "../Util/VansLog.h"

// OpenAL-Soft 头文件仅在此 .cpp 中引入，不暴露到其他模块
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/efx.h>

#include <algorithm>
#include <cmath>

#ifndef ALC_HRTF_SOFT
#define ALC_HRTF_SOFT 0x1992
#endif
#ifndef ALC_HRTF_STATUS_SOFT
#define ALC_HRTF_STATUS_SOFT 0x1993
#endif
#ifndef ALC_HRTF_DISABLED_SOFT
#define ALC_HRTF_DISABLED_SOFT 0x0000
#endif
#ifndef ALC_HRTF_ENABLED_SOFT
#define ALC_HRTF_ENABLED_SOFT 0x0001
#endif
#ifndef ALC_HRTF_DENIED_SOFT
#define ALC_HRTF_DENIED_SOFT 0x0002
#endif
#ifndef ALC_HRTF_REQUIRED_SOFT
#define ALC_HRTF_REQUIRED_SOFT 0x0003
#endif
#ifndef ALC_HRTF_HEADPHONES_DETECTED_SOFT
#define ALC_HRTF_HEADPHONES_DETECTED_SOFT 0x0004
#endif
#ifndef ALC_HRTF_UNSUPPORTED_FORMAT_SOFT
#define ALC_HRTF_UNSUPPORTED_FORMAT_SOFT 0x0005
#endif

namespace VansEngine
{
namespace
{
struct EfxApi
{
    LPALGENEFFECTS GenEffects = nullptr;
    LPALDELETEEFFECTS DeleteEffects = nullptr;
    LPALEFFECTI Effecti = nullptr;
    LPALEFFECTF Effectf = nullptr;
    LPALGENFILTERS GenFilters = nullptr;
    LPALDELETEFILTERS DeleteFilters = nullptr;
    LPALFILTERI Filteri = nullptr;
    LPALFILTERF Filterf = nullptr;
    LPALGENAUXILIARYEFFECTSLOTS GenAuxiliaryEffectSlots = nullptr;
    LPALDELETEAUXILIARYEFFECTSLOTS DeleteAuxiliaryEffectSlots = nullptr;
    LPALAUXILIARYEFFECTSLOTI AuxiliaryEffectSloti = nullptr;
    LPALAUXILIARYEFFECTSLOTF AuxiliaryEffectSlotf = nullptr;

    bool IsLoaded() const
    {
        return GenEffects && DeleteEffects && Effecti && Effectf &&
            GenFilters && DeleteFilters && Filteri && Filterf &&
            GenAuxiliaryEffectSlots && DeleteAuxiliaryEffectSlots &&
            AuxiliaryEffectSloti && AuxiliaryEffectSlotf;
    }
};

EfxApi g_EfxApi;

template <typename T>
T LoadEfxProc(const char* name)
{
    return reinterpret_cast<T>(alGetProcAddress(name));
}

void ClearAlErrors()
{
    while (alGetError() != AL_NO_ERROR) {}
}

void ResetSourceDefaults(ALuint source)
{
    const VansAudioProperties properties;
    const AudioConeSettings cone;
    alSourceStop(source);
    alSourcei(source, AL_BUFFER, 0);
    alSourcef(source, AL_GAIN, properties.m_Volume);
    alSourcef(source, AL_PITCH, properties.m_Pitch);
    alSourcei(source, AL_LOOPING, properties.m_Loop ? AL_TRUE : AL_FALSE);
    alSourcei(source, AL_SOURCE_RELATIVE, properties.m_Spatial ? AL_FALSE : AL_TRUE);
    alSource3f(source, AL_POSITION, properties.m_StereoPan, 0.0f, 0.0f);
    alSource3f(source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    alSource3f(source, AL_DIRECTION, 0.0f, 0.0f, 1.0f);
    alSourcef(source, AL_CONE_INNER_ANGLE, cone.innerAngleDegrees);
    alSourcef(source, AL_CONE_OUTER_ANGLE, cone.outerAngleDegrees);
    alSourcef(source, AL_CONE_OUTER_GAIN, cone.outerGain);
}

const char* HrtfStatusName(ALCint status)
{
    switch (status)
    {
    case ALC_HRTF_DISABLED_SOFT: return "disabled";
    case ALC_HRTF_ENABLED_SOFT: return "enabled";
    case ALC_HRTF_DENIED_SOFT: return "denied";
    case ALC_HRTF_REQUIRED_SOFT: return "required";
    case ALC_HRTF_HEADPHONES_DETECTED_SOFT: return "headphones_detected";
    case ALC_HRTF_UNSUPPORTED_FORMAT_SOFT: return "unsupported_format";
    default: return "unknown";
    }
}

bool IsHrtfActive(ALCint status)
{
    return status == ALC_HRTF_ENABLED_SOFT ||
        status == ALC_HRTF_REQUIRED_SOFT ||
        status == ALC_HRTF_HEADPHONES_DETECTED_SOFT;
}

bool UsesSameContext(const VansAudioDeviceConfig& first, const VansAudioDeviceConfig& second)
{
    return first.m_HrtfMode == second.m_HrtfMode &&
        first.m_OutputDevice == second.m_OutputDevice;
}
}

const char* VansAudioSourceAcquireStatusName(VansAudioSourceAcquireStatus status)
{
    switch (status)
    {
    case VansAudioSourceAcquireStatus::Acquired: return "acquired";
    case VansAudioSourceAcquireStatus::NotInitialized: return "not_initialized";
    case VansAudioSourceAcquireStatus::LimitReached: return "limit_reached";
    case VansAudioSourceAcquireStatus::BackendFailure: return "backend_failure";
    default: return "unknown";
    }
}

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
bool VansAudioSystem::Initialize(const VansAudioDeviceConfig& config)
{
    if (m_Initialized)
    {
        std::string error;
        const bool applied = ApplyDeviceConfig(config, error);
        if (!applied)
            VANS_LOG_ERROR("[VansAudioSystem] " << error);
        return applied;
    }

    const VansAudioDeviceConfig candidate = NormalizeAudioDeviceConfig(config);
    const char* requestedDevice = candidate.m_OutputDevice.empty()
        ? nullptr
        : candidate.m_OutputDevice.c_str();
    ALCdevice* device = alcOpenDevice(requestedDevice);
    if (!device)
    {
        VANS_LOG_ERROR("[VansAudioSystem] Cannot open audio device: "
            << (requestedDevice ? requestedDevice : "(system default)"));
        return false;
    }

    const bool hrtfSupported = alcIsExtensionPresent(device, "ALC_SOFT_HRTF") == ALC_TRUE;
    ALCcontext* ctx = nullptr;
    if (candidate.m_HrtfMode == VansAudioHrtfMode::Required && !hrtfSupported)
    {
        VANS_LOG_ERROR("[VansAudioSystem] Required HRTF is not supported by the selected device");
        alcCloseDevice(device);
        return false;
    }
    if (hrtfSupported)
    {
        const ALCint hrtfEnabled = candidate.m_HrtfMode == VansAudioHrtfMode::Disabled
            ? ALC_FALSE
            : ALC_TRUE;
        ALCint hrtfAttributes[] = { ALC_HRTF_SOFT, hrtfEnabled, 0 };
        ctx = alcCreateContext(device, hrtfAttributes);
        if (!ctx)
        {
            if (candidate.m_HrtfMode != VansAudioHrtfMode::Enabled)
            {
                VANS_LOG_ERROR("[VansAudioSystem] Requested HRTF policy could not create a context");
                alcCloseDevice(device);
                return false;
            }
            VANS_LOG_WARN("[VansAudioSystem] OpenAL HRTF request failed; falling back to default context");
        }
    }
    else if (candidate.m_HrtfMode == VansAudioHrtfMode::Enabled)
    {
        VANS_LOG_WARN("[VansAudioSystem] OpenAL Soft HRTF extension is not available");
    }

    if (!ctx)
        ctx = alcCreateContext(device, nullptr);
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

    ALCint hrtfStatus = ALC_HRTF_DISABLED_SOFT;
    if (hrtfSupported)
        alcGetIntegerv(device, ALC_HRTF_STATUS_SOFT, 1, &hrtfStatus);
    if (candidate.m_HrtfMode == VansAudioHrtfMode::Required && !IsHrtfActive(hrtfStatus))
    {
        VANS_LOG_ERROR("[VansAudioSystem] Required HRTF was not activated; status="
            << HrtfStatusName(hrtfStatus));
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(ctx);
        alcCloseDevice(device);
        return false;
    }
    if (candidate.m_HrtfMode == VansAudioHrtfMode::Disabled && IsHrtfActive(hrtfStatus))
    {
        VANS_LOG_ERROR("[VansAudioSystem] HRTF remained active after it was disabled");
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(ctx);
        alcCloseDevice(device);
        return false;
    }

    m_Device = static_cast<void*>(device);
    m_Context = static_cast<void*>(ctx);
    m_DeviceConfig = candidate;
    m_Initialized = true;
    m_PooledSources.clear();
    m_ActiveSources.clear();
    m_SourceLimitRejections = 0;
    m_SourceBackendFailures = 0;

    // 初始化 listener 默认值
    alListener3f(AL_POSITION,    0.0f, 0.0f,  0.0f);
    alListener3f(AL_VELOCITY,    0.0f, 0.0f,  0.0f);
    const float orientation[6] = { 0.0f, 0.0f, -1.0f,  0.0f, 1.0f, 0.0f };
    alListenerfv(AL_ORIENTATION, orientation);
    alListenerf(AL_GAIN, m_DeviceConfig.m_MasterGain);

    const ALCchar* devName = alcGetString(device, ALC_DEVICE_SPECIFIER);
    m_ActiveDeviceName = devName ? devName : "";
    m_HrtfStatus = hrtfSupported ? HrtfStatusName(hrtfStatus) : "unsupported";
    VANS_LOG("[VansAudioSystem] Initialized device="
        << (m_ActiveDeviceName.empty() ? "(unknown)" : m_ActiveDeviceName)
        << ", hrtfPolicy=" << VansAudioHrtfModeName(m_DeviceConfig.m_HrtfMode)
        << ", hrtfStatus=" << m_HrtfStatus
        << ", sourceLimit=" << m_DeviceConfig.m_SourceLimit);

    // Distance attenuation is authored and applied by VansAudioVoice.
    // Keep OpenAL source positions active for panning while avoiding a second attenuation pass.
    alDistanceModel(AL_NONE);
    InitializeEffects();
    return true;
}

bool VansAudioSystem::ApplyDeviceConfig(const VansAudioDeviceConfig& config, std::string& error)
{
    const VansAudioDeviceConfig candidate = NormalizeAudioDeviceConfig(config);
    if (!m_Initialized)
    {
        if (Initialize(candidate))
        {
            error.clear();
            return true;
        }
        error = "Audio device configuration could not be initialized";
        return false;
    }

    if (UsesSameContext(m_DeviceConfig, candidate))
    {
        SetMasterVolume(candidate.m_MasterGain);
        SetSourceLimit(candidate.m_SourceLimit);
        error.clear();
        return true;
    }
    if (!m_ActiveSources.empty())
    {
        error = "Audio device configuration cannot change while sources are active";
        return false;
    }

    const VansAudioDeviceConfig previous = m_DeviceConfig;
    Shutdown();
    if (Initialize(candidate))
    {
        error.clear();
        return true;
    }

    const bool restored = Initialize(previous);
    error = restored
        ? "Audio device configuration failed; the previous configuration was restored"
        : "Audio device configuration and rollback both failed";
    return false;
}

VansAudioSourceAcquireStatus VansAudioSystem::TryAcquireSource(std::uint32_t& sourceId)
{
    sourceId = 0;
    if (!m_Initialized)
        return VansAudioSourceAcquireStatus::NotInitialized;

    if (m_ActiveSources.size() >= m_DeviceConfig.m_SourceLimit)
    {
        ++m_SourceLimitRejections;
        return VansAudioSourceAcquireStatus::LimitReached;
    }

    if (!m_PooledSources.empty())
    {
        sourceId = m_PooledSources.back();
        m_PooledSources.pop_back();
        m_ActiveSources.insert(sourceId);
        ResetSourceDefaults(static_cast<ALuint>(sourceId));
        ClearAlErrors();
        return VansAudioSourceAcquireStatus::Acquired;
    }

    ALuint source = 0;
    ClearAlErrors();
    alGenSources(1, &source);
    const ALenum generationError = alGetError();
    if (generationError != AL_NO_ERROR || source == 0)
    {
        if (source != 0)
            alDeleteSources(1, &source);
        ++m_SourceBackendFailures;
        return VansAudioSourceAcquireStatus::BackendFailure;
    }
    sourceId = static_cast<std::uint32_t>(source);
    m_ActiveSources.insert(sourceId);
    return VansAudioSourceAcquireStatus::Acquired;
}

void VansAudioSystem::ReleaseSource(std::uint32_t& sourceId)
{
    if (sourceId == 0)
        return;

    if (!m_Initialized)
    {
        sourceId = 0;
        return;
    }

    const auto active = m_ActiveSources.find(sourceId);
    if (active == m_ActiveSources.end())
    {
        VANS_LOG_WARN("[VansAudioSystem] Ignored release of unknown source " << sourceId);
        sourceId = 0;
        return;
    }

    ALuint source = static_cast<ALuint>(sourceId);
    ResetSourceDefaults(source);
    ClearAlErrors();
    m_ActiveSources.erase(active);
    if (m_ActiveSources.size() + m_PooledSources.size() >= m_DeviceConfig.m_SourceLimit)
        alDeleteSources(1, &source);
    else
        m_PooledSources.push_back(sourceId);
    sourceId = 0;
}

void VansAudioSystem::SetSourceLimit(std::size_t sourceLimit)
{
    m_DeviceConfig.m_SourceLimit = std::clamp<std::size_t>(sourceLimit, 1, 256);
    if (!m_Initialized)
        return;

    while (m_ActiveSources.size() + m_PooledSources.size() > m_DeviceConfig.m_SourceLimit &&
        !m_PooledSources.empty())
    {
        ALuint source = static_cast<ALuint>(m_PooledSources.back());
        m_PooledSources.pop_back();
        alDeleteSources(1, &source);
    }
}

void VansAudioSystem::InitializeEffects()
{
    m_EfxSupported = false;
    m_DefaultReverbEffect = 0;
    m_DefaultReverbSlot = 0;

    auto* device = static_cast<ALCdevice*>(m_Device);
    if (!device || !alcIsExtensionPresent(device, ALC_EXT_EFX_NAME))
    {
        VANS_LOG_WARN("[VansAudioSystem] OpenAL EFX is not available; reverb will be disabled");
        return;
    }

    g_EfxApi.GenEffects = LoadEfxProc<LPALGENEFFECTS>("alGenEffects");
    g_EfxApi.DeleteEffects = LoadEfxProc<LPALDELETEEFFECTS>("alDeleteEffects");
    g_EfxApi.Effecti = LoadEfxProc<LPALEFFECTI>("alEffecti");
    g_EfxApi.Effectf = LoadEfxProc<LPALEFFECTF>("alEffectf");
    g_EfxApi.GenFilters = LoadEfxProc<LPALGENFILTERS>("alGenFilters");
    g_EfxApi.DeleteFilters = LoadEfxProc<LPALDELETEFILTERS>("alDeleteFilters");
    g_EfxApi.Filteri = LoadEfxProc<LPALFILTERI>("alFilteri");
    g_EfxApi.Filterf = LoadEfxProc<LPALFILTERF>("alFilterf");
    g_EfxApi.GenAuxiliaryEffectSlots =
        LoadEfxProc<LPALGENAUXILIARYEFFECTSLOTS>("alGenAuxiliaryEffectSlots");
    g_EfxApi.DeleteAuxiliaryEffectSlots =
        LoadEfxProc<LPALDELETEAUXILIARYEFFECTSLOTS>("alDeleteAuxiliaryEffectSlots");
    g_EfxApi.AuxiliaryEffectSloti =
        LoadEfxProc<LPALAUXILIARYEFFECTSLOTI>("alAuxiliaryEffectSloti");
    g_EfxApi.AuxiliaryEffectSlotf =
        LoadEfxProc<LPALAUXILIARYEFFECTSLOTF>("alAuxiliaryEffectSlotf");

    if (!g_EfxApi.IsLoaded())
    {
        VANS_LOG_WARN("[VansAudioSystem] OpenAL EFX extension is present but function loading failed");
        return;
    }

    ALuint effect = 0;
    ALuint slot = 0;
    ClearAlErrors();
    g_EfxApi.GenEffects(1, &effect);
    g_EfxApi.GenAuxiliaryEffectSlots(1, &slot);
    if (alGetError() != AL_NO_ERROR || effect == 0 || slot == 0)
    {
        if (slot != 0)
            g_EfxApi.DeleteAuxiliaryEffectSlots(1, &slot);
        if (effect != 0)
            g_EfxApi.DeleteEffects(1, &effect);
        VANS_LOG_WARN("[VansAudioSystem] Failed to create OpenAL EFX default reverb resources");
        return;
    }

    g_EfxApi.Effecti(effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
    m_DefaultReverbEffect = effect;
    m_HasCommittedDefaultReverbParameters = false;
    CommitDefaultReverbParameters();
    g_EfxApi.AuxiliaryEffectSloti(slot, AL_EFFECTSLOT_EFFECT, static_cast<ALint>(effect));
    g_EfxApi.AuxiliaryEffectSlotf(slot, AL_EFFECTSLOT_GAIN, m_DefaultReverbWetGain);

    if (alGetError() != AL_NO_ERROR)
    {
        g_EfxApi.AuxiliaryEffectSloti(slot, AL_EFFECTSLOT_EFFECT, AL_EFFECTSLOT_NULL);
        g_EfxApi.DeleteAuxiliaryEffectSlots(1, &slot);
        g_EfxApi.DeleteEffects(1, &effect);
        VANS_LOG_WARN("[VansAudioSystem] Failed to initialize OpenAL EFX default reverb");
        return;
    }

    m_DefaultReverbSlot = slot;
    m_LastCommittedDefaultReverbWetGain = m_DefaultReverbWetGain;
    m_EfxSupported = true;
    VANS_LOG("[VansAudioSystem] OpenAL EFX default reverb slot initialized");
}

void VansAudioSystem::ShutdownEffects()
{
    if (m_DefaultReverbSlot != 0 && g_EfxApi.DeleteAuxiliaryEffectSlots)
    {
        ALuint slot = static_cast<ALuint>(m_DefaultReverbSlot);
        if (g_EfxApi.AuxiliaryEffectSloti)
            g_EfxApi.AuxiliaryEffectSloti(slot, AL_EFFECTSLOT_EFFECT, AL_EFFECTSLOT_NULL);
        g_EfxApi.DeleteAuxiliaryEffectSlots(1, &slot);
    }
    if (m_DefaultReverbEffect != 0 && g_EfxApi.DeleteEffects)
    {
        ALuint effect = static_cast<ALuint>(m_DefaultReverbEffect);
        g_EfxApi.DeleteEffects(1, &effect);
    }
    m_DefaultReverbSlot = 0;
    m_DefaultReverbEffect = 0;
    m_EfxSupported = false;
    m_LastCommittedDefaultReverbWetGain = -1.0f;
    m_DefaultReverbPreset = AudioReverbPreset::Generic;
    m_DefaultReverbPresetName = AudioReverbPresetToString(AudioReverbPreset::Generic);
    m_DefaultReverbParameters = GetAudioReverbPresetParameters(AudioReverbPreset::Generic);
    m_LastCommittedDefaultReverbParameters = AudioReverbPresetParameters{};
    m_HasCommittedDefaultReverbParameters = false;
}

void VansAudioSystem::CommitDefaultReverbParameters()
{
    if (m_DefaultReverbEffect == 0 || !g_EfxApi.Effectf)
        return;
    m_DefaultReverbParameters.Normalize();
    if (m_HasCommittedDefaultReverbParameters &&
        AudioReverbPresetParametersNearlyEqual(
            m_LastCommittedDefaultReverbParameters,
            m_DefaultReverbParameters))
    {
        return;
    }

    const AudioReverbPresetParameters params = m_DefaultReverbParameters;
    g_EfxApi.Effectf(static_cast<ALuint>(m_DefaultReverbEffect), AL_REVERB_DENSITY, params.density);
    g_EfxApi.Effectf(static_cast<ALuint>(m_DefaultReverbEffect), AL_REVERB_DIFFUSION, params.diffusion);
    g_EfxApi.Effectf(static_cast<ALuint>(m_DefaultReverbEffect), AL_REVERB_GAIN, params.gain);
    g_EfxApi.Effectf(static_cast<ALuint>(m_DefaultReverbEffect), AL_REVERB_GAINHF, params.gainHF);
    g_EfxApi.Effectf(static_cast<ALuint>(m_DefaultReverbEffect), AL_REVERB_DECAY_TIME, params.decayTime);
    m_LastCommittedDefaultReverbParameters = params;
    m_HasCommittedDefaultReverbParameters = true;
}

bool VansAudioSystem::ApplyDefaultReverbSend(
    std::uint32_t sourceId,
    float sendGain,
    std::uint32_t& sourceSendFilter) const
{
    if (sourceId == 0)
        return false;

    const float clampedSend = std::clamp(sendGain, 0.0f, 1.0f);
    if (!m_EfxSupported || m_DefaultReverbSlot == 0 || clampedSend <= 0.0001f)
    {
        alSource3i(
            static_cast<ALuint>(sourceId),
            AL_AUXILIARY_SEND_FILTER,
            AL_EFFECTSLOT_NULL,
            0,
            AL_FILTER_NULL);
        ReleaseSourceEffectFilter(sourceSendFilter);
        return clampedSend <= 0.0001f;
    }

    if (sourceSendFilter == 0)
    {
        ALuint filter = 0;
        ClearAlErrors();
        g_EfxApi.GenFilters(1, &filter);
        if (alGetError() != AL_NO_ERROR || filter == 0)
        {
            VANS_LOG_WARN("[VansAudioSystem] Failed to create EFX reverb send filter");
            return false;
        }
        sourceSendFilter = filter;
    }

    const ALuint filter = static_cast<ALuint>(sourceSendFilter);
    g_EfxApi.Filteri(filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
    g_EfxApi.Filterf(filter, AL_LOWPASS_GAIN, clampedSend);
    g_EfxApi.Filterf(filter, AL_LOWPASS_GAINHF, 1.0f);
    alSource3i(
        static_cast<ALuint>(sourceId),
        AL_AUXILIARY_SEND_FILTER,
        static_cast<ALint>(m_DefaultReverbSlot),
        0,
        static_cast<ALint>(filter));

    const ALenum error = alGetError();
    if (error != AL_NO_ERROR)
    {
        VANS_LOG_WARN("[VansAudioSystem] Failed to apply EFX reverb send, error=" << error);
        return false;
    }
    return true;
}

bool VansAudioSystem::ApplySourceDirectLowpass(
    std::uint32_t sourceId,
    float highFrequencyGain,
    std::uint32_t& sourceFilter) const
{
    if (sourceId == 0)
        return false;

    const float clampedHighFrequencyGain = std::clamp(highFrequencyGain, 0.0f, 1.0f);
    if (!m_EfxSupported || clampedHighFrequencyGain >= 0.999f)
    {
        alSourcei(static_cast<ALuint>(sourceId), AL_DIRECT_FILTER, AL_FILTER_NULL);
        ReleaseSourceEffectFilter(sourceFilter);
        return clampedHighFrequencyGain >= 0.999f;
    }

    if (sourceFilter == 0)
    {
        ALuint filter = 0;
        ClearAlErrors();
        g_EfxApi.GenFilters(1, &filter);
        if (alGetError() != AL_NO_ERROR || filter == 0)
        {
            VANS_LOG_WARN("[VansAudioSystem] Failed to create EFX direct lowpass filter");
            return false;
        }
        sourceFilter = filter;
    }

    const ALuint filter = static_cast<ALuint>(sourceFilter);
    g_EfxApi.Filteri(filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
    g_EfxApi.Filterf(filter, AL_LOWPASS_GAIN, 1.0f);
    g_EfxApi.Filterf(filter, AL_LOWPASS_GAINHF, clampedHighFrequencyGain);
    alSourcei(static_cast<ALuint>(sourceId), AL_DIRECT_FILTER, static_cast<ALint>(filter));

    const ALenum error = alGetError();
    if (error != AL_NO_ERROR)
    {
        VANS_LOG_WARN("[VansAudioSystem] Failed to apply EFX direct lowpass, error=" << error);
        return false;
    }
    return true;
}

void VansAudioSystem::ReleaseSourceEffectFilter(std::uint32_t& sourceSendFilter) const
{
    if (sourceSendFilter == 0 || !g_EfxApi.DeleteFilters)
        return;
    ALuint filter = static_cast<ALuint>(sourceSendFilter);
    g_EfxApi.DeleteFilters(1, &filter);
    sourceSendFilter = 0;
}

void VansAudioSystem::SetDefaultReverbWetGain(float wetGain)
{
    m_DefaultReverbWetGain = std::clamp(wetGain, 0.0f, 1.0f);
    if (!m_EfxSupported || m_DefaultReverbSlot == 0 || !g_EfxApi.AuxiliaryEffectSlotf)
        return;

    if (m_LastCommittedDefaultReverbWetGain >= 0.0f &&
        std::abs(m_DefaultReverbWetGain - m_LastCommittedDefaultReverbWetGain) < 0.0005f)
    {
        return;
    }

    g_EfxApi.AuxiliaryEffectSlotf(
        static_cast<ALuint>(m_DefaultReverbSlot),
        AL_EFFECTSLOT_GAIN,
        m_DefaultReverbWetGain);
    m_LastCommittedDefaultReverbWetGain = m_DefaultReverbWetGain;
}

void VansAudioSystem::SetDefaultReverbPreset(AudioReverbPreset preset)
{
    m_DefaultReverbPreset = preset;
    m_DefaultReverbPresetName = AudioReverbPresetToString(preset);
    m_DefaultReverbParameters = GetAudioReverbPresetParameters(preset);
    if (!m_EfxSupported || m_DefaultReverbEffect == 0)
        return;
    CommitDefaultReverbParameters();
}

void VansAudioSystem::SetDefaultReverbParameters(
    AudioReverbPresetParameters parameters,
    const char* presetName)
{
    parameters.Normalize();
    m_DefaultReverbParameters = parameters;
    if (presetName && *presetName)
        m_DefaultReverbPresetName = presetName;
    if (!m_EfxSupported || m_DefaultReverbEffect == 0)
        return;
    CommitDefaultReverbParameters();
}

// ===========================================================================
// Shutdown — 释放上下文和设备
// ===========================================================================
void VansAudioSystem::Shutdown()
{
    if (!m_Initialized)
        return;

    ShutdownEffects();
    if (!m_ActiveSources.empty())
    {
        VANS_LOG_WARN("[VansAudioSystem] Shutdown reclaiming "
            << m_ActiveSources.size() << " active source lease(s)");
    }
    for (std::uint32_t pooledSource : m_PooledSources)
    {
        ALuint source = static_cast<ALuint>(pooledSource);
        alDeleteSources(1, &source);
    }
    m_PooledSources.clear();
    for (std::uint32_t activeSource : m_ActiveSources)
    {
        ALuint source = static_cast<ALuint>(activeSource);
        alDeleteSources(1, &source);
    }
    m_ActiveSources.clear();
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
    m_ActiveDeviceName.clear();
    m_HrtfStatus = "unavailable";
    VANS_LOG("[VansAudioSystem] 已关闭");
}

// ===========================================================================
// UpdateListener — 每帧更新监听者空间变换
// ===========================================================================
void VansAudioSystem::UpdateListener(float px, float py, float pz,
                                      float fx, float fy, float fz,
                                      float ux, float uy, float uz,
                                      float vx, float vy, float vz) const
{
    if (!m_Initialized) return;

    alListener3f(AL_POSITION, px, py, pz);
    alListener3f(AL_VELOCITY, vx, vy, vz);

    // OpenAL orientation 格式：[forward(3), up(3)]
    const float orientation[6] = { fx, fy, fz, ux, uy, uz };
    alListenerfv(AL_ORIENTATION, orientation);
}

// ===========================================================================
// SetMasterVolume
// ===========================================================================
void VansAudioSystem::SetMasterVolume(float gain)
{
    m_DeviceConfig.m_MasterGain = std::clamp(gain, 0.0f, 1.0f);
    if (!m_Initialized) return;
    alListenerf(AL_GAIN, m_DeviceConfig.m_MasterGain);
}

} // namespace VansEngine
