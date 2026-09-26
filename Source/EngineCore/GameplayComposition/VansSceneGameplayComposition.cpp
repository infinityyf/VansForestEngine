#include "VansSceneGameplayComposition.h"

#include "../AssetCore/VansAssetObjectRepository.h"
#include "../GameplayActionAdapters/VansSceneGameplayContributors.h"
#include "../GameplayActionCore/VansGameplayRuntime.h"
#include "../GameplayActionSchema/VansGAFProjectConfiguration.h"
#include "../PhysicsCore/VansCollisionLayerManager.h"
#include "../PhysicsCore/VansPhysicsQuery.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../RenderCore/Timeline/VansVirtualCameraParameterStore.h"
#include "../RenderCore/VansCameraControlArbiter.h"
#include "../RenderCore/VansScene.h"
#include "../SceneRuntime/Transform/VansTransformStore.h"
#include "../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../Timeline/VansEngineTimelineRegistry.h"
#include "../TimelineCore/VansTimelineCompiler.h"
#include "../TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "../TimelineRuntime/VansTimelineRuntimeSystem.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <memory>
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
}

bool Vans::VansSceneGameplayComposition::InitializeActionRuntime(
	VansGraphics::VansScene& scene,
	std::string& error)
{
	error.clear();
	if (!scene.m_RuntimeWorld)
		scene.m_RuntimeWorld = std::make_unique<VansRuntimeWorld>();
	if (!scene.m_CameraControlArbiter)
		scene.m_CameraControlArbiter =
			std::make_unique<VansGraphics::VansCameraControlArbiter>();
	if (!scene.m_CameraControlArbiter->Runtime().SetLensLimits(
		scene.m_CameraLensLimits, error))
	{
		error = "Camera runtime lens limits are invalid: " + error;
		return false;
	}

	scene.m_CameraControlArbiter->Runtime().SetBindingResolver(
		[&scene](VansGenerationHandle context, std::string_view,
			VansCameraBindingSnapshot& binding)
		{
			const VansEntityHandle entity{ context.index, context.generation };
			if (!scene.m_RuntimeWorld || !scene.m_RuntimeWorld->IsAlive(entity)) return false;
			auto* storage = scene.m_RuntimeWorld->FindStorage<VansRuntimeTransformComponent>(
				VansRuntimeComponentType_Transform);
			if (!storage) return false;
			for (VansComponentHandle component :
				scene.m_RuntimeWorld->CollectComponentsOwnedBy(entity))
			{
				if (component.typeId != VansRuntimeComponentType_Transform) continue;
				const VansRuntimeTransformComponent* runtimeTransform = storage->Get(component);
				if (!runtimeTransform || runtimeTransform->transformStoreId == UINT32_MAX) return false;
				const VansTransform& transform =
					VansTransformStore::Read(runtimeTransform->transformStoreId);
				binding.pose.position = transform.m_Position;
				binding.pose.rotationDegrees = transform.m_Rotation;
				return true;
			}
			return false;
		});

	scene.m_CameraControlArbiter->Runtime().SetCollisionResolver(
		[&scene](const VansCameraCollisionQuery& query, VansCameraCollisionResult& result)
		{
			const glm::vec3 delta = query.desiredPosition - query.origin;
			const float distance = glm::length(delta);
			if (distance <= 0.0001f) return false;
			if (!VansEngine::VansPhysicsQuery::IsAvailable()) return false;
			std::uint32_t layerMask = 0u;
			auto& layerManager = VansEngine::VansCollisionLayerManager::Get();
			for (const std::string& name : query.layers)
			{
				int index = -1;
				if (!layerManager.TryGetLayerIndex(name, index))
					return false;
				layerMask |= 1u << static_cast<std::uint32_t>(index);
			}
			std::uint32_t ignoredTransform = UINT32_MAX;
			const VansEntityHandle entity{
				query.bindingContext.index, query.bindingContext.generation };
			if (scene.m_RuntimeWorld && scene.m_RuntimeWorld->IsAlive(entity))
			{
				auto* storage = scene.m_RuntimeWorld->FindStorage<VansRuntimeTransformComponent>(
					VansRuntimeComponentType_Transform);
				if (storage)
					for (VansComponentHandle component :
						scene.m_RuntimeWorld->CollectComponentsOwnedBy(entity))
						if (component.typeId == VansRuntimeComponentType_Transform)
						{
							if (const auto* transform = storage->Get(component))
								ignoredTransform = transform->transformStoreId;
							break;
						}
			}
			VansEngine::VansPhysicsSphereSweepRequest request;
			request.origin = query.origin;
			request.direction = delta;
			request.distance = distance;
			request.radius = (std::max)(query.radius, 0.001f);
			request.filter.layerMask = query.layers.empty() ? 0xFFFFFFFFu : layerMask;
			request.filter.ignoredTransformId = ignoredTransform;
			request.filter.includeTriggers = false;
			VansEngine::VansPhysicsQueryHit hit;
			result.blocked = VansEngine::VansPhysicsQuery::SweepSphereClosest(request, hit);
			result.distance = result.blocked ? hit.distance : distance;
			return true;
		});

	if (!scene.m_GameplayRuntime)
		scene.m_GameplayRuntime = std::make_unique<VansGameplayRuntime>();
	if (scene.m_GameplayRuntime->IsInitialized()) return true;

	VansProjectManager& projectManager = VansProjectManager::Get();
	const VansGAFProjectConfiguration* configuration =
		projectManager.GetGAFProjectConfiguration();
	if (!configuration)
	{
		error = "GAF project configuration is unavailable in memory";
		return false;
	}
	if (!scene.m_TimelineRuntime)
	{
		const VansEngineTimelineCatalog catalog = VansGetEngineTimelineCatalog();
		if (!catalog)
		{
			error = std::string(catalog.error);
			return false;
		}
		scene.m_TimelineRuntime = std::make_unique<VansTimelineRuntimeSystem>(*catalog.clocks);
	}
	auto resolvePosition = [&scene](VansEntityHandle entity, glm::vec3& position)
	{
		if (!scene.m_RuntimeWorld || !scene.m_RuntimeWorld->IsAlive(entity)) return false;
		auto* storage = scene.m_RuntimeWorld->FindStorage<VansRuntimeTransformComponent>(
			VansRuntimeComponentType_Transform);
		if (!storage) return false;
		for (VansComponentHandle component : scene.m_RuntimeWorld->CollectComponentsOwnedBy(entity))
		{
			if (component.typeId != VansRuntimeComponentType_Transform) continue;
			const VansRuntimeTransformComponent* transform = storage->Get(component);
			if (!transform || transform->transformStoreId == UINT32_MAX) return false;
			position = VansTransformStore::Read(transform->transformStoreId).m_Position;
			return true;
		}
		return false;
	};
	VansGameplayRuntimeDependencies dependencies;
	const VansSceneGameplayContributorContext contributorContext{
		*scene.m_RuntimeWorld,
		*scene.m_GameplayRuntime,
		scene.m_CameraControlArbiter->Runtime(),
		*scene.m_TimelineRuntime,
		std::move(resolvePosition),
		scene.MakeProjectileSceneBackend(),
		&scene.m_AudioManager,
		scene.MakeDecalSceneBackend(),
		scene.MakeCombatSceneBackend(),
		scene.MakeVFXSceneBackend()
	};
	if (!VansDiscoverSceneGameplayContributors(
		*configuration, contributorContext, dependencies, error))
		return false;
	return scene.m_GameplayRuntime->Initialize(
		projectManager.EnumerateAssetRecords(),
		projectManager.GetAssetObjectRepository(),
		configuration->settings,
		dependencies,
		error);
}

bool Vans::VansSceneGameplayComposition::ConfigureTimelineRuntime(
	VansGraphics::VansScene& scene,
	std::string& error)
{
	error.clear();
	if (!scene.m_RuntimeWorld)
	{
		error = "Timeline runtime requires the scene RuntimeWorld";
		return false;
	}
	if (!scene.m_GameplayRuntime || !scene.m_GameplayRuntime->IsInitialized())
	{
		error = "Timeline runtime requires an initialized GameplayRuntime";
		return false;
	}
	if (!scene.m_Camera)
	{
		error = "Timeline runtime requires the scene camera";
		return false;
	}
	const VansEngineTimelineCatalog catalog = VansGetEngineTimelineCatalog();
	if (!catalog)
	{
		error = std::string(catalog.error);
		return false;
	}
	const auto payloads = scene.m_GameplayRuntime->Assets().PayloadSchemas();
	if (!payloads || !payloads->IsSealed())
	{
		error = "Timeline runtime requires the sealed Gameplay payload registry";
		return false;
	}
	if (!scene.m_TimelineRuntime)
		scene.m_TimelineRuntime = std::make_unique<VansTimelineRuntimeSystem>(*catalog.clocks);
	if (!scene.m_CameraControlArbiter)
	{
		scene.m_CameraControlArbiter =
			std::make_unique<VansGraphics::VansCameraControlArbiter>();
		if (!scene.m_CameraControlArbiter->Runtime().SetLensLimits(
			scene.m_CameraLensLimits, error))
		{
			error = "Camera runtime lens limits are invalid: " + error;
			return false;
		}
	}
	if (!scene.m_VirtualCameraParameters)
		scene.m_VirtualCameraParameters =
			std::make_unique<VansGraphics::VansVirtualCameraParameterStore>();
	std::string integrationError;
	auto registries = VansBuildEngineTimelineRegistries({
		scene,
		*scene.m_RuntimeWorld,
		*scene.m_GameplayRuntime,
		*scene.m_Camera,
		*scene.m_CameraControlArbiter,
		*scene.m_VirtualCameraParameters
	}, integrationError);
	if (!registries)
	{
		error = std::move(integrationError);
		return false;
	}
	scene.m_TimelineRuntime->RegisterWorld(scene.m_RuntimeWorld.get());
	if (!scene.m_TimelineRuntime->SetApplierRegistry(
		std::move(registries.appliers), registries.manifestHash, error)) return false;
	if (!scene.m_TimelineRuntime->SetPayloadSchemaRegistry(payloads, error)) return false;

	using CacheEntry = std::pair<std::uint64_t, std::weak_ptr<const VansCompiledTimeline>>;
	auto cache = std::make_shared<std::unordered_map<std::string, CacheEntry>>();
	auto resolveTimeline = [](const std::string& guidText, std::uint64_t* generation,
		std::string& resolveError) -> std::shared_ptr<const VansTimelineAsset>
	{
		VansAssetGuid guid;
		if (!VansAssetGuid::TryParse(guidText, guid))
		{
			resolveError = "Timeline asset GUID is invalid: " + guidText;
			return {};
		}
		VansAssetObjectSnapshotInfo info;
		const auto& repository = VansProjectManager::Get().GetAssetObjectRepository();
		if (!repository.FindInfo(guid, info) || info.assetType != VansAssetType::Timeline)
		{
			resolveError = "Timeline asset is not loaded in the object repository: " + guidText;
			return {};
		}
		if (generation) *generation = info.generation;
		auto asset = repository.ResolveLatest<VansTimelineAsset>(guid);
		if (!asset)
			resolveError = "Timeline repository entry has the wrong decoded object type: " + guidText;
		return asset;
	};
	scene.m_TimelineRuntime->SetAssetGenerationQuery(
		[resolveTimeline](const std::string& guid, std::uint64_t& generation)
		{
			std::string resolveError;
			return resolveTimeline(guid, &generation, resolveError) != nullptr;
		});
	VansTimelineRuntimeSystem* timelineRuntime = scene.m_TimelineRuntime.get();
	const VansTimelineTrackExtensionRegistry* trackExtensions = catalog.trackExtensions;
	scene.m_TimelineRuntime->SetAssetLoader(
		[resolveTimeline, cache, timelineRuntime, trackExtensions](
			const VansRuntimeTimelineComponent& component,
			std::shared_ptr<const VansCompiledTimeline>& timeline,
			std::string& loadError)
		{
			std::uint64_t generation = 0;
			const auto asset = resolveTimeline(component.assetGuid, &generation, loadError);
			if (!asset) return false;
			VansTimelineCompileOptions options;
			options.extensions = trackExtensions;
			options.runtimeRegistryManifestHash = timelineRuntime->RuntimeRegistryManifestHash();
			const std::uint64_t expectedManifest = VansTimelineCompiler::RegistryManifestHash(options);
			if (const auto found = cache->find(component.assetGuid);
				found != cache->end() && found->second.first == generation)
			{
				timeline = found->second.second.lock();
				if (timeline && timeline->RegistryManifestHash() == expectedManifest) return true;
				timeline.reset();
			}
			options.validation.requireRuntimeCapabilities = true;
			options.validation.hasOutputApplier = [timelineRuntime](VansTimelineOutputTypeId type)
			{ return timelineRuntime->HasOutputApplier(type); };
			options.validation.hasPayloadSchema = [timelineRuntime](VansTimelinePayloadTypeId type)
			{ return timelineRuntime->HasPayloadSchema(type); };
			options.validation.validatePayload = [timelineRuntime](
				VansTimelinePayloadTypeId type,
				const VansSerializedValue& payload,
				std::string& payloadError)
			{ return timelineRuntime->ValidatePayload(type, payload, payloadError); };
			options.dependencyLoader = [resolveTimeline](
				const VansTimelineDependency& dependency,
				VansTimelineAsset& nested,
				std::string& identity,
				std::string& nestedError)
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
			VansTimelineCompileResult result = VansTimelineCompiler::Compile(*asset, options);
			if (!result)
			{
				loadError = TimelineDiagnosticsText(result.diagnostics);
				if (loadError.empty()) loadError = "Timeline compilation failed";
				return false;
			}
			timeline = result.timeline;
			(*cache)[component.assetGuid] = { generation, timeline };
			return true;
		});
	if (scene.m_LoadMode == VansGraphics::VansSceneLoadMode::Runtime)
		scene.m_TimelineRuntime->SyncTimelineComponents();
	VANS_LOG("[Timeline] Runtime integration configured: loadMode=" <<
		(scene.m_LoadMode == VansGraphics::VansSceneLoadMode::Runtime ? "Runtime" : "Editor"));
	return true;
}
