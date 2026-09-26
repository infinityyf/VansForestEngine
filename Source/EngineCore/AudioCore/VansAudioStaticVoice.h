#pragma once

#include "VansAudioVoice.h"

#include <cstdint>

namespace VansEngine
{
    class VansAudioStaticVoice final : public VansAudioVoice
    {
    public:
        VansAudioStaticVoice() = default;
        ~VansAudioStaticVoice() override;

        bool Open(std::uint32_t sharedBufferId, const VansAudioProperties& properties);
        void Close();

        VansAudioVoiceKind GetKind() const override { return VansAudioVoiceKind::Static; }
        bool IsBound() const override { return m_SharedBufferId != 0; }
        void Play() override;
        void Pause() override;
        void Stop() override;
        void Resume() override;
        bool Seek(double seconds) override;
        double GetPlaybackOffsetSeconds() const override;
        bool IsPlaying() const override;
        bool IsPaused() const override;

    private:
        void OnVirtualizationChanged() override;
        bool AcquireHardwareVoice();
        void ReleaseHardwareVoice();

        std::uint32_t m_SharedBufferId = 0;
        float m_PlaybackOffsetSeconds = 0.0f;
        bool m_LogicalPlaying = false;
        bool m_LogicalPaused = false;
    };
}
