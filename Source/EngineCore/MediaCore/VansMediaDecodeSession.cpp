#include "VansMediaDecodeSession.h"

#include <algorithm>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace VansEngine
{
namespace
{
enum class VansMediaStreamKind
{
    None,
    Audio,
    Video
};
}

struct VansMediaDecodeState
{
    AVFormatContext* m_Format = nullptr;
    AVCodecContext* m_Codec = nullptr;
    SwrContext* m_Resampler = nullptr;
    SwsContext* m_Scaler = nullptr;
    AVFrame* m_Frame = nullptr;
    AVPacket* m_Packet = nullptr;
    int m_StreamIndex = -1;
    VansMediaStreamKind m_StreamKind = VansMediaStreamKind::None;
    double m_TimeBase = 0.0;
    double m_Duration = 0.0;
    int m_Width = 0;
    int m_Height = 0;
    int m_Channels = 0;
    int m_SampleRate = 0;
    bool m_PacketPending = false;
    bool m_InputEnded = false;
    bool m_DrainSent = false;
};

namespace
{
void ResetDecodeProgress(VansMediaDecodeState& state)
{
    if (state.m_Packet)
        av_packet_unref(state.m_Packet);
    if (state.m_Frame)
        av_frame_unref(state.m_Frame);
    state.m_PacketPending = false;
    state.m_InputEnded = false;
    state.m_DrainSent = false;
}

VansMediaDecodeStatus ReceiveFrame(
    VansMediaDecodeState& state,
    VansMediaEndPolicy endPolicy,
    std::string& error)
{
    for (;;)
    {
        const int receiveResult = avcodec_receive_frame(state.m_Codec, state.m_Frame);
        if (receiveResult >= 0)
            return VansMediaDecodeStatus::FrameReady;
        if (receiveResult == AVERROR_EOF)
            return VansMediaDecodeStatus::EndOfStream;
        if (receiveResult != AVERROR(EAGAIN))
        {
            error = "Media decoder could not receive a frame";
            return VansMediaDecodeStatus::Failed;
        }

        if (state.m_PacketPending)
        {
            const int sendResult = avcodec_send_packet(state.m_Codec, state.m_Packet);
            if (sendResult == AVERROR(EAGAIN))
            {
                error = "Media decoder could not consume its pending packet";
                return VansMediaDecodeStatus::Failed;
            }
            av_packet_unref(state.m_Packet);
            state.m_PacketPending = false;
            if (sendResult < 0)
                continue;
            continue;
        }

        if (state.m_InputEnded)
        {
            if (endPolicy == VansMediaEndPolicy::DrainDecoder && !state.m_DrainSent)
            {
                const int drainResult = avcodec_send_packet(state.m_Codec, nullptr);
                if (drainResult < 0 && drainResult != AVERROR_EOF)
                {
                    error = "Media decoder could not enter drain mode";
                    return VansMediaDecodeStatus::Failed;
                }
                state.m_DrainSent = true;
                continue;
            }
            return VansMediaDecodeStatus::EndOfStream;
        }

        for (;;)
        {
            const int readResult = av_read_frame(state.m_Format, state.m_Packet);
            if (readResult < 0)
            {
                state.m_InputEnded = true;
                break;
            }
            if (state.m_Packet->stream_index != state.m_StreamIndex)
            {
                av_packet_unref(state.m_Packet);
                continue;
            }
            state.m_PacketPending = true;
            break;
        }
    }
}

bool OpenStream(
    VansMediaDecodeState& state,
    const std::string& filePath,
    AVMediaType mediaType,
    std::string& error)
{
    if (avformat_open_input(&state.m_Format, filePath.c_str(), nullptr, nullptr) < 0)
    {
        error = "Media container could not be opened";
        return false;
    }
    if (avformat_find_stream_info(state.m_Format, nullptr) < 0)
    {
        error = "Media stream information could not be read";
        return false;
    }

    for (unsigned int index = 0; index < state.m_Format->nb_streams; ++index)
    {
        if (state.m_Format->streams[index]->codecpar->codec_type == mediaType)
        {
            state.m_StreamIndex = static_cast<int>(index);
            break;
        }
    }
    if (state.m_StreamIndex < 0)
    {
        error = mediaType == AVMEDIA_TYPE_AUDIO
            ? "Media source contains no audio stream"
            : "Media source contains no video stream";
        return false;
    }

    AVStream* stream = state.m_Format->streams[state.m_StreamIndex];
    state.m_TimeBase = av_q2d(stream->time_base);
    if (stream->duration != AV_NOPTS_VALUE)
        state.m_Duration = static_cast<double>(stream->duration) * state.m_TimeBase;
    else if (state.m_Format->duration != AV_NOPTS_VALUE)
        state.m_Duration = static_cast<double>(state.m_Format->duration) /
            static_cast<double>(AV_TIME_BASE);

    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
    {
        error = "Media codec is unavailable";
        return false;
    }
    state.m_Codec = avcodec_alloc_context3(codec);
    if (!state.m_Codec)
    {
        error = "Media codec context allocation failed";
        return false;
    }
    if (avcodec_parameters_to_context(state.m_Codec, stream->codecpar) < 0 ||
        avcodec_open2(state.m_Codec, codec, nullptr) < 0)
    {
        error = "Media codec initialization failed";
        return false;
    }

    state.m_Frame = av_frame_alloc();
    state.m_Packet = av_packet_alloc();
    if (!state.m_Frame || !state.m_Packet)
    {
        error = "Media frame allocation failed";
        return false;
    }

    state.m_StreamKind = mediaType == AVMEDIA_TYPE_AUDIO
        ? VansMediaStreamKind::Audio
        : VansMediaStreamKind::Video;
    state.m_Width = stream->codecpar->width;
    state.m_Height = stream->codecpar->height;
    return true;
}
}

VansMediaDecodeSession::VansMediaDecodeSession()
    : m_State(std::make_unique<VansMediaDecodeState>())
{
}

VansMediaDecodeSession::~VansMediaDecodeSession()
{
    Close();
}

bool VansMediaDecodeSession::OpenAudio(
    const std::string& filePath,
    int targetChannels,
    int targetSampleRate,
    std::string& error)
{
    Close();
    error.clear();
    if (filePath.empty())
    {
        error = "Audio media path is empty";
        return false;
    }
    if (!OpenStream(*m_State, filePath, AVMEDIA_TYPE_AUDIO, error))
    {
        Close();
        return false;
    }

    targetChannels = targetChannels > 0
        ? targetChannels
        : m_State->m_Codec->ch_layout.nb_channels;
    targetSampleRate = targetSampleRate > 0
        ? targetSampleRate
        : m_State->m_Codec->sample_rate;

    AVChannelLayout targetLayout = {};
    av_channel_layout_default(&targetLayout, targetChannels);
    const int resampleResult = swr_alloc_set_opts2(
        &m_State->m_Resampler,
        &targetLayout,
        AV_SAMPLE_FMT_S16,
        targetSampleRate,
        &m_State->m_Codec->ch_layout,
        m_State->m_Codec->sample_fmt,
        m_State->m_Codec->sample_rate,
        0,
        nullptr);
    av_channel_layout_uninit(&targetLayout);
    if (resampleResult < 0 || !m_State->m_Resampler || swr_init(m_State->m_Resampler) < 0)
    {
        error = "Audio resampler initialization failed";
        Close();
        return false;
    }

    m_State->m_Channels = targetChannels;
    m_State->m_SampleRate = targetSampleRate;
    return true;
}

bool VansMediaDecodeSession::OpenVideo(
    const std::string& filePath,
    std::string& error)
{
    Close();
    error.clear();
    if (filePath.empty())
    {
        error = "Video media path is empty";
        return false;
    }
    if (!OpenStream(*m_State, filePath, AVMEDIA_TYPE_VIDEO, error))
    {
        Close();
        return false;
    }
    if (m_State->m_Width <= 0 || m_State->m_Height <= 0)
    {
        error = "Video stream has invalid dimensions";
        Close();
        return false;
    }
    return true;
}

void VansMediaDecodeSession::Close()
{
    if (!m_State)
        return;

    if (m_State->m_Scaler)
        sws_freeContext(m_State->m_Scaler);
    if (m_State->m_Resampler)
        swr_free(&m_State->m_Resampler);
    if (m_State->m_Packet)
        av_packet_free(&m_State->m_Packet);
    if (m_State->m_Frame)
        av_frame_free(&m_State->m_Frame);
    if (m_State->m_Codec)
        avcodec_free_context(&m_State->m_Codec);
    if (m_State->m_Format)
        avformat_close_input(&m_State->m_Format);

    *m_State = VansMediaDecodeState{};
}

bool VansMediaDecodeSession::IsOpen() const
{
    return m_State && m_State->m_Format && m_State->m_Codec &&
        m_State->m_StreamIndex >= 0;
}

int VansMediaDecodeSession::GetWidth() const
{
    return m_State ? m_State->m_Width : 0;
}

int VansMediaDecodeSession::GetHeight() const
{
    return m_State ? m_State->m_Height : 0;
}

int VansMediaDecodeSession::GetChannels() const
{
    return m_State ? m_State->m_Channels : 0;
}

int VansMediaDecodeSession::GetSampleRate() const
{
    return m_State ? m_State->m_SampleRate : 0;
}

double VansMediaDecodeSession::GetDuration() const
{
    return m_State ? m_State->m_Duration : 0.0;
}

VansMediaDecodeStatus VansMediaDecodeSession::DecodeAudioChunk(
    int targetSampleFrames,
    std::vector<std::int16_t>& samples,
    std::string& error)
{
    samples.clear();
    error.clear();
    if (!IsOpen() || m_State->m_StreamKind != VansMediaStreamKind::Audio ||
        !m_State->m_Resampler)
    {
        error = "Audio media session is not open";
        return VansMediaDecodeStatus::Failed;
    }

    targetSampleFrames = std::max(1, targetSampleFrames);
    const std::size_t targetSamples =
        static_cast<std::size_t>(targetSampleFrames) * m_State->m_Channels;
    samples.reserve(targetSamples);

    for (;;)
    {
        const VansMediaDecodeStatus status = ReceiveFrame(
            *m_State,
            VansMediaEndPolicy::DrainDecoder,
            error);
        if (status == VansMediaDecodeStatus::Failed)
            return VansMediaDecodeStatus::Failed;
        if (status == VansMediaDecodeStatus::EndOfStream)
        {
            for (;;)
            {
                const int delayedSamples = static_cast<int>(
                    swr_get_delay(m_State->m_Resampler, m_State->m_SampleRate));
                if (delayedSamples <= 0)
                    break;

                const std::size_t oldSize = samples.size();
                const int outputCapacity = delayedSamples + 64;
                samples.resize(oldSize + static_cast<std::size_t>(outputCapacity) *
                    m_State->m_Channels);
                std::uint8_t* output = reinterpret_cast<std::uint8_t*>(
                    samples.data() + oldSize);
                const std::uint8_t* emptyInput = nullptr;
                const int converted = swr_convert(
                    m_State->m_Resampler,
                    &output,
                    outputCapacity,
                    &emptyInput,
                    0);
                if (converted <= 0)
                {
                    samples.resize(oldSize);
                    break;
                }
                samples.resize(oldSize + static_cast<std::size_t>(converted) *
                    m_State->m_Channels);
            }
            return VansMediaDecodeStatus::EndOfStream;
        }

        const int outputCapacity = swr_get_out_samples(
            m_State->m_Resampler,
            m_State->m_Frame->nb_samples);
        if (outputCapacity > 0)
        {
            const std::size_t oldSize = samples.size();
            samples.resize(oldSize + static_cast<std::size_t>(outputCapacity) *
                m_State->m_Channels);
            std::uint8_t* output = reinterpret_cast<std::uint8_t*>(
                samples.data() + oldSize);
            const int converted = swr_convert(
                m_State->m_Resampler,
                &output,
                outputCapacity,
                const_cast<const std::uint8_t**>(m_State->m_Frame->data),
                m_State->m_Frame->nb_samples);
            if (converted > 0)
            {
                samples.resize(oldSize + static_cast<std::size_t>(converted) *
                    m_State->m_Channels);
            }
            else
            {
                samples.resize(oldSize);
            }
        }
        av_frame_unref(m_State->m_Frame);

        if (samples.size() >= targetSamples)
            return VansMediaDecodeStatus::FrameReady;
    }
}

VansMediaDecodeStatus VansMediaDecodeSession::DecodeVideoFrame(
    std::vector<std::uint8_t>& rgba,
    double& presentationTime,
    VansMediaEndPolicy endPolicy,
    std::string& error)
{
    presentationTime = 0.0;
    error.clear();
    if (!IsOpen() || m_State->m_StreamKind != VansMediaStreamKind::Video)
    {
        error = "Video media session is not open";
        return VansMediaDecodeStatus::Failed;
    }

    const VansMediaDecodeStatus status = ReceiveFrame(*m_State, endPolicy, error);
    if (status != VansMediaDecodeStatus::FrameReady)
        return status;

    m_State->m_Scaler = sws_getCachedContext(
        m_State->m_Scaler,
        m_State->m_Frame->width,
        m_State->m_Frame->height,
        static_cast<AVPixelFormat>(m_State->m_Frame->format),
        m_State->m_Width,
        m_State->m_Height,
        AV_PIX_FMT_RGBA,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr);
    if (!m_State->m_Scaler)
    {
        av_frame_unref(m_State->m_Frame);
        error = "Video pixel converter initialization failed";
        return VansMediaDecodeStatus::Failed;
    }

    rgba.resize(static_cast<std::size_t>(m_State->m_Width) *
        static_cast<std::size_t>(m_State->m_Height) * 4u);
    std::uint8_t* destination[4] = { rgba.data(), nullptr, nullptr, nullptr };
    int destinationStride[4] = { m_State->m_Width * 4, 0, 0, 0 };
    const int convertedRows = sws_scale(
        m_State->m_Scaler,
        m_State->m_Frame->data,
        m_State->m_Frame->linesize,
        0,
        m_State->m_Frame->height,
        destination,
        destinationStride);
    if (m_State->m_Frame->best_effort_timestamp != AV_NOPTS_VALUE)
    {
        presentationTime = static_cast<double>(
            m_State->m_Frame->best_effort_timestamp) * m_State->m_TimeBase;
    }
    av_frame_unref(m_State->m_Frame);
    if (convertedRows <= 0)
    {
        error = "Video pixel conversion failed";
        return VansMediaDecodeStatus::Failed;
    }
    return VansMediaDecodeStatus::FrameReady;
}

bool VansMediaDecodeSession::Seek(double seconds, std::string& error)
{
    error.clear();
    if (!IsOpen() || m_State->m_TimeBase <= 0.0)
    {
        error = "Media session cannot seek";
        return false;
    }

    seconds = std::max(0.0, seconds);
    if (m_State->m_Duration > 0.0)
        seconds = std::min(seconds, m_State->m_Duration);
    const std::int64_t timestamp = static_cast<std::int64_t>(seconds / m_State->m_TimeBase);
    if (av_seek_frame(
        m_State->m_Format,
        m_State->m_StreamIndex,
        timestamp,
        AVSEEK_FLAG_BACKWARD) < 0)
    {
        error = "Media session seek failed";
        return false;
    }

    avcodec_flush_buffers(m_State->m_Codec);
    if (m_State->m_Resampler && swr_init(m_State->m_Resampler) < 0)
    {
        error = "Audio resampler reset failed";
        return false;
    }
    ResetDecodeProgress(*m_State);
    return true;
}

bool VansMediaDecodeSession::Reset(std::string& error)
{
    return Seek(0.0, error);
}
}
