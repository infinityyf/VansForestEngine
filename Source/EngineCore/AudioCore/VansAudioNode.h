#pragma once
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <cstdint>

// 不在头文件中引入 OpenAL / FFmpeg 头（用无符号整型替代 ALuint）
// ALuint 的底层类型在所有平台均为 unsigned int（OpenAL 1.1 规范保证）

namespace VansEngine
{
    // ===========================================================================
    // AudioPlayMode — 决定是否将全部 PCM 解码到内存（Static）或流式读取（Streaming）
    // ===========================================================================
    enum class AudioPlayMode
    {
        Static,     // 短音效（≤ 10 秒），全量解码后一次性载入 OpenAL Buffer
        Streaming   // 长音乐 / 环境音，后台线程实时解码 + 滚动缓冲
    };

    // ===========================================================================
    // AudioNodeProperties — 构造 VansAudioNode 所需的配置参数
    // ===========================================================================
    struct AudioNodeProperties
    {
        std::string    m_Name;               // 引擎内唯一标识，来自 resource.json["audio"][i]["name"]
        std::string    m_FilePath;           // 音频文件绝对路径（assetPrefix + "/" + relPath）
        AudioPlayMode  m_PlayMode   = AudioPlayMode::Static;
        bool           m_Loop       = false;
        bool           m_AutoPlay   = false;
        float          m_Volume     = 1.0f;  // [0, 1]
        float          m_Pitch      = 1.0f;  // > 0
        bool           m_Spatial    = false; // 是否启用 3D 空间音效
        float          m_RefDist    = 1.0f;  // 参考距离（衰减计算起点）
        float          m_MaxDist    = 100.0f;// 最大距离（超过此距离音量归零）
        float          m_RollOff    = 1.0f;  // 滚降因子（仅 Spatial=true 时有效）
    };

    // ===========================================================================
    // VansAudioNode — 一个音频资源的完整播放单元
    //
    // 每个 VansAudioNode 对应 resource.json["audio"] 数组中的一条记录。
    // 一个 OpenAL Source + 若干 OpenAL Buffer 被封装在此处。
    //
    // 线程安全说明：
    //   - Open() / Close() / Tick() 在主线程调用
    //   - Streaming 模式的后台解码线程只访问 m_Decoder 和 m_PCMQueue
    //   - Play/Pause/Stop/Resume 在主线程调用，通过 m_SourceMutex 保护
    // ===========================================================================
    class VansAudioDecoder;   // 前向声明，避免循环包含

    class VansAudioNode
    {
    public:
        VansAudioNode()  = default;
        ~VansAudioNode();

        VansAudioNode(const VansAudioNode&)            = delete;
        VansAudioNode& operator=(const VansAudioNode&) = delete;

        // ── 资源管理 ────────────────────────────────────────────────────────
        // Open 调用后音频资源被载入（Static=立即解码；Streaming=打开解码器）
        bool Open(const AudioNodeProperties& props);
        void Close();

        // ── 播放控制（主线程） ───────────────────────────────────────────────
        void Play();
        void Pause();
        void Stop();
        void Resume();

        // ── 实时参数 ────────────────────────────────────────────────────────
        void  SetVolume(float gain);
        float GetVolume()  const { return m_Properties.m_Volume; }

        void  SetPitch(float pitch);
        float GetPitch()   const { return m_Properties.m_Pitch; }

        void  SetLoop(bool loop);
        bool  GetLoop()    const { return m_Properties.m_Loop; }

        // ── 空间化 ───────────────────────────────────────────────────────────
        // position : 声源世界坐标（每帧由 VansAudioManager 根据绑定对象 Transform 同步）
        void  SetPosition(float x, float y, float z);
        void  SetSpatial(bool enabled);
        bool  GetSpatial() const { return m_Properties.m_Spatial; }

        void  SetRefDistance(float d);
        void  SetMaxDistance(float d);

        // ── 状态查询 ────────────────────────────────────────────────────────
        bool IsPlaying() const;
        bool IsPaused()  const;
        bool IsBound()   const { return m_SourceId != 0; }

        const std::string& GetName()     const { return m_Properties.m_Name;     }
        const std::string& GetFilePath() const { return m_Properties.m_FilePath; }

        // ── 每帧驱动（Streaming 模式需要主线程调用） ─────────────────────────
        // 检查 OpenAL Source 状态，向 Source 补充已处理完的 Buffer
        void Tick();

    private:
        // ── Static 模式辅助 ─────────────────────────────────────────────────
        bool OpenStatic();

        // ── Streaming 模式辅助 ──────────────────────────────────────────────
        bool OpenStreaming();
        void StartDecodeThread();
        void StopDecodeThread();
        void DecodeThreadFunc();          // 后台解码线程入口
        void RefillStreamBuffers();       // 主线程：将 PCM 队列中的数据上传到空闲 Buffer

        // ── OpenAL 格式计算 ──────────────────────────────────────────────────
        static int32_t GetAlFormat(int channels); // 返回 ALenum(内联值)

    private:
        AudioNodeProperties m_Properties;

        // OpenAL 对象（ALuint 底层为 unsigned int）
        uint32_t m_SourceId = 0;                         // alGenSources() 返回值

        // Static 模式：单个 Buffer
        uint32_t m_StaticBufferId = 0;                   // alGenBuffers() 返回值

        // Streaming 模式：滚动缓冲池
        static constexpr int STREAM_BUFFER_COUNT = 4;
        static constexpr int STREAM_CHUNK_SAMPLES = 8192; // 每次填充的样本数（每通道）
        uint32_t m_StreamBuffers[STREAM_BUFFER_COUNT] = {};

        // Streaming 后台解码线程资源
        std::unique_ptr<VansAudioDecoder> m_Decoder;
        std::queue<std::vector<int16_t>>  m_PCMQueue;    // 待上传 PCM 块
        std::mutex                        m_PCMQueueMtx;
        std::condition_variable           m_PCMQueueCv;
        std::thread                       m_DecodeThread;
        std::atomic<bool>                 m_StopDecode{ false };
        std::atomic<bool>                 m_DecodeEOF { false }; // 解码线程已到达文件末尾

        // 用于保护 alSource 操作（Play/Pause/Stop）与 Tick 之间的并发
        mutable std::mutex m_SourceMutex;
    };

} // namespace VansEngine
