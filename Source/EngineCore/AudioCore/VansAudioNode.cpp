#include "VansAudioNode.h"
#include "VansAudioDecoder.h"
#include "../Util/VansLog.h"

// OpenAL 头文件仅在此 .cpp 中引入
#include <AL/al.h>
#include <AL/alc.h>

#include <cstring> // memcpy

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

    // 创建 OpenAL Source
    alGenSources(1, &m_SourceId);
    if (alGetError() != AL_NO_ERROR || m_SourceId == 0)
    {
        VANS_LOG_ERROR("[VansAudioNode] alGenSources 失败: " << m_Properties.m_Name);
        return false;
    }

    // 基础 Source 属性
    alSourcef(m_SourceId, AL_GAIN,  m_Properties.m_Volume);
    alSourcef(m_SourceId, AL_PITCH, m_Properties.m_Pitch);
    alSourcei(m_SourceId, AL_LOOPING,
              (m_Properties.m_PlayMode == AudioPlayMode::Static && m_Properties.m_Loop)
              ? AL_TRUE : AL_FALSE);

    // 空间化属性
    if (m_Properties.m_Spatial)
    {
        alSourcei(m_SourceId, AL_SOURCE_RELATIVE, AL_FALSE);
        alSourcef(m_SourceId, AL_REFERENCE_DISTANCE, m_Properties.m_RefDist);
        alSourcef(m_SourceId, AL_MAX_DISTANCE,        m_Properties.m_MaxDist);
        alSourcef(m_SourceId, AL_ROLLOFF_FACTOR,      m_Properties.m_RollOff);
    }
    else
    {
        // 非空间音效：绑定到监听者位置，不随距离衰减
        alSourcei(m_SourceId, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(m_SourceId, AL_POSITION, 0.0f, 0.0f, 0.0f);
        alSourcef(m_SourceId, AL_ROLLOFF_FACTOR, 0.0f);
    }

    bool ok = (m_Properties.m_PlayMode == AudioPlayMode::Static)
              ? OpenStatic()
              : OpenStreaming();

    if (!ok)
    {
        alDeleteSources(1, &m_SourceId);
        m_SourceId = 0;
        return false;
    }

    // AutoPlay
    if (m_Properties.m_AutoPlay)
        Play();

    return true;
}

// ---------------------------------------------------------------------------
// OpenStatic — 一次性解码全部 PCM，上传到单个 OpenAL Buffer
// ---------------------------------------------------------------------------
bool VansAudioNode::OpenStatic()
{
    VansAudioDecoder decoder;
    if (!decoder.Open(m_Properties.m_FilePath))
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
    if (!m_Decoder->Open(m_Properties.m_FilePath))
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
void VansAudioNode::Close()
{
    // 先停止后台解码线程
    StopDecodeThread();

    if (m_SourceId != 0)
    {
        alSourceStop(m_SourceId);
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

        alDeleteSources(1, &m_SourceId);
        m_SourceId = 0;
    }

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
    std::lock_guard<std::mutex> lk(m_SourceMutex);
    if (m_SourceId == 0) return;

    // Streaming 模式下 Loop 需要在解码线程侧处理（Source 不使用 AL_LOOPING=TRUE）
    alSourcePlay(m_SourceId);
}

void VansAudioNode::Pause()
{
    std::lock_guard<std::mutex> lk(m_SourceMutex);
    if (m_SourceId == 0) return;
    alSourcePause(m_SourceId);
}

void VansAudioNode::Stop()
{
    std::lock_guard<std::mutex> lk(m_SourceMutex);
    if (m_SourceId == 0) return;
    alSourceStop(m_SourceId);

    if (m_Properties.m_PlayMode == AudioPlayMode::Streaming && m_Decoder && m_Decoder->IsOpen())
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
    std::lock_guard<std::mutex> lk(m_SourceMutex);
    if (m_SourceId == 0) return;

    ALint state = 0;
    alGetSourcei(m_SourceId, AL_SOURCE_STATE, &state);
    if (state == AL_PAUSED)
        alSourcePlay(m_SourceId);
}

// ===========================================================================
// 实时参数设置
// ===========================================================================
void VansAudioNode::SetVolume(float gain)
{
    m_Properties.m_Volume = gain;
    if (m_SourceId) alSourcef(m_SourceId, AL_GAIN, gain);
}

void VansAudioNode::SetPitch(float pitch)
{
    m_Properties.m_Pitch = pitch;
    if (m_SourceId) alSourcef(m_SourceId, AL_PITCH, pitch);
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
    if (m_SourceId) alSource3f(m_SourceId, AL_POSITION, x, y, z);
}

void VansAudioNode::SetSpatial(bool enabled)
{
    m_Properties.m_Spatial = enabled;
    if (!m_SourceId) return;

    if (enabled)
    {
        alSourcei(m_SourceId, AL_SOURCE_RELATIVE, AL_FALSE);
        alSourcef(m_SourceId, AL_REFERENCE_DISTANCE, m_Properties.m_RefDist);
        alSourcef(m_SourceId, AL_MAX_DISTANCE,        m_Properties.m_MaxDist);
        alSourcef(m_SourceId, AL_ROLLOFF_FACTOR,      m_Properties.m_RollOff);
    }
    else
    {
        alSourcei(m_SourceId, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(m_SourceId, AL_POSITION, 0.0f, 0.0f, 0.0f);
        alSourcef(m_SourceId, AL_ROLLOFF_FACTOR, 0.0f);
    }
}

void VansAudioNode::SetRefDistance(float d)
{
    m_Properties.m_RefDist = d;
    if (m_SourceId) alSourcef(m_SourceId, AL_REFERENCE_DISTANCE, d);
}

void VansAudioNode::SetMaxDistance(float d)
{
    m_Properties.m_MaxDist = d;
    if (m_SourceId) alSourcef(m_SourceId, AL_MAX_DISTANCE, d);
}

// ===========================================================================
// 状态查询
// ===========================================================================
bool VansAudioNode::IsPlaying() const
{
    if (!m_SourceId) return false;
    ALint state = 0;
    alGetSourcei(m_SourceId, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

bool VansAudioNode::IsPaused() const
{
    if (!m_SourceId) return false;
    ALint state = 0;
    alGetSourcei(m_SourceId, AL_SOURCE_STATE, &state);
    return state == AL_PAUSED;
}

// ===========================================================================
// Tick — 主线程每帧调用，为 Streaming Source 补充已处理的 Buffer
// ===========================================================================
void VansAudioNode::Tick()
{
    if (!m_SourceId) return;
    if (m_Properties.m_PlayMode != AudioPlayMode::Streaming) return;

    std::lock_guard<std::mutex> lk(m_SourceMutex);

    // 检查并回收已处理完的 Buffer，用新的 PCM 数据重新填充
    RefillStreamBuffers();

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
                break;
            }
        }
    }
}

} // namespace VansEngine
