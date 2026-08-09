#include "VansAudioNode.h"
#include "VansAudioDecoder.h"
#include "VansAudioSystem.h"
#include "../Util/VansLog.h"

// Keep OpenAL headers local to this translation unit.
#include <AL/al.h>
#include <AL/alc.h>

#include <algorithm>
#include <cstring> // memcpy
#include <cmath>

namespace VansEngine
{

// ===========================================================================
// Return the OpenAL format value for a channel count.
// ===========================================================================
int32_t VansAudioNode::GetAlFormat(int channels)
{
    // ALenum values mirror AL_FORMAT_MONO16 and AL_FORMAT_STEREO16.
    if (channels == 1) return 0x1101; // AL_FORMAT_MONO16
    return                      0x1103; // AL_FORMAT_STEREO16
}

// ===========================================================================
// Destructor
// ===========================================================================
VansAudioNode::~VansAudioNode()
{
    Close();
}

// ===========================================================================
// Open dispatches to static or streaming setup by play mode.
// ===========================================================================
bool VansAudioNode::Open(const AudioNodeProperties& props)
{
    m_Properties = props;
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
    m_Properties.m_Volume = std::clamp(m_Properties.m_Volume, 0.0f, 4.0f);
    m_Properties.m_Pitch = std::max(m_Properties.m_Pitch, 0.01f);
    m_Properties.m_ReverbSend = std::clamp(m_Properties.m_ReverbSend, 0.0f, 1.0f);
    m_Properties.m_LowpassHighFrequencyGain =
        std::clamp(m_Properties.m_LowpassHighFrequencyGain, 0.0f, 1.0f);
    m_Properties.m_BusName = NormalizeAudioBusName(m_Properties.m_BusName);
    m_DistanceGain = m_Properties.m_Spatial ? m_DistanceGain : 1.0f;
    m_OcclusionGain = 1.0f;
    m_OcclusionHighFrequencyGain = 1.0f;
    m_BusGain = 1.0f;
    m_BusLowpassHighFrequencyGain = 1.0f;
    m_VirtualizationGain = 1.0f;
    m_HardwareVoiceSuspended = false;
    m_LogicalPlaying = false;
    m_LogicalPaused = false;

    // Create an OpenAL source.
    m_SourceId = VansAudioSystem::GetInstance().AcquireSource();
    if (m_SourceId == 0)
    {
        VANS_LOG_ERROR("[VansAudioNode] AcquireSource failed: " << m_Properties.m_Name);
        return false;
    }

    // Base source properties.
    alSourcef(m_SourceId, AL_PITCH, m_Properties.m_Pitch);
    alSourcei(m_SourceId, AL_LOOPING,
              (m_Properties.m_PlayMode == AudioPlayMode::Static && m_Properties.m_Loop)
              ? AL_TRUE : AL_FALSE);
    alSource3f(m_SourceId, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    ApplySpatialProperties();
    ApplyDirectionalProperties();
    ApplyEffectSends();
    ApplyDirectLowpass();
    CommitGain();

    bool ok = m_Properties.m_PlayMode == AudioPlayMode::Static
              ? OpenStatic()
              : true;

    if (!ok)
    {
        VansAudioSystem::GetInstance().ApplyDefaultReverbSend(
            m_SourceId,
            0.0f,
            m_ReverbSendFilterId);
        VansAudioSystem::GetInstance().ApplySourceDirectLowpass(
            m_SourceId,
            1.0f,
            m_DirectLowpassFilterId);
        VansAudioSystem::GetInstance().ReleaseSource(m_SourceId);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Decode the full file once and upload it into a single OpenAL buffer.
// ---------------------------------------------------------------------------
bool VansAudioNode::OpenStatic()
{
    VansAudioDecoder decoder;
    const int targetChannels = m_Properties.m_Spatial ? 1 : 2;
    if (!decoder.Open(m_Properties.m_FilePath, targetChannels))
    {
        VANS_LOG_ERROR("[VansAudioNode] Static decode failed: " << m_Properties.m_FilePath);
        return false;
    }

    int channels   = 2;
    int sampleRate = 48000;
    std::vector<int16_t> samples = decoder.DecodeAll(channels, sampleRate);

    if (samples.empty())
    {
        VANS_LOG_ERROR("[VansAudioNode] DecodeAll returned empty data: " << m_Properties.m_FilePath);
        return false;
    }

    alGenBuffers(1, &m_StaticBufferId);
    if (alGetError() != AL_NO_ERROR)
    {
        VANS_LOG_ERROR("[VansAudioNode] alGenBuffers failed");
        return false;
    }

    alBufferData(m_StaticBufferId,
                 static_cast<ALenum>(GetAlFormat(channels)),
                 samples.data(),
                 static_cast<ALsizei>(samples.size() * sizeof(int16_t)),
                 static_cast<ALsizei>(sampleRate));

    if (alGetError() != AL_NO_ERROR)
    {
        VANS_LOG_ERROR("[VansAudioNode] alBufferData failed");
        alDeleteBuffers(1, &m_StaticBufferId);
        m_StaticBufferId = 0;
        return false;
    }

    alSourcei(m_SourceId, AL_BUFFER, static_cast<ALint>(m_StaticBufferId));
    VANS_LOG("[VansAudioNode] Static load complete: " << m_Properties.m_Name
             << "  samples=" << samples.size() / channels);
    return true;
}

// ---------------------------------------------------------------------------
// Open the decoder, prefill buffers, and start the background decode thread.
// ---------------------------------------------------------------------------
bool VansAudioNode::OpenStreaming()
{
    m_Decoder = std::make_unique<VansAudioDecoder>();
    const int targetChannels = m_Properties.m_Spatial ? 1 : 2;
    if (!m_Decoder->Open(m_Properties.m_FilePath, targetChannels))
    {
        VANS_LOG_ERROR("[VansAudioNode] Streaming open failed: " << m_Properties.m_FilePath);
        m_Decoder.reset();
        return false;
    }

    alGenBuffers(STREAM_BUFFER_COUNT, m_StreamBuffers);
    if (alGetError() != AL_NO_ERROR)
    {
        VANS_LOG_ERROR("[VansAudioNode] alGenBuffers(Streaming) failed");
        m_Decoder.reset();
        return false;
    }

    // Prefill the first streaming buffers.
    for (int i = 0; i < STREAM_BUFFER_COUNT; ++i)
    {
        AudioPCMChunk chunk;
        {
            std::lock_guard<std::mutex> decoderLock(m_DecoderMutex);
            if (!m_Decoder || !m_Decoder->IsOpen())
                continue;
            chunk = m_Decoder->DecodeNextChunk();
        }
        if (chunk.samples.empty()) break;

        alBufferData(m_StreamBuffers[i],
                     static_cast<ALenum>(GetAlFormat(chunk.channels)),
                     chunk.samples.data(),
                     static_cast<ALsizei>(chunk.samples.size() * sizeof(int16_t)),
                     static_cast<ALsizei>(chunk.sampleRate));

        alSourceQueueBuffers(m_SourceId, 1, &m_StreamBuffers[i]);

        if (chunk.endOfStream) { m_DecodeEOF.store(true); break; }
    }

    if (alGetError() != AL_NO_ERROR)
    {
        VANS_LOG_ERROR("[VansAudioNode] Streaming buffer prefill failed");
        alDeleteBuffers(STREAM_BUFFER_COUNT, m_StreamBuffers);
        std::memset(m_StreamBuffers, 0, sizeof(m_StreamBuffers));
        m_Decoder.reset();
        return false;
    }

    // Start the background decode thread.
    StartDecodeThread();

    VANS_LOG("[VansAudioNode] Streaming initialization complete: " << m_Properties.m_Name);
    return true;
}

// ===========================================================================
// Stop the background thread and release OpenAL and decoder resources.
// ===========================================================================
bool VansAudioNode::EnsureStreamingReady()
{
    if (m_Properties.m_PlayMode != AudioPlayMode::Streaming)
        return true;
    if (m_StreamingReady)
        return true;
    if (m_StreamingInitFailed)
        return false;

    m_StreamingReady = OpenStreaming();
    m_StreamingInitFailed = !m_StreamingReady;
    return m_StreamingReady;
}

void VansAudioNode::SuspendHardwareVoice()
{
    if (m_Properties.m_PlayMode != AudioPlayMode::Streaming || !m_StreamingReady)
        return;

    std::lock_guard<std::mutex> lk(m_SourceMutex);
    if (m_SourceId == 0)
        return;

    ALint state = 0;
    alGetSourcei(m_SourceId, AL_SOURCE_STATE, &state);
    m_LogicalPlaying = state == AL_PLAYING || (m_LogicalPlaying && state != AL_STOPPED);
    m_LogicalPaused = state == AL_PAUSED;

    alSourceStop(m_SourceId);

    ALint queued = 0;
    alGetSourcei(m_SourceId, AL_BUFFERS_QUEUED, &queued);
    while (queued-- > 0)
    {
        ALuint buffer = 0;
        alSourceUnqueueBuffers(m_SourceId, 1, &buffer);
    }

    VansAudioSystem::GetInstance().ApplyDefaultReverbSend(
        m_SourceId,
        0.0f,
        m_ReverbSendFilterId);
    VansAudioSystem::GetInstance().ApplySourceDirectLowpass(
        m_SourceId,
        1.0f,
        m_DirectLowpassFilterId);
    VansAudioSystem::GetInstance().ReleaseSource(m_SourceId);
    m_LastCommittedGain = -1.0f;
    m_HardwareVoiceSuspended = true;
}

bool VansAudioNode::ResumeHardwareVoice()
{
    if (m_SourceId != 0)
        return true;
    if (m_Properties.m_PlayMode != AudioPlayMode::Streaming || !m_StreamingReady)
        return false;

    std::lock_guard<std::mutex> lk(m_SourceMutex);
    if (m_SourceId != 0)
        return true;

    m_SourceId = VansAudioSystem::GetInstance().AcquireSource();
    if (m_SourceId == 0)
        return false;

    alSourcef(m_SourceId, AL_PITCH, m_Properties.m_Pitch);
    alSourcei(m_SourceId, AL_LOOPING, AL_FALSE);
    alSource3f(m_SourceId, AL_VELOCITY, m_VelocityX, m_VelocityY, m_VelocityZ);
    ApplySpatialProperties();
    ApplyDirectionalProperties();
    ApplyEffectSends();
    ApplyDirectLowpass();
    CommitGain();

    QueueStreamBuffersFromPCMQueue(STREAM_BUFFER_COUNT);
    if (m_LogicalPlaying && !m_LogicalPaused)
        alSourcePlay(m_SourceId);

    if (alGetError() != AL_NO_ERROR)
    {
        VANS_LOG_WARN("[VansAudioNode] ResumeHardwareVoice failed: " << m_Properties.m_Name);
        VansAudioSystem::GetInstance().ApplyDefaultReverbSend(m_SourceId, 0.0f, m_ReverbSendFilterId);
        VansAudioSystem::GetInstance().ApplySourceDirectLowpass(m_SourceId, 1.0f, m_DirectLowpassFilterId);
        VansAudioSystem::GetInstance().ReleaseSource(m_SourceId);
        m_LastCommittedGain = -1.0f;
        m_HardwareVoiceSuspended = true;
        return false;
    }

    m_HardwareVoiceSuspended = false;
    m_PCMQueueCv.notify_one();
    return true;
}

void VansAudioNode::Close()
{
    // Stop the background decode thread first.
    StopDecodeThread();

    if (m_SourceId != 0)
    {
        alSourceStop(m_SourceId);
        VansAudioSystem::GetInstance().ApplyDefaultReverbSend(
            m_SourceId,
            0.0f,
            m_ReverbSendFilterId);
        VansAudioSystem::GetInstance().ApplySourceDirectLowpass(
            m_SourceId,
            1.0f,
            m_DirectLowpassFilterId);
        alSourcei(m_SourceId, AL_BUFFER, 0); // Detach all buffers.

        // Drain any buffers still queued by streaming playback.
        if (m_Properties.m_PlayMode == AudioPlayMode::Streaming)
        {
            ALint queued = 0;
            alGetSourcei(m_SourceId, AL_BUFFERS_QUEUED, &queued);
            while (queued-- > 0)
            {
                ALuint buf = 0;
                alSourceUnqueueBuffers(m_SourceId, 1, &buf);
            }
        }

        VansAudioSystem::GetInstance().ReleaseSource(m_SourceId);
    }
    m_HardwareVoiceSuspended = false;

    if (m_StaticBufferId != 0)
    {
        alDeleteBuffers(1, &m_StaticBufferId);
        m_StaticBufferId = 0;
    }

    // Release streaming buffers.
    if (m_Properties.m_PlayMode == AudioPlayMode::Streaming)
    {
        for (int i = 0; i < STREAM_BUFFER_COUNT; ++i)
        {
            if (m_StreamBuffers[i] != 0)
            {
                alDeleteBuffers(1, &m_StreamBuffers[i]);
                m_StreamBuffers[i] = 0;
            }
        }
    }

    m_Decoder.reset();
    m_StreamingReady = false;
    m_StreamingInitFailed = false;
    m_LogicalPlaying = false;
    m_LogicalPaused = false;

    // Clear the PCM queue.
    {
        std::lock_guard<std::mutex> lk(m_PCMQueueMtx);
        while (!m_PCMQueue.empty()) m_PCMQueue.pop();
    }
}

// ===========================================================================
// Play / Pause / Stop / Resume
// ===========================================================================
void VansAudioNode::Play()
{
    if (!EnsureStreamingReady())
        return;

    m_LogicalPlaying = true;
    m_LogicalPaused = false;
    if (m_SourceId == 0 && m_VirtualizationGain > 0.0005f && !ResumeHardwareVoice())
        return;

    std::lock_guard<std::mutex> lk(m_SourceMutex);
    if (m_SourceId == 0) return;

    // Streaming loops are handled by the decoder path; the OpenAL source does not loop.
    alSourcePlay(m_SourceId);
    ALenum err = alGetError();
    if (err != AL_NO_ERROR)
        VANS_LOG_WARN("[VansAudioNode::Play] '" << m_Properties.m_Name << "' alSourcePlay error=" << err);
}

void VansAudioNode::Pause()
{
    m_LogicalPaused = true;
    std::lock_guard<std::mutex> lk(m_SourceMutex);
    if (m_SourceId == 0) return;
    alSourcePause(m_SourceId);
}

void VansAudioNode::Stop()
{
    m_LogicalPlaying = false;
    m_LogicalPaused = false;
    std::lock_guard<std::mutex> lk(m_SourceMutex);
    if (m_SourceId == 0)
    {
        if (m_Properties.m_PlayMode == AudioPlayMode::Streaming &&
            m_StreamingReady &&
            m_Decoder &&
            m_Decoder->IsOpen())
        {
            {
                std::lock_guard<std::mutex> lk2(m_PCMQueueMtx);
                while (!m_PCMQueue.empty()) m_PCMQueue.pop();
            }
            {
                std::lock_guard<std::mutex> decoderLock(m_DecoderMutex);
                if (m_Decoder && m_Decoder->Reset())
                    m_DecodeEOF.store(false);
            }
            m_PCMQueueCv.notify_all();
        }
        return;
    }
    alGetError();               // Clear pending OpenAL errors before this operation.
    alSourceStop(m_SourceId);
    ALenum stopErr = alGetError();
    if (stopErr != AL_NO_ERROR)
        VANS_LOG_WARN("[VansAudioNode::Stop] '" << m_Properties.m_Name << "' alSourceStop error=" << stopErr);

    if (m_Properties.m_PlayMode == AudioPlayMode::Streaming &&
        m_StreamingReady &&
        m_Decoder &&
        m_Decoder->IsOpen())
    {
        // Clear queued buffers and reset the decoder to the start.
        ALint queued = 0;
        alGetSourcei(m_SourceId, AL_BUFFERS_QUEUED, &queued);
        while (queued-- > 0)
        {
            ALuint buf = 0;
            alSourceUnqueueBuffers(m_SourceId, 1, &buf);
        }

        {
            std::lock_guard<std::mutex> lk2(m_PCMQueueMtx);
            while (!m_PCMQueue.empty()) m_PCMQueue.pop();
        }

        {
            std::lock_guard<std::mutex> decoderLock(m_DecoderMutex);
            if (m_Decoder && m_Decoder->Reset())
                m_DecodeEOF.store(false);
        }

        // Prefill the first two buffers again.
        for (int i = 0; i < 2; ++i)
        {
            AudioPCMChunk chunk;
            {
                std::lock_guard<std::mutex> decoderLock(m_DecoderMutex);
                chunk = m_Decoder ? m_Decoder->DecodeNextChunk() : AudioPCMChunk{};
            }
            if (chunk.samples.empty()) break;

            alBufferData(m_StreamBuffers[i],
                         static_cast<ALenum>(GetAlFormat(chunk.channels)),
                         chunk.samples.data(),
                         static_cast<ALsizei>(chunk.samples.size() * sizeof(int16_t)),
                         static_cast<ALsizei>(chunk.sampleRate));

            alSourceQueueBuffers(m_SourceId, 1, &m_StreamBuffers[i]);
            if (chunk.endOfStream) { m_DecodeEOF.store(true); break; }
        }
    }
}

bool VansAudioNode::SetPlaybackOffsetSeconds(float seconds)
{
	if (m_Properties.m_PlayMode != AudioPlayMode::Static || m_SourceId == 0)
		return false;
	alSourcef(m_SourceId, AL_SEC_OFFSET, std::max(0.0f, seconds));
	return alGetError() == AL_NO_ERROR;
}

float VansAudioNode::GetPlaybackOffsetSeconds() const
{
	if (m_Properties.m_PlayMode != AudioPlayMode::Static || m_SourceId == 0)
		return 0.0f;
	float offset = 0.0f;
	alGetSourcef(m_SourceId, AL_SEC_OFFSET, &offset);
	return offset;
}

void VansAudioNode::Resume()
{
    m_LogicalPaused = false;
    if (m_SourceId == 0 && m_VirtualizationGain > 0.0005f && !ResumeHardwareVoice())
        return;

    std::lock_guard<std::mutex> lk(m_SourceMutex);
    if (m_SourceId == 0) return;

    ALint state = 0;
    alGetSourcei(m_SourceId, AL_SOURCE_STATE, &state);
    if (state == AL_PAUSED)
        alSourcePlay(m_SourceId);
    m_LogicalPlaying = true;
}

// ===========================================================================
// Runtime parameter updates.
// ===========================================================================
void VansAudioNode::SetVolume(float gain)
{
    m_Properties.m_Volume = std::clamp(gain, 0.0f, 4.0f);
    CommitGain();
}

void VansAudioNode::SetPitch(float pitch)
{
    m_Properties.m_Pitch = std::max(pitch, 0.01f);
    if (m_SourceId) alSourcef(m_SourceId, AL_PITCH, m_Properties.m_Pitch);
}

void VansAudioNode::SetLoop(bool loop)
{
    m_Properties.m_Loop = loop;
    if (m_SourceId && m_Properties.m_PlayMode == AudioPlayMode::Static)
        alSourcei(m_SourceId, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
    // Streaming loops are handled by Tick() and decoder Reset().
}

void VansAudioNode::SetPosition(float x, float y, float z)
{
    m_PositionX = x;
    m_PositionY = y;
    m_PositionZ = z;
    if (m_SourceId) alSource3f(m_SourceId, AL_POSITION, x, y, z);
}

void VansAudioNode::UpdateDistanceGain(float listenerX, float listenerY, float listenerZ)
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
    attenuation.mode = m_AttenuationModeRuntime;
    attenuation.referenceDistance = m_Properties.m_RefDist;
    attenuation.maxDistance = m_Properties.m_MaxDist;
    attenuation.rolloff = m_Properties.m_RollOff;
    SetSpatialGain(ComputeDistanceGain(distance, attenuation));
}

void VansAudioNode::SetSpatialGain(float distanceGain)
{
    m_DistanceGain = std::clamp(distanceGain, 0.0f, 1.0f);
    CommitGain();
}

void VansAudioNode::SetOcclusion(float gain, float highFrequencyGain)
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

void VansAudioNode::SetVelocity(float x, float y, float z)
{
    m_VelocityX = x;
    m_VelocityY = y;
    m_VelocityZ = z;
    if (m_SourceId)
        alSource3f(m_SourceId, AL_VELOCITY, m_VelocityX, m_VelocityY, m_VelocityZ);
}

void VansAudioNode::SetDirection(float x, float y, float z)
{
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length <= 0.0001f)
        return;
    m_DirectionX = x / length;
    m_DirectionY = y / length;
    m_DirectionZ = z / length;
    ApplyDirectionalProperties();
}

void VansAudioNode::SetCone(AudioConeSettings settings)
{
    m_ConeSettings = NormalizeAudioConeSettings(settings);
    ApplyDirectionalProperties();
}

int VansAudioNode::GetALSourceRelative() const
{
    if (!m_SourceId) return -1;
    ALint val = 0;
    alGetSourcei(m_SourceId, AL_SOURCE_RELATIVE, &val);
    return static_cast<int>(val);
}

void VansAudioNode::SetSpatial(bool enabled)
{
    m_Properties.m_Spatial = enabled;
    if (!enabled)
        m_DistanceGain = 1.0f;
    ApplySpatialProperties();
    ApplyDirectionalProperties();
    CommitGain();
}

void VansAudioNode::SetStereoPan(float pan)
{
	m_Properties.m_StereoPan = std::clamp(pan, -1.0f, 1.0f);
	if (!m_Properties.m_Spatial) ApplySpatialProperties();
}

void VansAudioNode::ApplySpatialProperties()
{
    if (!m_SourceId) return;

    if (m_Properties.m_Spatial)
    {
        alSourcei(m_SourceId, AL_SOURCE_RELATIVE, AL_FALSE);
        alSourcef(m_SourceId, AL_REFERENCE_DISTANCE, m_Properties.m_RefDist);
        alSourcef(m_SourceId, AL_MAX_DISTANCE,        m_Properties.m_MaxDist);
        alSourcef(m_SourceId, AL_ROLLOFF_FACTOR,      m_Properties.m_RollOff);
        alSource3f(m_SourceId, AL_POSITION, m_PositionX, m_PositionY, m_PositionZ);
    }
    else
    {
        alSourcei(m_SourceId, AL_SOURCE_RELATIVE, AL_TRUE);
		alSource3f(m_SourceId, AL_POSITION, m_Properties.m_StereoPan, 0.0f, 0.0f);
        alSourcef(m_SourceId, AL_ROLLOFF_FACTOR, 0.0f);
    }
}

void VansAudioNode::ApplyDirectionalProperties()
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

void VansAudioNode::SetRefDistance(float d)
{
    m_Properties.m_RefDist = std::max(d, 0.01f);
    if (m_Properties.m_MaxDist <= m_Properties.m_RefDist)
        m_Properties.m_MaxDist = m_Properties.m_RefDist + 0.01f;
    if (m_SourceId) alSourcef(m_SourceId, AL_REFERENCE_DISTANCE, m_Properties.m_RefDist);
}

void VansAudioNode::SetMaxDistance(float d)
{
    m_Properties.m_MaxDist = std::max(d, m_Properties.m_RefDist + 0.01f);
    if (m_SourceId) alSourcef(m_SourceId, AL_MAX_DISTANCE, m_Properties.m_MaxDist);
}

void VansAudioNode::SetRolloff(float rolloff)
{
    m_Properties.m_RollOff = std::max(rolloff, 0.0f);
    if (m_SourceId) alSourcef(m_SourceId, AL_ROLLOFF_FACTOR, m_Properties.m_Spatial ? m_Properties.m_RollOff : 0.0f);
}

void VansAudioNode::SetAttenuationMode(AudioAttenuationMode mode)
{
    m_AttenuationModeRuntime = mode;
    m_Properties.m_AttenuationMode = AudioAttenuationModeToString(mode);
}

void VansAudioNode::SetReverbSend(float send)
{
    m_Properties.m_ReverbSend = std::clamp(send, 0.0f, 1.0f);
    ApplyEffectSends();
}

void VansAudioNode::SetLowpassHighFrequencyGain(float highFrequencyGain)
{
    const float clampedGain = std::clamp(highFrequencyGain, 0.0f, 1.0f);
    if (std::abs(clampedGain - m_Properties.m_LowpassHighFrequencyGain) < 0.0005f)
        return;
    m_Properties.m_LowpassHighFrequencyGain = clampedGain;
    ApplyDirectLowpass();
}

void VansAudioNode::SetBusName(const std::string& busName)
{
    m_Properties.m_BusName = NormalizeAudioBusName(busName);
}

void VansAudioNode::SetBusGain(float gain)
{
    const float clampedGain = std::clamp(gain, 0.0f, 4.0f);
    if (std::abs(clampedGain - m_BusGain) < 0.0005f)
        return;
    m_BusGain = clampedGain;
    CommitGain();
}

void VansAudioNode::SetBusLowpassHighFrequencyGain(float highFrequencyGain)
{
    const float clampedGain = std::clamp(highFrequencyGain, 0.0f, 1.0f);
    if (std::abs(clampedGain - m_BusLowpassHighFrequencyGain) < 0.0005f)
        return;
    m_BusLowpassHighFrequencyGain = clampedGain;
    ApplyDirectLowpass();
}

void VansAudioNode::SetVirtualizationGain(float gain)
{
    const float clampedGain = std::clamp(gain, 0.0f, 1.0f);
    if (std::abs(clampedGain - m_VirtualizationGain) < 0.0005f)
        return;
    m_VirtualizationGain = clampedGain;
    if (m_Properties.m_PlayMode == AudioPlayMode::Streaming && m_StreamingReady)
    {
        if (m_VirtualizationGain <= 0.0005f)
        {
            SuspendHardwareVoice();
            return;
        }
        if (m_SourceId == 0)
            ResumeHardwareVoice();
    }
    CommitGain();
}

void VansAudioNode::ApplyEffectSends()
{
    if (!m_SourceId)
        return;
    VansAudioSystem::GetInstance().ApplyDefaultReverbSend(
        m_SourceId,
        m_Properties.m_ReverbSend,
        m_ReverbSendFilterId);
}

void VansAudioNode::ApplyDirectLowpass()
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

void VansAudioNode::CommitGain()
{
    if (!m_SourceId) return;

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

// ===========================================================================
// State queries.
// ===========================================================================
bool VansAudioNode::IsPlaying() const
{
    if (!m_SourceId) return m_LogicalPlaying && !m_LogicalPaused;
    ALint state = 0;
    alGetSourcei(m_SourceId, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

bool VansAudioNode::IsPaused() const
{
    if (!m_SourceId) return m_LogicalPaused;
    ALint state = 0;
    alGetSourcei(m_SourceId, AL_SOURCE_STATE, &state);
    return state == AL_PAUSED;
}

// ===========================================================================
// Main-thread update that refills processed streaming buffers.
// ===========================================================================
bool VansAudioNode::CanCreateStaticInstance() const
{
    return m_Properties.m_PlayMode == AudioPlayMode::Static &&
        m_StaticBufferId != 0 &&
        m_SourceId != 0;
}

void VansAudioNode::Tick()
{
    if (!m_SourceId) return;
    if (m_Properties.m_PlayMode != AudioPlayMode::Streaming) return;
    if (!m_StreamingReady) return;

    std::lock_guard<std::mutex> lk(m_SourceMutex);

    // Reclaim processed buffers and refill them with new PCM data.
    RefillStreamBuffers();

    ALint queuedBeforeStateCheck = 0;
    alGetSourcei(m_SourceId, AL_BUFFERS_QUEUED, &queuedBeforeStateCheck);
    if (queuedBeforeStateCheck == 0)
        QueueStreamBuffersFromPCMQueue(STREAM_BUFFER_COUNT);

    // Restart a stopped source when buffered data is still available.
    ALint state = 0;
    alGetSourcei(m_SourceId, AL_SOURCE_STATE, &state);
    if (state == AL_STOPPED)
    {
        ALint queued = 0;
        alGetSourcei(m_SourceId, AL_BUFFERS_QUEUED, &queued);
        if (queued > 0)
        {
            // Source stalled with queued buffers; trigger playback again.
            alSourcePlay(m_SourceId);
        }
        else if (m_DecodeEOF.load())
        {
            // All buffers were processed and the decoder reached EOF.
            if (m_Properties.m_Loop)
            {
                // Loop restart: reset the decoder, prefill, then play again.
                {
                    std::lock_guard<std::mutex> lk2(m_PCMQueueMtx);
                    while (!m_PCMQueue.empty()) m_PCMQueue.pop();
                }

                bool resetSucceeded = false;
                {
                    std::lock_guard<std::mutex> decoderLock(m_DecoderMutex);
                    resetSucceeded = m_Decoder && m_Decoder->Reset();
                    if (resetSucceeded)
                    {
                        m_DecodeEOF.store(false);

                        for (int i = 0; i < 2; ++i)
                        {
                            AudioPCMChunk chunk = m_Decoder->DecodeNextChunk();
                            if (chunk.samples.empty()) break;

                            alBufferData(m_StreamBuffers[i],
                                         static_cast<ALenum>(GetAlFormat(chunk.channels)),
                                         chunk.samples.data(),
                                         static_cast<ALsizei>(chunk.samples.size() * sizeof(int16_t)),
                                         static_cast<ALsizei>(chunk.sampleRate));

                            alSourceQueueBuffers(m_SourceId, 1, &m_StreamBuffers[i]);
                            if (chunk.endOfStream) { m_DecodeEOF.store(true); break; }
                        }
                    }
                }
                if (resetSucceeded)
                {
                    m_PCMQueueCv.notify_all();
                    alSourcePlay(m_SourceId);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Refill processed buffers from the PCM queue and requeue them.
// ---------------------------------------------------------------------------
int VansAudioNode::QueueStreamBuffersFromPCMQueue(int maxBuffers)
{
    if (!m_SourceId || maxBuffers <= 0)
        return 0;

    ALint queued = 0;
    alGetSourcei(m_SourceId, AL_BUFFERS_QUEUED, &queued);
    int queuedNow = 0;
    for (int bufferIndex = 0;
        bufferIndex < STREAM_BUFFER_COUNT && queued + queuedNow < STREAM_BUFFER_COUNT && queuedNow < maxBuffers;
        ++bufferIndex)
    {
        std::vector<int16_t> pcm;
        {
            std::lock_guard<std::mutex> lk(m_PCMQueueMtx);
            if (m_PCMQueue.empty())
                break;
            pcm = std::move(m_PCMQueue.front());
            m_PCMQueue.pop();
        }

        if (pcm.empty())
            continue;

        int channels = m_Decoder ? m_Decoder->GetChannels() : 2;
        int sampleRate = m_Decoder ? m_Decoder->GetSampleRate() : 48000;

        const ALuint buffer = m_StreamBuffers[bufferIndex];
        if (buffer == 0)
            continue;

        alBufferData(buffer,
                     static_cast<ALenum>(GetAlFormat(channels)),
                     pcm.data(),
                     static_cast<ALsizei>(pcm.size() * sizeof(int16_t)),
                     static_cast<ALsizei>(sampleRate));

        alSourceQueueBuffers(m_SourceId, 1, &buffer);
        ++queuedNow;
    }

    if (queuedNow > 0)
        m_PCMQueueCv.notify_one();
    return queuedNow;
}

void VansAudioNode::RefillStreamBuffers()
{
    ALint processed = 0;
    alGetSourcei(m_SourceId, AL_BUFFERS_PROCESSED, &processed);

    while (processed-- > 0)
    {
        ALuint bufId = 0;
        alSourceUnqueueBuffers(m_SourceId, 1, &bufId);

        // Pull data from the PCM queue.
        std::vector<int16_t> pcm;
        {
            std::lock_guard<std::mutex> lk(m_PCMQueueMtx);
            if (!m_PCMQueue.empty())
            {
                pcm = std::move(m_PCMQueue.front());
                m_PCMQueue.pop();
            }
        }
        m_PCMQueueCv.notify_one(); // Let the decode thread produce more data.

        if (pcm.empty()) continue;

        // Decoder output format is fixed after Open().
        int channels   = m_Decoder ? m_Decoder->GetChannels()   : 2;
        int sampleRate = m_Decoder ? m_Decoder->GetSampleRate() : 48000;

        alBufferData(bufId,
                     static_cast<ALenum>(GetAlFormat(channels)),
                     pcm.data(),
                     static_cast<ALsizei>(pcm.size() * sizeof(int16_t)),
                     static_cast<ALsizei>(sampleRate));

        alSourceQueueBuffers(m_SourceId, 1, &bufId);
    }
}

// ===========================================================================
// Streaming background decode thread.
// ===========================================================================
void VansAudioNode::StartDecodeThread()
{
    m_StopDecode.store(false);
    m_DecodeEOF .store(false);
    m_DecodeThread = std::thread(&VansAudioNode::DecodeThreadFunc, this);
}

void VansAudioNode::StopDecodeThread()
{
    m_StopDecode.store(true);
    m_PCMQueueCv.notify_all();

    if (m_DecodeThread.joinable())
        m_DecodeThread.join();
}

void VansAudioNode::DecodeThreadFunc()
{
    // Bound the PCM queue to avoid unbounded memory growth.
    static constexpr int MAX_QUEUE_SIZE = STREAM_BUFFER_COUNT;

    while (!m_StopDecode.load())
    {
        if (!m_Decoder || !m_Decoder->IsOpen() || m_DecodeEOF.load())
        {
            // Wait until Stop() or Reset() clears the EOF state.
            std::unique_lock<std::mutex> lk(m_PCMQueueMtx);
            m_PCMQueueCv.wait(lk, [this] {
                return m_StopDecode.load() || (!m_DecodeEOF.load() && m_Decoder && m_Decoder->IsOpen());
            });
            continue;
        }

        // Decode one PCM block.
        AudioPCMChunk chunk = m_Decoder->DecodeNextChunk();

        {
            std::unique_lock<std::mutex> lk(m_PCMQueueMtx);

            // Wait for the main thread to consume data when the queue is full.
            m_PCMQueueCv.wait(lk, [this] {
                return m_StopDecode.load() || (int)m_PCMQueue.size() < MAX_QUEUE_SIZE;
            });

            if (m_StopDecode.load()) break;

            if (!chunk.samples.empty())
                m_PCMQueue.push(std::move(chunk.samples));

            if (chunk.endOfStream)
            {
                m_DecodeEOF.store(true);
                // Looping is restarted by Tick(), which clears EOF and resets the decoder.
                m_PCMQueueCv.notify_all();
                continue;
            }
        }
    }
}

} // namespace VansEngine
