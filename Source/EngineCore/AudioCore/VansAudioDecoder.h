#pragma once
#include <string>
#include <vector>
#include <cstdint>

// FFmpeg 前向声明，避免将 C 头文件暴露到整个引擎（与 VansVideoTexture.h 策略一致）
struct AVFormatContext;
struct AVCodecContext;
struct SwrContext;

namespace VansEngine
{
    // ===========================================================================
    // AudioPCMChunk — 单次解码产出的 PCM 块（交错 16-bit 有符号整型）
    // ===========================================================================
    struct AudioPCMChunk
    {
        std::vector<int16_t> samples;       // 交错 PCM，大小 = numSamples * channels
        int                  channels    = 2;
        int                  sampleRate  = 48000;
        bool                 endOfStream = false;
    };

    // ===========================================================================
    // VansAudioDecoder — 基于 FFmpeg 的音频解码器
    //
    // 支持格式：MP3 / OGG / WAV / FLAC / AAC 等 FFmpeg 支持的所有音频容器。
    // 输出格式：固定输出交错 16-bit 有符号整型 PCM（AV_SAMPLE_FMT_S16）。
    //
    // 线程安全说明：
    //   Open() / Close() 必须在创建/销毁线程中调用。
    //   DecodeNextChunk() 设计为在后台线程中循环调用（流式模式）。
    //   Reset() 必须在外部加锁后再调用（用于循环播放的回绕操作）。
    // ===========================================================================
    class VansAudioDecoder
    {
    public:
        VansAudioDecoder()  = default;
        ~VansAudioDecoder();

        // 禁止拷贝，允许移动（有后台线程时不应移动，此处仅声明）
        VansAudioDecoder(const VansAudioDecoder&)            = delete;
        VansAudioDecoder& operator=(const VansAudioDecoder&) = delete;

        // ── 打开 / 关闭 ──────────────────────────────────────────────────────
        // filePath          : 音频文件绝对路径
        // targetChannels    : 输出声道数，-1 = 保持原始
        // targetSampleRate  : 输出采样率，-1 = 保持原始
        bool Open(const std::string& filePath,
                  int targetChannels   = 2,
                  int targetSampleRate = 48000);

        void Close();

        // ── 流式解码 ─────────────────────────────────────────────────────────
        // 解码下一块 PCM（约 4096 样本/通道）；设计为在后台线程中循环调用。
        // 返回 endOfStream=true 的 chunk 表示文件已读完。
        AudioPCMChunk DecodeNextChunk();

        // ── 静态解码 ─────────────────────────────────────────────────────────
        // 一次性解码全部 PCM（短音效专用，≤ 10 秒建议使用此方法）。
        // 返回空 vector 表示失败。
        std::vector<int16_t> DecodeAll(int& outChannels, int& outSampleRate);

        // ── 回绕 ─────────────────────────────────────────────────────────────
        // seek 到文件开头，用于循环播放；调用前必须持有外部锁。
        bool Reset();

        // ── 状态查询 ─────────────────────────────────────────────────────────
        bool   IsOpen()        const { return m_FmtCtx != nullptr; }
        int    GetChannels()   const { return m_TargetChannels;    }
        int    GetSampleRate() const { return m_TargetSampleRate;  }
        double GetDuration()   const { return m_Duration;          }

    private:
        AVFormatContext* m_FmtCtx         = nullptr;
        AVCodecContext*  m_CodecCtx        = nullptr;
        SwrContext*      m_SwrCtx          = nullptr;
        int              m_AudioStream     = -1;

        int    m_TargetChannels   = 2;
        int    m_TargetSampleRate = 48000;
        double m_Duration         = 0.0;   // 音频总时长（秒）
        std::string m_FilePath;
    };

} // namespace VansEngine
