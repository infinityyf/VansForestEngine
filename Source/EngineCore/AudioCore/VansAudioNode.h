#pragma once
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <cstdint>
#include "VansAudioAttenuation.h"
#include "VansAudioBus.h"
#include "VansAudioDirectionality.h"
#include "VansAudioOcclusion.h"
#include "../VansNode.h"

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
        std::string    m_Name;               // Runtime name generated from the asset record.
        std::string    m_FilePath;           // Resolved audio file path.
        AudioPlayMode  m_PlayMode   = AudioPlayMode::Static;
        bool           m_Loop       = false;
        bool           m_AutoPlay   = false;
        float          m_Volume     = 1.0f;  // [0, 1]
        float          m_Pitch      = 1.0f;  // > 0
        bool           m_Spatial    = false; // 是否启用 3D 空间音效
        float          m_RefDist    = 1.0f;  // 参考距离（衰减计算起点）
        float          m_MaxDist    = 100.0f;// 最大距离（超过此距离音量归零）
        float          m_RollOff    = 1.0f;  // 滚降因子（仅 Spatial=true 时有效）
        std::string    m_AttenuationMode = "linear";
        float          m_ReverbSend = 0.0f;  // [0, 1], 0 = dry only
        std::string    m_BusName = "SFX";
        float          m_LowpassHighFrequencyGain = 1.0f; // [0, 1], 1 = full bandwidth
    };

    // ===========================================================================
    // VansAudioNode — 一个音频资源的完整播放单元
    //
    // Each node corresponds to one generated audio asset descriptor.
    // 一个 OpenAL Source + 若干 OpenAL Buffer 被封装在此处。
    //
    // 线程安全说明：
    //   - Open() / Close() / Tick() 在主线程调用
    //   - Streaming 模式的后台解码线程只访问 m_Decoder 和 m_PCMQueue
    //   - Play/Pause/Stop/Resume 在主线程调用，通过 m_SourceMutex 保护
    // ===========================================================================
    class VansAudioDecoder;   // 前向声明，避免循环包含

    class VansAudioNode : public VansGraphics::VansNode
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

	protected:
		void OnDisable() override { Pause(); }

	public:
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
        void  SetRolloff(float rolloff);
        void  SetAttenuationMode(AudioAttenuationMode mode);
        void  SetReverbSend(float send);
        void  SetLowpassHighFrequencyGain(float highFrequencyGain);
        void  SetBusName(const std::string& busName);
        void  SetBusGain(float gain);
        void  SetBusLowpassHighFrequencyGain(float highFrequencyGain);
        void  SetVirtualizationGain(float gain);
        float GetRefDist()  const { return m_Properties.m_RefDist; }
        float GetMaxDist()  const { return m_Properties.m_MaxDist; }
        float GetRolloff()  const { return m_Properties.m_RollOff; }
        float GetReverbSend() const { return m_Properties.m_ReverbSend; }
        float GetLowpassHighFrequencyGain() const { return m_Properties.m_LowpassHighFrequencyGain; }
        const std::string& GetBusName() const { return m_Properties.m_BusName; }
        float GetBusGain() const { return m_BusGain; }
        float GetVirtualizationGain() const { return m_VirtualizationGain; }
        AudioAttenuationMode GetAttenuationMode() const { return m_AttenuationModeRuntime; }
        // 按当前衰减模型更新距离 gain。调用方只提供 Listener 位置，避免散落公式。
        void  UpdateDistanceGain(float listenerX, float listenerY, float listenerZ);
        // 手动设置距离衰减 gain，后续会与基础音量 / bus / occlusion 统一提交。
        void  SetSpatialGain(float distanceGain);
        // 查询 OpenAL source 的实际 AL_SOURCE_RELATIVE 状态（用于诊断）
        void  SetOcclusion(float gain, float highFrequencyGain);
        void  SetVelocity(float x, float y, float z);
        void  SetDirection(float x, float y, float z);
        void  SetCone(AudioConeSettings settings);
        int   GetALSourceRelative() const;

        // ── 状态查询 ────────────────────────────────────────────────────────
        bool IsPlaying() const;
        bool IsPaused()  const;
        bool IsBound()   const { return m_SourceId != 0 || m_HardwareVoiceSuspended; }
        bool IsHardwareVoiceActive() const { return m_SourceId != 0; }

        const std::string& GetName()     const { return m_Properties.m_Name;     }
        const std::string& GetFilePath() const { return m_Properties.m_FilePath; }
        bool               IsAutoPlay()  const { return m_Properties.m_AutoPlay; }
        const AudioNodeProperties& GetProperties() const { return m_Properties; }
        bool CanCreateStaticInstance() const;
        uint32_t GetStaticBufferId() const { return m_StaticBufferId; }

        // ── 每帧驱动（Streaming 模式需要主线程调用） ─────────────────────────
        // 检查 OpenAL Source 状态，向 Source 补充已处理完的 Buffer
        void Tick();

    private:
        // ── Static 模式辅助 ─────────────────────────────────────────────────
        bool OpenStatic();

        // ── Streaming 模式辅助 ──────────────────────────────────────────────
        bool EnsureStreamingReady();
        bool OpenStreaming();
        void SuspendHardwareVoice();
        bool ResumeHardwareVoice();
        void StartDecodeThread();
        void StopDecodeThread();
        void DecodeThreadFunc();          // 后台解码线程入口
        void RefillStreamBuffers();       // 主线程：将 PCM 队列中的数据上传到空闲 Buffer
        int QueueStreamBuffersFromPCMQueue(int maxBuffers);

        // ── OpenAL 格式计算 ──────────────────────────────────────────────────
        static int32_t GetAlFormat(int channels); // 返回 ALenum(内联值)
        void ApplySpatialProperties();
        void ApplyDirectionalProperties();
        void ApplyEffectSends();
        void ApplyDirectLowpass();
        void CommitGain();

    private:
        AudioNodeProperties m_Properties;
        AudioAttenuationMode m_AttenuationModeRuntime = AudioAttenuationMode::Linear;
        float m_PositionX = 0.0f;
        float m_PositionY = 0.0f;
        float m_PositionZ = 0.0f;
        float m_VelocityX = 0.0f;
        float m_VelocityY = 0.0f;
        float m_VelocityZ = 0.0f;
        float m_DirectionX = 0.0f;
        float m_DirectionY = 0.0f;
        float m_DirectionZ = 1.0f;
        AudioConeSettings m_ConeSettings;
        float m_DistanceGain = 1.0f;
        float m_OcclusionGain = 1.0f;
        float m_BusGain = 1.0f;
        float m_BusLowpassHighFrequencyGain = 1.0f;
        float m_VirtualizationGain = 1.0f;
        float m_LastCommittedGain = -1.0f;
        uint32_t m_ReverbSendFilterId = 0;
        uint32_t m_DirectLowpassFilterId = 0;
        float m_OcclusionHighFrequencyGain = 1.0f;

        // OpenAL 对象（ALuint 底层为 unsigned int）
        uint32_t m_SourceId = 0;                         // alGenSources() 返回值

        // Static 模式：单个 Buffer
        uint32_t m_StaticBufferId = 0;                   // alGenBuffers() 返回值

        // Streaming 模式：滚动缓冲池
        static constexpr int STREAM_BUFFER_COUNT = 4;
        static constexpr int STREAM_CHUNK_SAMPLES = 8192; // 每次填充的样本数（每通道）
        uint32_t m_StreamBuffers[STREAM_BUFFER_COUNT] = {};
        bool m_StreamingReady = false;
        bool m_StreamingInitFailed = false;
        bool m_HardwareVoiceSuspended = false;
        bool m_LogicalPlaying = false;
        bool m_LogicalPaused = false;

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
