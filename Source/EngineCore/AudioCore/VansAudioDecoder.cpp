#include "VansAudioDecoder.h"
#include "../Util/VansLog.h"

// FFmpeg C headers must stay inside extern "C".
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

VansAudioDecoder::~VansAudioDecoder()
{
    Close();
}

bool VansAudioDecoder::Open(const std::string& filePath,
                             int targetChannels,
                             int targetSampleRate)
{
    if (filePath.empty())
    {
        VANS_LOG_ERROR("[VansAudioDecoder] Open failed: empty filePath");
        return false;
    }

    m_FilePath = filePath;

    if (avformat_open_input(&m_FmtCtx, filePath.c_str(), nullptr, nullptr) < 0)
    {
        VANS_LOG_ERROR("[VansAudioDecoder] avformat_open_input failed: " << filePath);
        return false;
    }

    if (avformat_find_stream_info(m_FmtCtx, nullptr) < 0)
    {
        VANS_LOG_ERROR("[VansAudioDecoder] avformat_find_stream_info failed: " << filePath);
        avformat_close_input(&m_FmtCtx);
        return false;
    }

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
        VANS_LOG_ERROR("[VansAudioDecoder] Audio stream not found: " << filePath);
        avformat_close_input(&m_FmtCtx);
        return false;
    }

    AVStream*          stream   = m_FmtCtx->streams[m_AudioStream];
    AVCodecParameters* codecPar = stream->codecpar;

    if (stream->duration != AV_NOPTS_VALUE)
        m_Duration = static_cast<double>(stream->duration) * av_q2d(stream->time_base);
    else if (m_FmtCtx->duration != AV_NOPTS_VALUE)
        m_Duration = static_cast<double>(m_FmtCtx->duration) / static_cast<double>(AV_TIME_BASE);

    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec)
    {
        VANS_LOG_ERROR("[VansAudioDecoder] Decoder not found: " << filePath);
        avformat_close_input(&m_FmtCtx);
        return false;
    }

    m_CodecCtx = avcodec_alloc_context3(codec);
    if (!m_CodecCtx)
    {
        VANS_LOG_ERROR("[VansAudioDecoder] avcodec_alloc_context3 failed");
        avformat_close_input(&m_FmtCtx);
        return false;
    }

    if (avcodec_parameters_to_context(m_CodecCtx, codecPar) < 0 ||
        avcodec_open2(m_CodecCtx, codec, nullptr) < 0)
    {
        VANS_LOG_ERROR("[VansAudioDecoder] Decoder initialization failed: " << filePath);
        avcodec_free_context(&m_CodecCtx);
        avformat_close_input(&m_FmtCtx);
        return false;
    }

    m_TargetChannels   = (targetChannels   > 0) ? targetChannels   : m_CodecCtx->ch_layout.nb_channels;
    m_TargetSampleRate = (targetSampleRate > 0) ? targetSampleRate : m_CodecCtx->sample_rate;

    AVChannelLayout targetLayout = {};
    av_channel_layout_default(&targetLayout, m_TargetChannels);

    int swrRet = swr_alloc_set_opts2(
        &m_SwrCtx,
        &targetLayout,             AV_SAMPLE_FMT_S16,      m_TargetSampleRate,
        &m_CodecCtx->ch_layout,    m_CodecCtx->sample_fmt, m_CodecCtx->sample_rate,
        0, nullptr);

    av_channel_layout_uninit(&targetLayout);

    if (swrRet < 0 || !m_SwrCtx || swr_init(m_SwrCtx) < 0)
    {
        VANS_LOG_ERROR("[VansAudioDecoder] swr_alloc/init failed: " << filePath);
        if (m_SwrCtx) { swr_free(&m_SwrCtx); m_SwrCtx = nullptr; }
        avcodec_free_context(&m_CodecCtx);
        avformat_close_input(&m_FmtCtx);
        return false;
    }

    VANS_LOG("[VansAudioDecoder] Opened: " << filePath
             << "  channels=" << m_TargetChannels
             << "  sampleRate=" << m_TargetSampleRate
             << "  duration=" << m_Duration << "s");
    return true;
}

void VansAudioDecoder::Close()
{
    if (m_SwrCtx)  { swr_free(&m_SwrCtx);               m_SwrCtx   = nullptr; }
    if (m_CodecCtx){ avcodec_free_context(&m_CodecCtx);                        }
    if (m_FmtCtx)  { avformat_close_input(&m_FmtCtx);                          }

    m_AudioStream     = -1;
    m_Duration        = 0.0;
}

AudioPCMChunk VansAudioDecoder::DecodeNextChunk()
{
    std::lock_guard<std::mutex> decodeLock(m_DecodeMutex);

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
        VANS_LOG_ERROR("[VansAudioDecoder] DecodeNextChunk: allocation failed");
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
            avcodec_send_packet(m_CodecCtx, nullptr);
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

    VANS_LOG("[VansAudioDecoder] DecodeAll: " << allSamples.size() / m_TargetChannels << " samples");
    return allSamples;
}

bool VansAudioDecoder::Reset()
{
    std::lock_guard<std::mutex> decodeLock(m_DecodeMutex);

    if (!m_FmtCtx || m_AudioStream < 0)
        return false;

    if (av_seek_frame(m_FmtCtx, m_AudioStream, 0, AVSEEK_FLAG_BACKWARD) < 0)
    {
        VANS_LOG_WARN("[VansAudioDecoder] seek to start failed: " << m_FilePath);
        return false;
    }

    if (m_CodecCtx)
        avcodec_flush_buffers(m_CodecCtx);

    if (m_SwrCtx)
        swr_init(m_SwrCtx);

    return true;
}

} // namespace VansEngine
