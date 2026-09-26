#pragma once

#include <memory>
#include <string>

namespace VansEngine
{
    class VansAudioManager;
    class VansAudioNode;
    class VansAudioVoice;

    // Resolves one asset resource into one independent runtime voice.
    // Playback and mix operations belong to VansAudioVoice, not this binding.
    class VansAudioSourceBinding
    {
    public:
        VansAudioSourceBinding();
        ~VansAudioSourceBinding();

        bool Bind(VansAudioManager* manager, const std::string& sourceName);
        bool Bind(VansAudioManager* manager, VansAudioNode* resource, std::string sourceName);
        bool SwitchSource(const std::string& sourceName);
        void Clear();

        bool IsBound() const;
        VansAudioVoice* GetVoice() const { return m_Voice.get(); }
        VansAudioNode* GetResourceNode() const { return m_Resource; }
        const std::string& GetSourceName() const { return m_SourceName; }

    private:
        VansAudioManager* m_Manager = nullptr;
        VansAudioNode* m_Resource = nullptr;
        std::unique_ptr<VansAudioVoice> m_Voice;
        std::string m_SourceName;
    };
}
