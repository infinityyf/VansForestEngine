#include "VansAudioPreviewPlayer.h"

#include "VansAudioDecoder.h"
#include "VansAudioBus.h"
#include "VansAudioNode.h"
#include "VansAudioSystem.h"

#include <algorithm>

namespace VansEngine
{
    VansAudioPreviewPlayer::VansAudioPreviewPlayer() = default;

    VansAudioPreviewPlayer::~VansAudioPreviewPlayer()
    {
        Stop();
    }

    bool VansAudioPreviewPlayer::Play(
        const std::filesystem::path& filePath,
        const VansAudioPreviewSettings& settings,
        std::string& error)
    {
        error.clear();
        Stop();

        if (filePath.empty())
        {
            error = "Audio file path is empty";
            return false;
        }

        if (!VansAudioSystem::GetInstance().IsInitialized())
        {
            error = "Audio system is not initialized";
            return false;
        }

        AudioNodeProperties props;
        props.m_Name = "EditorAudioPreview";
        props.m_FilePath = filePath.lexically_normal().string();
        props.m_PlayMode = settings.streaming ? AudioPlayMode::Streaming : AudioPlayMode::Static;
        props.m_Loop = settings.loop;
        props.m_AutoPlay = false;
        props.m_Volume = std::clamp(settings.volume, 0.0f, 4.0f);
        props.m_Pitch = std::max(settings.pitch, 0.01f);
        props.m_Spatial = settings.spatial;
        props.m_RefDist = settings.referenceDistance;
        props.m_MaxDist = settings.maxDistance;
        props.m_RollOff = settings.rolloff;
        props.m_AttenuationMode = settings.attenuationMode;
        props.m_ReverbSend = settings.reverbSend;
        props.m_BusName = NormalizeAudioBusName(settings.bus);

        auto previewNode = std::make_unique<VansAudioNode>();
        if (!previewNode->Open(props))
        {
            error = "Cannot open audio preview: " + props.m_FilePath;
            return false;
        }

        previewNode->Play();
        m_CurrentPath = filePath;
        m_Node = std::move(previewNode);
        return true;
    }

    void VansAudioPreviewPlayer::Stop()
    {
        if (m_Node)
        {
            m_Node->Stop();
            m_Node->Close();
            m_Node.reset();
        }
        m_CurrentPath.clear();
    }

    void VansAudioPreviewPlayer::Tick()
    {
        if (m_Node)
            m_Node->Tick();
    }

    bool VansAudioPreviewPlayer::IsPlaying() const
    {
        return m_Node && m_Node->IsPlaying();
    }
}
