#include "VansAudioSourceBinding.h"

#include "VansAudioAttenuation.h"
#include "VansAudioDecoder.h"
#include "VansAudioManager.h"
#include "VansAudioNode.h"
#include "VansAudioSourceInstance.h"

#include <utility>

namespace VansEngine
{
namespace
{
    const std::string EmptyString;

    std::unique_ptr<VansAudioSourceInstance> TryCreateStaticInstance(VansAudioNode* node)
    {
        if (!node || !node->CanCreateStaticInstance())
            return nullptr;
        auto instance = std::make_unique<VansAudioSourceInstance>();
        if (!instance->OpenStatic(node->GetStaticBufferId(), node->GetProperties()))
            return nullptr;
        return instance;
    }

    std::unique_ptr<VansAudioNode> TryCreatePrivateStreamingNode(VansAudioNode* node)
    {
        if (!node || node->GetProperties().m_PlayMode != AudioPlayMode::Streaming)
            return nullptr;

        auto privateNode = std::make_unique<VansAudioNode>();
        AudioNodeProperties properties = node->GetProperties();
        properties.m_AutoPlay = false;
        if (!privateNode->Open(properties))
            return nullptr;
        return privateNode;
    }
}

VansAudioSourceBinding::VansAudioSourceBinding() = default;
VansAudioSourceBinding::~VansAudioSourceBinding() = default;

bool VansAudioSourceBinding::Bind(VansAudioManager* manager, const std::string& sourceName)
{
    m_Manager = manager;
    m_SourceName = sourceName;
    m_Node = manager ? manager->Get(sourceName) : nullptr;
    m_Instance = TryCreateStaticInstance(m_Node);
    m_PrivateNode = m_Instance ? nullptr : TryCreatePrivateStreamingNode(m_Node);
    if (m_PrivateNode && m_Manager)
        m_Manager->SuppressResourceAutoPlay(sourceName);
    return m_Node != nullptr;
}

void VansAudioSourceBinding::Bind(VansAudioManager* manager, VansAudioNode* node, std::string sourceName)
{
    m_Manager = manager;
    m_Node = node;
    m_SourceName = std::move(sourceName);
    m_Instance = TryCreateStaticInstance(m_Node);
    m_PrivateNode = m_Instance ? nullptr : TryCreatePrivateStreamingNode(m_Node);
    if (m_PrivateNode && m_Manager)
        m_Manager->SuppressResourceAutoPlay(m_SourceName);
}

void VansAudioSourceBinding::Clear()
{
    m_Manager = nullptr;
    m_Node = nullptr;
    m_PrivateNode.reset();
    m_Instance.reset();
    m_SourceName.clear();
}

bool VansAudioSourceBinding::SwitchSource(const std::string& sourceName)
{
    if (!m_Manager)
        return false;

    VansAudioNode* newNode = m_Manager->Get(sourceName);
    if (!newNode)
        return false;

    Stop();
    m_Node = newNode;
    m_Instance = TryCreateStaticInstance(m_Node);
    m_PrivateNode = m_Instance ? nullptr : TryCreatePrivateStreamingNode(m_Node);
    m_SourceName = sourceName;
    if (m_PrivateNode)
        m_Manager->SuppressResourceAutoPlay(m_SourceName);
    return true;
}

bool VansAudioSourceBinding::IsBound() const
{
    return m_Instance ? m_Instance->IsBound() :
        (m_PrivateNode ? m_PrivateNode->IsBound() : (m_Node && m_Node->IsBound()));
}

bool VansAudioSourceBinding::IsHardwareVoiceActive() const
{
    return m_Instance ? m_Instance->IsHardwareVoiceActive() :
        (m_PrivateNode ? m_PrivateNode->IsHardwareVoiceActive() :
            (m_Node && m_Node->IsHardwareVoiceActive()));
}

void VansAudioSourceBinding::Play() { if (m_Instance) m_Instance->Play(); else if (m_PrivateNode) m_PrivateNode->Play(); else if (m_Node) m_Node->Play(); }
void VansAudioSourceBinding::Pause() { if (m_Instance) m_Instance->Pause(); else if (m_PrivateNode) m_PrivateNode->Pause(); else if (m_Node) m_Node->Pause(); }
void VansAudioSourceBinding::Stop() { if (m_Instance) m_Instance->Stop(); else if (m_PrivateNode) m_PrivateNode->Stop(); else if (m_Node) m_Node->Stop(); }
void VansAudioSourceBinding::Resume() { if (m_Instance) m_Instance->Resume(); else if (m_PrivateNode) m_PrivateNode->Resume(); else if (m_Node) m_Node->Resume(); }
bool VansAudioSourceBinding::Seek(double seconds)
{
	const float offset = static_cast<float>(std::max(0.0, seconds));
	return m_Instance ? m_Instance->SetPlaybackOffsetSeconds(offset) :
		(m_PrivateNode ? m_PrivateNode->SetPlaybackOffsetSeconds(offset) :
			(m_Node && m_Node->SetPlaybackOffsetSeconds(offset)));
}
double VansAudioSourceBinding::GetPlaybackOffsetSeconds() const
{
	return m_Instance ? m_Instance->GetPlaybackOffsetSeconds() :
		(m_PrivateNode ? m_PrivateNode->GetPlaybackOffsetSeconds() :
			(m_Node ? m_Node->GetPlaybackOffsetSeconds() : 0.0f));
}
bool VansAudioSourceBinding::IsPlaying() const { return m_Instance ? m_Instance->IsPlaying() : (m_PrivateNode ? m_PrivateNode->IsPlaying() : (m_Node && m_Node->IsPlaying())); }
bool VansAudioSourceBinding::IsPaused() const { return m_Instance ? m_Instance->IsPaused() : (m_PrivateNode ? m_PrivateNode->IsPaused() : (m_Node && m_Node->IsPaused())); }
void VansAudioSourceBinding::SetEnabled(bool enabled) { if (m_Instance) m_Instance->SetEnabled(enabled); else if (m_PrivateNode) m_PrivateNode->SetEnabled(enabled); else if (m_Node) m_Node->SetEnabled(enabled); }
void VansAudioSourceBinding::SetPosition(float x, float y, float z) { if (m_Instance) m_Instance->SetPosition(x, y, z); else if (m_PrivateNode) m_PrivateNode->SetPosition(x, y, z); else if (m_Node) m_Node->SetPosition(x, y, z); }
void VansAudioSourceBinding::SetSpatial(bool enabled) { if (m_Instance) m_Instance->SetSpatial(enabled); else if (m_PrivateNode) m_PrivateNode->SetSpatial(enabled); else if (m_Node) m_Node->SetSpatial(enabled); }
bool VansAudioSourceBinding::GetSpatial() const { return m_Instance ? m_Instance->GetSpatial() : (m_PrivateNode ? m_PrivateNode->GetSpatial() : (m_Node && m_Node->GetSpatial())); }
void VansAudioSourceBinding::SetStereoPan(float pan) { if (m_Instance) m_Instance->SetStereoPan(pan); else if (m_PrivateNode) m_PrivateNode->SetStereoPan(pan); else if (m_Node) m_Node->SetStereoPan(pan); }
float VansAudioSourceBinding::GetStereoPan() const { return m_Instance ? m_Instance->GetStereoPan() : (m_PrivateNode ? m_PrivateNode->GetStereoPan() : (m_Node ? m_Node->GetStereoPan() : 0.0f)); }
void VansAudioSourceBinding::UpdateDistanceGain(float listenerX, float listenerY, float listenerZ)
{
    if (m_Instance)
        m_Instance->UpdateDistanceGain(listenerX, listenerY, listenerZ);
    else if (m_PrivateNode)
        m_PrivateNode->UpdateDistanceGain(listenerX, listenerY, listenerZ);
    else if (m_Node)
        m_Node->UpdateDistanceGain(listenerX, listenerY, listenerZ);
}
void VansAudioSourceBinding::SetVolume(float gain) { if (m_Instance) m_Instance->SetVolume(gain); else if (m_PrivateNode) m_PrivateNode->SetVolume(gain); else if (m_Node) m_Node->SetVolume(gain); }
float VansAudioSourceBinding::GetVolume() const { return m_Instance ? m_Instance->GetVolume() : (m_PrivateNode ? m_PrivateNode->GetVolume() : (m_Node ? m_Node->GetVolume() : 0.0f)); }
void VansAudioSourceBinding::SetPitch(float pitch) { if (m_Instance) m_Instance->SetPitch(pitch); else if (m_PrivateNode) m_PrivateNode->SetPitch(pitch); else if (m_Node) m_Node->SetPitch(pitch); }
float VansAudioSourceBinding::GetPitch() const { return m_Instance ? m_Instance->GetPitch() : (m_PrivateNode ? m_PrivateNode->GetPitch() : (m_Node ? m_Node->GetPitch() : 0.0f)); }
void VansAudioSourceBinding::SetLoop(bool loop) { if (m_Instance) m_Instance->SetLoop(loop); else if (m_PrivateNode) m_PrivateNode->SetLoop(loop); else if (m_Node) m_Node->SetLoop(loop); }
bool VansAudioSourceBinding::GetLoop() const { return m_Instance ? m_Instance->GetLoop() : (m_PrivateNode ? m_PrivateNode->GetLoop() : (m_Node && m_Node->GetLoop())); }
void VansAudioSourceBinding::SetRefDistance(float distance) { if (m_Instance) m_Instance->SetRefDistance(distance); else if (m_PrivateNode) m_PrivateNode->SetRefDistance(distance); else if (m_Node) m_Node->SetRefDistance(distance); }
float VansAudioSourceBinding::GetRefDistance() const { return m_Instance ? m_Instance->GetRefDistance() : (m_PrivateNode ? m_PrivateNode->GetRefDist() : (m_Node ? m_Node->GetRefDist() : 0.0f)); }
void VansAudioSourceBinding::SetMaxDistance(float distance) { if (m_Instance) m_Instance->SetMaxDistance(distance); else if (m_PrivateNode) m_PrivateNode->SetMaxDistance(distance); else if (m_Node) m_Node->SetMaxDistance(distance); }
float VansAudioSourceBinding::GetMaxDistance() const { return m_Instance ? m_Instance->GetMaxDistance() : (m_PrivateNode ? m_PrivateNode->GetMaxDist() : (m_Node ? m_Node->GetMaxDist() : 0.0f)); }
void VansAudioSourceBinding::SetRolloff(float rolloff) { if (m_Instance) m_Instance->SetRolloff(rolloff); else if (m_PrivateNode) m_PrivateNode->SetRolloff(rolloff); else if (m_Node) m_Node->SetRolloff(rolloff); }
float VansAudioSourceBinding::GetRolloff() const { return m_Instance ? m_Instance->GetRolloff() : (m_PrivateNode ? m_PrivateNode->GetRolloff() : (m_Node ? m_Node->GetRolloff() : 0.0f)); }
void VansAudioSourceBinding::SetAttenuationMode(AudioAttenuationMode mode) { if (m_Instance) m_Instance->SetAttenuationMode(mode); else if (m_PrivateNode) m_PrivateNode->SetAttenuationMode(mode); else if (m_Node) m_Node->SetAttenuationMode(mode); }
AudioAttenuationMode VansAudioSourceBinding::GetAttenuationMode() const
{
    return m_Instance ? m_Instance->GetAttenuationMode() : (m_PrivateNode ? m_PrivateNode->GetAttenuationMode() : (m_Node ? m_Node->GetAttenuationMode() : AudioAttenuationMode::Linear));
}
void VansAudioSourceBinding::SetReverbSend(float send) { if (m_Instance) m_Instance->SetReverbSend(send); else if (m_PrivateNode) m_PrivateNode->SetReverbSend(send); else if (m_Node) m_Node->SetReverbSend(send); }
float VansAudioSourceBinding::GetReverbSend() const { return m_Instance ? m_Instance->GetReverbSend() : (m_PrivateNode ? m_PrivateNode->GetReverbSend() : (m_Node ? m_Node->GetReverbSend() : 0.0f)); }
void VansAudioSourceBinding::SetLowpassHighFrequencyGain(float highFrequencyGain) { if (m_Instance) m_Instance->SetLowpassHighFrequencyGain(highFrequencyGain); else if (m_PrivateNode) m_PrivateNode->SetLowpassHighFrequencyGain(highFrequencyGain); else if (m_Node) m_Node->SetLowpassHighFrequencyGain(highFrequencyGain); }
float VansAudioSourceBinding::GetLowpassHighFrequencyGain() const { return m_Instance ? m_Instance->GetLowpassHighFrequencyGain() : (m_PrivateNode ? m_PrivateNode->GetLowpassHighFrequencyGain() : (m_Node ? m_Node->GetLowpassHighFrequencyGain() : 1.0f)); }
void VansAudioSourceBinding::SetBusName(const std::string& busName) { if (m_Instance) m_Instance->SetBusName(busName); else if (m_PrivateNode) m_PrivateNode->SetBusName(busName); else if (m_Node) m_Node->SetBusName(busName); }
const std::string& VansAudioSourceBinding::GetBusName() const
{
    return m_Instance ? m_Instance->GetBusName() : (m_PrivateNode ? m_PrivateNode->GetBusName() : (m_Node ? m_Node->GetBusName() : EmptyString));
}
void VansAudioSourceBinding::SetBusGain(float gain)
{
    if (m_Instance)
        m_Instance->SetBusGain(gain);
    else if (m_PrivateNode)
        m_PrivateNode->SetBusGain(gain);
    else if (m_Node)
        m_Node->SetBusGain(gain);
}

void VansAudioSourceBinding::SetBusLowpassHighFrequencyGain(float highFrequencyGain)
{
    if (m_Instance)
        m_Instance->SetBusLowpassHighFrequencyGain(highFrequencyGain);
    else if (m_PrivateNode)
        m_PrivateNode->SetBusLowpassHighFrequencyGain(highFrequencyGain);
    else if (m_Node)
        m_Node->SetBusLowpassHighFrequencyGain(highFrequencyGain);
}

void VansAudioSourceBinding::SetVirtualizationGain(float gain)
{
    if (m_Instance)
        m_Instance->SetVirtualizationGain(gain);
    else if (m_PrivateNode)
        m_PrivateNode->SetVirtualizationGain(gain);
    else if (m_Node)
        m_Node->SetVirtualizationGain(gain);
}

float VansAudioSourceBinding::GetVirtualizationGain() const
{
    return m_Instance ? m_Instance->GetVirtualizationGain() :
        (m_PrivateNode ? m_PrivateNode->GetVirtualizationGain() :
            (m_Node ? m_Node->GetVirtualizationGain() : 1.0f));
}

void VansAudioSourceBinding::SetOcclusion(float gain, float highFrequencyGain)
{
    if (m_Instance)
        m_Instance->SetOcclusion(gain, highFrequencyGain);
    else if (m_PrivateNode)
        m_PrivateNode->SetOcclusion(gain, highFrequencyGain);
    else if (m_Node)
        m_Node->SetOcclusion(gain, highFrequencyGain);
}

void VansAudioSourceBinding::SetVelocity(float x, float y, float z)
{
    if (m_Instance)
        m_Instance->SetVelocity(x, y, z);
    else if (m_PrivateNode)
        m_PrivateNode->SetVelocity(x, y, z);
    else if (m_Node)
        m_Node->SetVelocity(x, y, z);
}

void VansAudioSourceBinding::SetDirection(float x, float y, float z)
{
    if (m_Instance)
        m_Instance->SetDirection(x, y, z);
    else if (m_PrivateNode)
        m_PrivateNode->SetDirection(x, y, z);
    else if (m_Node)
        m_Node->SetDirection(x, y, z);
}

void VansAudioSourceBinding::SetCone(AudioConeSettings settings)
{
    if (m_Instance)
        m_Instance->SetCone(settings);
    else if (m_PrivateNode)
        m_PrivateNode->SetCone(settings);
    else if (m_Node)
        m_Node->SetCone(settings);
}

void VansAudioSourceBinding::Tick()
{
    if (m_PrivateNode && GetVirtualizationGain() > 0.0005f)
        m_PrivateNode->Tick();
}

const std::string& VansAudioSourceBinding::GetFilePath() const
{
    return m_Instance ? m_Instance->GetFilePath() : (m_PrivateNode ? m_PrivateNode->GetFilePath() : (m_Node ? m_Node->GetFilePath() : EmptyString));
}
}
