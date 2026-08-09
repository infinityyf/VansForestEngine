#include "VansAudioSourceInstance.h"

#include "VansAudioSystem.h"
#include "../Util/VansLog.h"

#include <AL/al.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace VansEngine
{
VansAudioSourceInstance::~VansAudioSourceInstance()
{
    Close();
}

VansAudioSourceInstance::VansAudioSourceInstance(VansAudioSourceInstance&& other) noexcept
{
    MoveFrom(other);
}

VansAudioSourceInstance& VansAudioSourceInstance::operator=(VansAudioSourceInstance&& other) noexcept
{
    if (this != &other)
    {
        Close();
        MoveFrom(other);
    }
    return *this;
}

void VansAudioSourceInstance::MoveFrom(VansAudioSourceInstance& other) noexcept
{
    m_Properties = std::move(other.m_Properties);
    m_AttenuationModeRuntime = other.m_AttenuationModeRuntime;
    m_SourceId = other.m_SourceId;
    m_SharedBufferId = other.m_SharedBufferId;
    m_ReverbSendFilterId = other.m_ReverbSendFilterId;
    m_DirectLowpassFilterId = other.m_DirectLowpassFilterId;
    m_PositionX = other.m_PositionX;
    m_PositionY = other.m_PositionY;
    m_PositionZ = other.m_PositionZ;
    m_VelocityX = other.m_VelocityX;
    m_VelocityY = other.m_VelocityY;
    m_VelocityZ = other.m_VelocityZ;
    m_DirectionX = other.m_DirectionX;
    m_DirectionY = other.m_DirectionY;
    m_DirectionZ = other.m_DirectionZ;
    m_ConeSettings = other.m_ConeSettings;
    m_DistanceGain = other.m_DistanceGain;
    m_OcclusionGain = other.m_OcclusionGain;
    m_OcclusionHighFrequencyGain = other.m_OcclusionHighFrequencyGain;
    m_BusGain = other.m_BusGain;
    m_BusLowpassHighFrequencyGain = other.m_BusLowpassHighFrequencyGain;
    m_VirtualizationGain = other.m_VirtualizationGain;
    m_LastCommittedGain = other.m_LastCommittedGain;
    m_PlaybackOffsetSeconds = other.m_PlaybackOffsetSeconds;
    m_LogicalPlaying = other.m_LogicalPlaying;
    m_LogicalPaused = other.m_LogicalPaused;

    other.m_SourceId = 0;
    other.m_SharedBufferId = 0;
    other.m_ReverbSendFilterId = 0;
    other.m_DirectLowpassFilterId = 0;
    other.m_PlaybackOffsetSeconds = 0.0f;
    other.m_LogicalPlaying = false;
    other.m_LogicalPaused = false;
}

bool VansAudioSourceInstance::OpenStatic(uint32_t sharedBufferId, const AudioNodeProperties& properties)
{
    Close();
    if (sharedBufferId == 0)
        return false;

    m_Properties = properties;
    m_Properties.m_PlayMode = AudioPlayMode::Static;
    m_Properties.m_AutoPlay = false;
    m_Properties.m_BusName = NormalizeAudioBusName(m_Properties.m_BusName);
    m_Properties.m_Volume = std::clamp(m_Properties.m_Volume, 0.0f, 4.0f);
    m_Properties.m_Pitch = std::max(m_Properties.m_Pitch, 0.01f);
    m_Properties.m_ReverbSend = std::clamp(m_Properties.m_ReverbSend, 0.0f, 1.0f);
    m_Properties.m_LowpassHighFrequencyGain =
        std::clamp(m_Properties.m_LowpassHighFrequencyGain, 0.0f, 1.0f);
    m_OcclusionGain = 1.0f;
    m_OcclusionHighFrequencyGain = 1.0f;
    m_BusLowpassHighFrequencyGain = 1.0f;
    m_VirtualizationGain = 1.0f;
    m_PlaybackOffsetSeconds = 0.0f;
    m_LogicalPlaying = false;
    m_LogicalPaused = false;
    m_AttenuationModeRuntime = AudioAttenuationModeFromString(m_Properties.m_AttenuationMode);

    AudioAttenuationSettings attenuation;
    attenuation.mode = m_AttenuationModeRuntime;
    attenuation.referenceDistance = m_Properties.m_RefDist;
    attenuation.maxDistance = m_Properties.m_MaxDist;
    attenuation.rolloff = m_Properties.m_RollOff;
    attenuation.Normalize();
    m_Properties.m_RefDist = attenuation.referenceDistance;
    m_Properties.m_MaxDist = attenuation.maxDistance;
    m_Properties.m_RollOff = attenuation.rolloff;
    m_SharedBufferId = sharedBufferId;

    if (!AcquireHardwareVoice())
    {
        VANS_LOG_ERROR("[VansAudioSourceInstance] AcquireSource failed: " << m_Properties.m_Name);
        m_SharedBufferId = 0;
        return false;
    }
    return true;
}

bool VansAudioSourceInstance::AcquireHardwareVoice()
{
    if (m_SourceId != 0)
        return true;
    if (m_SharedBufferId == 0)
        return false;

    m_SourceId = VansAudioSystem::GetInstance().AcquireSource();
    if (m_SourceId == 0)
        return false;

    alSourcei(m_SourceId, AL_BUFFER, static_cast<ALint>(m_SharedBufferId));
    alSourcef(m_SourceId, AL_PITCH, m_Properties.m_Pitch);
    alSourcei(m_SourceId, AL_LOOPING, m_Properties.m_Loop ? AL_TRUE : AL_FALSE);
    alSource3f(m_SourceId, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    ApplySpatialProperties();
    ApplyDirectionalProperties();
    ApplyEffectSends();
    ApplyDirectLowpass();
    CommitGain();
    if (m_PlaybackOffsetSeconds > 0.0f)
        alSourcef(m_SourceId, AL_SEC_OFFSET, m_PlaybackOffsetSeconds);
    if (m_LogicalPlaying && !m_LogicalPaused)
        alSourcePlay(m_SourceId);

    if (alGetError() != AL_NO_ERROR)
    {
        VANS_LOG_ERROR("[VansAudioSourceInstance] AcquireHardwareVoice failed: " << m_Properties.m_Name);
        ReleaseHardwareVoice();
        return false;
    }
    return true;
}

void VansAudioSourceInstance::ReleaseHardwareVoice()
{
    if (m_SourceId == 0)
        return;

    ALint state = 0;
    alGetSourcei(m_SourceId, AL_SOURCE_STATE, &state);
    m_LogicalPlaying = state == AL_PLAYING || (m_LogicalPlaying && state != AL_STOPPED);
    m_LogicalPaused = state == AL_PAUSED;
    if (state == AL_PLAYING || state == AL_PAUSED)
    {
        ALfloat offset = 0.0f;
        alGetSourcef(m_SourceId, AL_SEC_OFFSET, &offset);
        m_PlaybackOffsetSeconds = std::max(0.0f, static_cast<float>(offset));
    }

    VansAudioSystem::GetInstance().ApplyDefaultReverbSend(m_SourceId, 0.0f, m_ReverbSendFilterId);
    VansAudioSystem::GetInstance().ApplySourceDirectLowpass(m_SourceId, 1.0f, m_DirectLowpassFilterId);
    VansAudioSystem::GetInstance().ReleaseSource(m_SourceId);
    m_LastCommittedGain = -1.0f;
}

void VansAudioSourceInstance::Close()
{
    ReleaseHardwareVoice();
    m_SharedBufferId = 0;
    m_LastCommittedGain = -1.0f;
    m_PlaybackOffsetSeconds = 0.0f;
    m_LogicalPlaying = false;
    m_LogicalPaused = false;
}

void VansAudioSourceInstance::Play()
{
    m_LogicalPlaying = true;
    m_LogicalPaused = false;
    if (m_SourceId == 0 && m_VirtualizationGain > 0.0005f)
        AcquireHardwareVoice();
    if (m_SourceId) alSourcePlay(m_SourceId);
}

void VansAudioSourceInstance::Pause()
{
    m_LogicalPaused = true;
    if (m_SourceId) alSourcePause(m_SourceId);
}

void VansAudioSourceInstance::Stop()
{
    m_LogicalPlaying = false;
    m_LogicalPaused = false;
    m_PlaybackOffsetSeconds = 0.0f;
    if (m_SourceId) alSourceStop(m_SourceId);
}

bool VansAudioSourceInstance::SetPlaybackOffsetSeconds(float seconds)
{
	m_PlaybackOffsetSeconds = std::max(0.0f, seconds);
	if (!m_SourceId && !AcquireHardwareVoice())
		return false;
	alSourcef(m_SourceId, AL_SEC_OFFSET, m_PlaybackOffsetSeconds);
	return alGetError() == AL_NO_ERROR;
}

float VansAudioSourceInstance::GetPlaybackOffsetSeconds() const
{
	if (!m_SourceId) return m_PlaybackOffsetSeconds;
	float offset = m_PlaybackOffsetSeconds;
	alGetSourcef(m_SourceId, AL_SEC_OFFSET, &offset);
	return offset;
}

void VansAudioSourceInstance::Resume()
{
    m_LogicalPaused = false;
    if (m_SourceId == 0 && m_VirtualizationGain > 0.0005f)
        AcquireHardwareVoice();
    if (!m_SourceId) return;
    ALint state = 0;
    alGetSourcei(m_SourceId, AL_SOURCE_STATE, &state);
    if (state == AL_PAUSED)
        alSourcePlay(m_SourceId);
    m_LogicalPlaying = true;
}

bool VansAudioSourceInstance::IsPlaying() const
{
    if (!m_SourceId) return m_LogicalPlaying && !m_LogicalPaused;
    ALint state = 0;
    alGetSourcei(m_SourceId, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

bool VansAudioSourceInstance::IsPaused() const
{
    if (!m_SourceId) return m_LogicalPaused;
    ALint state = 0;
    alGetSourcei(m_SourceId, AL_SOURCE_STATE, &state);
    return state == AL_PAUSED;
}

void VansAudioSourceInstance::SetEnabled(bool enabled)
{
    if (!enabled)
        Pause();
}

void VansAudioSourceInstance::SetPosition(float x, float y, float z)
{
    m_PositionX = x;
    m_PositionY = y;
    m_PositionZ = z;
    if (m_SourceId) alSource3f(m_SourceId, AL_POSITION, x, y, z);
}

void VansAudioSourceInstance::SetSpatial(bool enabled)
{
    m_Properties.m_Spatial = enabled;
    if (!enabled)
        m_DistanceGain = 1.0f;
    ApplySpatialProperties();
    ApplyDirectionalProperties();
    CommitGain();
}

void VansAudioSourceInstance::SetStereoPan(float pan)
{
	m_Properties.m_StereoPan = std::clamp(pan, -1.0f, 1.0f);
	if (!m_Properties.m_Spatial) ApplySpatialProperties();
}

void VansAudioSourceInstance::UpdateDistanceGain(float listenerX, float listenerY, float listenerZ)
{
    if (!m_Properties.m_Spatial)
    {
        m_DistanceGain = 1.0f;
        CommitGain();
        return;
    }

    const float dx = m_PositionX - listenerX;
    const float dy = m_PositionY - listenerY;
    const float dz = m_PositionZ - listenerZ;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

    AudioAttenuationSettings attenuation;
    attenuation.mode = m_AttenuationModeRuntime;
    attenuation.referenceDistance = m_Properties.m_RefDist;
    attenuation.maxDistance = m_Properties.m_MaxDist;
    attenuation.rolloff = m_Properties.m_RollOff;
    m_DistanceGain = ComputeDistanceGain(distance, attenuation);
    CommitGain();
}

void VansAudioSourceInstance::SetVolume(float gain)
{
    m_Properties.m_Volume = std::clamp(gain, 0.0f, 4.0f);
    CommitGain();
}

void VansAudioSourceInstance::SetPitch(float pitch)
{
    m_Properties.m_Pitch = std::max(pitch, 0.01f);
    if (m_SourceId) alSourcef(m_SourceId, AL_PITCH, m_Properties.m_Pitch);
}

void VansAudioSourceInstance::SetLoop(bool loop)
{
    m_Properties.m_Loop = loop;
    if (m_SourceId) alSourcei(m_SourceId, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
}

void VansAudioSourceInstance::SetRefDistance(float distance)
{
    m_Properties.m_RefDist = std::max(distance, 0.01f);
    if (m_Properties.m_MaxDist <= m_Properties.m_RefDist)
        m_Properties.m_MaxDist = m_Properties.m_RefDist + 0.01f;
    if (m_SourceId) alSourcef(m_SourceId, AL_REFERENCE_DISTANCE, m_Properties.m_RefDist);
}

void VansAudioSourceInstance::SetMaxDistance(float distance)
{
    m_Properties.m_MaxDist = std::max(distance, m_Properties.m_RefDist + 0.01f);
    if (m_SourceId) alSourcef(m_SourceId, AL_MAX_DISTANCE, m_Properties.m_MaxDist);
}

void VansAudioSourceInstance::SetRolloff(float rolloff)
{
    m_Properties.m_RollOff = std::max(rolloff, 0.0f);
    if (m_SourceId) alSourcef(m_SourceId, AL_ROLLOFF_FACTOR, m_Properties.m_Spatial ? m_Properties.m_RollOff : 0.0f);
}

void VansAudioSourceInstance::SetAttenuationMode(AudioAttenuationMode mode)
{
    m_AttenuationModeRuntime = mode;
    m_Properties.m_AttenuationMode = AudioAttenuationModeToString(mode);
}

void VansAudioSourceInstance::SetReverbSend(float send)
{
    m_Properties.m_ReverbSend = std::clamp(send, 0.0f, 1.0f);
    ApplyEffectSends();
}

void VansAudioSourceInstance::SetLowpassHighFrequencyGain(float highFrequencyGain)
{
    const float clampedGain = std::clamp(highFrequencyGain, 0.0f, 1.0f);
    if (std::abs(clampedGain - m_Properties.m_LowpassHighFrequencyGain) < 0.0005f)
        return;
    m_Properties.m_LowpassHighFrequencyGain = clampedGain;
    ApplyDirectLowpass();
}

void VansAudioSourceInstance::SetBusName(const std::string& busName)
{
    m_Properties.m_BusName = NormalizeAudioBusName(busName);
}

void VansAudioSourceInstance::SetBusGain(float gain)
{
    const float clampedGain = std::clamp(gain, 0.0f, 4.0f);
    if (std::abs(clampedGain - m_BusGain) < 0.0005f)
        return;
    m_BusGain = clampedGain;
    CommitGain();
}

void VansAudioSourceInstance::SetBusLowpassHighFrequencyGain(float highFrequencyGain)
{
    const float clampedGain = std::clamp(highFrequencyGain, 0.0f, 1.0f);
    if (std::abs(clampedGain - m_BusLowpassHighFrequencyGain) < 0.0005f)
        return;
    m_BusLowpassHighFrequencyGain = clampedGain;
    ApplyDirectLowpass();
}

void VansAudioSourceInstance::SetVirtualizationGain(float gain)
{
    const float clampedGain = std::clamp(gain, 0.0f, 1.0f);
    if (std::abs(clampedGain - m_VirtualizationGain) < 0.0005f)
        return;
    m_VirtualizationGain = clampedGain;
    if (m_VirtualizationGain <= 0.0005f)
    {
        ReleaseHardwareVoice();
        return;
    }
    if (m_SourceId == 0 && m_SharedBufferId != 0)
        AcquireHardwareVoice();
    CommitGain();
}

void VansAudioSourceInstance::SetOcclusion(float gain, float highFrequencyGain)
{
    const float clampedGain = std::clamp(gain, 0.0f, 1.0f);
    const float clampedHighFrequencyGain = std::clamp(highFrequencyGain, 0.0f, 1.0f);
    const bool gainChanged = std::abs(clampedGain - m_OcclusionGain) >= 0.0005f;
    const bool highFrequencyChanged =
        std::abs(clampedHighFrequencyGain - m_OcclusionHighFrequencyGain) >= 0.0005f;
    if (!gainChanged && !highFrequencyChanged)
        return;

    m_OcclusionGain = clampedGain;
    m_OcclusionHighFrequencyGain = clampedHighFrequencyGain;
    if (m_SourceId && highFrequencyChanged)
        ApplyDirectLowpass();
    CommitGain();
}

void VansAudioSourceInstance::SetVelocity(float x, float y, float z)
{
    m_VelocityX = x;
    m_VelocityY = y;
    m_VelocityZ = z;
    if (m_SourceId)
        alSource3f(m_SourceId, AL_VELOCITY, m_VelocityX, m_VelocityY, m_VelocityZ);
}

void VansAudioSourceInstance::SetDirection(float x, float y, float z)
{
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length <= 0.0001f)
        return;
    m_DirectionX = x / length;
    m_DirectionY = y / length;
    m_DirectionZ = z / length;
    ApplyDirectionalProperties();
}

void VansAudioSourceInstance::SetCone(AudioConeSettings settings)
{
    m_ConeSettings = NormalizeAudioConeSettings(settings);
    ApplyDirectionalProperties();
}

void VansAudioSourceInstance::ApplySpatialProperties()
{
    if (!m_SourceId) return;
    if (m_Properties.m_Spatial)
    {
        alSourcei(m_SourceId, AL_SOURCE_RELATIVE, AL_FALSE);
        alSourcef(m_SourceId, AL_REFERENCE_DISTANCE, m_Properties.m_RefDist);
        alSourcef(m_SourceId, AL_MAX_DISTANCE, m_Properties.m_MaxDist);
        alSourcef(m_SourceId, AL_ROLLOFF_FACTOR, m_Properties.m_RollOff);
        alSource3f(m_SourceId, AL_POSITION, m_PositionX, m_PositionY, m_PositionZ);
    }
    else
    {
        alSourcei(m_SourceId, AL_SOURCE_RELATIVE, AL_TRUE);
		alSource3f(m_SourceId, AL_POSITION, m_Properties.m_StereoPan, 0.0f, 0.0f);
        alSourcef(m_SourceId, AL_ROLLOFF_FACTOR, 0.0f);
    }
}

void VansAudioSourceInstance::ApplyDirectionalProperties()
{
    if (!m_SourceId)
        return;

    AudioConeSettings settings = NormalizeAudioConeSettings(m_ConeSettings);
    if (!m_Properties.m_Spatial)
        settings.enabled = false;
    settings.Normalize();

    alSource3f(m_SourceId, AL_DIRECTION, m_DirectionX, m_DirectionY, m_DirectionZ);
    alSourcef(m_SourceId, AL_CONE_INNER_ANGLE, settings.innerAngleDegrees);
    alSourcef(m_SourceId, AL_CONE_OUTER_ANGLE, settings.outerAngleDegrees);
    alSourcef(m_SourceId, AL_CONE_OUTER_GAIN, settings.outerGain);
}

void VansAudioSourceInstance::ApplyEffectSends()
{
    if (!m_SourceId)
        return;
    VansAudioSystem::GetInstance().ApplyDefaultReverbSend(
        m_SourceId,
        m_Properties.m_ReverbSend,
        m_ReverbSendFilterId);
}

void VansAudioSourceInstance::ApplyDirectLowpass()
{
    if (!m_SourceId)
        return;
    const float highFrequencyGain = std::clamp(
        m_Properties.m_LowpassHighFrequencyGain *
            m_BusLowpassHighFrequencyGain *
            m_OcclusionHighFrequencyGain,
        0.0f,
        1.0f);
    VansAudioSystem::GetInstance().ApplySourceDirectLowpass(
        m_SourceId,
        highFrequencyGain,
        m_DirectLowpassFilterId);
}

void VansAudioSourceInstance::CommitGain()
{
    if (!m_SourceId)
        return;
    const float finalGain = std::clamp(
        m_Properties.m_Volume *
            m_DistanceGain *
            m_OcclusionGain *
            m_BusGain *
            m_VirtualizationGain,
        0.0f,
        4.0f);
    if (m_LastCommittedGain >= 0.0f && std::abs(finalGain - m_LastCommittedGain) < 0.0005f)
        return;
    alSourcef(m_SourceId, AL_GAIN, finalGain);
    m_LastCommittedGain = finalGain;
}
}
