#include "VansAudioDecoder.h"
#include "../Util/VansLog.h"

// FFmpeg C 头文件必须在 extern "C" 块内引入，避免 C++ 名称修饰（与 VansVideoTexture.cpp 一致）
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace VansEngine
{

// ===========================================================================
// 析构函数
// ===========================================================================
VansAudioDecoder::~VansAudioDecoder()
{
    Close();
}

// ===========================================================================
// Open — 打开音频文件，初始化 FFmpeg 解码上下文和 SwrContext 重采样器
// ===========================================================================
bool VansAudioDecoder::Open(const std::string& filePath,
                             int targetChannels,
                             int targetSampleRate)
{
    if (filePath.empty())
    {
        VANS_LOG_ERROR("[VansAudioDecoder] Open 失败：filePath 为空");
        return false;
    }

    m_FilePath = filePath;

    // ── 1. 打开容器格式 ──────────────────────────────────────────────────────
    if (avformat_open_input(&m_FmtCtx, filePath.c_str(), nullptr, nullptr) < 0)
    {
        VANS_LOG_ERROR("[VansAudioDecoder] avformat_open_input 失败: " << filePath);
        return false;
    }

    if (avformat_find_stream_info(m_FmtCtx, nullptr) < 0)
    {
        VANS_LOG_ERROR("[VansAudioDecoder] avformat_find_stream_info 失败: " << filePath);
        avformat_close_input(&m_FmtCtx);
        return false;
    }

    // ── 2. 查找第一条音频流 ──────────────────────────────────────────────────
    m_AudioStream = -1;
    for (unsigned i = 0; i < m_FmtCtx->nb_streams; ++i)
    {
        if (m_FmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            m_AudioStream = static_cast<int>(i);
            break;
        }
    }

    if (m_AudioStream < 0)
    {
        VANS_LOG_ERROR("[VansAudioDecoder] 未找到音频流: " << filePath);
        avformat_close_input(&m_FmtCtx);
        return false;
    }

    AVStream*          stream   = m_FmtCtx->streams[m_AudioStream];
    AVCodecParameters* codecPar = stream->codecpar;

    // 计算时长
    if (stream->duration != AV_NOPTS_VALUE)
        m_Duration = static_cast<double>(stream->duration) * av_q2d(stream->time_base);
    else if (m_FmtCtx->duration != AV_NOPTS_VALUE)
        m_Duration = static_cast<double>(m_FmtCtx->duration) / static_cast<double>(AV_TIME_BASE);

    // ── 3. 初始化解码器 ──────────────────────────────────────────────────────
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec)
    {
        VANS_LOG_ERROR("[VansAudioDecoder] 找不到解码器: " << filePath);
        avformat_close_input(&m_FmtCtx);
        return false;
    }

    m_CodecCtx = avcodec_alloc_context3(codec);
    if (!m_CodecCtx)
    {
        VANS_LOG_ERROR("[VansAudioDecoder] avcodec_alloc_context3 失败");
        avformat_close_input(&m_FmtCtx);
        return false;
    }

    if (avcodec_parameters_to_context(m_CodecCtx, codecPar) < 0 ||
        avcodec_open2(m_CodecCtx, codec, nullptr) < 0)
    {
        VANS_LOG_ERROR("[VansAudioDecoder] 解码器初始化失败: " << filePath);
        avcodec_free_context(&m_CodecCtx);
        avformat_close_input(&m_FmtCtx);
        return false;
    }

    // ── 4. 确定目标格式 ──────────────────────────────────────────────────────
    m_TargetChannels   = (targetChannels   > 0) ? targetChannels   : m_CodecCtx->ch_layout.nb_channels;
    m_TargetSampleRate = (targetSampleRate > 0) ? targetSampleRate : m_CodecCtx->sample_rate;

    // ── 5. 初始化 SwrContext 重采样器 ────────────────────────────────────────
    // 统一输出：目标声道布局（mono/stereo）+ AV_SAMPLE_FMT_S16 + 目标采样率
    // MSVC C++ 不支持 AV_CHANNEL_LAYOUT_MONO/STEREO（C99 复合字面量），
    // 改用 av_channel_layout_default() 构造目标声道布局
    AVChannelLayout targetLayout = {};
    av_channel_layout_default(&targetLayout, m_TargetChannels);

    int swrRet = swr_alloc_set_opts2(
        &m_SwrCtx,
        &targetLayout,             AV_SAMPLE_FMT_S16,     m_TargetSampleRate,
        &m_CodecCtx->ch_layout,    m_CodecCtx->sample_fmt, m_CodecCtx->sample_rate,
        0, nullptr);

    av_channel_layout_uninit(&targetLayout);

    if (swrRet < 0 || !m_SwrCtx || swr_init(m_SwrCtx) < 0)
    {
        VANS_LOG_ERROR("[VansAudioDecoder] swr_alloc/init 失败: " << filePath);
        if (m_SwrCtx) { swr_free(&m_SwrCtx); m_SwrCtx = nullptr; }
        avcodec_free_context(&m_CodecCtx);
        avformat_close_input(&m_FmtCtx);
        return false;
    }

    VANS_LOG("[VansAudioDecoder] 打开成功: " << filePath
             << "  channels=" << m_TargetChannels
             << "  sampleRate=" << m_TargetSampleRate
             << "  duration=" << m_Duration << "s");
    return true;
}

// ===========================================================================
// Close — 释放所有 FFmpeg 资源
// ===========================================================================
void VansAudioDecoder::Close()
{
    if (m_SwrCtx)  { swr_free(&m_SwrCtx);               m_SwrCtx   = nullptr; }
    if (m_CodecCtx){ avcodec_free_context(&m_CodecCtx);                        }
    if (m_FmtCtx)  { avformat_close_input(&m_FmtCtx);                          }

    m_AudioStream     = -1;
    m_Duration        = 0.0;
}

// ===========================================================================
// DecodeNextChunk — 解码下一块约 4096 样本的 PCM（流式专用）
// ===========================================================================
AudioPCMChunk VansAudioDecoder::DecodeNextChunk()
{
    AudioPCMChunk chunk;
    chunk.channels   = m_TargetChannels;
    chunk.sampleRate = m_TargetSampleRate;

    if (!m_FmtCtx || !m_CodecCtx || !m_SwrCtx)
    {
        chunk.endOfStream = true;
        return chunk;
    }

    AVFrame*  frame  = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();

    if (!frame || !packet)
    {
        VANS_LOG_ERROR("[VansAudioDecoder] DecodeNextChunk: 内存分配失败");
        if (frame)  av_frame_free(&frame);
        if (packet) av_packet_free(&packet);
        chunk.endOfStream = true;
        return chunk;
    }

    static constexpr int TARGET_SAMPLES = 4096;
    chunk.samples.reserve(static_cast<size_t>(TARGET_SAMPLES * m_TargetChannels));

    bool gotEnough = false;
    bool eof       = false;

    while (!gotEnough && !eof)
    {
        int readRet = av_read_frame(m_FmtCtx, packet);
        if (readRet < 0)
        {
            // EOF 或读取错误
            // flush 解码器中的剩余帧
            avcodec_send_packet(m_CodecCtx, nullptr);
            while (avcodec_receive_frame(m_CodecCtx, frame) >= 0)
            {
                // 重采样
                int maxSamples = swr_get_out_samples(m_SwrCtx, frame->nb_samples);
                if (maxSamples <= 0) { av_frame_unref(frame); continue; }

                std::vector<int16_t> converted(static_cast<size_t>(maxSamples * m_TargetChannels));
                uint8_t* outPtr = reinterpret_cast<uint8_t*>(converted.data());

                int convertedSamples = swr_convert(m_SwrCtx, &outPtr, maxSamples,
                    const_cast<const uint8_t**>(frame->data), frame->nb_samples);
                if (convertedSamples > 0)
                {
                    converted.resize(static_cast<size_t>(convertedSamples * m_TargetChannels));
                    chunk.samples.insert(chunk.samples.end(), converted.begin(), converted.end());
                }
                av_frame_unref(frame);
            }
            eof = true;
            break;
        }

        if (packet->stream_index != m_AudioStream)
        {
            av_packet_unref(packet);
            continue;
        }

        if (avcodec_send_packet(m_CodecCtx, packet) < 0)
        {
            av_packet_unref(packet);
            continue;
        }
        av_packet_unref(packet);

        while (avcodec_receive_frame(m_CodecCtx, frame) >= 0)
        {
            int maxSamples = swr_get_out_samples(m_SwrCtx, frame->nb_samples);
            if (maxSamples <= 0) { av_frame_unref(frame); continue; }

            std::vector<int16_t> converted(static_cast<size_t>(maxSamples * m_TargetChannels));
            uint8_t* outPtr = reinterpret_cast<uint8_t*>(converted.data());

            int convertedSamples = swr_convert(m_SwrCtx, &outPtr, maxSamples,
                const_cast<const uint8_t**>(frame->data), frame->nb_samples);
            if (convertedSamples > 0)
            {
                converted.resize(static_cast<size_t>(convertedSamples * m_TargetChannels));
                chunk.samples.insert(chunk.samples.end(), converted.begin(), converted.end());
            }
            av_frame_unref(frame);

            if ((int)chunk.samples.size() >= TARGET_SAMPLES * m_TargetChannels)
            {
                gotEnough = true;
                break;
            }
        }
    }

    // flush 重采样器中的残余样本
    if (eof)
    {
        for (;;)
        {
            const int delayedSamples = static_cast<int>(swr_get_delay(m_SwrCtx, m_TargetSampleRate));
            if (delayedSamples <= 0) break;

            std::vector<int16_t> flushed(static_cast<size_t>((delayedSamples + 64) * m_TargetChannels));
            uint8_t* outPtr = reinterpret_cast<uint8_t*>(flushed.data());
            const uint8_t* nullSrc = nullptr;

            int n = swr_convert(m_SwrCtx, &outPtr, delayedSamples + 64, &nullSrc, 0);
            if (n <= 0) break;

            flushed.resize(static_cast<size_t>(n * m_TargetChannels));
            chunk.samples.insert(chunk.samples.end(), flushed.begin(), flushed.end());
        }
        chunk.endOfStream = true;
    }

    av_frame_free(&frame);
    av_packet_free(&packet);

    return chunk;
}

// ===========================================================================
// DecodeAll — 一次性解码全部 PCM（静态模式专用）
// ===========================================================================
std::vector<int16_t> VansAudioDecoder::DecodeAll(int& outChannels, int& outSampleRate)
{
    outChannels   = m_TargetChannels;
    outSampleRate = m_TargetSampleRate;

    std::vector<int16_t> allSamples;
    allSamples.reserve(static_cast<size_t>(m_Duration * m_TargetSampleRate * m_TargetChannels) + 4096);

    while (true)
    {
        AudioPCMChunk chunk = DecodeNextChunk();
        if (!chunk.samples.empty())
            allSamples.insert(allSamples.end(), chunk.samples.begin(), chunk.samples.end());

        if (chunk.endOfStream)
            break;
    }

    VANS_LOG("[VansAudioDecoder] DecodeAll: " << allSamples.size() / m_TargetChannels << " 样本");
    return allSamples;
}

// ===========================================================================
// Reset — seek 到文件开头并 flush 解码器（用于循环播放回绕）
// ===========================================================================
bool VansAudioDecoder::Reset()
{
    if (!m_FmtCtx || m_AudioStream < 0)
        return false;

    if (av_seek_frame(m_FmtCtx, m_AudioStream, 0, AVSEEK_FLAG_BACKWARD) < 0)
    {
        VANS_LOG_WARN("[VansAudioDecoder] seek to start 失败: " << m_FilePath);
        return false;
    }

    if (m_CodecCtx)
        avcodec_flush_buffers(m_CodecCtx);

    if (m_SwrCtx)
        swr_init(m_SwrCtx); // 重置重采样器内部状态

    return true;
}

} // namespace VansEngine
