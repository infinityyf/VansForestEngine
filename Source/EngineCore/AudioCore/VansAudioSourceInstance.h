#pragma once

#include "VansAudioAttenuation.h"
#include "VansAudioBus.h"
#include "VansAudioNode.h"

#include <cstdint>
#include <string>

namespace VansEngine
{
    class VansAudioSourceInstance
    {
    public:
        VansAudioSourceInstance() = default;
        ~VansAudioSourceInstance();

        VansAudioSourceInstance(const VansAudioSourceInstance&) = delete;
        VansAudioSourceInstance& operator=(const VansAudioSourceInstance&) = delete;
        VansAudioSourceInstance(VansAudioSourceInstance&& other) noexcept;
        VansAudioSourceInstance& operator=(VansAudioSourceInstance&& other) noexcept;

        bool OpenStatic(uint32_t sharedBufferId, const AudioNodeProperties& properties);
        void Close();
        bool IsBound() const { return m_SharedBufferId != 0; }
        bool IsHardwareVoiceActive() const { return m_SourceId != 0; }

        void Play();
        void Pause();
        void Stop();
        void Resume();
        bool IsPlaying() const;
        bool IsPaused() const;
        void SetEnabled(bool enabled);

        void SetPosition(float x, float y, float z);
        void SetSpatial(bool enabled);
        bool GetSpatial() const { return m_Properties.m_Spatial; }
        void UpdateDistanceGain(float listenerX, float listenerY, float listenerZ);

        void SetVolume(float gain);
        float GetVolume() const { return m_Properties.m_Volume; }
        void SetPitch(float pitch);
        float GetPitch() const { return m_Properties.m_Pitch; }
        void SetLoop(bool loop);
        bool GetLoop() const { return m_Properties.m_Loop; }
        void SetRefDistance(float distance);
        float GetRefDistance() const { return m_Properties.m_RefDist; }
        void SetMaxDistance(float distance);
        float GetMaxDistance() const { return m_Properties.m_MaxDist; }
        void SetRolloff(float rolloff);
        float GetRolloff() const { return m_Properties.m_RollOff; }
        void SetAttenuationMode(AudioAttenuationMode mode);
        AudioAttenuationMode GetAttenuationMode() const { return m_AttenuationModeRuntime; }
        void SetReverbSend(float send);
        float GetReverbSend() const { return m_Properties.m_ReverbSend; }
        void SetLowpassHighFrequencyGain(float highFrequencyGain);
        float GetLowpassHighFrequencyGain() const { return m_Properties.m_LowpassHighFrequencyGain; }
        void SetBusName(const std::string& busName);
        const std::string& GetBusName() const { return m_Properties.m_BusName; }
        void SetBusGain(float gain);
        float GetBusGain() const { return m_BusGain; }
        void SetBusLowpassHighFrequencyGain(float highFrequencyGain);
        void SetVirtualizationGain(float gain);
        float GetVirtualizationGain() const { return m_VirtualizationGain; }
        void SetOcclusion(float gain, float highFrequencyGain);
        void SetVelocity(float x, float y, float z);
        void SetDirection(float x, float y, float z);
        void SetCone(AudioConeSettings settings);
        const std::string& GetFilePath() const { return m_Properties.m_FilePath; }

    private:
        void ApplySpatialProperties();
        void ApplyDirectionalProperties();
        void ApplyEffectSends();
        void ApplyDirectLowpass();
        void CommitGain();
        void MoveFrom(VansAudioSourceInstance& other) noexcept;
        bool AcquireHardwareVoice();
        void ReleaseHardwareVoice();

        AudioNodeProperties m_Properties;
        AudioAttenuationMode m_AttenuationModeRuntime = AudioAttenuationMode::Linear;
        uint32_t m_SourceId = 0;
        uint32_t m_SharedBufferId = 0;
        uint32_t m_ReverbSendFilterId = 0;
        uint32_t m_DirectLowpassFilterId = 0;
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
        float m_OcclusionHighFrequencyGain = 1.0f;
        float m_BusGain = 1.0f;
        float m_BusLowpassHighFrequencyGain = 1.0f;
        float m_VirtualizationGain = 1.0f;
        float m_LastCommittedGain = -1.0f;
        float m_PlaybackOffsetSeconds = 0.0f;
        bool m_LogicalPlaying = false;
        bool m_LogicalPaused = false;
    };
}
