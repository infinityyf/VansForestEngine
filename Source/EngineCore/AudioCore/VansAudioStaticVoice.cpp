#include "VansAudioStaticVoice.h"
#include "VansAudioSystem.h"

#include "../Util/VansLog.h"

#include <AL/al.h>

#include <algorithm>

namespace VansEngine
{
VansAudioStaticVoice::~VansAudioStaticVoice()
{
    Close();
}

bool VansAudioStaticVoice::Open(
    std::uint32_t sharedBufferId,
    const VansAudioProperties& properties)
{
    Close();
    if (sharedBufferId == 0)
        return false;

    VansAudioProperties voiceProperties = properties;
    voiceProperties.m_PlayMode = VansAudioPlayMode::Static;
    voiceProperties.m_AutoPlay = false;
    InitializeVoice(voiceProperties);
    m_SharedBufferId = sharedBufferId;
    m_PlaybackOffsetSeconds = 0.0f;
    m_LogicalPlaying = false;
    m_LogicalPaused = false;

    if (!AcquireHardwareVoice())
    {
        VANS_LOG_ERROR("[VansAudioStaticVoice] AcquireSource failed: " << m_Properties.m_Name);
        m_SharedBufferId = 0;
        return false;
    }
    return true;
}

bool VansAudioStaticVoice::AcquireHardwareVoice()
{
    if (m_SourceId != 0)
        return true;
    if (m_SharedBufferId == 0)
        return false;

    const VansAudioSourceAcquireStatus acquireStatus = AcquireSource();
    if (acquireStatus != VansAudioSourceAcquireStatus::Acquired)
    {
        VANS_LOG_WARN("[VansAudioStaticVoice] AcquireSource failed: " << m_Properties.m_Name
            << " status=" << VansAudioSourceAcquireStatusName(acquireStatus));
        return false;
    }

    alSourcei(m_SourceId, AL_BUFFER, static_cast<ALint>(m_SharedBufferId));
    if (m_PlaybackOffsetSeconds > 0.0f)
        alSourcef(m_SourceId, AL_SEC_OFFSET, m_PlaybackOffsetSeconds);
    if (m_LogicalPlaying && !m_LogicalPaused)
        alSourcePlay(m_SourceId);

    if (alGetError() != AL_NO_ERROR)
    {
        VANS_LOG_ERROR("[VansAudioStaticVoice] Source setup failed: " << m_Properties.m_Name);
        ReleaseSource();
        return false;
    }
    return true;
}

void VansAudioStaticVoice::ReleaseHardwareVoice()
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
    ReleaseSource();
}

void VansAudioStaticVoice::Close()
{
    ReleaseHardwareVoice();
    m_SharedBufferId = 0;
    m_PlaybackOffsetSeconds = 0.0f;
    m_LogicalPlaying = false;
    m_LogicalPaused = false;
}

void VansAudioStaticVoice::Play()
{
    m_LogicalPlaying = true;
    m_LogicalPaused = false;
    if (m_SourceId == 0 && m_VirtualizationGain > 0.0005f)
        AcquireHardwareVoice();
    if (m_SourceId)
        alSourcePlay(m_SourceId);
}

void VansAudioStaticVoice::Pause()
{
    m_LogicalPaused = true;
    if (m_SourceId)
        alSourcePause(m_SourceId);
}

void VansAudioStaticVoice::Stop()
{
    m_LogicalPlaying = false;
    m_LogicalPaused = false;
    m_PlaybackOffsetSeconds = 0.0f;
    if (m_SourceId)
        alSourceStop(m_SourceId);
}

void VansAudioStaticVoice::Resume()
{
    m_LogicalPaused = false;
    if (m_SourceId == 0 && m_VirtualizationGain > 0.0005f)
        AcquireHardwareVoice();
    if (!m_SourceId)
        return;
    ALint state = 0;
    alGetSourcei(m_SourceId, AL_SOURCE_STATE, &state);
    if (state == AL_PAUSED)
        alSourcePlay(m_SourceId);
    m_LogicalPlaying = true;
}

bool VansAudioStaticVoice::Seek(double seconds)
{
    m_PlaybackOffsetSeconds = static_cast<float>(std::max(0.0, seconds));
    if (!m_SourceId && !AcquireHardwareVoice())
        return false;
    alSourcef(m_SourceId, AL_SEC_OFFSET, m_PlaybackOffsetSeconds);
    return alGetError() == AL_NO_ERROR;
}

double VansAudioStaticVoice::GetPlaybackOffsetSeconds() const
{
    if (!m_SourceId)
        return m_PlaybackOffsetSeconds;
    float offset = m_PlaybackOffsetSeconds;
    alGetSourcef(m_SourceId, AL_SEC_OFFSET, &offset);
    return offset;
}

bool VansAudioStaticVoice::IsPlaying() const
{
    if (!m_SourceId)
        return m_LogicalPlaying && !m_LogicalPaused;
    ALint state = 0;
    alGetSourcei(m_SourceId, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

bool VansAudioStaticVoice::IsPaused() const
{
    if (!m_SourceId)
        return m_LogicalPaused;
    ALint state = 0;
    alGetSourcei(m_SourceId, AL_SOURCE_STATE, &state);
    return state == AL_PAUSED;
}

void VansAudioStaticVoice::OnVirtualizationChanged()
{
    if (m_VirtualizationGain <= 0.0005f)
    {
        ReleaseHardwareVoice();
        return;
    }
    if (m_SourceId == 0 && m_SharedBufferId != 0)
        AcquireHardwareVoice();
}
}
