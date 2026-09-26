#include "VansAudioVoice.h"

#include "VansAudioSystem.h"

#include <AL/al.h>

#include <algorithm>
#include <cmath>

namespace VansEngine
{
void VansAudioVoice::InitializeVoice(const VansAudioProperties& properties)
{
    m_Properties = properties;
    m_AttenuationMode = AudioAttenuationModeFromString(m_Properties.m_AttenuationMode);

    AudioAttenuationSettings attenuation;
    attenuation.mode = m_AttenuationMode;
    attenuation.referenceDistance = m_Properties.m_RefDist;
    attenuation.maxDistance = m_Properties.m_MaxDist;
    attenuation.rolloff = m_Properties.m_RollOff;
    attenuation.Normalize();
    m_Properties.m_RefDist = attenuation.referenceDistance;
    m_Properties.m_MaxDist = attenuation.maxDistance;
    m_Properties.m_RollOff = attenuation.rolloff;
    m_Properties.m_Volume = std::clamp(m_Properties.m_Volume, 0.0f, 4.0f);
    m_Properties.m_Pitch = std::max(m_Properties.m_Pitch, 0.01f);
    m_Properties.m_StereoPan = std::clamp(m_Properties.m_StereoPan, -1.0f, 1.0f);
    m_Properties.m_ReverbSend = std::clamp(m_Properties.m_ReverbSend, 0.0f, 1.0f);
    m_Properties.m_LowpassHighFrequencyGain =
        std::clamp(m_Properties.m_LowpassHighFrequencyGain, 0.0f, 1.0f);
    m_Properties.m_BusName = NormalizeAudioBusName(m_Properties.m_BusName);

    m_PositionX = 0.0f;
    m_PositionY = 0.0f;
    m_PositionZ = 0.0f;
    m_VelocityX = 0.0f;
    m_VelocityY = 0.0f;
    m_VelocityZ = 0.0f;
    m_DirectionX = 0.0f;
    m_DirectionY = 0.0f;
    m_DirectionZ = 1.0f;
    m_Cone = AudioConeSettings{};
    m_DistanceGain = 1.0f;
    m_OcclusionGain = 1.0f;
    m_OcclusionHighFrequencyGain = 1.0f;
    m_BusGain = 1.0f;
    m_BusLowpassHighFrequencyGain = 1.0f;
    m_VirtualizationGain = 1.0f;
    m_LastCommittedGain = -1.0f;
}

VansAudioSourceAcquireStatus VansAudioVoice::AcquireSource()
{
    if (m_SourceId != 0)
        return VansAudioSourceAcquireStatus::Acquired;

    const VansAudioSourceAcquireStatus status =
        VansAudioSystem::GetInstance().TryAcquireSource(m_SourceId);
    if (status != VansAudioSourceAcquireStatus::Acquired)
        return status;

    alSourcef(m_SourceId, AL_PITCH, m_Properties.m_Pitch);
    alSourcei(
        m_SourceId,
        AL_LOOPING,
        m_Properties.m_PlayMode == VansAudioPlayMode::Static && m_Properties.m_Loop
            ? AL_TRUE
            : AL_FALSE);
    alSource3f(m_SourceId, AL_VELOCITY, m_VelocityX, m_VelocityY, m_VelocityZ);
    ApplySpatialProperties();
    ApplyDirectionalProperties();
    ApplyEffectSends();
    ApplyDirectLowpass();
    CommitGain();
    return VansAudioSourceAcquireStatus::Acquired;
}

void VansAudioVoice::ReleaseSource()
{
    if (m_SourceId == 0)
        return;
    VansAudioSystem& audioSystem = VansAudioSystem::GetInstance();
    if (audioSystem.IsInitialized())
    {
        audioSystem.ApplyDefaultReverbSend(m_SourceId, 0.0f, m_ReverbSendFilterId);
        audioSystem.ApplySourceDirectLowpass(m_SourceId, 1.0f, m_DirectLowpassFilterId);
    }
    else
    {
        m_ReverbSendFilterId = 0;
        m_DirectLowpassFilterId = 0;
    }
    audioSystem.ReleaseSource(m_SourceId);
    m_LastCommittedGain = -1.0f;
}

void VansAudioVoice::SetPlaybackEnabled(bool enabled)
{
    if (!enabled)
        Pause();
}

void VansAudioVoice::SetPosition(float x, float y, float z)
{
    m_PositionX = x;
    m_PositionY = y;
    m_PositionZ = z;
    if (m_SourceId)
        alSource3f(m_SourceId, AL_POSITION, x, y, z);
}

void VansAudioVoice::SetSpatial(bool enabled)
{
    m_Properties.m_Spatial = enabled;
    if (!enabled)
        m_DistanceGain = 1.0f;
    ApplySpatialProperties();
    ApplyDirectionalProperties();
    CommitGain();
}

void VansAudioVoice::SetStereoPan(float pan)
{
    m_Properties.m_StereoPan = std::clamp(pan, -1.0f, 1.0f);
    if (!m_Properties.m_Spatial)
        ApplySpatialProperties();
}

void VansAudioVoice::UpdateDistanceGain(float listenerX, float listenerY, float listenerZ)
{
    if (!m_Properties.m_Spatial)
    {
        SetSpatialGain(1.0f);
        return;
    }

    const float dx = m_PositionX - listenerX;
    const float dy = m_PositionY - listenerY;
    const float dz = m_PositionZ - listenerZ;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

    AudioAttenuationSettings attenuation;
    attenuation.mode = m_AttenuationMode;
    attenuation.referenceDistance = m_Properties.m_RefDist;
    attenuation.maxDistance = m_Properties.m_MaxDist;
    attenuation.rolloff = m_Properties.m_RollOff;
    SetSpatialGain(ComputeDistanceGain(distance, attenuation));
}

void VansAudioVoice::SetSpatialGain(float gain)
{
    m_DistanceGain = std::clamp(gain, 0.0f, 1.0f);
    CommitGain();
}

void VansAudioVoice::SetVolume(float gain)
{
    m_Properties.m_Volume = std::clamp(gain, 0.0f, 4.0f);
    CommitGain();
}

void VansAudioVoice::SetPitch(float pitch)
{
    m_Properties.m_Pitch = std::max(pitch, 0.01f);
    if (m_SourceId)
        alSourcef(m_SourceId, AL_PITCH, m_Properties.m_Pitch);
}

void VansAudioVoice::SetLoop(bool loop)
{
    m_Properties.m_Loop = loop;
    if (m_SourceId && m_Properties.m_PlayMode == VansAudioPlayMode::Static)
        alSourcei(m_SourceId, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
}

void VansAudioVoice::SetRefDistance(float distance)
{
    m_Properties.m_RefDist = std::max(distance, 0.01f);
    if (m_Properties.m_MaxDist <= m_Properties.m_RefDist)
        m_Properties.m_MaxDist = m_Properties.m_RefDist + 0.01f;
}

void VansAudioVoice::SetMaxDistance(float distance)
{
    m_Properties.m_MaxDist = std::max(distance, m_Properties.m_RefDist + 0.01f);
}

void VansAudioVoice::SetRolloff(float rolloff)
{
    m_Properties.m_RollOff = std::max(rolloff, 0.0f);
}

void VansAudioVoice::SetAttenuationMode(AudioAttenuationMode mode)
{
    m_AttenuationMode = mode;
    m_Properties.m_AttenuationMode = AudioAttenuationModeToString(mode);
}

void VansAudioVoice::SetReverbSend(float send)
{
    m_Properties.m_ReverbSend = std::clamp(send, 0.0f, 1.0f);
    ApplyEffectSends();
}

void VansAudioVoice::SetLowpassHighFrequencyGain(float gain)
{
    const float clamped = std::clamp(gain, 0.0f, 1.0f);
    if (std::abs(clamped - m_Properties.m_LowpassHighFrequencyGain) < 0.0005f)
        return;
    m_Properties.m_LowpassHighFrequencyGain = clamped;
    ApplyDirectLowpass();
}

void VansAudioVoice::SetBusName(const std::string& busName)
{
    m_Properties.m_BusName = NormalizeAudioBusName(busName);
}

void VansAudioVoice::SetBusGain(float gain)
{
    const float clamped = std::clamp(gain, 0.0f, 4.0f);
    if (std::abs(clamped - m_BusGain) < 0.0005f)
        return;
    m_BusGain = clamped;
    CommitGain();
}

void VansAudioVoice::SetBusLowpassHighFrequencyGain(float gain)
{
    const float clamped = std::clamp(gain, 0.0f, 1.0f);
    if (std::abs(clamped - m_BusLowpassHighFrequencyGain) < 0.0005f)
        return;
    m_BusLowpassHighFrequencyGain = clamped;
    ApplyDirectLowpass();
}

void VansAudioVoice::SetVirtualizationGain(float gain)
{
    const float clamped = std::clamp(gain, 0.0f, 1.0f);
    if (std::abs(clamped - m_VirtualizationGain) < 0.0005f)
        return;
    m_VirtualizationGain = clamped;
    OnVirtualizationChanged();
    CommitGain();
}

void VansAudioVoice::SetOcclusion(float gain, float highFrequencyGain)
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

void VansAudioVoice::SetVelocity(float x, float y, float z)
{
    m_VelocityX = x;
    m_VelocityY = y;
    m_VelocityZ = z;
    if (m_SourceId)
        alSource3f(m_SourceId, AL_VELOCITY, x, y, z);
}

void VansAudioVoice::SetDirection(float x, float y, float z)
{
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length <= 0.0001f)
        return;
    m_DirectionX = x / length;
    m_DirectionY = y / length;
    m_DirectionZ = z / length;
    ApplyDirectionalProperties();
}

void VansAudioVoice::SetCone(AudioConeSettings settings)
{
    m_Cone = NormalizeAudioConeSettings(settings);
    ApplyDirectionalProperties();
}

void VansAudioVoice::ApplySpatialProperties()
{
    if (!m_SourceId)
        return;
    if (m_Properties.m_Spatial)
    {
        alSourcei(m_SourceId, AL_SOURCE_RELATIVE, AL_FALSE);
        alSource3f(m_SourceId, AL_POSITION, m_PositionX, m_PositionY, m_PositionZ);
    }
    else
    {
        alSourcei(m_SourceId, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(m_SourceId, AL_POSITION, m_Properties.m_StereoPan, 0.0f, 0.0f);
    }
}

void VansAudioVoice::ApplyDirectionalProperties()
{
    if (!m_SourceId)
        return;
    AudioConeSettings settings = NormalizeAudioConeSettings(m_Cone);
    if (!m_Properties.m_Spatial)
        settings.enabled = false;
    settings.Normalize();
    alSource3f(m_SourceId, AL_DIRECTION, m_DirectionX, m_DirectionY, m_DirectionZ);
    alSourcef(m_SourceId, AL_CONE_INNER_ANGLE, settings.innerAngleDegrees);
    alSourcef(m_SourceId, AL_CONE_OUTER_ANGLE, settings.outerAngleDegrees);
    alSourcef(m_SourceId, AL_CONE_OUTER_GAIN, settings.outerGain);
}

void VansAudioVoice::ApplyEffectSends()
{
    if (m_SourceId)
        VansAudioSystem::GetInstance().ApplyDefaultReverbSend(
            m_SourceId, m_Properties.m_ReverbSend, m_ReverbSendFilterId);
}

void VansAudioVoice::ApplyDirectLowpass()
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
        m_SourceId, highFrequencyGain, m_DirectLowpassFilterId);
}

void VansAudioVoice::CommitGain()
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
    if (m_LastCommittedGain >= 0.0f &&
        std::abs(finalGain - m_LastCommittedGain) < 0.0005f)
    {
        return;
    }
    alSourcef(m_SourceId, AL_GAIN, finalGain);
    m_LastCommittedGain = finalGain;
}
}
