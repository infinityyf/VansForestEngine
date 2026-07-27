#include "VansAudioManager.h"
#include "VansAudioDecoder.h"
#include "VansAudioSystem.h"
#include "../SceneCore/VansSceneAudioRuntimeConfig.h"
#include "../SceneCore/VansSceneResourcePlan.h"
#include "../Util/VansLog.h"

#include <utility>

namespace VansEngine
{

VansAudioManager::~VansAudioManager() = default;

void VansAudioManager::Load(
    const std::vector<Vans::VansSceneAudioResourceRequest>& audios,
    const std::string& assetPrefix)
{
    for (const Vans::VansSceneAudioResourceRequest& entry : audios)
    {
        const std::string& name = entry.name;
        const std::string& rel = entry.path;

        if (name.empty() || rel.empty())
        {
            VANS_LOG_WARN("[VansAudioManager] Load: name or path is empty, skipped");
            continue;
        }

        if (m_Nodes.count(name))
        {
            VANS_LOG_WARN("[VansAudioManager] Load: duplicate audio name '" << name << "', skipped");
            continue;
        }

        const std::string fullPath = assetPrefix + "/" + rel;

        AudioNodeProperties props;
        props.m_Name = name;
        props.m_FilePath = fullPath;
        props.m_PlayMode = entry.playMode == "streaming" ? AudioPlayMode::Streaming : AudioPlayMode::Static;
        props.m_Loop = entry.loop;
        props.m_AutoPlay = entry.autoPlay;
        props.m_Volume = entry.volume;
        props.m_Pitch = entry.pitch;
        props.m_Spatial = entry.spatial;
        props.m_RefDist = entry.referenceDistance;
        props.m_MaxDist = entry.maxDistance;
        props.m_RollOff = entry.rolloff;

        auto node = std::make_unique<VansAudioNode>();
        if (!node->Open(props))
        {
            VANS_LOG_ERROR("[VansAudioManager] VansAudioNode::Open failed: " << fullPath);
            continue;
        }

        VANS_LOG("[VansAudioManager] Loaded audio: " << name << " <- " << rel);
        m_Nodes[name] = std::move(node);
    }
}

void VansAudioManager::ApplySceneConfig(
    const std::vector<Vans::VansSceneAudioRuntimeOverride>& audioSources)
{
    for (const Vans::VansSceneAudioRuntimeOverride& entry : audioSources)
    {
        if (entry.name.empty())
        {
            continue;
        }

        VansAudioNode* node = Get(entry.name);
        if (!node)
        {
            VANS_LOG_WARN("[VansAudioManager] ApplySceneConfig: audio node not found '" << entry.name << "'");
            continue;
        }

        if (entry.volume) node->SetVolume(*entry.volume);
        if (entry.pitch) node->SetPitch(*entry.pitch);
        if (entry.loop) node->SetLoop(*entry.loop);
        if (entry.spatial) node->SetSpatial(*entry.spatial);
        if (entry.referenceDistance) node->SetRefDistance(*entry.referenceDistance);
        if (entry.maxDistance) node->SetMaxDistance(*entry.maxDistance);
        if (entry.autoPlay) node->Play();
    }
}

VansAudioNode* VansAudioManager::Get(const std::string& name) const
{
    auto it = m_Nodes.find(name);
    return (it != m_Nodes.end()) ? it->second.get() : nullptr;
}

void VansAudioManager::TickAll(
    double /*deltaTime*/,
    float camPosX,
    float camPosY,
    float camPosZ,
    float camFwdX,
    float camFwdY,
    float camFwdZ,
    float camUpX,
    float camUpY,
    float camUpZ)
{
    VansAudioSystem::GetInstance().UpdateListener(
        camPosX,
        camPosY,
        camPosZ,
        camFwdX,
        camFwdY,
        camFwdZ,
        camUpX,
        camUpY,
        camUpZ);

    for (auto& [name, node] : m_Nodes)
    {
        if (node)
        {
            node->Tick();
        }
    }
}

void VansAudioManager::PlayAutoPlay()
{
    for (auto& [name, node] : m_Nodes)
    {
        if (!node)
        {
            continue;
        }

        if (node->IsAutoPlay())
        {
            node->Play();
            VANS_LOG("[VansAudioManager] AutoPlay: " << name);
        }
    }
}

void VansAudioManager::StopAll()
{
    for (auto& [name, node] : m_Nodes)
    {
        if (node)
        {
            node->Stop();
        }
    }
}

void VansAudioManager::Clear()
{
    for (auto& [name, node] : m_Nodes)
    {
        if (node)
        {
            node->Close();
        }
    }
    m_Nodes.clear();
}

} // namespace VansEngine
