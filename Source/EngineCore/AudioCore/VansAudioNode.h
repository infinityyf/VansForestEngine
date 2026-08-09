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
#include "VansAudioAttenuation.h"
#include "VansAudioBus.h"
#include "VansAudioDirectionality.h"
#include "VansAudioOcclusion.h"
#include "../VansNode.h"

// Avoid including OpenAL and FFmpeg headers from this public header.
// ALuint is represented by uint32_t here; OpenAL 1.1 defines it as unsigned int.

namespace VansEngine
{
    // Static sounds are decoded into memory once; streaming sounds are decoded on demand.
    enum class AudioPlayMode
    {
        Static,
        Streaming
    };

    // Runtime settings used to create an audio node.
    struct AudioNodeProperties
    {
        std::string    m_Name;               // Runtime name generated from the asset record.
        std::string    m_FilePath;           // Resolved audio file path.
        AudioPlayMode  m_PlayMode   = AudioPlayMode::Static;
        bool           m_Loop       = false;
        bool           m_AutoPlay   = false;
        float          m_Volume     = 1.0f;  // [0, 1]
        float          m_Pitch      = 1.0f;  // > 0
        bool           m_Spatial    = false;
		float          m_StereoPan  = 0.0f;
        float          m_RefDist    = 1.0f;  // Distance where attenuation starts.
        float          m_MaxDist    = 100.0f;// Distance where linear attenuation reaches zero.
        float          m_RollOff    = 1.0f;  // Spatial attenuation rolloff factor.
        std::string    m_AttenuationMode = "linear";
        float          m_ReverbSend = 0.0f;  // [0, 1], 0 = dry only
        std::string    m_BusName = "SFX";
        float          m_LowpassHighFrequencyGain = 1.0f; // [0, 1], 1 = full bandwidth
    };

    // Playback unit for one audio asset.
    //
    // Each node wraps one OpenAL source and either one static buffer or a streaming buffer ring.
    // Open(), Close(), Tick(), and transport controls are expected to run on the main thread.
    class VansAudioDecoder;

    class VansAudioNode : public VansGraphics::VansNode
    {
    public:
        VansAudioNode()  = default;
        ~VansAudioNode();

        VansAudioNode(const VansAudioNode&)            = delete;
        VansAudioNode& operator=(const VansAudioNode&) = delete;

        bool Open(const AudioNodeProperties& props);
        void Close();

        void Play();
        void Pause();

    protected:
        void OnDisable() override { Pause(); }

    public:
        void Stop();
        void Resume();
		bool SetPlaybackOffsetSeconds(float seconds);
		float GetPlaybackOffsetSeconds() const;

        void  SetVolume(float gain);
        float GetVolume()  const { return m_Properties.m_Volume; }

        void  SetPitch(float pitch);
        float GetPitch()   const { return m_Properties.m_Pitch; }

        void  SetLoop(bool loop);
        bool  GetLoop()    const { return m_Properties.m_Loop; }

        void  SetPosition(float x, float y, float z);
        void  SetSpatial(bool enabled);
        bool  GetSpatial() const { return m_Properties.m_Spatial; }
		void  SetStereoPan(float pan);
		float GetStereoPan() const { return m_Properties.m_StereoPan; }

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
        void  UpdateDistanceGain(float listenerX, float listenerY, float listenerZ);
        void  SetSpatialGain(float distanceGain);
        void  SetOcclusion(float gain, float highFrequencyGain);
        void  SetVelocity(float x, float y, float z);
        void  SetDirection(float x, float y, float z);
        void  SetCone(AudioConeSettings settings);
        int   GetALSourceRelative() const;

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

        void Tick();

    private:
        bool OpenStatic();
        bool EnsureStreamingReady();
        bool OpenStreaming();
        void SuspendHardwareVoice();
        bool ResumeHardwareVoice();
        void StartDecodeThread();
        void StopDecodeThread();
        void DecodeThreadFunc();
        void RefillStreamBuffers();
        int QueueStreamBuffersFromPCMQueue(int maxBuffers);

        static int32_t GetAlFormat(int channels);
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

        uint32_t m_SourceId = 0;
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
