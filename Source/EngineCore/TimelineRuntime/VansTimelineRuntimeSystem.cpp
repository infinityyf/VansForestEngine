#include "VansTimelineRuntimeSystem.h"

#include "../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../SceneRuntime/VansRuntimeWorld.h"
#include "../Util/VansLog.h"

#include <unordered_set>

namespace Vans
{
VansTimelineRuntimeSystem::VansTimelineRuntimeSystem()
	: m_Clocks(VansTimelineClockRegistry::BuiltIns()),
	  m_Appliers(std::make_shared<VansTimelineApplierRegistry>()),
	  m_Payloads(std::make_shared<VansPayloadSchemaRegistry>())
{
	std::string error;
	m_Appliers->Seal(error);
	m_Payloads->Seal(error);
	m_Sessions = std::make_unique<VansTimelineSessionService>(m_Clocks, *m_Appliers, m_Payloads.get());
}

void VansTimelineRuntimeSystem::RegisterWorld(VansRuntimeWorld* world) { m_World = world; }
void VansTimelineRuntimeSystem::SetAssetLoader(VansTimelineRuntimeAssetLoader loader) { m_AssetLoader = std::move(loader); }
void VansTimelineRuntimeSystem::SetAssetGenerationQuery(VansTimelineRuntimeAssetGenerationQuery query) { m_AssetGenerationQuery = std::move(query); }

void VansTimelineRuntimeSystem::SetApplierRegistry(
	std::shared_ptr<VansTimelineApplierRegistry> appliers)
{
	StopAll();
	m_Appliers = appliers ? std::move(appliers) : std::make_shared<VansTimelineApplierRegistry>();
	std::string error;
	if (!m_Appliers->IsSealed()) m_Appliers->Seal(error);
	m_Sessions = std::make_unique<VansTimelineSessionService>(m_Clocks, *m_Appliers, m_Payloads.get());
}

void VansTimelineRuntimeSystem::SetPayloadSchemaRegistry(
	std::shared_ptr<VansPayloadSchemaRegistry> payloads)
{
	StopAll();
	m_Payloads = payloads ? std::move(payloads) : std::make_shared<VansPayloadSchemaRegistry>();
	std::string error;
	if (!m_Payloads->IsSealed()) m_Payloads->Seal(error);
	m_Sessions = std::make_unique<VansTimelineSessionService>(m_Clocks, *m_Appliers, m_Payloads.get());
}

bool VansTimelineRuntimeSystem::HasOutputApplier(VansTimelineOutputTypeId type) const
{
	return m_Appliers && m_Appliers->SlotOf(type) != VansInvalidTimelineApplierSlot;
}

bool VansTimelineRuntimeSystem::HasPayloadSchema(VansTimelinePayloadTypeId type) const
{
	return m_Payloads && m_Payloads->Resolve(type) != nullptr;
}

bool VansTimelineRuntimeSystem::ValidatePayload(
	VansTimelinePayloadTypeId type,
	const VansSerializedValue& payload,
	std::string& error) const
{
	if (!m_Payloads) { error = "Event.PayloadRegistryUnavailable"; return false; }
	return m_Payloads->Validate(type, payload, error);
}

VansTimelineSessionResult VansTimelineRuntimeSystem::CreateActionSession(
	std::string assetReference,
	VansEntityHandle owner,
	VansTimelineSessionScope scope)
{
	if (!m_AssetLoader)
		return { false, {}, "Timeline runtime asset loader is not configured" };
	if (assetReference.empty() || !owner.IsValid() || !scope)
		return { false, {}, "Timeline Action Session descriptor is invalid" };
	VansRuntimeTimelineComponent component;
	component.assetGuid = std::move(assetReference);
	std::shared_ptr<const VansCompiledTimeline> timeline;
	std::string error;
	if (!m_AssetLoader(component, timeline, error))
		return { false, {}, std::move(error) };
	VansTimelineSessionDesc desc;
	desc.kind = VansTimelineSessionKind::External;
	desc.timeline = std::move(timeline);
	desc.world = m_World;
	desc.owner = owner;
	desc.scope = std::move(scope);
	desc.clockType = std::string(TimelineClockNames::Manual);
	desc.debugLabel = "GAF.ActionSession";
	return m_Sessions->Create(desc);
}

VansTimelineSessionHandle VansTimelineRuntimeSystem::CreateComponentSession(
	const VansRuntimeTimelineComponent& component,
	const VansComponentHeader& header,
	std::string& error)
{
	if (!m_AssetLoader) { error = "Timeline runtime asset loader is not configured"; return {}; }
	std::shared_ptr<const VansCompiledTimeline> timeline;
	if (!m_AssetLoader(component, timeline, error)) return {};
	VansTimelineSessionDesc desc;
	desc.kind = VansTimelineSessionKind::Component;
	desc.timeline = std::move(timeline);
	desc.world = m_World;
	desc.owner = header.owner;
	desc.clockType = component.instance.clockType;
	desc.bindingOverrides = component.instance.bindingOverrides;
	desc.parameterOverrides = component.instance.parameterOverrides;
	desc.loopMode = component.instance.loopMode;
	desc.loopCount = component.instance.loopCount;
	desc.playRate = component.instance.playbackSpeed;
	desc.restoreStateOnStop = component.instance.restoreStateOnStop;
	desc.debugLabel = header.stableGuid;
	VansTimelineSessionResult result = m_Sessions->Create(desc);
	if (!result) error = result.error;
	return result.handle;
}

void VansTimelineRuntimeSystem::SyncTimelineComponents()
{
	if (!m_World) return;
	auto* storage = static_cast<VansComponentStorage<VansRuntimeTimelineComponent>*>(
		m_World->FindStorage(VansRuntimeComponentType_Timeline));
	if (!storage)
	{
		for (auto& [key, facade] : m_Components) m_Sessions->Release(facade.session);
		m_Components.clear();
		return;
	}
	std::unordered_set<ComponentKey, ComponentKeyHash> alive;
	const auto& components = storage->DenseData();
	const auto& headers = storage->Headers();
	for (std::size_t index = 0; index < components.size(); ++index)
	{
		const VansRuntimeTimelineComponent& component = components[index];
		const VansComponentHeader& header = headers[index];
		const ComponentKey key = Key(header.self);
		alive.insert(key);
		auto found = m_Components.find(key);
		std::uint64_t assetGeneration = 0;
		if (m_AssetGenerationQuery) m_AssetGenerationQuery(component.assetGuid, assetGeneration);
		if (found != m_Components.end() && found->second.assetGuid == component.assetGuid &&
			found->second.assetGeneration == assetGeneration)
		{
			if (header.effectiveEnabled != found->second.wasEnabled)
			{
				if (header.effectiveEnabled && component.instance.playOn == VansTimelinePlayOn::Enable)
					m_Sessions->Play(found->second.session);
				else if (!header.effectiveEnabled) m_Sessions->Stop(found->second.session);
				found->second.wasEnabled = header.effectiveEnabled;
			}
			continue;
		}
		if (found != m_Components.end()) { m_Sessions->Release(found->second.session); m_Components.erase(found); }
		std::string error;
		const VansTimelineSessionHandle session = CreateComponentSession(component, header, error);
		if (!session.IsValid())
		{
			VANS_LOG_ERROR("[Timeline] Session creation failed for component " << header.stableGuid <<
				": " << error);
			m_Diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
				"Timeline.SessionCreateFailed", {}, header.stableGuid, "assetGuid", error });
			continue;
		}
		VANS_LOG("[Timeline] Component session ready: component=" << header.stableGuid <<
			" asset=" << component.assetGuid << " enabled=" << header.effectiveEnabled);
		m_Components.emplace(key, ComponentFacade{ header.self, session, component.assetGuid, assetGeneration, header.effectiveEnabled });
		if (header.effectiveEnabled && (component.instance.playOn == VansTimelinePlayOn::Awake ||
			component.instance.playOn == VansTimelinePlayOn::Enable)) m_Sessions->Play(session);
	}
	for (auto iterator = m_Components.begin(); iterator != m_Components.end();)
	{
		if (alive.find(iterator->first) != alive.end()) { ++iterator; continue; }
		m_Sessions->Release(iterator->second.session);
		iterator = m_Components.erase(iterator);
	}
}

void VansTimelineRuntimeSystem::UpdateRuntimePostScript(double deltaSeconds)
{
	SyncTimelineComponents();
	m_Sessions->AdvanceAndEvaluateAll(VansTimelineSessionKind::Component,
		VansTimelineEvaluationPhase::PostScript, deltaSeconds);
}

void VansTimelineRuntimeSystem::UpdateRuntimeCamera(double deltaSeconds)
{
	(void)deltaSeconds;
	m_Sessions->AdvanceAndEvaluateAll(VansTimelineSessionKind::Component,
		VansTimelineEvaluationPhase::Camera, 0.0);
	m_Sessions->AdvanceAndEvaluateAll(VansTimelineSessionKind::External,
		VansTimelineEvaluationPhase::Camera, 0.0);
}

void VansTimelineRuntimeSystem::UpdatePreviewsPostScript(double deltaSeconds)
{
	m_Sessions->AdvanceAndEvaluateAll(VansTimelineSessionKind::Preview,
		VansTimelineEvaluationPhase::PostScript, deltaSeconds);
}

void VansTimelineRuntimeSystem::UpdatePreviewsCamera(double deltaSeconds)
{
	(void)deltaSeconds;
	m_Sessions->AdvanceAndEvaluateAll(VansTimelineSessionKind::Preview,
		VansTimelineEvaluationPhase::Camera, 0.0);
}

bool VansTimelineRuntimeSystem::PlayComponent(VansComponentHandle component, bool restart)
{
	SyncTimelineComponents();
	const auto found = m_Components.find(Key(component));
	return found != m_Components.end() && m_Sessions->Play(found->second.session, restart);
}

bool VansTimelineRuntimeSystem::PauseComponent(VansComponentHandle component)
{
	const auto found = m_Components.find(Key(component));
	return found != m_Components.end() && m_Sessions->Pause(found->second.session);
}

bool VansTimelineRuntimeSystem::ResumeComponent(VansComponentHandle component)
{
	const auto found = m_Components.find(Key(component));
	return found != m_Components.end() && m_Sessions->Resume(found->second.session);
}

bool VansTimelineRuntimeSystem::StopComponent(VansComponentHandle component)
{
	const auto found = m_Components.find(Key(component));
	return found != m_Components.end() && m_Sessions->Stop(found->second.session);
}

bool VansTimelineRuntimeSystem::GetComponentState(
	VansComponentHandle component,
	VansTimelinePlayerState& state,
	VansTimelineTick& tick) const
{
	const auto found = m_Components.find(Key(component));
	if (found == m_Components.end()) return false;
	const auto view = m_Sessions->Query(found->second.session);
	if (!view) return false;
	state = view->state; tick = view->tick; return true;
}

bool VansTimelineRuntimeSystem::StartPreview(
	std::string previewId,
	std::shared_ptr<const VansCompiledTimeline> timeline,
	VansEntityHandle owner,
	bool safeEvents,
	bool includeSubTimelines,
	std::string& error)
{
	StopPreview(previewId);
	VansTimelineSessionDesc desc;
	desc.kind = VansTimelineSessionKind::Preview;
	desc.timeline = std::move(timeline);
	desc.world = m_World;
	desc.owner = owner;
	desc.clockType = std::string(TimelineClockNames::Manual);
	desc.previewSafeEvents = safeEvents;
	desc.includeSubTimelines = includeSubTimelines;
	desc.debugLabel = previewId;
	VansTimelineSessionResult result = m_Sessions->Create(desc);
	if (!result) { error = result.error; return false; }
	m_Previews.emplace(std::move(previewId), result.handle);
	return true;
}

bool VansTimelineRuntimeSystem::PlayPreview(const std::string& id)
{
	const auto found = m_Previews.find(id); return found != m_Previews.end() && m_Sessions->Play(found->second);
}

bool VansTimelineRuntimeSystem::PausePreview(const std::string& id)
{
	const auto found = m_Previews.find(id); return found != m_Previews.end() && m_Sessions->Pause(found->second);
}

bool VansTimelineRuntimeSystem::ConfigurePreview(
	const std::string& id, double playRate, int direction, bool loopPlaybackRange)
{
	const auto found = m_Previews.find(id);
	if (found == m_Previews.end()) return false;
	return m_Sessions->ConfigurePlayback(found->second, playRate, direction,
		loopPlaybackRange ? VansTimelineLoopMode::Loop : VansTimelineLoopMode::None,
		loopPlaybackRange ? INT32_MAX : 1);
}

bool VansTimelineRuntimeSystem::SeekPreview(
	const std::string& id, VansTimelineTick tick, VansTimelineSeekPolicy policy)
{
	const auto found = m_Previews.find(id); return found != m_Previews.end() && m_Sessions->Seek(found->second, tick, policy);
}

bool VansTimelineRuntimeSystem::StopPreview(const std::string& id)
{
	const auto found = m_Previews.find(id);
	if (found == m_Previews.end()) return false;
	m_Sessions->Release(found->second); m_Previews.erase(found); return true;
}

bool VansTimelineRuntimeSystem::GetPreviewState(
	const std::string& id, VansTimelinePlayerState& state, VansTimelineTick& tick) const
{
	const auto found = m_Previews.find(id);
	if (found == m_Previews.end()) return false;
	const auto view = m_Sessions->Query(found->second);
	if (!view) return false;
	state = view->state; tick = view->tick; return true;
}

void VansTimelineRuntimeSystem::StopAll()
{
	if (m_Sessions) m_Sessions->StopAll();
	m_Components.clear(); m_Previews.clear();
}

void VansTimelineRuntimeSystem::Clear()
{
	StopAll(); m_World = nullptr; m_Diagnostics.clear();
}

const VansTimelineDiagnostics& VansTimelineRuntimeSystem::Diagnostics() const
{
	return m_Sessions && !m_Sessions->Diagnostics().empty() ? m_Sessions->Diagnostics() : m_Diagnostics;
}
}
