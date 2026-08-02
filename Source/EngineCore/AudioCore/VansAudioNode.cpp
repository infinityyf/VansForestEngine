#include "VansAudioNode.h"
#include "VansAudioDecoder.h"
#include "VansAudioSystem.h"
#include "../Util/VansLog.h"

// OpenAL 头文件仅在此 .cpp 中引入
#include <AL/al.h>
#include <AL/alc.h>

#include <algorithm>
#include <cstring> // memcpy
#include <cmath>

namespace VansEngine
{

// ===========================================================================
// 辅助：从声道数返回 OpenAL 格式值（al.h 中 #define 值的枚举镜像）
// ===========================================================================
int32_t VansAudioNode::GetAlFormat(int channels)
{
    // ALenum 在 al.h 中定义：AL_FORMAT_MONO16 = 0x1101, AL_FORMAT_STEREO16 = 0x1103
    if (channels == 1) return 0x1101; // AL_FORMAT_MONO16
    return                      0x1103; // AL_FORMAT_STEREO16
}

// ===========================================================================
// 析构函数
// ===========================================================================
VansAudioNode::~VansAudioNode()
{
    Close();
}

// ===========================================================================
// Open — 根据 PlayMode 分派到 Static 或 Streaming 实现
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

    // 创建 OpenAL Source
    m_SourceId = VansAudioSystem::GetInstance().AcquireSource();
    if (m_SourceId == 0)
    {
        VANS_LOG_ERROR("[VansAudioNode] AcquireSource failed: " << m_Properties.m_Name);
        return false;
    }

    // 基础 Source 属性
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
// OpenStatic — 一次性解码全部 PCM，上传到单个 OpenAL Buffer
// ---------------------------------------------------------------------------
bool VansAudioNode::OpenStatic()
{
    VansAudioDecoder decoder;
    const int targetChannels = m_Properties.m_Spatial ? 1 : 2;
    if (!decoder.Open(m_Properties.m_FilePath, targetChannels))
    {
        VANS_LOG_ERROR("[VansAudioNode] Static 解码失败: " << m_Properties.m_FilePath);
        return false;
    }

    int channels   = 2;
    int sampleRate = 48000;
    std::vector<int16_t> samples = decoder.DecodeAll(channels, sampleRate);

    if (samples.empty())
    {
        VANS_LOG_ERROR("[VansAudioNode] DecodeAll 返回空数据: " << m_Properties.m_FilePath);
        return false;
    }

    alGenBuffers(1, &m_StaticBufferId);
    if (alGetError() != AL_NO_ERROR)
    {
        VANS_LOG_ERROR("[VansAudioNode] alGenBuffers 失败");
        return false;
    }

    alBufferData(m_StaticBufferId,
                 static_cast<ALenum>(GetAlFormat(channels)),
                 samples.data(),
                 static_cast<ALsizei>(samples.size() * sizeof(int16_t)),
                 static_cast<ALsizei>(sampleRate));

    if (alGetError() != AL_NO_ERROR)
    {
        VANS_LOG_ERROR("[VansAudioNode] alBufferData 失败");
        alDeleteBuffers(1, &m_StaticBufferId);
        m_StaticBufferId = 0;
        return false;
    }

    alSourcei(m_SourceId, AL_BUFFER, static_cast<ALint>(m_StaticBufferId));
    VANS_LOG("[VansAudioNode] Static 加载完成: " << m_Properties.m_Name
             << "  samples=" << samples.size() / channels);
    return true;
}

// ---------------------------------------------------------------------------
// OpenStreaming — 打开解码器，预填充部分 Buffer，启动后台解码线程
// ---------------------------------------------------------------------------
bool VansAudioNode::OpenStreaming()
{
    m_Decoder = std::make_unique<VansAudioDecoder>();
    const int targetChannels = m_Properties.m_Spatial ? 1 : 2;
    if (!m_Decoder->Open(m_Properties.m_FilePath, targetChannels))
    {
        VANS_LOG_ERROR("[VansAudioNode] Streaming 打开失败: " << m_Properties.m_FilePath);
        m_Decoder.reset();
        return false;
    }

    alGenBuffers(STREAM_BUFFER_COUNT, m_StreamBuffers);
    if (alGetError() != AL_NO_ERROR)
    {
        VANS_LOG_ERROR("[VansAudioNode] alGenBuffers(Streaming) 失败");
        m_Decoder.reset();
        return false;
    }

    // 预填充前 STREAM_BUFFER_COUNT 个 Buffer
    for (int i = 0; i < STREAM_BUFFER_COUNT; ++i)
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

    if (alGetError() != AL_NO_ERROR)
    {
        VANS_LOG_ERROR("[VansAudioNode] 预填充 Buffer 失败");
        alDeleteBuffers(STREAM_BUFFER_COUNT, m_StreamBuffers);
        std::memset(m_StreamBuffers, 0, sizeof(m_StreamBuffers));
        m_Decoder.reset();
        return false;
    }

    // 启动后台解码线程
    StartDecodeThread();

    VANS_LOG("[VansAudioNode] Streaming 初始化完成: " << m_Properties.m_Name);
    return true;
}

// ===========================================================================
// Close — 终止后台线程，释放 OpenAL 资源，释放解码器
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
    // 先停止后台解码线程
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
        alSourcei(m_SourceId, AL_BUFFER, 0); // 解绑所有 Buffer

        // 取出 Streaming 模式下仍排队的 Buffer
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

    // 清理 Streaming Buffers
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

    // 清空 PCM 队列
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

    // Streaming 模式下 Loop 需要在解码线程侧处理（Source 不使用 AL_LOOPING=TRUE）
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
            m_DecodeEOF.store(false);
            m_Decoder->Reset();
            m_PCMQueueCv.notify_all();
        }
        return;
    }
    alGetError();               // 清空挂起的 OpenAL 错误，避免污染后续调用
    alSourceStop(m_SourceId);
    ALenum stopErr = alGetError();
    if (stopErr != AL_NO_ERROR)
        VANS_LOG_WARN("[VansAudioNode::Stop] '" << m_Properties.m_Name << "' alSourceStop error=" << stopErr);

    if (m_Properties.m_PlayMode == AudioPlayMode::Streaming &&
        m_StreamingReady &&
        m_Decoder &&
        m_Decoder->IsOpen())
    {
        // 清空排队 Buffer，重置解码器到文件开头
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

        m_DecodeEOF.store(false);
        m_Decoder->Reset();

        // 重新预填充前两个 Buffer
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
// 实时参数设置
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
    // Streaming 模式的 Loop 由 Tick() + 解码线程的 Reset() 处理
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
        alSource3f(m_SourceId, AL_POSITION, 0.0f, 0.0f, 0.0f);
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
// 状态查询
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
// Tick — 主线程每帧调用，为 Streaming Source 补充已处理的 Buffer
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

    // 检查并回收已处理完的 Buffer，用新的 PCM 数据重新填充
    RefillStreamBuffers();

    ALint queuedBeforeStateCheck = 0;
    alGetSourcei(m_SourceId, AL_BUFFERS_QUEUED, &queuedBeforeStateCheck);
    if (queuedBeforeStateCheck == 0)
        QueueStreamBuffersFromPCMQueue(STREAM_BUFFER_COUNT);

    // 如果 Source 意外 STOPPED（Buffer 耗尽时会触发），且仍有数据，则重新 Play
    ALint state = 0;
    alGetSourcei(m_SourceId, AL_SOURCE_STATE, &state);
    if (state == AL_STOPPED)
    {
        ALint queued = 0;
        alGetSourcei(m_SourceId, AL_BUFFERS_QUEUED, &queued);
        if (queued > 0)
        {
            // Source stall：仍有 Buffer 但 Source 停了，重新触发
            alSourcePlay(m_SourceId);
        }
        else if (m_DecodeEOF.load())
        {
            // 所有 Buffer 已处理，且解码器已到文件末尾
            if (m_Properties.m_Loop)
            {
                // 循环：重置解码器，重新预填充并播放
                m_DecodeEOF.store(false);
                m_Decoder->Reset();
                {
                    std::lock_guard<std::mutex> lk2(m_PCMQueueMtx);
                    while (!m_PCMQueue.empty()) m_PCMQueue.pop();
                }
                m_PCMQueueCv.notify_all();

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
                alSourcePlay(m_SourceId);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// RefillStreamBuffers — 取出已处理的 Buffer，从 PCM 队列填充并重新入队
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

        // 从 PCM 队列取数据
        std::vector<int16_t> pcm;
        {
            std::lock_guard<std::mutex> lk(m_PCMQueueMtx);
            if (!m_PCMQueue.empty())
            {
                pcm = std::move(m_PCMQueue.front());
                m_PCMQueue.pop();
            }
        }
        m_PCMQueueCv.notify_one(); // 通知解码线程可以继续生产

        if (pcm.empty()) continue;

        // 假设 Decoder 设置的声道和采样率在 Open 时已固定
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
// Streaming 后台解码线程
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
    // PCM 队列最大容纳 4 块（≈ STREAM_BUFFER_COUNT），防止内存无限增长
    static constexpr int MAX_QUEUE_SIZE = STREAM_BUFFER_COUNT;

    while (!m_StopDecode.load())
    {
        if (!m_Decoder || !m_Decoder->IsOpen() || m_DecodeEOF.load())
        {
            // 等待 Stop() / Reset() 重置 EOF 标志
            std::unique_lock<std::mutex> lk(m_PCMQueueMtx);
            m_PCMQueueCv.wait(lk, [this] {
                return m_StopDecode.load() || (!m_DecodeEOF.load() && m_Decoder && m_Decoder->IsOpen());
            });
            continue;
        }

        // 解码一块 PCM
        AudioPCMChunk chunk = m_Decoder->DecodeNextChunk();

        {
            std::unique_lock<std::mutex> lk(m_PCMQueueMtx);

            // 如果队列已满，等待主线程消费
            m_PCMQueueCv.wait(lk, [this] {
                return m_StopDecode.load() || (int)m_PCMQueue.size() < MAX_QUEUE_SIZE;
            });

            if (m_StopDecode.load()) break;

            if (!chunk.samples.empty())
                m_PCMQueue.push(std::move(chunk.samples));

            if (chunk.endOfStream)
            {
                m_DecodeEOF.store(true);
                // 循环时由主线程 Tick() 重置 EOF + 重新 Reset() 解码器
                m_PCMQueueCv.notify_all();
                continue;
            }
        }
    }
}

} // namespace VansEngine
