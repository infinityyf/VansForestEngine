#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace VansEngine
{
    class VansAudioNode;

    struct VansAudioPreviewSettings
    {
        bool streaming = true;
        bool loop = false;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool spatial = false;
        float referenceDistance = 1.0f;
        float maxDistance = 100.0f;
        float rolloff = 1.0f;
        std::string attenuationMode = "linear";
        float reverbSend = 0.0f;
        std::string bus = "Preview";
    };

    class VansAudioPreviewPlayer
    {
    public:
        VansAudioPreviewPlayer();
        ~VansAudioPreviewPlayer();

        VansAudioPreviewPlayer(const VansAudioPreviewPlayer&) = delete;
        VansAudioPreviewPlayer& operator=(const VansAudioPreviewPlayer&) = delete;

        bool Play(const std::filesystem::path& filePath, const VansAudioPreviewSettings& settings, std::string& error);
        void Stop();
        void Tick();

        bool IsPlaying() const;
        const std::filesystem::path& CurrentPath() const { return m_CurrentPath; }

    private:
        std::unique_ptr<VansAudioNode> m_Node;
        std::filesystem::path m_CurrentPath;
    };
}
