#include "VansVideoTexture.h"
#include "../../Util/VansLog.h"

// FFmpeg C 头文件必须在 extern "C" 块内引入，避免 C++ 名称修饰
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

namespace VansGraphics
{

// ===========================================================================
// 析构函数
// ===========================================================================
VansVideoTexture::~VansVideoTexture()
{
    Close();
}

// ===========================================================================
// Open — 打开视频文件，初始化解码上下文，创建 GPU 纹理，启动解码线程
// ===========================================================================
bool VansVideoTexture::Open(VansVKDevice* device,
                             const std::string& filePath,
                             bool loop,
                             bool autoPlay,
                             bool isSrgb)
{
    if (!device || filePath.empty())
    {
        VANS_LOG_ERROR("[VansVideoTexture] Open 失败：device 为空或 filePath 为空");
        return false;
    }

    m_VkDevice     = device;
    m_FilePath     = filePath;
    m_Loop         = loop;
    m_IsSrgb       = isSrgb;
    m_PlayTime     = 0.0;
    m_ShouldStop.store(false);

    // ── 1. 打开容器 ──────────────────────────────────────────────────────────
    if (avformat_open_input(&m_FmtCtx, filePath.c_str(), nullptr, nullptr) < 0)
    {
        VANS_LOG_ERROR("[VansVideoTexture] avformat_open_input 失败: " << filePath);
        return false;
    }

    if (avformat_find_stream_info(m_FmtCtx, nullptr) < 0)
    {
        VANS_LOG_ERROR("[VansVideoTexture] avformat_find_stream_info 失败: " << filePath);
        avformat_close_input(&m_FmtCtx);
        return false;
    }

    // ── 2. 查找视频流 ────────────────────────────────────────────────────────
    m_VideoStream = -1;
    for (unsigned i = 0; i < m_FmtCtx->nb_streams; ++i)
    {
        if (m_FmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            m_VideoStream = static_cast<int>(i);
            break;
        }
    }

    if (m_VideoStream < 0)
    {
        VANS_LOG_ERROR("[VansVideoTexture] 未找到视频流: " << filePath);
        avformat_close_input(&m_FmtCtx);
        return false;
    }

    AVStream* stream = m_FmtCtx->streams[m_VideoStream];
    m_TimeBase       = av_q2d(stream->time_base);
    m_Width          = stream->codecpar->width;
    m_Height         = stream->codecpar->height;

    // 计算视频总时长，用于循环时的 PTS 偏移
    if (stream->duration != AV_NOPTS_VALUE)
        m_VideoDuration = static_cast<double>(stream->duration) * m_TimeBase;
    else if (m_FmtCtx->duration != AV_NOPTS_VALUE)
        m_VideoDuration = static_cast<double>(m_FmtCtx->duration) / static_cast<double>(AV_TIME_BASE);
    else
        m_VideoDuration = 0.0; // 未知时长，循环 PTS 偏移将为 0

    // ── 3. 初始化解码器 ──────────────────────────────────────────────────────
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
    {
        VANS_LOG_ERROR("[VansVideoTexture] 找不到解码器: " << filePath);
        avformat_close_input(&m_FmtCtx);
        return false;
    }

    m_CodecCtx = avcodec_alloc_context3(codec);
    if (!m_CodecCtx)
    {
        VANS_LOG_ERROR("[VansVideoTexture] avcodec_alloc_context3 失败: " << filePath);
        avformat_close_input(&m_FmtCtx);
        return false;
    }

    if (avcodec_parameters_to_context(m_CodecCtx, stream->codecpar) < 0)
    {
        VANS_LOG_ERROR("[VansVideoTexture] avcodec_parameters_to_context 失败: " << filePath);
        avcodec_free_context(&m_CodecCtx);
        avformat_close_input(&m_FmtCtx);
        return false;
    }

    if (avcodec_open2(m_CodecCtx, codec, nullptr) < 0)
    {
        VANS_LOG_ERROR("[VansVideoTexture] avcodec_open2 失败: " << filePath);
        avcodec_free_context(&m_CodecCtx);
        avformat_close_input(&m_FmtCtx);
        return false;
    }

    // ── 4. 初始化 SwsContext（所有像素格式 → RGBA8）─────────────────────────
    m_SwsCtx = sws_getContext(
        m_Width, m_Height, m_CodecCtx->pix_fmt,
        m_Width, m_Height, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!m_SwsCtx)
    {
        VANS_LOG_ERROR("[VansVideoTexture] sws_getContext 失败: " << filePath);
        avcodec_free_context(&m_CodecCtx);
        avformat_close_input(&m_FmtCtx);
        return false;
    }

    // ── 5. 解码首帧，初始化 GPU 纹理 ──────────────────────────────────────────
    // 首帧解码完成后用于 LoadFromMemory 创建 VkImage；
    // 解码完成后立即 seek 回起点，保证后台线程从头开始。
    const int firstFrameSize = m_Width * m_Height * 4;
    std::vector<uint8_t> firstPixels(static_cast<size_t>(firstFrameSize), 0);
    bool gotFirstFrame = false;

    {
        AVFrame*  frame  = av_frame_alloc();
        AVPacket* packet = av_packet_alloc();

        if (frame && packet)
        {
            while (!gotFirstFrame && av_read_frame(m_FmtCtx, packet) >= 0)
            {
                if (packet->stream_index == m_VideoStream &&
                    avcodec_send_packet(m_CodecCtx, packet) >= 0)
                {
                    while (avcodec_receive_frame(m_CodecCtx, frame) >= 0)
                    {
                        uint8_t* dstData[4]   = { firstPixels.data(), nullptr, nullptr, nullptr };
                        int      dstStride[4] = { m_Width * 4, 0, 0, 0 };
                        sws_scale(m_SwsCtx,
                            frame->data, frame->linesize, 0, m_Height,
                            dstData, dstStride);
                        gotFirstFrame = true;
                        av_frame_unref(frame);
                        break;
                    }
                }
                av_packet_unref(packet);
            }
        }

        if (frame)  av_frame_free(&frame);
        if (packet) av_packet_free(&packet);
    }

    // seek 回视频起点，后台线程将从头解码
    av_seek_frame(m_FmtCtx, m_VideoStream, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_CodecCtx);

    // ── 6. 在 GPU 上创建纹理 ───────────────────────────────────────────────────
    VkFormat gpuFormat = m_IsSrgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    m_GpuTexture.LoadFromMemory(
        m_VkDevice->GetCommandBuffer(),
        firstPixels.data(), static_cast<size_t>(firstFrameSize),
        m_Width, m_Height, gpuFormat,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    m_IsReady.store(true);

    // ── 7. 启动后台解码线程 ──────────────────────────────────────────────────
    if (autoPlay)
        m_Playing.store(true);

    m_DecodeThread = std::thread(&VansVideoTexture::DecodeThreadFunc, this);

    VANS_LOG("[VansVideoTexture] 打开视频: " << filePath
             << " (" << m_Width << "x" << m_Height
             << ", duration=" << m_VideoDuration << "s)");
    return true;
}

// ===========================================================================
// Close — 停止后台线程，释放所有 FFmpeg 资源
// ===========================================================================
void VansVideoTexture::Close()
{
    // 通知后台线程退出
    m_ShouldStop.store(true);
    m_Playing.store(false);
    m_ProducerCv.notify_all();
    m_ConsumerCv.notify_all();

    if (m_DecodeThread.joinable())
        m_DecodeThread.join();

    // 清空帧队列
    {
        std::lock_guard<std::mutex> lock(m_QueueMutex);
        while (!m_FrameQueue.empty())
            m_FrameQueue.pop();
    }

    // 释放 FFmpeg 资源
    if (m_SwsCtx)   { sws_freeContext(m_SwsCtx);          m_SwsCtx    = nullptr; }
    if (m_CodecCtx) { avcodec_free_context(&m_CodecCtx);                          }
    if (m_FmtCtx)   { avformat_close_input(&m_FmtCtx);                            }

    m_IsReady.store(false);
    m_VideoStream  = -1;
    m_Width        = 0;
    m_Height       = 0;
    m_PlayTime     = 0.0;
}

// ===========================================================================
// Tick — 推进播放时间，将队列中 pts <= m_PlayTime 的帧上传到 GPU
// ===========================================================================
bool VansVideoTexture::Tick(double deltaTime)
{
    if (!m_IsReady.load() || !m_Playing.load())
        return false;

    m_PlayTime += deltaTime;

    // 从队列中取出所有已到期帧，仅保留最新一帧用于渲染（追帧）
    VideoFrameData frameToUpload;
    bool           hasFrame = false;

    {
        std::lock_guard<std::mutex> lock(m_QueueMutex);
        while (!m_FrameQueue.empty() && m_FrameQueue.front().pts <= m_PlayTime)
        {
            frameToUpload = std::move(m_FrameQueue.front());
            m_FrameQueue.pop();
            hasFrame = true;
        }
    }

    if (hasFrame)
    {
        // 通知后台线程队列有空位
        m_ProducerCv.notify_one();

        const int dataSize = m_Width * m_Height * 4;
        UploadFrameToGPU(frameToUpload.pixels.data(), dataSize);

        // 缓存本帧像素：供面光源 emissive 数组层每帧 CPU 侧拷贝（主线程独占写，无需 mutex）
        m_LastFramePixels = frameToUpload.pixels; // vector copy（大小 width*height*4）
        m_HasNewFrame = true;

        return true;
    }

    return false;
}

// ===========================================================================
// UploadFrameToGPU — 将 RGBA8 像素同步上传到 GPU（主线程）
// ===========================================================================
void VansVideoTexture::UploadFrameToGPU(const uint8_t* pixels, int dataSize)
{
    VkOffset3D offset = { 0, 0, 0 };
    VkExtent3D extent = {
        static_cast<uint32_t>(m_Width),
        static_cast<uint32_t>(m_Height),
        1u
    };

    m_VkDevice->SetDeviceImageData(
        m_GpuTexture.GetImage(),
        m_VkDevice->GetCommandBuffer(),
        const_cast<uint8_t*>(pixels),
        0, dataSize,
        offset, extent,
        0, 0);
}

// ===========================================================================
// CopyNewFrameToArrayLayer — 将本帧新像素写入目标贴图数组的指定层（主线程）
// 统一接口：消费方（面光源等）无需直接访问内部 CPU 像素缓存。
// 若无新帧则立即返回 false，不产生任何 GPU 操作。
// ===========================================================================
bool VansVideoTexture::CopyNewFrameToArrayLayer(VansTexture* targetArray,
                                                 VansVKCommandBuffer& cmd,
                                                 int layerIndex)
{
    if (!targetArray || !m_HasNewFrame || m_LastFramePixels.empty())
        return false;

    bool ok = targetArray->UpdateArrayLayerFromPixels(
        cmd, m_LastFramePixels.data(), m_Width, m_Height, layerIndex);
    ConsumeNewFrame();
    return ok;
}

// ===========================================================================
// GetImageView / GetSampler — 转发到底层 VansVKImage
// ===========================================================================
VkImageView VansVideoTexture::GetImageView()
{
    return m_GpuTexture.GetImage().GetImageView();
}

VkSampler VansVideoTexture::GetSampler()
{
    return m_GpuTexture.GetImage().GetSampler();
}

// ===========================================================================
// DecodeThreadFunc — 后台解码线程
//
// 流程：
//   av_read_frame → avcodec_send_packet → avcodec_receive_frame
//     → sws_scale（转 RGBA）→ 写入 m_FrameQueue
//
// 循环处理：
//   遇到 EOF 且 m_Loop==true 时，seek 回起点并将 ptsOffset 增加 m_VideoDuration，
//   使后续 PTS 对主线程 m_PlayTime 单调递增，无需重置 m_PlayTime，避免数据竞争。
// ===========================================================================
void VansVideoTexture::DecodeThreadFunc()
{
    AVFrame*  frame  = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();

    if (!frame || !packet)
    {
        VANS_LOG_ERROR("[VansVideoTexture] DecodeThreadFunc: 内存分配失败");
        if (frame)  av_frame_free(&frame);
        if (packet) av_packet_free(&packet);
        return;
    }

    const int rgbaBufSize = m_Width * m_Height * 4;
    double    ptsOffset   = 0.0; // 循环时累加视频总时长，保持 PTS 单调递增

    while (!m_ShouldStop.load())
    {
        const int readRet = av_read_frame(m_FmtCtx, packet);

        if (readRet < 0)
        {
            // EOF 或读取错误
            if (m_Loop && !m_ShouldStop.load())
            {
                // 循环回绕：偏移 PTS，seek 到起点
                ptsOffset += (m_VideoDuration > 0.0 ? m_VideoDuration : 1.0);
                av_seek_frame(m_FmtCtx, m_VideoStream, 0, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(m_CodecCtx);
                continue;
            }
            break; // 非循环模式：播放结束
        }

        if (packet->stream_index != m_VideoStream)
        {
            av_packet_unref(packet);
            continue;
        }

        // 发送 packet 给解码器
        if (avcodec_send_packet(m_CodecCtx, packet) < 0)
        {
            av_packet_unref(packet);
            continue;
        }
        av_packet_unref(packet);

        // 接收并处理所有解码帧
        while (!m_ShouldStop.load())
        {
            const int recvRet = avcodec_receive_frame(m_CodecCtx, frame);
            if (recvRet == AVERROR(EAGAIN) || recvRet == AVERROR_EOF)
                break;
            if (recvRet < 0)
                break;

            // 计算调整后 PTS（秒）
            double pts = ptsOffset;
            if (frame->best_effort_timestamp != AV_NOPTS_VALUE)
                pts += static_cast<double>(frame->best_effort_timestamp) * m_TimeBase;

            // 等待队列有空位 且 处于播放状态（暂停时不预解码，避免占用帧队列）
            {
                std::unique_lock<std::mutex> lock(m_QueueMutex);
                m_ProducerCv.wait(lock, [this] {
                    return (static_cast<int>(m_FrameQueue.size()) < MAX_QUEUE_SIZE
                            && m_Playing.load())
                        || m_ShouldStop.load();
                });
                if (m_ShouldStop.load())
                {
                    av_frame_unref(frame);
                    goto done;
                }
            }

            // 格式转换并推入队列
            {
                VideoFrameData vfd;
                vfd.pts = pts;
                vfd.pixels.resize(static_cast<size_t>(rgbaBufSize));

                uint8_t* dstData[4]   = { vfd.pixels.data(), nullptr, nullptr, nullptr };
                int      dstStride[4] = { m_Width * 4, 0, 0, 0 };
                sws_scale(m_SwsCtx,
                    frame->data, frame->linesize, 0, m_Height,
                    dstData, dstStride);

                std::lock_guard<std::mutex> lock(m_QueueMutex);
                m_FrameQueue.push(std::move(vfd));
                m_ConsumerCv.notify_one();
            }

            av_frame_unref(frame);
        }
    }

done:
    av_frame_free(&frame);
    av_packet_free(&packet);
}

} // namespace VansGraphics
