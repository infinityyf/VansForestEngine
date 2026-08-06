#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>

// Keep FFmpeg C headers out of this public header.
struct AVFormatContext;
struct AVCodecContext;
struct SwrContext;

namespace VansEngine
{
    // One decoded PCM block in interleaved signed 16-bit format.
    struct AudioPCMChunk
    {
        std::vector<int16_t> samples;       // samples.size() = numSamples * channels
        int                  channels    = 2;
        int                  sampleRate  = 48000;
        bool                 endOfStream = false;
    };

    // FFmpeg-backed audio decoder.
    //
    // Supported formats depend on the bundled FFmpeg build.
    // Output is always interleaved signed 16-bit PCM.
    //
    // Threading:
    //   Open() and Close() are lifecycle operations.
    //   DecodeNextChunk() and Reset() serialize access to FFmpeg state internally.
    class VansAudioDecoder
    {
    public:
        VansAudioDecoder()  = default;
        ~VansAudioDecoder();

        VansAudioDecoder(const VansAudioDecoder&)            = delete;
        VansAudioDecoder& operator=(const VansAudioDecoder&) = delete;

        // Open or close an audio file.
        // filePath          : absolute audio file path
        // targetChannels    : output channel count, -1 keeps source count
        // targetSampleRate  : output sample rate, -1 keeps source rate
        bool Open(const std::string& filePath,
                  int targetChannels   = 2,
                  int targetSampleRate = 48000);

        void Close();

        // Decode the next streaming block, roughly 4096 samples per channel.
        AudioPCMChunk DecodeNextChunk();

        // Decode the whole file at once; intended for short static sounds.
        std::vector<int16_t> DecodeAll(int& outChannels, int& outSampleRate);

        // Seek back to the start for loop playback.
        bool Reset();

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
        double m_Duration         = 0.0;
        std::string m_FilePath;
        std::mutex m_DecodeMutex;
    };

} // namespace VansEngine
