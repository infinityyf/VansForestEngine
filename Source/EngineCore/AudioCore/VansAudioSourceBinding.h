#pragma once

#include "VansAudioDirectionality.h"

#include <string>
#include <memory>

namespace VansEngine
{
    enum class AudioAttenuationMode;
    class VansAudioManager;
    class VansAudioNode;
    class VansAudioSourceInstance;

    class VansAudioSourceBinding
    {
    public:
        VansAudioSourceBinding();
        ~VansAudioSourceBinding();

        bool Bind(VansAudioManager* manager, const std::string& sourceName);
        void Bind(VansAudioManager* manager, VansAudioNode* node, std::string sourceName);
        void Clear();
        bool UsesInstance() const { return m_Instance != nullptr; }
        bool UsesPrivateNode() const { return m_PrivateNode != nullptr; }
        bool UsesIndependentPlayback() const { return m_Instance != nullptr || m_PrivateNode != nullptr; }
        bool IsHardwareVoiceActive() const;

        bool SwitchSource(const std::string& sourceName);
        bool IsBound() const;

        void Play();
        void Pause();
        void Stop();
        void Resume();
		bool Seek(double seconds);
		double GetPlaybackOffsetSeconds() const;

        bool IsPlaying() const;
        bool IsPaused() const;

        void SetEnabled(bool enabled);
        void SetPosition(float x, float y, float z);
        void SetSpatial(bool enabled);
        bool GetSpatial() const;
		void SetStereoPan(float pan);
		float GetStereoPan() const;
        void UpdateDistanceGain(float listenerX, float listenerY, float listenerZ);

        void SetVolume(float gain);
        float GetVolume() const;
        void SetPitch(float pitch);
        float GetPitch() const;
        void SetLoop(bool loop);
        bool GetLoop() const;
        void SetRefDistance(float distance);
        float GetRefDistance() const;
        void SetMaxDistance(float distance);
        float GetMaxDistance() const;
        void SetRolloff(float rolloff);
        float GetRolloff() const;
        void SetAttenuationMode(AudioAttenuationMode mode);
        AudioAttenuationMode GetAttenuationMode() const;
        void SetReverbSend(float send);
        float GetReverbSend() const;
        void SetLowpassHighFrequencyGain(float highFrequencyGain);
        float GetLowpassHighFrequencyGain() const;
        void SetBusName(const std::string& busName);
        const std::string& GetBusName() const;
        void SetBusGain(float gain);
        void SetBusLowpassHighFrequencyGain(float highFrequencyGain);
        void SetVirtualizationGain(float gain);
        float GetVirtualizationGain() const;
        void SetOcclusion(float gain, float highFrequencyGain);
        void SetVelocity(float x, float y, float z);
        void SetDirection(float x, float y, float z);
        void SetCone(AudioConeSettings settings);
        void Tick();

        const std::string& GetSourceName() const { return m_SourceName; }
        const std::string& GetFilePath() const;
        VansAudioNode* GetNode() const { return m_Node; }

    private:
        VansAudioManager* m_Manager = nullptr;
        VansAudioNode* m_Node = nullptr;
        std::unique_ptr<VansAudioNode> m_PrivateNode;
        std::unique_ptr<VansAudioSourceInstance> m_Instance;
        std::string m_SourceName;
    };
}
