#pragma once
#include "VansTexture.h"
#include "VansVKDevice.h"
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

// FFmpeg 前向声明，避免将 C 头文件暴露到整个引擎
struct AVFormatContext;
struct AVCodecContext;
struct SwsContext;

namespace VansGraphics
{
    // ── 帧数据：RGBA8 像素 + 显示时间戳 ─────────────────────────────────────
    struct VideoFrameData
    {
        std::vector<uint8_t> pixels; // RGBA8，大小 = width * height * 4
        double               pts    = 0.0; // 显示时间戳（秒，循环时已加偏移）
    };

    // ===========================================================================
    // VansVideoTexture — CPU 解码（FFmpeg）+ Vulkan 逐帧上传
    //
    // 使用方式：
    //   1. Open()        — 打开文件，解码首帧初始化 GPU 纹理，启动后台解码线程
    //   2. Tick(dt)      — 每帧调用：推进播放时间，挑选本帧应显示的新帧
    //   3. RecordPendingUpload(cmd) — 在渲染命令录制阶段将新帧上传到 GPU
    //   4. GetTexture()  — 获取底层 VansTexture*（可用于绑定到 PBR 材质）
    //   5. Close()       — 停止后台线程，释放所有 FFmpeg 资源
    //
    // 线程模型：
    //   - 后台解码线程负责 av_read_frame → avcodec_receive_frame → sws_scale，
    //     结果写入 m_FrameQueue（mutex 保护）。
    //   - 主线程 Tick() 从 m_FrameQueue 取帧并缓存像素，GPU 上传延迟到
    //     RecordPendingUpload()，合并进当前帧图形命令缓冲，避免同步 submit/wait。
    // ===========================================================================
    class VansVideoTexture
    {
    public:
        VansVideoTexture()  = default;
        ~VansVideoTexture();

        // ── 打开 / 关闭 ──────────────────────────────────────────────────────
        // filePath  : 视频文件的绝对路径
        // loop      : 是否循环播放
        // autoPlay  : 打开后立即开始播放，false 则需手动调用 Play()
        // isSrgb    : 图像颜色空间（true = VK_FORMAT_R8G8B8A8_SRGB）
        bool Open(VansVKDevice* device,
                  const std::string& filePath,
                  bool loop     = true,
                  bool autoPlay = true,
                  bool isSrgb   = true);

        void Close();

        // ── 每帧驱动 ─────────────────────────────────────────────────────────
        // deltaTime : 本帧耗时（秒），与 VansTimer::GetLastFrameDelta() 一致
        // 返回 true 表示本帧选中了新像素，等待后续 RecordPendingUpload() 上传到 GPU
        bool Tick(double deltaTime);

        // ── 播放控制 ─────────────────────────────────────────────────────────
        void Play()  { m_Playing.store(true);  m_ProducerCv.notify_all(); }
        void Pause() { m_Playing.store(false); m_ProducerCv.notify_all(); }
        // Stop — 停止播放并将解码位置重置到视频起点
        // 下次调用 Play() 时将从第 0 帧开始播放
        void Stop();
        bool IsPlaying() const { return m_Playing.load(); }
        bool IsReady()   const { return m_IsReady.load();  }

        // ── GPU 资源访问 ──────────────────────────────────────────────────────
        // VkImageView / VkSampler 在 Open → Close 整个生命周期内保持不变，
        // 可在场景加载时一次性写入 Bindless 描述符集，无需每帧更新。
        VkImageView  GetImageView();
        VkSampler    GetSampler();
        VansTexture* GetTexture() { return &m_GpuTexture; }

        int GetWidth()  const { return m_Width;  }
        int GetHeight() const { return m_Height; }

        // 在已 Begin 的图形 command buffer 中记录待上传视频帧。
        // 返回 true 表示本次记录了 GPU 上传命令。
        bool RecordPendingUpload(VansVKCommandBuffer& cmd);

        // ── 统一 GPU 数据接口（供面光源等消费方使用）────────────────────────────
        // 若本帧有新像素，将其写入目标贴图数组的指定层并消费新帧标志。
        // 返回 true 表示本次执行了写入；消费方无需直接访问内部 CPU 像素缓存。
        bool CopyNewFrameToArrayLayer(VansTexture* targetArray,
                                      VansVKCommandBuffer& cmd,
                                      int layerIndex);

		// 在当前帧图形命令缓冲中记录新帧写入数组层，不独立提交或等待 fence。
		bool RecordNewFrameToArrayLayer(VansTexture* targetArray,
			VansVKCommandBuffer& cmd,
			int layerIndex);

    private:
        // ── 后台解码线程函数 ─────────────────────────────────────────────────
        void DecodeThreadFunc();

        // ── 将像素数据上传到 GPU（仅主线程录制阶段调用）────────────────────
        bool RecordFrameUpload(VansVKCommandBuffer& cmd, const uint8_t* pixels, int dataSize);

        // ── 最后一帧 CPU 像素缓存内部访问器（仅供 CopyNewFrameToArrayLayer 使用）──
        bool HasNewFrame() const { return m_HasNewFrame; }
        void ConsumeNewFrame()   { m_HasNewFrame = false; }

    private:
        // ── FFmpeg 解码上下文 ──────────────────────────────────────────────
        AVFormatContext* m_FmtCtx      = nullptr;
        AVCodecContext*  m_CodecCtx    = nullptr;
        SwsContext*      m_SwsCtx      = nullptr;
        int              m_VideoStream = -1;
        double           m_TimeBase    = 0.0;    // av_q2d(stream->time_base)
        double           m_VideoDuration = 0.0;  // 视频总时长（秒），用于循环 PTS 偏移
        std::string      m_FilePath;

        // ── 视频属性 ────────────────────────────────────────────────────────
        int  m_Width  = 0;
        int  m_Height = 0;
        bool m_Loop   = true;
        bool m_IsSrgb = true;

        // ── 播放状态 ────────────────────────────────────────────────────────
        std::atomic<bool> m_Playing  = { false };
        std::atomic<bool> m_IsReady  = { false };
        double            m_PlayTime = 0.0; // 当前播放时间（秒），仅主线程写

        // ── GPU 贴图 ─────────────────────────────────────────────────────────
        VansTexture   m_GpuTexture;
        VansVKDevice* m_VkDevice = nullptr;

        // ── 帧队列（生产者：解码线程 → 消费者：主线程）──────────────────────
        static constexpr int     MAX_QUEUE_SIZE = 4;
        std::queue<VideoFrameData> m_FrameQueue;
        std::mutex                 m_QueueMutex;
        std::condition_variable    m_ProducerCv; // 生产者等待队列有空位
        std::condition_variable    m_ConsumerCv; // 消费者等待队列有帧（预留）

        // ── 最后一帧像素缓存（主线程专用）────────────────────────────────────
        std::vector<uint8_t> m_LastFramePixels;
        bool                 m_HasNewFrame = false;
        bool                 m_HasPendingUpload = false;

        // ── 后台线程 ─────────────────────────────────────────────────────────
        std::thread       m_DecodeThread;
        std::atomic<bool> m_ShouldStop   = { false };
        // 由 Stop() 设置，通知解码线程执行 seek 回起点并重置 ptsOffset
        std::atomic<bool> m_NeedRestart  = { false };
    };

} // namespace VansGraphics
