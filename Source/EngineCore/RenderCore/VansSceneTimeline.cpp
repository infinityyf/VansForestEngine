#include "VansScene.h"

#include "Timeline/VansCameraTimelineIntegration.h"
#include "Timeline/VansPostProcessTimelineIntegration.h"
#include "Timeline/VansRenderPropertyTimelineIntegration.h"
#include "Timeline/VansVirtualCameraParameterStore.h"
#include "VansCameraControlArbiter.h"
#include "../AssetCore/VansAssetObjectRepository.h"
#include "../AssetCore/VansAssetResolver.h"
#include "../AudioCore/Timeline/VansAudioTimelineIntegration.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../SceneRuntime/Timeline/VansTransformTimelineIntegration.h"
#include "../SceneRuntime/Timeline/VansTransformTimelineAccess.h"
#include "../SceneRuntime/Timeline/VansActivationTimelineIntegration.h"
#include "../SceneRuntime/Timeline/VansPropertyTimelineIntegration.h"
#include "../ParticleCore/Timeline/VansParticleTimelineIntegration.h"
#include "../RuntimeUI/Timeline/VansUITimelineIntegration.h"
#include "../AnimationCore/Timeline/VansAnimationTimelineIntegration.h"
#include "../GameplayActionTimeline/VansGameplayActionTimelineIntegration.h"
#include "Timeline/VansMediaTimelineIntegration.h"
#include "../SceneRuntime/VansRuntimeComponentTypes.h"
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
#include <unordered_map>

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

std::shared_ptr<Vans::VansTimelineApplierRegistry> BuildTimelineAppliers(
	VansGraphics::VansScene& scene,
	Vans::VansRuntimeWorld& world,
	std::string& error)
{
	auto registry = std::make_shared<Vans::VansTimelineApplierRegistry>();
	if (!Vans::VansRegisterTransformTimelineIntegration(
		world, VansCreateTimelineTransformAccess(scene, world), *registry, error)) return {};
	if (!Vans::VansRegisterActivationTimelineIntegration(world, *registry, error)) return {};
	if (!Vans::VansRegisterPropertyTimelineIntegration(world,
		Vans::VansTimelinePropertyAccessRegistry::BuiltIns(), *registry, error)) return {};
	if (!Vans::VansRegisterAudioTimelineIntegration(world, *scene.GetAudioManager(), *registry, error)) return {};
	if (!Vans::VansRegisterParticleTimelineIntegration(world, *registry, error)) return {};
	if (!Vans::VansRegisterUITimelineIntegration(*registry, error)) return {};
	if (!Vans::VansRegisterAnimationTimelineIntegration(
		world, Vans::VansProjectManager::Get().GetAssetObjectRepository(),
		*registry, error)) return {};
	if (!VansGraphics::VansRegisterMediaTimelineIntegration(world, *scene.GetVideoManager(), *registry, error)) return {};
	if (!VansGraphics::VansRegisterRenderPropertyTimelineIntegration(scene, world, *registry, error)) return {};
	if (!VansGraphics::VansRegisterPostProcessTimelineIntegration(
		scene.GetMaterialManager()->m_PostProcessProfile,
		Vans::VansProjectManager::Get().GetAssetObjectRepository(), *registry, error)) return {};
	if (!scene.GetGameplayRuntime())
	{
		error = "Timeline Gameplay Action integration requires the scene GameplayRuntime";
		return {};
	}
	if (!Vans::VansRegisterGameplayActionTimelineIntegration(
		*scene.GetGameplayRuntime(), *registry, error)) return {};
	if (!scene.GetCamera()) { error = "Timeline camera integration requires the scene camera"; return {}; }
	// Camera control is a scene-owned service because it arbitrates Timeline with camera scripts.
	// It is constructed before the registry so appliers never capture an ephemeral adapter object.
	return registry;
}
}

void VansGraphics::VansScene::ConfigureTimelineRuntime()
{
	if (!m_RuntimeWorld) return;
	if (!m_TimelineRuntime) m_TimelineRuntime = std::make_unique<Vans::VansTimelineRuntimeSystem>();
	if (!m_CameraControlArbiter) m_CameraControlArbiter = std::make_unique<VansCameraControlArbiter>();
	if (!m_VirtualCameraParameters) m_VirtualCameraParameters = std::make_unique<VansVirtualCameraParameterStore>();
	std::string integrationError;
	auto appliers = BuildTimelineAppliers(*this, *m_RuntimeWorld, integrationError);
	if (!appliers)
	{
		VANS_LOG_ERROR("[Timeline] " << integrationError);
		return;
	}
	if (!VansRegisterCameraTimelineIntegration(
		*m_RuntimeWorld, *m_Camera, *m_CameraControlArbiter, *m_VirtualCameraParameters,
		*appliers, integrationError) ||
		!appliers->Seal(integrationError))
	{
		VANS_LOG_ERROR("[Timeline] " << integrationError);
		return;
	}
	VANS_LOG("[Timeline] Runtime integration configured: loadMode=" <<
		(m_LoadMode == VansSceneLoadMode::Runtime ? "Runtime" : "Editor"));
	m_TimelineRuntime->RegisterWorld(m_RuntimeWorld.get());
	m_TimelineRuntime->SetApplierRegistry(std::move(appliers));
	auto payloads = std::make_shared<Vans::VansPayloadSchemaRegistry>();
	std::string payloadError;
	if (!payloads->Seal(payloadError))
	{
		VANS_LOG_ERROR("[Timeline] " << payloadError);
		return;
	}
	m_TimelineRuntime->SetPayloadSchemaRegistry(std::move(payloads));

	using CacheEntry = std::pair<std::uint64_t, std::weak_ptr<const Vans::VansCompiledTimeline>>;
	auto cache = std::make_shared<std::unordered_map<std::string, CacheEntry>>();
	auto resolveTimeline = [](const std::string& guidText, std::uint64_t* generation,
		std::string& error) -> std::shared_ptr<const Vans::VansTimelineAsset>
	{
		Vans::VansAssetGuid guid;
		if (!Vans::VansAssetGuid::TryParse(guidText, guid))
		{
			error = "Timeline asset GUID is invalid: " + guidText;
			return {};
		}
		Vans::VansAssetObjectSnapshotInfo info;
		const auto& repository = Vans::VansProjectManager::Get().GetAssetObjectRepository();
		if (!repository.FindInfo(guid, info) || info.assetType != Vans::VansAssetType::Timeline)
		{
			error = "Timeline asset is not loaded in the object repository: " + guidText;
			return {};
		}
		if (generation)
			*generation = info.generation;
		auto asset = repository.ResolveLatest<Vans::VansTimelineAsset>(guid);
		if (!asset)
			error = "Timeline repository entry has the wrong decoded object type: " + guidText;
		return asset;
	};
	m_TimelineRuntime->SetAssetGenerationQuery([resolveTimeline](const std::string& guid, std::uint64_t& generation)
	{
		std::string error;
		return resolveTimeline(guid, &generation, error) != nullptr;
	});
	Vans::VansTimelineRuntimeSystem* timelineRuntime = m_TimelineRuntime.get();
	m_TimelineRuntime->SetAssetLoader([resolveTimeline, cache, timelineRuntime](
		const Vans::VansRuntimeTimelineComponent& component,
		std::shared_ptr<const Vans::VansCompiledTimeline>& timeline,
		std::string& error)
	{
		std::uint64_t generation = 0;
		const auto asset = resolveTimeline(component.assetGuid, &generation, error);
		if (!asset) return false;
		if (const auto found = cache->find(component.assetGuid); found != cache->end() && found->second.first == generation)
			if (timeline = found->second.second.lock()) return true;
		Vans::VansTimelineCompileOptions options;
		options.extensions = &Vans::VansTimelineTrackExtensionRegistry::BuiltIns();
		options.validation.runtimeValidation = true;
		options.validation.hasOutputApplier = [timelineRuntime](Vans::VansTimelineOutputTypeId type)
		{ return timelineRuntime->HasOutputApplier(type); };
		options.validation.hasPayloadSchema = [timelineRuntime](Vans::VansTimelinePayloadTypeId type)
		{ return timelineRuntime->HasPayloadSchema(type); };
		options.validation.validatePayload = [timelineRuntime](Vans::VansTimelinePayloadTypeId type,
			const Vans::VansSerializedValue& payload, std::string& payloadError)
		{ return timelineRuntime->ValidatePayload(type, payload, payloadError); };
		options.dependencyLoader = [resolveTimeline](const Vans::VansTimelineDependency& dependency,
			Vans::VansTimelineAsset& nested, std::string& identity, std::string& nestedError)
		{
			if (dependency.stableType != "Timeline") return true;
			if (dependency.guid.empty())
			{
				nestedError = "SubTimeline dependencies require indexed asset GUIDs";
				return false;
			}
			const auto child = resolveTimeline(dependency.guid, nullptr, nestedError);
			if (!child) return false;
			identity = dependency.guid;
			nested = *child;
			return true;
		};
		Vans::VansTimelineCompileResult result = Vans::VansTimelineCompiler::Compile(*asset, options);
		if (!result)
		{
			error = TimelineDiagnosticsText(result.diagnostics);
			if (error.empty()) error = "Timeline compilation failed";
			return false;
		}
		timeline = result.timeline;
		(*cache)[component.assetGuid] = { generation, timeline };
		return true;
	});
	if (m_LoadMode == VansSceneLoadMode::Runtime) m_TimelineRuntime->SyncTimelineComponents();
}

void VansGraphics::VansScene::UpdateTimelinesPostScript(double deltaSeconds)
{
	if (!m_TimelineRuntime) return;
	m_TimelineRuntime->UpdateRuntimePostScript(deltaSeconds);
	if (m_RuntimeWorld) m_RuntimeWorld->FlushCommands();
}

void VansGraphics::VansScene::BeginCameraControlFrame()
{
	if (m_Camera && m_CameraControlArbiter) m_CameraControlArbiter->BeginFrame(*m_Camera);
}

void VansGraphics::VansScene::CaptureCameraControlBase()
{
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
	if (m_RuntimeWorld) m_RuntimeWorld->FlushCommands();
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
	const auto* storage = static_cast<const Vans::VansComponentStorage<Vans::VansRuntimeTimelineComponent>*>(
		m_RuntimeWorld->FindStorage(Vans::VansRuntimeComponentType_Timeline));
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
	Vans::VansTimelineCompileOptions options;
	options.extensions = &Vans::VansTimelineTrackExtensionRegistry::BuiltIns();
	options.validation.runtimeValidation = false;
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
