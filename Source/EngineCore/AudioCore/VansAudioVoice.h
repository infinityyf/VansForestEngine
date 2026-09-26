#pragma once

#include "VansAudioAttenuation.h"
#include "VansAudioBus.h"
#include "VansAudioDirectionality.h"

#include <cstdint>
#include <string>

namespace VansEngine
{
    enum class VansAudioSourceAcquireStatus;

    enum class VansAudioPlayMode
    {
        Static,
        Streaming
    };

    enum class VansAudioVoiceKind
    {
        Static,
        Streaming
    };

    inline const char* VansAudioVoiceKindName(VansAudioVoiceKind kind)
    {
        return kind == VansAudioVoiceKind::Streaming ? "Streaming" : "Static";
    }

    struct VansAudioProperties
    {
        std::string    m_Name;
        std::string    m_FilePath;
        VansAudioPlayMode  m_PlayMode = VansAudioPlayMode::Static;
        bool           m_Loop = false;
        bool           m_AutoPlay = false;
        float          m_Volume = 1.0f;
        float          m_Pitch = 1.0f;
        bool           m_Spatial = false;
        float          m_StereoPan = 0.0f;
        float          m_RefDist = 1.0f;
        float          m_MaxDist = 100.0f;
        float          m_RollOff = 1.0f;
        std::string    m_AttenuationMode = "linear";
        float          m_ReverbSend = 0.0f;
        std::string    m_BusName = "SFX";
        float          m_LowpassHighFrequencyGain = 1.0f;
    };

    // One runtime playback voice. This class is the sole owner of common
    // OpenAL source state, spatialization, attenuation, EFX, and gain mixing.
    // Derived classes own only static-buffer or streaming transport behavior.
    class VansAudioVoice
    {
    public:
        virtual ~VansAudioVoice() = default;

        VansAudioVoice(const VansAudioVoice&) = delete;
        VansAudioVoice& operator=(const VansAudioVoice&) = delete;

        virtual VansAudioVoiceKind GetKind() const = 0;
        virtual bool IsBound() const = 0;
        bool IsHardwareVoiceActive() const { return m_SourceId != 0; }

        virtual void Play() = 0;
        virtual void Pause() = 0;
        virtual void Stop() = 0;
        virtual void Resume() = 0;
        virtual bool Seek(double seconds) = 0;
        virtual double GetPlaybackOffsetSeconds() const = 0;
        virtual bool IsPlaying() const = 0;
        virtual bool IsPaused() const = 0;
        virtual void Tick() {}

        void SetPlaybackEnabled(bool enabled);
        void SetPosition(float x, float y, float z);
        void SetSpatial(bool enabled);
        bool GetSpatial() const { return m_Properties.m_Spatial; }
        void SetStereoPan(float pan);
        float GetStereoPan() const { return m_Properties.m_StereoPan; }
        void UpdateDistanceGain(float listenerX, float listenerY, float listenerZ);

        void SetVolume(float gain);
        float GetVolume() const { return m_Properties.m_Volume; }
        void SetPitch(float pitch);
        float GetPitch() const { return m_Properties.m_Pitch; }
        void SetLoop(bool loop);
        bool GetLoop() const { return m_Properties.m_Loop; }
        // Distance settings feed only the CPU ComputeDistanceGain path.
        void SetRefDistance(float distance);
        float GetRefDistance() const { return m_Properties.m_RefDist; }
        void SetMaxDistance(float distance);
        float GetMaxDistance() const { return m_Properties.m_MaxDist; }
        void SetRolloff(float rolloff);
        float GetRolloff() const { return m_Properties.m_RollOff; }
        void SetAttenuationMode(AudioAttenuationMode mode);
        AudioAttenuationMode GetAttenuationMode() const { return m_AttenuationMode; }
        void SetReverbSend(float send);
        float GetReverbSend() const { return m_Properties.m_ReverbSend; }
        void SetLowpassHighFrequencyGain(float gain);
        float GetLowpassHighFrequencyGain() const { return m_Properties.m_LowpassHighFrequencyGain; }
        void SetBusName(const std::string& busName);
        const std::string& GetBusName() const { return m_Properties.m_BusName; }
        void SetBusGain(float gain);
        float GetBusGain() const { return m_BusGain; }
        void SetBusLowpassHighFrequencyGain(float gain);
        void SetVirtualizationGain(float gain);
        float GetVirtualizationGain() const { return m_VirtualizationGain; }
        void SetOcclusion(float gain, float highFrequencyGain);
        void SetVelocity(float x, float y, float z);
        void SetDirection(float x, float y, float z);
        void SetCone(AudioConeSettings settings);

        const std::string& GetName() const { return m_Properties.m_Name; }
        const std::string& GetFilePath() const { return m_Properties.m_FilePath; }
        bool IsAutoPlay() const { return m_Properties.m_AutoPlay; }
        const VansAudioProperties& GetProperties() const { return m_Properties; }

    protected:
        VansAudioVoice() = default;

        void InitializeVoice(const VansAudioProperties& properties);
        VansAudioSourceAcquireStatus AcquireSource();
        void ReleaseSource();
        void SetSpatialGain(float gain);
        virtual void OnVirtualizationChanged() = 0;

        VansAudioProperties m_Properties;
        std::uint32_t m_SourceId = 0;
        float m_VirtualizationGain = 1.0f;

    private:
        void ApplySpatialProperties();
        void ApplyDirectionalProperties();
        void ApplyEffectSends();
        void ApplyDirectLowpass();
        void CommitGain();

        AudioAttenuationMode m_AttenuationMode = AudioAttenuationMode::Linear;
        std::uint32_t m_ReverbSendFilterId = 0;
        std::uint32_t m_DirectLowpassFilterId = 0;
        float m_PositionX = 0.0f;
        float m_PositionY = 0.0f;
        float m_PositionZ = 0.0f;
        float m_VelocityX = 0.0f;
        float m_VelocityY = 0.0f;
        float m_VelocityZ = 0.0f;
        float m_DirectionX = 0.0f;
        float m_DirectionY = 0.0f;
        float m_DirectionZ = 1.0f;
        AudioConeSettings m_Cone;
        float m_DistanceGain = 1.0f;
        float m_OcclusionGain = 1.0f;
        float m_OcclusionHighFrequencyGain = 1.0f;
        float m_BusGain = 1.0f;
        float m_BusLowpassHighFrequencyGain = 1.0f;
        float m_LastCommittedGain = -1.0f;
    };
}
