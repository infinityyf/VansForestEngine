#pragma once
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <cstdint>
#include <memory>
#include "VansAudioVoice.h"
#include "VansAudioOcclusion.h"
#include "../VansNode.h"

// Avoid including OpenAL and FFmpeg headers from this public header.
// ALuint is represented by uint32_t here; OpenAL 1.1 defines it as unsigned int.

namespace VansEngine
{
    // Playback unit for one audio asset.
    //
    // Each node wraps one OpenAL source and either one static buffer or a streaming buffer ring.
    // Open(), Close(), Tick(), and transport controls are expected to run on the main thread.
    class VansAudioDecoder;

    class VansAudioNode : public VansGraphics::VansNode, public VansAudioVoice
    {
    public:
        VansAudioNode();
        ~VansAudioNode();

        VansAudioNode(const VansAudioNode&)            = delete;
        VansAudioNode& operator=(const VansAudioNode&) = delete;

        bool Open(const VansAudioProperties& props);
        void Close();

        VansAudioVoiceKind GetKind() const override
        {
            return m_Properties.m_PlayMode == VansAudioPlayMode::Streaming
                ? VansAudioVoiceKind::Streaming
                : VansAudioVoiceKind::Static;
        }
        bool IsBound() const override { return m_SourceId != 0 || m_HardwareVoiceSuspended; }

        void Play() override;
        void Pause() override;

    protected:
        void OnDisable() override { Pause(); }

    public:
        void Stop() override;
        void Resume() override;
        bool Seek(double seconds) override;
        double GetPlaybackOffsetSeconds() const override;
        bool IsPlaying() const override;
        bool IsPaused() const override;

        bool CanCreateStaticVoice() const;
        uint32_t GetStaticBufferId() const { return m_StaticBufferId; }

        void Tick() override;

    private:
        bool OpenStatic();
        bool EnsureStreamingReady();
        bool OpenStreaming();
        void SuspendHardwareVoice();
        bool ResumeHardwareVoice();
        void OnVirtualizationChanged() override;
        void StartDecodeThread();
        void StopDecodeThread();
        void DecodeThreadFunc();
        void RefillStreamBuffers();
        int QueueStreamBuffersFromPCMQueue(int maxBuffers);

        static int32_t GetAlFormat(int channels);
    private:
        uint32_t m_StaticBufferId = 0;

        static constexpr int STREAM_BUFFER_COUNT = 4;
        static constexpr int STREAM_CHUNK_SAMPLES = 8192;
        uint32_t m_StreamBuffers[STREAM_BUFFER_COUNT] = {};
        bool m_StreamingReady = false;
        bool m_StreamingInitFailed = false;
        bool m_HardwareVoiceSuspended = false;
        bool m_LogicalPlaying = false;
        bool m_LogicalPaused = false;

        std::unique_ptr<VansAudioDecoder> m_Decoder;
        std::mutex                        m_DecoderMutex;
        std::queue<std::vector<int16_t>>  m_PCMQueue;
        std::mutex                        m_PCMQueueMtx;
        std::condition_variable           m_PCMQueueCv;
        std::thread                       m_DecodeThread;
        std::atomic<bool>                 m_StopDecode{ false };
        std::atomic<bool>                 m_DecodeEOF { false };

        mutable std::mutex m_SourceMutex;
    };

} // namespace VansEngine
