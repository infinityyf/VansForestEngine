#include "VansAudioSourceBinding.h"

#include "VansAudioManager.h"
#include "VansAudioNode.h"
#include "VansAudioStaticVoice.h"
#include "VansAudioVoice.h"

#include <utility>

namespace VansEngine
{
namespace
{
std::unique_ptr<VansAudioVoice> CreateVoice(VansAudioNode* resource)
{
    if (!resource)
        return nullptr;

    if (resource->GetProperties().m_PlayMode == VansAudioPlayMode::Static)
    {
        if (!resource->CanCreateStaticVoice())
            return nullptr;
        auto voice = std::make_unique<VansAudioStaticVoice>();
        if (!voice->Open(resource->GetStaticBufferId(), resource->GetProperties()))
            return nullptr;
        return voice;
    }

    auto voice = std::make_unique<VansAudioNode>();
    VansAudioProperties properties = resource->GetProperties();
    properties.m_AutoPlay = false;
    if (!voice->Open(properties))
        return nullptr;
    return voice;
}

}

VansAudioSourceBinding::VansAudioSourceBinding() = default;
VansAudioSourceBinding::~VansAudioSourceBinding() = default;

bool VansAudioSourceBinding::Bind(
    VansAudioManager* manager,
    const std::string& sourceName)
{
    return Bind(manager, manager ? manager->Get(sourceName) : nullptr, sourceName);
}

bool VansAudioSourceBinding::Bind(
    VansAudioManager* manager,
    VansAudioNode* resource,
    std::string sourceName)
{
    std::unique_ptr<VansAudioVoice> voice = CreateVoice(resource);
    if (!voice)
        return false;

    Clear();
    m_Manager = manager;
    m_Resource = resource;
    m_Voice = std::move(voice);
    m_SourceName = std::move(sourceName);
    if (m_Voice->GetKind() == VansAudioVoiceKind::Streaming && m_Manager)
        m_Manager->SuppressResourceAutoPlay(m_SourceName);
    return true;
}

bool VansAudioSourceBinding::SwitchSource(const std::string& sourceName)
{
    if (!m_Manager)
        return false;

    VansAudioNode* resource = m_Manager->Get(sourceName);
    std::unique_ptr<VansAudioVoice> voice = CreateVoice(resource);
    if (!voice)
        return false;

    if (m_Voice)
        m_Voice->Stop();
    m_Resource = resource;
    m_Voice = std::move(voice);
    m_SourceName = sourceName;
    if (m_Voice->GetKind() == VansAudioVoiceKind::Streaming)
        m_Manager->SuppressResourceAutoPlay(m_SourceName);
    return true;
}

void VansAudioSourceBinding::Clear()
{
    m_Voice.reset();
    m_Resource = nullptr;
    m_Manager = nullptr;
    m_SourceName.clear();
}

bool VansAudioSourceBinding::IsBound() const
{
    return m_Voice && m_Voice->IsBound();
}
}
