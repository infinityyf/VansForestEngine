#include "VansScene.h"

#include "Timeline/VansVirtualCameraParameterStore.h"
#include "VansCameraControlArbiter.h"
#include "../AssetCore/VansAssetObjectRepository.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../Timeline/VansEngineTimelineRegistry.h"
#include "../TimelineCore/VansTimelineCompiler.h"
#include "../TimelineCore/VansTimelineSerialization.h"
#include "../TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "../TimelineRuntime/VansTimelineRuntimeSystem.h"
#include "../TimelineRuntime/VansTimelinePropertyAccessRegistry.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>

namespace
{
std::string TimelineDiagnosticsText(const Vans::VansTimelineDiagnostics& diagnostics)
{
	std::ostringstream stream;
	for (const Vans::VansTimelineDiagnostic& diagnostic : diagnostics)
	{
		if (diagnostic.severity != Vans::VansTimelineDiagnosticSeverity::Error) continue;
		if (stream.tellp() > 0) stream << "; ";
		stream << diagnostic.code;
		if (!diagnostic.objectId.empty()) stream << " [" << diagnostic.objectId << "]";
		if (!diagnostic.message.empty()) stream << ": " << diagnostic.message;
	}
	return stream.str();
}

}

void VansGraphics::VansScene::UpdateTimelinesPostScript(double deltaSeconds)
{
	if (!m_TimelineRuntime) return;
	m_TimelineRuntime->UpdateRuntimePostScript(deltaSeconds);
	if (m_RuntimeWorld)
		m_RuntimeWorld->CommitCommands(Vans::VansRuntimeCommandCommitPoint::RuntimeFrame);
}

void VansGraphics::VansScene::BeginCameraControlFrame()
{
	if (m_Camera && m_CameraControlArbiter) m_CameraControlArbiter->BeginFrame(*m_Camera);
}

void VansGraphics::VansScene::CaptureCameraControlBase()
{
	// Editor viewport navigation, camera scripts, and editor state restore write the
	// base camera only outside the contribution window.  CaptureBase closes that
	// window until Resolve, making the scheduler order an asserted contract rather
	// than an implicit dependency of those direct base-view writes.
	if (m_Camera && m_CameraControlArbiter) m_CameraControlArbiter->CaptureBase(*m_Camera);
}

void VansGraphics::VansScene::ResolveCameraControlFrame()
{
	if (m_Camera && m_CameraControlArbiter) m_CameraControlArbiter->Resolve(*m_Camera);
}

VansGraphics::VansCameraControlArbiter& VansGraphics::VansScene::CameraControlArbiter()
{
	return *m_CameraControlArbiter;
}

bool VansGraphics::VansScene::IsUserCameraLookSuppressed() const
{
	return m_CameraControlArbiter && m_CameraControlArbiter->IsUserLookSuppressed();
}

void VansGraphics::VansScene::UpdateTimelinesCamera(double deltaSeconds)
{
	if (!m_TimelineRuntime) return;
	m_TimelineRuntime->UpdateRuntimeCamera(deltaSeconds);
}

void VansGraphics::VansScene::UpdateTimelinePreviewsPostScript(double deltaSeconds)
{
	if (!m_TimelineRuntime) return;
	m_TimelineRuntime->UpdatePreviewsPostScript(deltaSeconds);
	if (m_RuntimeWorld)
		m_RuntimeWorld->CommitCommands(Vans::VansRuntimeCommandCommitPoint::RuntimeFrame);
}

void VansGraphics::VansScene::UpdateTimelinePreviewsCamera(double deltaSeconds)
{
	if (!m_TimelineRuntime) return;
	m_TimelineRuntime->UpdatePreviewsCamera(deltaSeconds);
}

bool VansGraphics::VansScene::PlayRuntimeTimeline(const std::string& componentGuid, bool restart)
{
	if (!m_RuntimeWorld || !m_TimelineRuntime || componentGuid.empty())
	{
		VANS_LOG_ERROR("[Timeline] Play rejected before lookup: componentGuid=" << componentGuid);
		return false;
	}
	const Vans::VansComponentHandle component = m_RuntimeWorld->FindComponentByGuid(
		componentGuid, Vans::VansRuntimeComponentType_Timeline);
	if (!component.IsValid())
	{
		VANS_LOG_ERROR("[Timeline] Play component not found: " << componentGuid);
		return false;
	}
	const bool played = m_TimelineRuntime->PlayComponent(component, restart);
	VANS_LOG("[Timeline] Play component " << componentGuid << " restart=" << restart <<
		" result=" << played);
	return played;
}

bool VansGraphics::VansScene::PauseRuntimeTimeline(const std::string& componentGuid)
{
	if (!m_RuntimeWorld || !m_TimelineRuntime || componentGuid.empty()) return false;
	const auto component = m_RuntimeWorld->FindComponentByGuid(componentGuid, Vans::VansRuntimeComponentType_Timeline);
	return component.IsValid() && m_TimelineRuntime->PauseComponent(component);
}

bool VansGraphics::VansScene::ResumeRuntimeTimeline(const std::string& componentGuid)
{
	if (!m_RuntimeWorld || !m_TimelineRuntime || componentGuid.empty()) return false;
	const auto component = m_RuntimeWorld->FindComponentByGuid(componentGuid, Vans::VansRuntimeComponentType_Timeline);
	return component.IsValid() && m_TimelineRuntime->ResumeComponent(component);
}

bool VansGraphics::VansScene::StopRuntimeTimeline(const std::string& componentGuid)
{
	if (!m_RuntimeWorld || !m_TimelineRuntime || componentGuid.empty()) return false;
	const auto component = m_RuntimeWorld->FindComponentByGuid(componentGuid, Vans::VansRuntimeComponentType_Timeline);
	return component.IsValid() && m_TimelineRuntime->StopComponent(component);
}

bool VansGraphics::VansScene::GetRuntimeTimelineState(
	const std::string& componentGuid, std::string& state, std::int64_t& tick) const
{
	if (!m_RuntimeWorld || !m_TimelineRuntime || componentGuid.empty()) return false;
	const auto component = m_RuntimeWorld->FindComponentByGuid(componentGuid, Vans::VansRuntimeComponentType_Timeline);
	Vans::VansTimelinePlayerState playerState{};
	if (!component.IsValid() || !m_TimelineRuntime->GetComponentState(component, playerState, tick)) return false;
	static constexpr const char* names[] = { "Unloaded", "Stopped", "Playing", "Paused", "Completed", "Error" };
	state = names[static_cast<std::size_t>(playerState)];
	return true;
}

std::string VansGraphics::VansScene::FindTimelineInstanceOwnerGuid(const std::string& assetGuid) const
{
	if (!m_RuntimeWorld || assetGuid.empty()) return {};
	const auto* storage = m_RuntimeWorld->FindStorage<Vans::VansRuntimeTimelineComponent>(
		Vans::VansRuntimeComponentType_Timeline);
	if (!storage) return {};
	for (std::size_t index = 0; index < storage->DenseData().size(); ++index)
	{
		if (storage->DenseData()[index].assetGuid != assetGuid) continue;
		const auto* owner = m_RuntimeWorld->Entities().Get(storage->Headers()[index].owner);
		if (owner) return owner->stableGuid;
	}
	return {};
}

bool VansGraphics::VansScene::StartTimelinePreview(
	const std::string& previewId, const std::string& canonicalJson,
	const std::string& ownerEntityGuid, bool safeEvents, bool includeSubTimelines, std::string& error)
{
	if (!m_RuntimeWorld || !m_TimelineRuntime) { error = "Timeline preview runtime is unavailable"; return false; }
	Vans::VansTimelineAsset asset;
	try
	{
		if (!Vans::VansTimelineSerialization::Decode(
			Vans::VansTimelineSerialization::Json::parse(canonicalJson), asset, error)) return false;
	}
	catch (const std::exception& exception) { error = exception.what(); return false; }
	const Vans::VansEngineTimelineCatalog catalog = Vans::VansGetEngineTimelineCatalog();
	if (!catalog) { error = std::string(catalog.error); return false; }
	Vans::VansTimelineCompileOptions options;
	options.extensions = catalog.trackExtensions;
	options.runtimeRegistryManifestHash = m_TimelineRuntime->RuntimeRegistryManifestHash();
	options.validation.requireRuntimeCapabilities = true;
	options.validation.preview = true;
	options.validation.hasOutputApplier = [this](Vans::VansTimelineOutputTypeId type)
	{ return m_TimelineRuntime->HasOutputApplier(type); };
	options.validation.hasPayloadSchema = [this](Vans::VansTimelinePayloadTypeId type)
	{ return m_TimelineRuntime->HasPayloadSchema(type); };
	options.validation.validatePayload = [this](Vans::VansTimelinePayloadTypeId type,
		const Vans::VansSerializedValue& payload, std::string& payloadError)
	{ return m_TimelineRuntime->ValidatePayload(type, payload, payloadError); };
	options.dependencyLoader = [](const Vans::VansTimelineDependency& dependency,
		Vans::VansTimelineAsset& nested, std::string& identity, std::string& nestedError)
	{
		if (dependency.guid.empty()) { nestedError = "Preview dependency GUID is missing"; return false; }
		Vans::VansAssetGuid guid;
		if (!Vans::VansAssetGuid::TryParse(dependency.guid, guid))
		{
			nestedError = "Preview dependency GUID is invalid";
			return false;
		}
		const auto resolved = Vans::VansProjectManager::Get().GetAssetObjectRepository()
			.ResolveLatest<Vans::VansTimelineAsset>(guid);
		if (!resolved)
		{
			nestedError = "Preview Timeline dependency is not loaded in memory";
			return false;
		}
		identity = dependency.guid;
		nested = *resolved;
		return true;
	};
	auto compiled = Vans::VansTimelineCompiler::Compile(asset, options);
	if (!compiled) { error = TimelineDiagnosticsText(compiled.diagnostics); return false; }
	const Vans::VansEntityHandle owner = ownerEntityGuid.empty()
		? Vans::VansEntityHandle{} : m_RuntimeWorld->Entities().FindByGuid(ownerEntityGuid);
	if (!ownerEntityGuid.empty() && !owner.IsValid()) { error = "Preview owner does not exist"; return false; }
	return m_TimelineRuntime->StartPreview(previewId, compiled.timeline, owner,
		safeEvents, includeSubTimelines, error);
}

bool VansGraphics::VansScene::PlayTimelinePreview(const std::string& id)
{ return m_TimelineRuntime && m_TimelineRuntime->PlayPreview(id); }
bool VansGraphics::VansScene::PauseTimelinePreview(const std::string& id)
{ return m_TimelineRuntime && m_TimelineRuntime->PausePreview(id); }
bool VansGraphics::VansScene::ConfigureTimelinePreviewPlayback(
	const std::string& id, double rate, int direction, bool loop)
{ return m_TimelineRuntime && m_TimelineRuntime->ConfigurePreview(id, rate, direction, loop); }
bool VansGraphics::VansScene::SeekTimelinePreview(const std::string& id, std::int64_t tick, bool safeEdges)
{
	return m_TimelineRuntime && m_TimelineRuntime->SeekPreview(id, tick,
		safeEdges ? Vans::VansTimelineSeekPolicy::SafeEdges : Vans::VansTimelineSeekPolicy::ContinuousOnly);
}
bool VansGraphics::VansScene::StopTimelinePreview(const std::string& id)
{ return m_TimelineRuntime && m_TimelineRuntime->StopPreview(id); }
bool VansGraphics::VansScene::GetTimelinePreviewState(
	const std::string& id, int& state, std::int64_t& tick) const
{
	if (!m_TimelineRuntime) return false;
	Vans::VansTimelinePlayerState playerState{};
	if (!m_TimelineRuntime->GetPreviewState(id, playerState, tick)) return false;
	state = static_cast<int>(playerState);
	return true;
}
