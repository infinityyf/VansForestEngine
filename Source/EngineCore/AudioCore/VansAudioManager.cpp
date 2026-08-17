#include "VansAudioManager.h"
#include "VansAudioDecoder.h"
#include "VansAudioMixConfig.h"
#include "VansAudioSystem.h"
#include "../SceneCore/VansSceneAudioRuntimeConfig.h"
#include "../SceneCore/VansSceneResourcePlan.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <utility>

namespace VansEngine
{

VansAudioManager::~VansAudioManager() = default;

void VansAudioManager::Load(
    const std::vector<Vans::VansSceneAudioResourceRequest>& audios)
{
    for (const Vans::VansSceneAudioResourceRequest& entry : audios)
    {
        const std::string& key = entry.assetGuid;
        const std::string displayName = entry.name.empty() ? entry.assetGuid : entry.name;
        const std::string& rel = entry.path;

        if (key.empty() || rel.empty())
        {
            VANS_LOG_WARN("[VansAudioManager] Load: assetGuid or path is empty, skipped");
            continue;
        }

        if (m_Nodes.count(key))
        {
            VANS_LOG_WARN("[VansAudioManager] Load: duplicate audio assetGuid '" << key << "', skipped");
            continue;
        }

        const std::string fullPath = std::filesystem::path(rel).lexically_normal().string();

        AudioNodeProperties props;
        props.m_Name = displayName;
        props.m_FilePath = fullPath;
        std::string playMode = entry.playMode;
        std::transform(playMode.begin(), playMode.end(), playMode.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        props.m_PlayMode = playMode == "streaming" ? AudioPlayMode::Streaming : AudioPlayMode::Static;
        props.m_Loop = entry.loop;
        props.m_AutoPlay = entry.autoPlay;
        props.m_Volume = entry.volume;
        props.m_Pitch = entry.pitch;
        props.m_Spatial = entry.spatial;
        props.m_RefDist = entry.referenceDistance;
        props.m_MaxDist = entry.maxDistance;
        props.m_RollOff = entry.rolloff;
        props.m_AttenuationMode = entry.attenuationMode;
        props.m_ReverbSend = entry.reverbSend;
        props.m_BusName = entry.bus;
        props.m_LowpassHighFrequencyGain = entry.lowpassHighFrequencyGain;
        EnsureBus(props.m_BusName);

        auto node = std::make_unique<VansAudioNode>();
        if (!node->Open(props))
        {
            VANS_LOG_ERROR("[VansAudioManager] VansAudioNode::Open failed: " << fullPath);
            continue;
        }

        VANS_LOG("[VansAudioManager] Loaded audio: " << key
            << " (" << displayName << ") <- " << rel);
        m_Nodes[key] = std::move(node);
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
        if (entry.rolloff) node->SetRolloff(*entry.rolloff);
        if (entry.attenuationMode)
            node->SetAttenuationMode(AudioAttenuationModeFromString(*entry.attenuationMode));
        if (entry.reverbSend) node->SetReverbSend(*entry.reverbSend);
        if (entry.bus) node->SetBusName(*entry.bus);
        if (entry.lowpassHighFrequencyGain)
            node->SetLowpassHighFrequencyGain(*entry.lowpassHighFrequencyGain);
    }
}

VansAudioNode* VansAudioManager::Get(const std::string& name) const
{
    auto it = m_Nodes.find(name);
    return (it != m_Nodes.end()) ? it->second.get() : nullptr;
}

VansAudioOneShotHandle VansAudioManager::PlayOneShot(const VansAudioOneShotRequest& request)
{
    if (request.sourceName.empty() || !Get(request.sourceName)) return {};
    auto binding = std::make_unique<VansAudioSourceBinding>();
    if (!binding->Bind(this, request.sourceName) || !binding->IsBound()) return {};
    binding->SetVolume(request.volume);
    binding->SetPitch(request.pitch);
    binding->SetStereoPan(request.stereoPan);
    binding->SetBusName(request.bus);
    binding->SetSpatial(request.spatial);
    binding->SetLoop(request.loop);
    binding->SetRefDistance(request.referenceDistance);
    binding->SetMaxDistance(request.maxDistance);
    binding->SetRolloff(request.rolloff);
    binding->SetReverbSend(request.reverbSend);
    binding->SetPosition(request.positionX, request.positionY, request.positionZ);
    if (request.startSeconds > 0.0) binding->Seek(request.startSeconds);
    binding->Play();
    return m_OneShots.Emplace(OneShot{ std::move(binding), true });
}

bool VansAudioManager::StopOneShot(VansAudioOneShotHandle handle)
{
    OneShot* oneShot = m_OneShots.Resolve(handle);
    if (!oneShot) return false;
    if (oneShot->binding) oneShot->binding->Stop();
    return m_OneShots.Release(handle);
}

void VansAudioManager::TickAll(
    double deltaTime,
    float camPosX,
    float camPosY,
    float camPosZ,
    float camFwdX,
    float camFwdY,
    float camFwdZ,
    float camUpX,
    float camUpY,
    float camUpZ,
    float camVelX,
    float camVelY,
    float camVelZ)
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
        camUpZ,
        camVelX,
        camVelY,
        camVelZ);

    std::vector<VansAudioOneShotHandle> completedOneShots;
    m_OneShots.ForEach([&](VansAudioOneShotHandle handle, OneShot& oneShot)
    {
        if (!oneShot.binding) { completedOneShots.push_back(handle); return; }
        oneShot.binding->Tick();
        oneShot.observedPlaying = oneShot.observedPlaying || oneShot.binding->IsPlaying();
        if (oneShot.observedPlaying && !oneShot.binding->IsPlaying() && !oneShot.binding->IsPaused())
            completedOneShots.push_back(handle);
    });
    for (VansAudioOneShotHandle handle : completedOneShots) m_OneShots.Release(handle);

    TickBusFades(static_cast<float>(deltaTime));
    ApplyBusGains();

    for (auto& [name, node] : m_Nodes)
    {
        if (node)
        {
            node->Tick();
        }
    }
}

AudioBusState& VansAudioManager::EnsureBus(const std::string& busName)
{
    const std::string normalized = NormalizeAudioBusName(busName);
    return m_Buses.try_emplace(normalized).first->second;
}

const AudioBusState* VansAudioManager::FindBus(const std::string& busName) const
{
    const std::string normalized = NormalizeAudioBusName(busName);
    const auto it = m_Buses.find(normalized);
    return it == m_Buses.end() ? nullptr : &it->second;
}

bool VansAudioManager::HasSoloedBus() const
{
    for (const auto& [name, bus] : m_Buses)
    {
        if (bus.soloed)
            return true;
    }
    return false;
}

void VansAudioManager::TickBusFades(float deltaTime)
{
    for (auto& [name, bus] : m_Buses)
        TickAudioBusFade(bus, deltaTime);
}

void VansAudioManager::ApplyBusGains()
{
    AudioBusState& master = EnsureBus("Master");
    const bool anySoloed = HasSoloedBus();
    for (auto& [name, node] : m_Nodes)
    {
        if (!node)
            continue;

        const std::string& busName = node->GetBusName();
        AudioBusState& bus = m_Buses.try_emplace(busName).first->second;
        bus.Normalize();
        const bool busIsMaster = busName == "Master";
        node->SetBusGain(ComputeAudioBusEffectiveGain(master, bus, anySoloed, busIsMaster));
        node->SetBusLowpassHighFrequencyGain(
            master.lowpassHighFrequencyGain *
                (busIsMaster ? 1.0f : bus.lowpassHighFrequencyGain));
    }
}

void VansAudioManager::SetBusGain(const std::string& busName, float gain)
{
    AudioBusState& bus = EnsureBus(busName);
    SetAudioBusGainImmediate(bus, gain);
}

void VansAudioManager::SetBusLowpassHighFrequencyGain(const std::string& busName, float highFrequencyGain)
{
    AudioBusState& bus = EnsureBus(busName);
    bus.lowpassHighFrequencyGain = std::clamp(highFrequencyGain, 0.0f, 1.0f);
}

void VansAudioManager::SetBusMuted(const std::string& busName, bool muted)
{
    EnsureBus(busName).muted = muted;
}

void VansAudioManager::SetBusSoloed(const std::string& busName, bool soloed)
{
    EnsureBus(busName).soloed = soloed;
}

void VansAudioManager::FadeBusGain(const std::string& busName, float targetGain, float fadeSeconds)
{
    AudioBusState& bus = EnsureBus(busName);
    StartAudioBusGainFade(bus, targetGain, fadeSeconds);
}

void VansAudioManager::ApplyBusSnapshot(const AudioBusSnapshot& snapshot)
{
    for (const AudioBusSnapshotEntry& entry : snapshot.buses)
    {
        AudioBusState& bus = EnsureBus(entry.busName);
        StartAudioBusGainFade(bus, entry.gain, snapshot.fadeSeconds);
        if (entry.overrideMuted)
            bus.muted = entry.muted;
        if (entry.overrideSoloed)
            bus.soloed = entry.soloed;
        if (entry.overrideLowpassHighFrequencyGain)
            bus.lowpassHighFrequencyGain = std::clamp(entry.lowpassHighFrequencyGain, 0.0f, 1.0f);
    }
}

bool VansAudioManager::ApplyNamedBusSnapshot(const std::string& snapshotName)
{
    const auto it = m_NamedSnapshots.find(snapshotName);
    if (it == m_NamedSnapshots.end())
        return false;
    ApplyBusSnapshot(it->second);
    return true;
}

void VansAudioManager::ApplyMixConfig(const AudioMixConfig& config)
{
    for (const AudioMixBusConfig& busConfig : config.buses)
    {
        AudioBusState& bus = EnsureBus(busConfig.busName);
        SetAudioBusGainImmediate(bus, busConfig.gain);
        bus.muted = busConfig.muted;
        bus.soloed = busConfig.soloed;
        bus.lowpassHighFrequencyGain =
            std::clamp(busConfig.lowpassHighFrequencyGain, 0.0f, 1.0f);
    }

    m_NamedSnapshots = config.snapshots;
    ClearDuckingRules();
    for (AudioDuckingRule rule : config.duckingRules)
        AddDuckingRule(std::move(rule));

    if (!config.defaultSnapshot.empty())
        ApplyNamedBusSnapshot(config.defaultSnapshot);
}

void VansAudioManager::AddDuckingRule(AudioDuckingRule rule)
{
    rule.Normalize();
    if (rule.triggerBusName == rule.targetBusName)
        return;
    EnsureBus(rule.triggerBusName);
    EnsureBus(rule.targetBusName);
    m_DuckingRules.push_back(std::move(rule));
}

void VansAudioManager::ClearDuckingRules()
{
    m_DuckingRules.clear();
    m_ActiveDuckingRuleKeys.clear();
    m_ActiveBusVoiceCounts.clear();
    for (auto& [name, bus] : m_Buses)
        StartAudioBusDuckingFade(bus, 1.0f, 0.0f);
}

void VansAudioManager::UpdateDucking(const std::vector<std::string>& activeBusNames)
{
    m_ActiveBusVoiceCounts.clear();
    for (const std::string& busName : activeBusNames)
        m_ActiveBusVoiceCounts[NormalizeAudioBusName(busName)] += 1;

    struct TargetState
    {
        float targetGain = 1.0f;
        float fadeSeconds = 0.0f;
        bool hasActiveRule = false;
    };

    std::unordered_map<std::string, TargetState> desiredTargets;
    std::unordered_set<std::string> activeRuleKeys;

    for (std::size_t ruleIndex = 0; ruleIndex < m_DuckingRules.size(); ++ruleIndex)
    {
        AudioDuckingRule rule = m_DuckingRules[ruleIndex];
        rule.Normalize();
        if (!rule.enabled)
            continue;

        TargetState& target = desiredTargets[rule.targetBusName];
        target.fadeSeconds = std::max(target.fadeSeconds, rule.releaseSeconds);

        if (m_ActiveBusVoiceCounts[rule.triggerBusName] <= 0)
            continue;

        const std::string ruleKey = std::to_string(ruleIndex) + ":" + rule.triggerBusName + ">" + rule.targetBusName;
        activeRuleKeys.insert(ruleKey);
        if (!target.hasActiveRule || rule.targetGain < target.targetGain)
        {
            target.targetGain = rule.targetGain;
            target.fadeSeconds = rule.attackSeconds;
            target.hasActiveRule = true;
        }
        else if (rule.targetGain == target.targetGain)
        {
            target.fadeSeconds = std::min(target.fadeSeconds, rule.attackSeconds);
        }
    }

    for (auto& [targetBusName, target] : desiredTargets)
    {
        AudioBusState& bus = EnsureBus(targetBusName);
        StartAudioBusDuckingFade(bus, target.targetGain, target.fadeSeconds);
    }
    m_ActiveDuckingRuleKeys = std::move(activeRuleKeys);
}

AudioBusState VansAudioManager::GetBusState(const std::string& busName) const
{
    if (const AudioBusState* bus = FindBus(busName))
        return *bus;
    return AudioBusState{};
}

float VansAudioManager::GetEffectiveBusGain(const std::string& busName) const
{
    const std::string normalized = NormalizeAudioBusName(busName);
    const AudioBusState master = GetBusState("Master");
    const AudioBusState bus = GetBusState(normalized);
    return ComputeAudioBusEffectiveGain(master, bus, HasSoloedBus(), normalized == "Master");
}

std::vector<AudioBusDebugEntry> VansAudioManager::GetBusDebugSnapshot() const
{
    std::vector<AudioBusDebugEntry> snapshot;
    snapshot.reserve(m_Buses.size());

    const AudioBusState master = GetBusState("Master");
    const bool anySoloed = HasSoloedBus();
    for (const auto& [name, bus] : m_Buses)
    {
        snapshot.push_back(AudioBusDebugEntry{
            name,
            bus,
            ComputeAudioBusEffectiveGain(master, bus, anySoloed, name == "Master"),
            m_ActiveBusVoiceCounts.count(name) != 0 ? m_ActiveBusVoiceCounts.at(name) : 0 });
    }

    std::sort(snapshot.begin(), snapshot.end(),
        [](const AudioBusDebugEntry& lhs, const AudioBusDebugEntry& rhs)
        {
            if (lhs.name == "Master") return true;
            if (rhs.name == "Master") return false;
            return lhs.name < rhs.name;
        });
    return snapshot;
}

std::vector<AudioDuckingRuleDebugEntry> VansAudioManager::GetDuckingRuleDebugSnapshot() const
{
    std::vector<AudioDuckingRuleDebugEntry> snapshot;
    snapshot.reserve(m_DuckingRules.size());
    for (std::size_t ruleIndex = 0; ruleIndex < m_DuckingRules.size(); ++ruleIndex)
    {
        AudioDuckingRule rule = m_DuckingRules[ruleIndex];
        rule.Normalize();
        const std::string ruleKey =
            std::to_string(ruleIndex) + ":" + rule.triggerBusName + ">" + rule.targetBusName;
        snapshot.push_back(AudioDuckingRuleDebugEntry{
            rule,
            m_ActiveDuckingRuleKeys.count(ruleKey) != 0 });
    }
    return snapshot;
}

void VansAudioManager::BeginVoiceLeaseFrame()
{
    m_VoiceLeaseFrameStats = {};
}

void VansAudioManager::RecordVoiceLeaseTransition(
    bool hardwareActiveBefore,
    bool hardwareActiveAfter)
{
    if (hardwareActiveBefore && !hardwareActiveAfter)
        ++m_VoiceLeaseFrameStats.suspendedThisFrame;
    else if (!hardwareActiveBefore && hardwareActiveAfter)
        ++m_VoiceLeaseFrameStats.resumedThisFrame;
}

void VansAudioManager::SuppressResourceAutoPlay(const std::string& sourceName)
{
    if (!sourceName.empty())
        m_SuppressedResourceAutoPlay.insert(sourceName);
}

void VansAudioManager::PlayAutoPlay()
{
    for (auto& [name, node] : m_Nodes)
    {
        if (!node)
        {
            continue;
        }

        if (m_SuppressedResourceAutoPlay.count(name) != 0)
            continue;

        if (node->IsAutoPlay() && node->GetProperties().m_PlayMode == AudioPlayMode::Streaming)
        {
            node->Play();
            VANS_LOG("[VansAudioManager] AutoPlay: " << name);
        }
    }
}

void VansAudioManager::StopAll()
{
	std::vector<VansAudioOneShotHandle> oneShots;
	m_OneShots.ForEach([&](VansAudioOneShotHandle handle, OneShot& oneShot)
	{
		if (oneShot.binding) oneShot.binding->Stop();
		oneShots.push_back(handle);
	});
	for (VansAudioOneShotHandle handle : oneShots) m_OneShots.Release(handle);
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
	m_OneShots.Clear();
    for (auto& [name, node] : m_Nodes)
    {
        if (node)
        {
            node->Close();
        }
    }
    m_Nodes.clear();
    m_Buses.clear();
    m_NamedSnapshots.clear();
    m_DuckingRules.clear();
    m_ActiveDuckingRuleKeys.clear();
    m_ActiveBusVoiceCounts.clear();
    m_SuppressedResourceAutoPlay.clear();
}

} // namespace VansEngine
