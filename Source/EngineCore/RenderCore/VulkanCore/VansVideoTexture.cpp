#include "VansVideoTexture.h"
#include "../../MediaCore/VansMediaDecodeSession.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansProfiler.h"
#include <cstring>

namespace VansGraphics
{

VansVideoTexture::VansVideoTexture() = default;

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

    Close();
    m_VkDevice = device;
    m_Loop = loop;
    m_IsSrgb = isSrgb;
    m_PlayTime = 0.0;
    m_ShouldStop.store(false);
    m_NeedRestart.store(false);
    m_SeekRequestSeconds.store(-1.0);

    auto decodeSession = std::make_unique<VansEngine::VansMediaDecodeSession>();
    std::string error;
    if (!decodeSession->OpenVideo(filePath, error))
    {
        VANS_LOG_ERROR("[VansVideoTexture] 打开视频失败: " << filePath << " (" << error << ")");
        return false;
    }

    m_Width = decodeSession->GetWidth();
    m_Height = decodeSession->GetHeight();
    m_VideoDuration = decodeSession->GetDuration();

    // ── 1. 解码首帧，初始化 GPU 纹理 ──────────────────────────────────────────
    // 首帧解码完成后用于 LoadFromMemory 创建 VkImage；
    // 解码完成后立即 seek 回起点，保证后台线程从头开始。
    const int firstFrameSize = m_Width * m_Height * 4;
    std::vector<uint8_t> firstPixels(static_cast<size_t>(firstFrameSize), 0);

    {
        VANS_PROFILE_SCOPE("Video::Open.FirstFrameDecode", Vans::ProfileCategory::Video);
        double firstFrameTime = 0.0;
        const VansEngine::VansMediaDecodeStatus status = decodeSession->DecodeVideoFrame(
            firstPixels,
            firstFrameTime,
            VansEngine::VansMediaEndPolicy::StopAtInputEnd,
            error);
        if (status == VansEngine::VansMediaDecodeStatus::Failed)
        {
            VANS_LOG_WARN("[VansVideoTexture] 首帧解码失败，使用黑色初始帧: "
                << filePath << " (" << error << ")");
            firstPixels.assign(static_cast<size_t>(firstFrameSize), 0);
        }
    }

    error.clear();
    if (!decodeSession->Reset(error))
    {
        VANS_LOG_ERROR("[VansVideoTexture] 视频复位失败: " << filePath << " (" << error << ")");
        return false;
    }
    m_DecodeSession = std::move(decodeSession);

    // ── 2. 在 GPU 上创建纹理 ───────────────────────────────────────────────────
    VkFormat gpuFormat = m_IsSrgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    {
        VANS_PROFILE_SCOPE("Video::Open.InitialTextureUpload", Vans::ProfileCategory::Video);
        m_GpuTexture.LoadFromMemory(
            m_VkDevice->GetCommandBuffer(),
            firstPixels.data(), static_cast<size_t>(firstFrameSize),
            m_Width, m_Height, gpuFormat,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    }

    CreateFrameUploadBuffer(firstFrameSize);

    m_IsReady.store(true);

    // ── 3. 启动后台解码线程 ──────────────────────────────────────────────────
    if (autoPlay)
        m_Playing.store(true);

    m_DecodeThread = std::thread(&VansVideoTexture::DecodeThreadFunc, this);

    VANS_LOG("[VansVideoTexture] 打开视频: " << filePath
             << " (" << m_Width << "x" << m_Height
             << ", duration=" << m_VideoDuration << "s)");
    return true;
}

// ===========================================================================
// Close — 停止后台线程，释放媒体会话和 GPU 资源
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

	std::lock_guard<std::mutex> pendingLock(m_PendingFrameMutex);

    // 清空帧队列
    {
        std::lock_guard<std::mutex> lock(m_QueueMutex);
        while (!m_FrameQueue.empty())
            m_FrameQueue.pop();
		RecycleFramePixelsLocked(m_LastFramePixels);
		RecycleUploadSlotLocked(m_LastFrameUploadSlot);
        ClearFramePixelPoolLocked();
        ClearUploadSlotsLocked();
    }

    m_DecodeSession.reset();

    m_IsReady.store(false);
    m_Width        = 0;
    m_Height       = 0;
    m_VideoDuration = 0.0;
    m_PlayTime     = 0.0;
    m_HasNewFrame = false;
    m_HasPendingUpload = false;
    m_LastFrameUploadSlot = -1;
    std::vector<uint8_t>().swap(m_LastFramePixels);

    if (m_FrameUploadBufferReady && m_VkDevice)
    {
        m_FrameUploadBuffer.DestroyVulkanBuffer(m_VkDevice->GetLogicDevice());
        m_FrameUploadBufferReady = false;
    }
}

// ===========================================================================
// Stop — 停止播放并将解码位置重置到视频起点
//
// 与 Pause() 的区别：Pause 仅暂停，保留当前播放进度；
// Stop 额外通知解码线程 seek 回第 0 帧，下次 Play() 从头开始。
// 主线程调用安全（m_PlayTime 仅主线程写）。
// ===========================================================================
void VansVideoTexture::Stop()
{
    if (!m_IsReady.load())
        return;

    // 1. 停止播放
    m_Playing.store(false);

    // 2. 通知解码线程执行 seek 回起点并重置 ptsOffset
    m_NeedRestart.store(true);
    m_ProducerCv.notify_all();

	std::lock_guard<std::mutex> pendingLock(m_PendingFrameMutex);

    // 3. 清空主线程侧的待显示帧队列（旧帧已不需要）
    {
        std::lock_guard<std::mutex> lock(m_QueueMutex);
        while (!m_FrameQueue.empty())
        {
            VideoFrameData frame = std::move(m_FrameQueue.front());
            m_FrameQueue.pop();
            RecycleFramePixelsLocked(frame.pixels);
            RecycleUploadSlotLocked(frame.uploadSlot);
        }
        RecycleFramePixelsLocked(m_LastFramePixels);
        RecycleUploadSlotLocked(m_LastFrameUploadSlot);
    }

    // 4. 重置播放时间（仅主线程写）
    m_PlayTime = 0.0;
    m_HasNewFrame = false;
    m_HasPendingUpload = false;
}

bool VansVideoTexture::Seek(double seconds)
{
	if (!m_IsReady.load()) return false;
	const double target = std::clamp(seconds, 0.0, std::max(0.0, m_VideoDuration));
	std::lock_guard<std::mutex> pendingLock(m_PendingFrameMutex);
	{
		std::lock_guard<std::mutex> lock(m_QueueMutex);
		while (!m_FrameQueue.empty())
		{
			VideoFrameData frame = std::move(m_FrameQueue.front());
			m_FrameQueue.pop();
			RecycleFramePixelsLocked(frame.pixels);
			RecycleUploadSlotLocked(frame.uploadSlot);
		}
		RecycleFramePixelsLocked(m_LastFramePixels);
		RecycleUploadSlotLocked(m_LastFrameUploadSlot);
	}
	m_PlayTime = target;
	m_HasNewFrame = false;
	m_HasPendingUpload = false;
	m_SeekRequestSeconds.store(target);
	m_ProducerCv.notify_all();
	return true;
}

// ===========================================================================
// Tick — 推进播放时间，挑选队列中 pts <= m_PlayTime 的最新帧
// ===========================================================================
bool VansVideoTexture::Tick(double deltaTime)
{
    if (!m_IsReady.load() || !m_Playing.load())
        return false;

    m_PlayTime += deltaTime * m_PlaybackRate;

    // 从队列中取出所有已到期帧，仅保留最新一帧用于渲染（追帧）
    VideoFrameData frameToUpload;
    bool           hasFrame = false;

    {
        VANS_PROFILE_SCOPE("Video::Tick.SelectFrameLock", Vans::ProfileCategory::Video);
        std::lock_guard<std::mutex> lock(m_QueueMutex);
        while (!m_FrameQueue.empty() && m_FrameQueue.front().pts <= m_PlayTime)
        {
            if (hasFrame)
            {
                RecycleFramePixelsLocked(frameToUpload.pixels);
                RecycleUploadSlotLocked(frameToUpload.uploadSlot);
            }
            frameToUpload = std::move(m_FrameQueue.front());
            m_FrameQueue.pop();
            hasFrame = true;
        }
    }

    if (hasFrame)
    {
        VANS_PROFILE_SCOPE("Video::Tick.PublishPendingFrame", Vans::ProfileCategory::Video);
        // 通知后台线程队列有空位
        m_ProducerCv.notify_one();

		std::lock_guard<std::mutex> pendingLock(m_PendingFrameMutex);
        {
            std::lock_guard<std::mutex> lock(m_QueueMutex);
            RecycleFramePixelsLocked(m_LastFramePixels);
            RecycleUploadSlotLocked(m_LastFrameUploadSlot);
        }

        // 缓存本帧像素：供后续图形命令录制阶段上传，也供面光源 emissive 数组层复用。
        // 使用 std::move 避免逐字节拷贝整个帧缓冲（1080p ≈ 8 MB）
        m_LastFramePixels = std::move(frameToUpload.pixels);
        m_LastFrameUploadSlot = frameToUpload.uploadSlot;
        frameToUpload.uploadSlot = -1;
        m_HasNewFrame = true;
        m_HasPendingUpload = true;

        return true;
    }

    return false;
}

// ===========================================================================
// RecordPendingUpload — 将待显示视频帧记录到当前帧图形命令缓冲
// ===========================================================================
bool VansVideoTexture::RecordPendingUpload(VansVKCommandBuffer& cmd)
{
	std::lock_guard<std::mutex> pendingLock(m_PendingFrameMutex);
	return RecordPendingUploadLocked(cmd);
}

bool VansVideoTexture::RecordPendingUploadLocked(VansVKCommandBuffer& cmd)
{
    if (!m_IsReady.load() || !m_HasPendingUpload ||
        (m_LastFrameUploadSlot < 0 && m_LastFramePixels.empty()))
        return false;

    VANS_PROFILE_SCOPE("Video::Upload.RecordMainTexture", Vans::ProfileCategory::Video);
    const int dataSize = m_Width * m_Height * 4;
    if (m_LastFrameUploadSlot >= 0)
    {
        if (!RecordFrameUploadFromSlot(cmd, m_LastFrameUploadSlot, dataSize))
            return false;
    }
    else if (!RecordFrameUpload(cmd, m_LastFramePixels.data(), dataSize))
    {
        return false;
    }

    m_HasPendingUpload = false;
    return true;
}

// ===========================================================================
// RecordFrameUpload — 将 RGBA8 像素写入本帧 staging，并记录 GPU copy 命令
// ===========================================================================
bool VansVideoTexture::RecordFrameUpload(VansVKCommandBuffer& cmd, const uint8_t* pixels, int dataSize)
{
    VANS_PROFILE_SCOPE("Video::Upload.RecordFrameUpload", Vans::ProfileCategory::Video);
    VkOffset3D offset = { 0, 0, 0 };
    VkExtent3D extent = {
        static_cast<uint32_t>(m_Width),
        static_cast<uint32_t>(m_Height),
        1u
    };

    return m_VkDevice->RecordDeviceImageData(
        m_GpuTexture.GetImage(),
        cmd,
        pixels,
        dataSize,
        offset, extent,
        0, 0);
}

bool VansVideoTexture::RecordFrameUploadFromSlot(VansVKCommandBuffer& cmd, int uploadSlot, int dataSize)
{
    VANS_PROFILE_SCOPE("Video::Upload.RecordFrameUploadStaged", Vans::ProfileCategory::Video);
    if (!m_FrameUploadBufferReady || uploadSlot < 0 || uploadSlot >= MAX_UPLOAD_SLOTS)
        return false;

    VkOffset3D offset = { 0, 0, 0 };
    VkExtent3D extent = {
        static_cast<uint32_t>(m_Width),
        static_cast<uint32_t>(m_Height),
        1u
    };

    const VkDeviceSize sourceOffset =
        static_cast<VkDeviceSize>(uploadSlot) * static_cast<VkDeviceSize>(dataSize);
    return m_VkDevice->RecordDeviceImageBufferData(
        m_GpuTexture.GetImage(),
        cmd,
        m_FrameUploadBuffer,
        sourceOffset,
        offset, extent,
        0, 0);
}

// ===========================================================================
// AcquireFramePixelsLocked — 从 CPU 像素缓存池获取 RGBA 缓冲
// ===========================================================================
std::vector<uint8_t> VansVideoTexture::AcquireFramePixelsLocked(int dataSize)
{
    if (!m_FreeFrameBuffers.empty())
    {
        std::vector<uint8_t> pixels = std::move(m_FreeFrameBuffers.back());
        m_FreeFrameBuffers.pop_back();
        pixels.resize(static_cast<size_t>(dataSize));
        return pixels;
    }

    return std::vector<uint8_t>(static_cast<size_t>(dataSize));
}

// ===========================================================================
// RecycleFramePixelsLocked — 回收 RGBA 缓冲，减少逐帧堆分配和释放尖峰
// ===========================================================================
void VansVideoTexture::RecycleFramePixelsLocked(std::vector<uint8_t>& pixels)
{
    if (pixels.empty())
        return;

    const size_t expectedSize = static_cast<size_t>(m_Width) * static_cast<size_t>(m_Height) * 4u;
    if (expectedSize > 0 && pixels.capacity() >= expectedSize &&
        static_cast<int>(m_FreeFrameBuffers.size()) < MAX_FREE_FRAME_BUFFERS)
    {
        pixels.clear();
        m_FreeFrameBuffers.push_back(std::move(pixels));
        return;
    }

    std::vector<uint8_t>().swap(pixels);
}

// ===========================================================================
// ClearFramePixelPoolLocked — 释放队列和缓存池持有的 CPU 帧内存
// ===========================================================================
void VansVideoTexture::ClearFramePixelPoolLocked()
{
    m_FreeFrameBuffers.clear();
    m_FreeFrameBuffers.shrink_to_fit();
}

bool VansVideoTexture::CreateFrameUploadBuffer(int dataSize)
{
    if (!m_VkDevice || dataSize <= 0 || m_FrameUploadBufferReady)
        return m_FrameUploadBufferReady;

    const VkDeviceSize bufferSize =
        static_cast<VkDeviceSize>(dataSize) * static_cast<VkDeviceSize>(MAX_UPLOAD_SLOTS);
    const bool bufferCreated = m_FrameUploadBuffer.CreatVulkanBuffer(
            m_VkDevice->GetLogicDevice(),
            bufferSize,
            VK_FORMAT_R32_SFLOAT,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!bufferCreated || !m_FrameUploadBuffer.PersistentMap())
    {
        if (bufferCreated)
            m_FrameUploadBuffer.DestroyVulkanBuffer(m_VkDevice->GetLogicDevice());
        VANS_LOG_WARN("[VansVideoTexture] frame upload buffer unavailable; falling back to main-thread staging copy");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_QueueMutex);
        m_FreeUploadSlots.clear();
        m_FreeUploadSlots.reserve(MAX_UPLOAD_SLOTS);
        for (int i = MAX_UPLOAD_SLOTS - 1; i >= 0; --i)
            m_FreeUploadSlots.push_back(i);
    }
    m_FrameUploadBufferReady = true;
    return true;
}

int VansVideoTexture::AcquireUploadSlotLocked()
{
    if (!m_FrameUploadBufferReady || m_FreeUploadSlots.empty())
        return -1;

    int slot = m_FreeUploadSlots.back();
    m_FreeUploadSlots.pop_back();
    return slot;
}

void VansVideoTexture::RecycleUploadSlotLocked(int& uploadSlot)
{
    if (uploadSlot < 0)
        return;

    if (m_FrameUploadBufferReady &&
        static_cast<int>(m_FreeUploadSlots.size()) < MAX_UPLOAD_SLOTS)
    {
        m_FreeUploadSlots.push_back(uploadSlot);
    }
    uploadSlot = -1;
}

void VansVideoTexture::ClearUploadSlotsLocked()
{
    m_FreeUploadSlots.clear();
    m_FreeUploadSlots.shrink_to_fit();
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
	std::lock_guard<std::mutex> pendingLock(m_PendingFrameMutex);
    if (!targetArray || !m_HasNewFrame || m_LastFramePixels.empty())
        return false;

    bool ok = targetArray->UpdateArrayLayerFromPixels(
        cmd, m_LastFramePixels.data(), m_Width, m_Height, layerIndex);
    ConsumeNewFrame();
    return ok;
}

// ===========================================================================
// RecordNewFrameToArrayLayer — 将本帧新像素记录写入目标贴图数组层
// ===========================================================================
bool VansVideoTexture::RecordNewFrameToArrayLayer(VansTexture* targetArray,
                                                   VansVKCommandBuffer& cmd,
                                                   int layerIndex)
{
	std::lock_guard<std::mutex> pendingLock(m_PendingFrameMutex);
    if (!targetArray || !m_HasNewFrame || m_LastFramePixels.empty())
        return false;

    // 面光源视频优先复用已经上传到 GPU 的主视频纹理，避免同一帧再次走 CPU staging。
    // 若外部调用顺序未提前上传主视频纹理，这里补录一次主纹理上传。
    if (m_HasPendingUpload && !RecordPendingUploadLocked(cmd))
        return false;

    bool ok = false;
    {
        VANS_PROFILE_SCOPE("RectLightVideo::RecordGpuFrameCopy", Vans::ProfileCategory::Video);
        ok = targetArray->RecordArrayLayerCopyFromTexture(cmd, &m_GpuTexture, layerIndex);
    }

    if (!ok)
    {
        ok = targetArray->RecordArrayLayerUploadFromPixels(
            cmd, m_LastFramePixels.data(), m_Width, m_Height, layerIndex);
    }

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
// MediaCore 统一负责容器、codec、seek 与 RGBA 转换；这里保留播放状态、循环
// PTS、帧池、上传槽和有界队列。循环 PTS 单调递增，主线程无需重置播放时间。
// ===========================================================================
void VansVideoTexture::DecodeThreadFunc()
{
    if (!m_DecodeSession)
    {
        VANS_LOG_ERROR("[VansVideoTexture] DecodeThreadFunc: 媒体会话未初始化");
        return;
    }

    const int rgbaBufSize = m_Width * m_Height * 4;
    double    ptsOffset   = 0.0; // 循环时累加视频总时长，保持 PTS 单调递增

    while (!m_ShouldStop.load())
    {
		const double seekSeconds = m_SeekRequestSeconds.exchange(-1.0);
		if (seekSeconds >= 0.0)
		{
			ptsOffset = 0.0;
			std::string error;
			if (!m_DecodeSession->Seek(seekSeconds, error))
				VANS_LOG_WARN("[VansVideoTexture] 视频跳转失败: " << error);
		}
        // Stop() 请求：清空队列、复位媒体会话和 ptsOffset。
        if (m_NeedRestart.exchange(false))
        {
            {
                std::lock_guard<std::mutex> lock(m_QueueMutex);
                while (!m_FrameQueue.empty())
                {
                    VideoFrameData oldFrame = std::move(m_FrameQueue.front());
                    m_FrameQueue.pop();
                    RecycleFramePixelsLocked(oldFrame.pixels);
                    RecycleUploadSlotLocked(oldFrame.uploadSlot);
                }
            }
            ptsOffset = 0.0;
            std::string error;
            if (!m_DecodeSession->Reset(error))
                VANS_LOG_WARN("[VansVideoTexture] 视频复位失败: " << error);
        }

        // 暂停时不预解码，队列满时等待主线程消费。
        {
            std::unique_lock<std::mutex> lock(m_QueueMutex);
            m_ProducerCv.wait(lock, [this] {
                return (static_cast<int>(m_FrameQueue.size()) < MAX_QUEUE_SIZE
                        && m_Playing.load())
                    || m_ShouldStop.load()
                    || m_NeedRestart.load()
                    || m_SeekRequestSeconds.load() >= 0.0;
            });
            if (m_ShouldStop.load())
                break;
            if (m_NeedRestart.load() || m_SeekRequestSeconds.load() >= 0.0)
                continue;
        }

        VideoFrameData frame;
        {
            std::lock_guard<std::mutex> lock(m_QueueMutex);
            frame.pixels = AcquireFramePixelsLocked(rgbaBufSize);
            frame.uploadSlot = AcquireUploadSlotLocked();
        }

        std::string error;
        double presentationTime = 0.0;
        {
            VANS_PROFILE_SCOPE("Video::Decode.MediaFrame", Vans::ProfileCategory::Video);
            const VansEngine::VansMediaDecodeStatus status = m_DecodeSession->DecodeVideoFrame(
                frame.pixels,
                presentationTime,
                VansEngine::VansMediaEndPolicy::StopAtInputEnd,
                error);
            if (status == VansEngine::VansMediaDecodeStatus::EndOfStream)
            {
                std::lock_guard<std::mutex> lock(m_QueueMutex);
                RecycleFramePixelsLocked(frame.pixels);
                RecycleUploadSlotLocked(frame.uploadSlot);
                if (!m_Loop || m_ShouldStop.load())
                    break;

                ptsOffset += (m_VideoDuration > 0.0 ? m_VideoDuration : 1.0);
                if (!m_DecodeSession->Reset(error))
                {
                    VANS_LOG_ERROR("[VansVideoTexture] 循环复位失败: " << error);
                    break;
                }
                continue;
            }
            if (status == VansEngine::VansMediaDecodeStatus::Failed)
            {
                std::lock_guard<std::mutex> lock(m_QueueMutex);
                RecycleFramePixelsLocked(frame.pixels);
                RecycleUploadSlotLocked(frame.uploadSlot);
                VANS_LOG_ERROR("[VansVideoTexture] 视频帧解码失败: " << error);
                break;
            }
        }

        if (m_ShouldStop.load() || m_NeedRestart.load() ||
            m_SeekRequestSeconds.load() >= 0.0)
        {
            std::lock_guard<std::mutex> lock(m_QueueMutex);
            RecycleFramePixelsLocked(frame.pixels);
            RecycleUploadSlotLocked(frame.uploadSlot);
            continue;
        }

        frame.pts = ptsOffset + presentationTime;
        if (frame.uploadSlot >= 0)
        {
            VANS_PROFILE_SCOPE("Video::Decode.StageUploadMemcpy", Vans::ProfileCategory::Video);
            const VkDeviceSize uploadOffset =
                static_cast<VkDeviceSize>(frame.uploadSlot) * static_cast<VkDeviceSize>(rgbaBufSize);
            m_FrameUploadBuffer.UpdateMapped(frame.pixels.data(), uploadOffset, rgbaBufSize);
        }

        {
            VANS_PROFILE_SCOPE("Video::Decode.QueuePush", Vans::ProfileCategory::Video);
            std::lock_guard<std::mutex> lock(m_QueueMutex);
            m_FrameQueue.push(std::move(frame));
            m_ConsumerCv.notify_one();
        }
    }
}

} // namespace VansGraphics
