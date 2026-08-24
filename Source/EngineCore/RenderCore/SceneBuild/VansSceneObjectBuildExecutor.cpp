#include "../VansScene.h"

#include "VansSceneAnimationComponentBuilder.h"
#include "VansSceneCameraMediaComponentBuilder.h"
#include "VansSceneClothAnimationBindingExecutor.h"
#include "VansSceneLightComponentBuilder.h"
#include "VansSceneParticleComponentBuilder.h"
#include "VansScenePhysicsComponentBuilder.h"
#include "VansSceneRenderNodeBuilder.h"
#include "VansSceneScriptComponentBuilder.h"
#include "VansSceneVehicleComponentBuilder.h"
#include "../../SceneCore/VansSceneObjectBuildPlan.h"
#include "../../SceneCore/VansSceneRuntimeComponentKey.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../GameplayActionCore/VansGameplayRuntime.h"
#include "../../GameplayActionAdapters/Camera/VansCameraActionService.h"
#include "../../CameraGameplayAction/VansCameraActionGraphNodes.h"
#include "../../GameplayActionSchema/VansGAFProjectConfiguration.h"
#include "../../PhysicsCore/VansCollisionLayerManager.h"
#include "../../PhysicsCore/VansPhysics.h"
#include "../../PhysicsCore/VansPhysicsNode.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../ScriptCore/VansTransform.h"
#include "../../Util/VansLog.h"
#include "../VulkanCore/VansMesh.h"
#include "../VansCameraControlArbiter.h"

#include <algorithm>
#include <array>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
class CameraCollisionQueryFilter final : public physx::PxQueryFilterCallback
{
public:
	CameraCollisionQueryFilter(std::unordered_set<physx::PxU32> layers,
		std::uint32_t ignoredTransform)
		: m_Layers(std::move(layers)), m_FilterByLayer(!m_Layers.empty()),
		  m_IgnoredTransform(ignoredTransform) {}

	physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData&,
		const physx::PxShape* shape, const physx::PxRigidActor* actor,
		physx::PxHitFlags&) override
	{
		return Filter(shape, actor);
	}

	physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData&,
		const physx::PxQueryHit&, const physx::PxShape* shape,
		const physx::PxRigidActor* actor) override
	{
		return Filter(shape, actor);
	}

private:
	physx::PxQueryHitType::Enum Filter(const physx::PxShape* shape,
		const physx::PxRigidActor* actor) const
	{
		if (!shape) return physx::PxQueryHitType::eNONE;
		const physx::PxFilterData target = shape->getQueryFilterData();
		if ((target.word2 & 0x1u) != 0u ||
			(m_FilterByLayer && m_Layers.find(target.word0) == m_Layers.end()))
			return physx::PxQueryHitType::eNONE;
		if (m_IgnoredTransform != UINT32_MAX && actor && actor->userData)
		{
			auto* node = static_cast<VansEngine::VansPhysicsNode*>(actor->userData);
			if (node && node->GetTransformID() == m_IgnoredTransform)
				return physx::PxQueryHitType::eNONE;
		}
		return physx::PxQueryHitType::eBLOCK;
	}

	std::unordered_set<physx::PxU32> m_Layers;
	bool m_FilterByLayer = false;
	std::uint32_t m_IgnoredTransform = UINT32_MAX;
};

struct RuntimeComponentBuildResults
{
	VansScriptRenderComponent* render = nullptr;
	VansScriptAudioReverbZoneComponent* audioReverbZone = nullptr;
	VansScriptParticleComponent* particle = nullptr;
	VansGraphics::VansSceneCameraMediaBuildResult cameraMedia;
	VansGraphics::VansScenePhysicsBuildResult physics;
	VansGraphics::VansSceneScriptBuildResult scripts;
	VansGraphics::VansSceneLightBuildResult lights;
};

glm::vec3 ToVec3(const std::array<float, 3>& value)
{
	return glm::vec3(value[0], value[1], value[2]);
}

void ApplyRuntimeComponentGuids(
	VansScriptObject& object,
	const std::unordered_map<std::string, std::string>& componentGuids)
{
	for (VansScriptComponent* component : object.m_Components)
	{
		if (!component)
			continue;
		const std::string key = Vans::CanonicalRuntimeComponentKeyForName(component->m_ComponentName);
		const auto found = componentGuids.find(key);
		if (found != componentGuids.end())
			component->m_ComponentGuid = found->second;
	}
}

std::uint32_t ResolveRuntimeTransformStoreId(const VansScriptObject& object)
{
	if (auto* renderComponent = object.GetComponent<VansScriptRenderComponent>())
		return renderComponent->m_RenderNode ? renderComponent->m_RenderNode->m_TransformID : UINT32_MAX;
	return object.m_OwnsTransform ? object.m_TransformID : UINT32_MAX;
}

std::string FindRuntimeComponentGuid(
	const std::unordered_map<std::string, std::string>& componentGuids,
	const std::string& key)
{
	const auto found = componentGuids.find(key);
	return found != componentGuids.end() ? found->second : std::string();
}

Vans::VansRuntimeScriptFieldType ToRuntimeScriptFieldType(VansScriptSerializedFieldType type)
{
	switch (type)
	{
	case VansScriptSerializedFieldType::Bool:
		return Vans::VansRuntimeScriptFieldType::Bool;
	case VansScriptSerializedFieldType::Int:
		return Vans::VansRuntimeScriptFieldType::Int;
	case VansScriptSerializedFieldType::Float:
		return Vans::VansRuntimeScriptFieldType::Float;
	case VansScriptSerializedFieldType::String:
		return Vans::VansRuntimeScriptFieldType::String;
	case VansScriptSerializedFieldType::ObjectReference:
		return Vans::VansRuntimeScriptFieldType::ObjectReference;
	case VansScriptSerializedFieldType::Null:
	default:
		return Vans::VansRuntimeScriptFieldType::Null;
	}
}

Vans::VansRuntimeScriptState ToRuntimeScriptState(VansLuaScriptState state)
{
	switch (state)
	{
	case VansLuaScriptState::Loading:
		return Vans::VansRuntimeScriptState::Loading;
	case VansLuaScriptState::Active:
		return Vans::VansRuntimeScriptState::Active;
	case VansLuaScriptState::Disabled:
		return Vans::VansRuntimeScriptState::Disabled;
	case VansLuaScriptState::Faulted:
		return Vans::VansRuntimeScriptState::Faulted;
	case VansLuaScriptState::Destroyed:
		return Vans::VansRuntimeScriptState::Destroyed;
	case VansLuaScriptState::Unloaded:
	default:
		return Vans::VansRuntimeScriptState::Unloaded;
	}
}

Vans::VansRuntimeScriptComponent BuildRuntimeScriptComponent(const VansLuaScriptComponent& component)
{
	Vans::VansRuntimeScriptComponent runtimeScript;
	runtimeScript.scriptPath = component.m_ScriptPath;
	runtimeScript.entryName = component.m_EntryName;
	runtimeScript.enableRequested = component.m_EnableRequested;
	runtimeScript.state = ToRuntimeScriptState(component.m_State);
	runtimeScript.isValid = component.m_IsValid;
	runtimeScript.hasStarted = component.m_HasStarted;
	for (const auto& [name, field] : component.m_SerializedFields)
	{
		Vans::VansRuntimeScriptFieldValue runtimeField;
		runtimeField.type = ToRuntimeScriptFieldType(field.type);
		runtimeField.boolValue = field.boolValue;
		runtimeField.intValue = field.intValue;
		runtimeField.floatValue = field.floatValue;
		runtimeField.stringValue = field.stringValue;
		runtimeField.objectReference = field.objectReference;
		runtimeScript.serializedFields.emplace(name, std::move(runtimeField));
	}
	return runtimeScript;
}

void RegisterDeferredAnimationRuntimeComponents(
	VansGraphics::VansScene& scene,
	Vans::VansRuntimeWorld& runtimeWorld,
	const std::vector<Vans::VansSceneObjectBuildConfig>& objectConfigs)
{
	for (const Vans::VansSceneObjectBuildConfig& objectConfig : objectConfigs)
	{
		if (objectConfig.entityGuid.empty())
			continue;
		VansScriptObject* object = scene.FindObjectByGuid(objectConfig.entityGuid);
		const Vans::VansEntityHandle entity = runtimeWorld.Entities().FindByGuid(objectConfig.entityGuid);
		if (!object || !entity.IsValid())
			continue;

		if (auto* animationComponent = object->GetComponent<VansScriptAnimationComponent>())
		{
			const std::string animationGuid =
				FindRuntimeComponentGuid(objectConfig.componentGuids, "animation");
			if (!animationGuid.empty())
			{
				const VansGraphics::VansSkeletonInstanceHandle skeletonInstance =
					animationComponent->m_AnimNode
						? scene.RegisterSkeletonInstance(*animationComponent->m_AnimNode)
						: VansGraphics::VansSkeletonInstanceHandle{};
				runtimeWorld.Commands().AddAnimationComponent(
					entity,
					animationGuid,
					animationComponent->m_AnimNode,
					skeletonInstance.id,
					skeletonInstance.generation,
					animationComponent->IsEnabled());
			}
		}

		if (auto* ragdollComponent = object->GetComponent<VansScriptRagdollComponent>())
		{
			runtimeWorld.Commands().AddRagdollComponent(
				entity,
				FindRuntimeComponentGuid(objectConfig.componentGuids, "ragdoll"),
				ragdollComponent->m_AnimNode,
				static_cast<std::uint8_t>(ragdollComponent->m_InitialDriveMode),
				ragdollComponent->m_ProfilePath,
				ragdollComponent->m_ProfileName,
				ragdollComponent->m_ConfiguredBodyCount,
				ragdollComponent->m_ConfiguredJointCount,
				ragdollComponent->IsEnabled());
		}
	}
	runtimeWorld.FlushCommands();
}

void RegisterDeferredVehicleRuntimeComponents(
	Vans::VansRuntimeWorld& runtimeWorld,
	const std::vector<Vans::VansSceneObjectBuildConfig>& objectConfigs,
	const std::vector<VansScriptVehicleComponent*>& vehicleComponents)
{
	const std::size_t count = std::min(objectConfigs.size(), vehicleComponents.size());
	for (std::size_t i = 0; i < count; ++i)
	{
		const Vans::VansSceneObjectBuildConfig& objectConfig = objectConfigs[i];
		VansScriptVehicleComponent* vehicleComponent = vehicleComponents[i];
		if (!objectConfig.vehicleObject.vehicle || !vehicleComponent)
			continue;

		const Vans::VansEntityHandle entity =
			runtimeWorld.Entities().FindByGuid(objectConfig.entityGuid);
		if (!entity.IsValid())
			continue;

		runtimeWorld.Commands().AddVehicleComponent(
			entity,
			vehicleComponent->m_ComponentGuid,
			vehicleComponent->m_Vehicle,
			vehicleComponent->IsEnabled());
	}
	runtimeWorld.FlushCommands();
}

void QueueCameraMediaRuntimeComponents(
	Vans::VansRuntimeWorld& runtimeWorld,
	Vans::VansEntityHandle entity,
	const VansGraphics::VansSceneCameraMediaBuildResult& cameraMedia,
	std::vector<std::pair<VansScriptComponent*, std::uint16_t>>& registrationChecks)
{
	if (cameraMedia.camera)
	{
		runtimeWorld.Commands().AddCameraComponent(
			entity,
			cameraMedia.camera->m_ComponentGuid,
			cameraMedia.camera->m_Camera,
			cameraMedia.camera->IsEnabled());
		registrationChecks.push_back({ cameraMedia.camera, Vans::VansRuntimeComponentType_Camera });
	}

	if (cameraMedia.audio)
	{
		runtimeWorld.Commands().AddAudioComponent(
			entity,
			cameraMedia.audio->m_ComponentGuid,
			cameraMedia.audio->m_Source.GetNode(),
			&cameraMedia.audio->m_Source,
			cameraMedia.audio->m_Source.GetSourceName(),
			cameraMedia.audio->m_ConeSettings,
			cameraMedia.audio->m_DopplerEnabled,
			cameraMedia.audio->m_HasLastAudioPosition,
			cameraMedia.audio->m_LastAudioPositionX,
			cameraMedia.audio->m_LastAudioPositionY,
			cameraMedia.audio->m_LastAudioPositionZ,
			cameraMedia.audio->m_OcclusionSettings,
			cameraMedia.audio->m_OcclusionState,
			cameraMedia.audio->IsEnabled());
		registrationChecks.push_back({ cameraMedia.audio, Vans::VansRuntimeComponentType_Audio });
	}

	if (cameraMedia.video)
	{
		runtimeWorld.Commands().AddVideoComponent(
			entity,
			cameraMedia.video->m_ComponentGuid,
			cameraMedia.video->m_VideoTex,
			cameraMedia.video->m_VideoManager,
			cameraMedia.video->m_BindlessFirstSlot,
			cameraMedia.video->IsEnabled());
		registrationChecks.push_back({ cameraMedia.video, Vans::VansRuntimeComponentType_Video });
	}
}

void QueueRenderRuntimeComponent(
	Vans::VansRuntimeWorld& runtimeWorld,
	Vans::VansEntityHandle entity,
	VansScriptRenderComponent* renderComponent,
	std::vector<std::pair<VansScriptComponent*, std::uint16_t>>& registrationChecks)
{
	if (!renderComponent)
		return;

	runtimeWorld.Commands().AddRenderComponent(
		entity,
		renderComponent->m_ComponentGuid,
		renderComponent->m_RenderNode,
		renderComponent->m_RenderNodes,
		renderComponent->IsEnabled());
	registrationChecks.push_back({ renderComponent, Vans::VansRuntimeComponentType_Render });
}

void QueuePhysicsRuntimeComponents(
	Vans::VansRuntimeWorld& runtimeWorld,
	Vans::VansEntityHandle entity,
	const VansGraphics::VansScenePhysicsBuildResult& physicsBuild,
	std::vector<std::pair<VansScriptComponent*, std::uint16_t>>& registrationChecks)
{
	if (physicsBuild.physics)
	{
		runtimeWorld.Commands().AddPhysicsComponent(
			entity,
			physicsBuild.physics->m_ComponentGuid,
			physicsBuild.physics->m_PhysicsNode,
			physicsBuild.physics->IsEnabled());
		registrationChecks.push_back({ physicsBuild.physics, Vans::VansRuntimeComponentType_Physics });
	}

	if (physicsBuild.cloth)
	{
		runtimeWorld.Commands().AddClothComponent(
			entity,
			physicsBuild.cloth->m_ComponentGuid,
			physicsBuild.cloth->m_ClothNode,
			physicsBuild.cloth->m_ProfilePath,
			physicsBuild.cloth->IsEnabled());
		registrationChecks.push_back({ physicsBuild.cloth, Vans::VansRuntimeComponentType_Cloth });
	}

	if (physicsBuild.characterController)
	{
		runtimeWorld.Commands().AddCharacterControllerComponent(
			entity,
			physicsBuild.characterController->m_ComponentGuid,
			physicsBuild.characterController->m_ControllerNode,
			physicsBuild.characterController->IsEnabled());
		registrationChecks.push_back({
			physicsBuild.characterController,
			Vans::VansRuntimeComponentType_CharacterController });
	}
}

void QueueAudioReverbZoneRuntimeComponent(
	Vans::VansRuntimeWorld& runtimeWorld,
	Vans::VansEntityHandle entity,
	VansScriptAudioReverbZoneComponent* reverbZoneComponent,
	std::vector<std::pair<VansScriptComponent*, std::uint16_t>>& registrationChecks)
{
	if (!reverbZoneComponent)
		return;

	const std::string key = Vans::CanonicalRuntimeComponentKeyForName(reverbZoneComponent->m_ComponentName);
	const std::uint16_t typeId = Vans::VansRuntimeComponentTypeIdForKey(key);
	if (typeId == Vans::VansInvalidComponentTypeId)
	{
		VANS_LOG_ERROR("[SceneBuild] Audio reverb component '"
			<< reverbZoneComponent->m_ComponentName << "' has no runtime type id");
		return;
	}

	Vans::VansRuntimeAudioReverbZoneComponent reverbZone;
	reverbZone.shape = reverbZoneComponent->m_Shape;
	reverbZone.preset = reverbZoneComponent->m_Preset;
	reverbZone.presetAssetGuid = reverbZoneComponent->m_PresetAssetGuid;
	reverbZone.presetParameters = reverbZoneComponent->m_PresetParameters;
	reverbZone.overridePresetParameters = reverbZoneComponent->m_OverridePresetParameters;
	reverbZone.radius = reverbZoneComponent->m_Radius;
	reverbZone.halfExtentX = reverbZoneComponent->m_HalfExtentX;
	reverbZone.halfExtentY = reverbZoneComponent->m_HalfExtentY;
	reverbZone.halfExtentZ = reverbZoneComponent->m_HalfExtentZ;
	reverbZone.fadeDistance = reverbZoneComponent->m_FadeDistance;
	reverbZone.wetGain = reverbZoneComponent->m_WetGain;
	reverbZone.priority = reverbZoneComponent->m_Priority;
	runtimeWorld.Commands().AddAudioReverbZoneComponent(
		entity,
		typeId,
		reverbZoneComponent->m_ComponentGuid,
		std::move(reverbZone),
		reverbZoneComponent->IsEnabled());
	registrationChecks.push_back({ reverbZoneComponent, typeId });
}

void QueueParticleRuntimeComponent(
	Vans::VansRuntimeWorld& runtimeWorld,
	Vans::VansEntityHandle entity,
	VansScriptParticleComponent* particleComponent,
	std::vector<std::pair<VansScriptComponent*, std::uint16_t>>& registrationChecks)
{
	if (!particleComponent)
		return;

	runtimeWorld.Commands().AddParticleComponent(
		entity,
		particleComponent->m_ComponentGuid,
		particleComponent->m_Runtime.get(),
		particleComponent->m_RenderNode,
		particleComponent->m_PlayOnAwake,
		particleComponent->m_IsPlaying,
		particleComponent->m_PlayTime,
		particleComponent->m_HasWorldPositionOverride,
		particleComponent->m_WorldPositionOverride.x,
		particleComponent->m_WorldPositionOverride.y,
		particleComponent->m_WorldPositionOverride.z,
		particleComponent->IsEnabled());
	registrationChecks.push_back({ particleComponent, Vans::VansRuntimeComponentType_Particle });
}

void QueueScriptRuntimeComponents(
	Vans::VansRuntimeWorld& runtimeWorld,
	Vans::VansEntityHandle entity,
	const VansGraphics::VansSceneScriptBuildResult& scripts,
	std::vector<std::pair<VansScriptComponent*, std::uint16_t>>& registrationChecks)
{
	for (VansScriptUIComponent* uiComponent : scripts.uiControllers)
	{
		if (!uiComponent)
			continue;

		Vans::VansRuntimeUIComponent runtimeUI;
		runtimeUI.autoOpenScreens = uiComponent->m_AutoOpenScreens;
		runtimeUI.preloadScreens = uiComponent->m_PreloadScreens;
		runtimeUI.openScreens.assign(uiComponent->m_OpenScreens.begin(), uiComponent->m_OpenScreens.end());
		runtimeWorld.Commands().AddUIComponent(
			entity,
			uiComponent->m_ComponentGuid,
			std::move(runtimeUI),
			uiComponent->IsEnabled());
		registrationChecks.push_back({ uiComponent, Vans::VansRuntimeComponentType_UI });
	}

	for (VansLuaScriptComponent* scriptComponent : scripts.scripts)
	{
		if (!scriptComponent)
			continue;

		runtimeWorld.Commands().AddScriptComponent(
			entity,
			scriptComponent->m_ComponentGuid,
			BuildRuntimeScriptComponent(*scriptComponent),
			scriptComponent->m_EnableRequested);
		registrationChecks.push_back({ scriptComponent, Vans::VansRuntimeComponentType_Script });
	}
}

void QueueLightRuntimeComponents(
	Vans::VansRuntimeWorld& runtimeWorld,
	Vans::VansEntityHandle entity,
	const VansGraphics::VansSceneLightBuildResult& lights,
	std::vector<std::pair<VansScriptComponent*, std::uint16_t>>& registrationChecks)
{
	auto queueLight = [&](
		VansScriptComponent* component,
		std::uint16_t typeId,
		VansGraphics::VansLightManager* lightManager,
		int lightIndex,
		Vans::VansRuntimeLightKind kind)
	{
		if (!component)
			return;

		runtimeWorld.Commands().AddLightComponent(
			entity,
			typeId,
			component->m_ComponentGuid,
			lightManager,
			lightIndex,
			kind,
			component->IsEnabled());
		registrationChecks.push_back({ component, typeId });
	};

	if (lights.directionalLight)
	{
		queueLight(
			lights.directionalLight,
			Vans::VansRuntimeComponentType_DirectionalLight,
			lights.directionalLight->m_LightManager,
			lights.directionalLight->m_LightIndex,
			Vans::VansRuntimeLightKind::Directional);
	}
	if (lights.pointLight)
	{
		queueLight(
			lights.pointLight,
			Vans::VansRuntimeComponentType_PointLight,
			lights.pointLight->m_LightManager,
			lights.pointLight->m_LightIndex,
			Vans::VansRuntimeLightKind::Point);
	}
	if (lights.spotLight)
	{
		queueLight(
			lights.spotLight,
			Vans::VansRuntimeComponentType_SpotLight,
			lights.spotLight->m_LightManager,
			lights.spotLight->m_LightIndex,
			Vans::VansRuntimeLightKind::Spot);
	}
	if (lights.rectLight)
	{
		queueLight(
			lights.rectLight,
			Vans::VansRuntimeComponentType_RectLight,
			lights.rectLight->m_LightManager,
			lights.rectLight->m_LightIndex,
			Vans::VansRuntimeLightKind::Rect);
	}
}

bool RegisterRuntimeComponents(
	Vans::VansRuntimeWorld& runtimeWorld,
	Vans::VansEntityHandle entity,
	VansScriptObject& object,
	const std::unordered_map<std::string, std::string>& componentGuids,
	const RuntimeComponentBuildResults& buildResults,
	const std::optional<Vans::VansSceneTimelineComponentConfig>& timelineConfig,
	const std::optional<Vans::VansGameplayActionHostSetup>& actionHostConfig,
	Vans::VansGameplayRuntime* gameplayRuntime,
	std::string& error)
{
	std::vector<std::pair<VansScriptComponent*, std::uint16_t>> registrationChecks;
	const auto transformGuid = componentGuids.find("transform");
	if (transformGuid != componentGuids.end() && !transformGuid->second.empty())
	{
		runtimeWorld.Commands().AddTransformComponent(
			entity,
			transformGuid->second,
			ResolveRuntimeTransformStoreId(object),
			true);
	}
	QueueRenderRuntimeComponent(
		runtimeWorld,
		entity,
		buildResults.render,
		registrationChecks);
	QueueCameraMediaRuntimeComponents(
		runtimeWorld,
		entity,
		buildResults.cameraMedia,
		registrationChecks);
	QueuePhysicsRuntimeComponents(
		runtimeWorld,
		entity,
		buildResults.physics,
		registrationChecks);
	QueueAudioReverbZoneRuntimeComponent(
		runtimeWorld,
		entity,
		buildResults.audioReverbZone,
		registrationChecks);
	QueueParticleRuntimeComponent(
		runtimeWorld,
		entity,
		buildResults.particle,
		registrationChecks);
	QueueScriptRuntimeComponents(
		runtimeWorld,
		entity,
		buildResults.scripts,
		registrationChecks);
	QueueLightRuntimeComponents(
		runtimeWorld,
		entity,
		buildResults.lights,
		registrationChecks);
	if (timelineConfig && timelineConfig->valid)
	{
		const std::string timelineComponentGuid = FindRuntimeComponentGuid(componentGuids, "timeline");
		Vans::VansRuntimeTimelineComponent timeline;
		timeline.assetGuid = timelineConfig->timelineAssetGuid;
		timeline.assetPath = timelineConfig->timelineAssetPath;
		timeline.instance = timelineConfig->instance;
		runtimeWorld.Commands().AddTimelineComponent(
			entity,
			timelineComponentGuid,
			std::move(timeline),
			timelineConfig->enabled);
		runtimeWorld.FlushCommands();
		const Vans::VansComponentHandle timelineComponent = runtimeWorld.FindComponentByGuid(
			timelineComponentGuid, Vans::VansRuntimeComponentType_Timeline);
		if (!timelineComponent.IsValid())
		{
			VANS_LOG_ERROR("[SceneBuild] Runtime command buffer did not add Timeline component guid='"
				<< timelineComponentGuid << "'");
		}
		else
		{
			VANS_LOG("[Timeline] Registered component='" << timelineComponentGuid
				<< "' asset='" << timelineConfig->timelineAssetGuid << "'");
		}
	}
	if (actionHostConfig)
	{
		if (!gameplayRuntime || !gameplayRuntime->IsInitialized())
		{
			error = "ActionHost requires an initialized Gameplay Runtime";
			return false;
		}
		const std::string actionHostComponentGuid =
			FindRuntimeComponentGuid(componentGuids, "action_host");
		if (actionHostComponentGuid.empty())
		{
			error = "ActionHost component is missing its stable component GUID";
			return false;
		}
		std::shared_ptr<Vans::VansActionHost> host =
			gameplayRuntime->CreateHost(entity, *actionHostConfig, error);
		if (!host)
			return false;
		runtimeWorld.Commands().AddActionHostComponent(
			entity,
			actionHostComponentGuid,
			std::move(host),
			actionHostConfig->enabled);
		runtimeWorld.FlushCommands();
		const Vans::VansComponentHandle actionHostComponent = runtimeWorld.FindComponentByGuid(
			actionHostComponentGuid, Vans::VansRuntimeComponentType_ActionHost);
		if (!actionHostComponent.IsValid())
		{
			error = "Runtime command buffer did not add ActionHost component '" +
				actionHostComponentGuid + "'";
			return false;
		}
	}
	runtimeWorld.FlushCommands();
	if (transformGuid != componentGuids.end() && !transformGuid->second.empty())
	{
		const Vans::VansComponentHandle runtimeTransformComponent = runtimeWorld.FindComponentByGuid(
			transformGuid->second,
			Vans::VansRuntimeComponentType_Transform);
		if (!runtimeTransformComponent.IsValid())
		{
			VANS_LOG_ERROR("[SceneBuild] Runtime command buffer did not add transform component guid='"
				<< transformGuid->second << "'");
		}
	}
	for (const auto& [component, typeId] : registrationChecks)
	{
		const Vans::VansComponentHandle runtimeComponent =
			runtimeWorld.FindComponentByGuid(component->m_ComponentGuid, typeId);
		if (!runtimeComponent.IsValid())
		{
			VANS_LOG_ERROR("[SceneBuild] Runtime command buffer did not add component '"
				<< component->m_ComponentName << "' guid='" << component->m_ComponentGuid << "'");
		}
	}
	return true;
}
}

bool VansGraphics::VansScene::LoadSceneObjects(
	VkDevice& device,
	const Vans::VansSceneObjectBuildPlan& objectBuildPlan,
	const std::string& projectRoot)
{
	using namespace VansEngine;

	struct ParentLink
	{
		std::string childName;
		std::string parentName;
	};

	struct ParentEntityLink
	{
		uint32_t childTransformID = UINT32_MAX;
		std::string childEntityGuid;
		std::string childName;
		Vans::VansSceneParentReference parent;
	};

	std::vector<ParentLink> parentLinks;
	std::vector<ParentEntityLink> parentEntityLinks;
	std::vector<VansSceneAnimationComponentBuilder::PendingAnimationComponent> pendingAnimComps;
	std::vector<Vans::VansSceneVehicleObjectConfig> vehicleObjectConfigs;
	std::vector<VansScriptVehicleComponent*> vehicleComponents;
	std::unordered_set<uint32_t> vehicleDrivenTransformIDs;

	vehicleObjectConfigs.reserve(objectBuildPlan.objects.size());
	vehicleComponents.reserve(objectBuildPlan.objects.size());
	if (!m_RuntimeWorld)
		m_RuntimeWorld = std::make_unique<Vans::VansRuntimeWorld>();
	if (!m_CameraControlArbiter)
		m_CameraControlArbiter = std::make_unique<VansCameraControlArbiter>();
	m_CameraControlArbiter->CoreRuntime().SetBindingResolver(
		[this](Vans::VansGenerationHandle context, std::string_view,
			Vans::VansCameraBindingSnapshot& binding)
		{
			const Vans::VansEntityHandle entity{ context.index, context.generation };
			if (!m_RuntimeWorld || !m_RuntimeWorld->IsAlive(entity)) return false;
			auto* storage = static_cast<Vans::VansComponentStorage<
				Vans::VansRuntimeTransformComponent>*>(m_RuntimeWorld->FindStorage(
					Vans::VansRuntimeComponentType_Transform));
			if (!storage) return false;
			for (Vans::VansComponentHandle component :
				m_RuntimeWorld->CollectComponentsOwnedBy(entity))
			{
				if (component.typeId != Vans::VansRuntimeComponentType_Transform) continue;
				const Vans::VansRuntimeTransformComponent* runtimeTransform = storage->Get(component);
				if (!runtimeTransform || runtimeTransform->transformStoreId == UINT32_MAX) return false;
				const VansTransform& transform = VansTransformStore::GetTransform(
					runtimeTransform->transformStoreId);
				binding.pose.position = transform.m_Position;
				binding.pose.rotationDegrees = transform.m_Rotation;
				return true;
			}
			return false;
		});
	m_CameraControlArbiter->CoreRuntime().SetCollisionResolver(
		[this](const Vans::VansCameraCollisionQuery& query,
			Vans::VansCameraCollisionResult& result)
		{
			const glm::vec3 delta = query.desiredPosition - query.origin;
			const float distance = glm::length(delta);
			if (distance <= 0.0001f) return false;
			auto& physics = VansEngine::VansPhysicsSystem::GetInstance();
			physx::PxScene* scene = physics.GetScene();
			if (!scene) return false;
			std::unordered_set<physx::PxU32> layers;
			auto& layerManager = VansEngine::VansCollisionLayerManager::Get();
			for (const std::string& name : query.layers)
			{
				const int index = layerManager.GetLayerIndex(name);
				if (index >= 0) layers.insert(static_cast<physx::PxU32>(index));
			}
			std::uint32_t ignoredTransform = UINT32_MAX;
			const Vans::VansEntityHandle entity{
				query.bindingContext.index, query.bindingContext.generation };
			if (m_RuntimeWorld && m_RuntimeWorld->IsAlive(entity))
			{
				auto* storage = static_cast<Vans::VansComponentStorage<
					Vans::VansRuntimeTransformComponent>*>(m_RuntimeWorld->FindStorage(
						Vans::VansRuntimeComponentType_Transform));
				if (storage)
					for (Vans::VansComponentHandle component :
						m_RuntimeWorld->CollectComponentsOwnedBy(entity))
						if (component.typeId == Vans::VansRuntimeComponentType_Transform)
						{
							if (const auto* transform = storage->Get(component))
								ignoredTransform = transform->transformStoreId;
							break;
						}
			}
			CameraCollisionQueryFilter filter(std::move(layers), ignoredTransform);
			physx::PxQueryFilterData filterData;
			filterData.flags = physx::PxQueryFlag::eSTATIC |
				physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER;
			physx::PxSweepBuffer hit;
			std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
			physx::PxSceneReadLock readLock(*scene);
			result.blocked = scene->sweep(
				physx::PxSphereGeometry((std::max)(query.radius, 0.001f)),
				physx::PxTransform(physx::PxVec3(query.origin.x, query.origin.y, query.origin.z)),
				physx::PxVec3(delta.x / distance, delta.y / distance, delta.z / distance),
				distance, hit, physx::PxHitFlag::eDEFAULT, filterData, &filter) && hit.hasBlock;
			result.distance = result.blocked ? hit.block.distance : distance;
			return true;
		});
	if (!m_GameplayRuntime)
		m_GameplayRuntime = std::make_unique<Vans::VansGameplayRuntime>();
	if (!m_GameplayRuntime->IsInitialized())
	{
		std::string gameplayError;
		Vans::VansProjectManager& projectManager = Vans::VansProjectManager::Get();
		Vans::VansGAFProjectConfiguration gameplayConfiguration;
		if (!Vans::VansGAFProjectConfiguration::LoadForProject(
			projectManager.GetProjectRootPath(),
			projectManager.GetPathResolver().GetEngineRoot(),
			gameplayConfiguration,
			gameplayError))
		{
			VANS_LOG_ERROR("[SceneBuild] Could not load GAF project configuration: " << gameplayError);
			return false;
		}
		Vans::VansGameplayRuntimeDependencies gameplayDependencies;
		gameplayDependencies.graphNodeRegistrars.push_back(
			Vans::VansRegisterCameraActionGraphNodes);
		gameplayDependencies.serviceFactories.push_back(
			[this](const Vans::VansGameplayAssetLibrary& assets, std::string& error)
				-> std::shared_ptr<Vans::IVansActionService>
			{
				auto resolvePosition = [this](Vans::VansEntityHandle entity, glm::vec3& position)
				{
					if (!m_RuntimeWorld || !m_RuntimeWorld->IsAlive(entity)) return false;
					auto* storage = static_cast<Vans::VansComponentStorage<
						Vans::VansRuntimeTransformComponent>*>(m_RuntimeWorld->FindStorage(
							Vans::VansRuntimeComponentType_Transform));
					if (!storage) return false;
					for (Vans::VansComponentHandle component :
						m_RuntimeWorld->CollectComponentsOwnedBy(entity))
					{
						if (component.typeId != Vans::VansRuntimeComponentType_Transform) continue;
						const Vans::VansRuntimeTransformComponent* transform = storage->Get(component);
						if (!transform || transform->transformStoreId == UINT32_MAX) return false;
						position = VansTransformStore::GetTransform(
							transform->transformStoreId).m_Position;
						return true;
					}
					return false;
				};
				return Vans::VansCameraActionService::Create(
					m_CameraControlArbiter->CoreRuntime(), assets, error,
					std::move(resolvePosition));
			});
		if (!m_GameplayRuntime->Initialize(projectManager.EnumerateAssetRecords(),
			gameplayConfiguration.settings, gameplayDependencies, gameplayError))
		{
			VANS_LOG_ERROR("[SceneBuild] Could not initialize Gameplay Runtime: " << gameplayError);
			return false;
		}
	}

	// === [VansSceneLoadPass::Pass1_ComponentInstantiation] ===
	for (const Vans::VansSceneObjectBuildConfig& objectConfig : objectBuildPlan.objects)
	{
		RuntimeComponentBuildResults runtimeComponentBuildResults;
		VansScriptObject* obj = new VansScriptObject();
		obj->m_EntityGuid = objectConfig.entityGuid;
		obj->m_ObjectName = objectConfig.name;

		const bool hasObjTransform = objectConfig.transform.has_value();
		glm::vec3 objPos(0.0f), objRot(0.0f), objScl(1.0f);
		if (objectConfig.transform)
		{
			objPos = ToVec3(objectConfig.transform->position);
			objRot = ToVec3(objectConfig.transform->rotation);
			objScl = ToVec3(objectConfig.transform->scale);
		}

		bool objectTransformAllocated = obj->m_OwnsTransform;
		auto ensureObjectTransform = [&]()
		{
			if (!objectTransformAllocated &&
				obj->GetComponent<VansScriptRenderComponent>() == nullptr)
			{
				obj->m_TransformID = VansTransformStore::AllocateTransform();
				obj->m_OwnsTransform = true;
				if (objectConfig.transform)
				{
					auto& transform = VansTransformStore::GetTransform(obj->m_TransformID);
					transform.m_Position = objPos;
					transform.m_Rotation = objRot;
					transform.m_Scale = objScl;
				}
				objectTransformAllocated = true;
			}
		};

		if (objectConfig.render)
		{
			Vans::VansSceneRenderNodeConfig renderConfig = *objectConfig.render;
			if (objectConfig.transform)
				renderConfig.transform = objectConfig.transform;

			VansRenderNode* rn = VansSceneRenderNodeBuilder::LoadSingleRenderNode(*this, device, renderConfig);
			std::vector<VansRenderNode*> renderNodes;

			if (!rn)
			{
				auto groupIt = m_MultiMeshGroups.find(renderConfig.name);
				if (groupIt != m_MultiMeshGroups.end() && !groupIt->second.childNodes.empty())
				{
					rn = groupIt->second.childNodes[0];
					renderNodes = groupIt->second.childNodes;
				}
			}

			if (rn)
			{
				if (hasObjTransform)
					rn->SetTransformData(objPos, objRot, objScl);

				auto* rc = new VansScriptRenderComponent();
				rc->m_ComponentName = "render";
				rc->m_RenderNode = rn;
				if (renderNodes.empty())
					renderNodes.push_back(rn);
				rc->m_RenderNodes = std::move(renderNodes);

				if (!objectConfig.renderEnabled)
				{
					for (auto* renderNode : rc->m_RenderNodes)
						if (renderNode) renderNode->SetEnabled(false);
				}
				rc->m_Enabled = objectConfig.renderEnabled;

				obj->AddComponent(rc);
				runtimeComponentBuildResults.render = rc;
				obj->m_TransformID = rn->m_TransformID;

				if (!renderConfig.parent.empty())
				{
					ParentLink link;
					link.childName = renderConfig.name;
					link.parentName = renderConfig.parent;
					parentLinks.push_back(std::move(link));
				}

			}
		}
		// A scene Transform is a runtime component even when the object has no
		// render, physics, camera, or other component that would otherwise force
		// allocation. This is required for pure Transform targets such as
		// virtual cameras and camera focus markers.
		if (hasObjTransform)
			ensureObjectTransform();

		runtimeComponentBuildResults.physics =
			VansScenePhysicsComponentBuilder::BuildPhysicsClothAndCharacter(
				*this,
				*obj,
				objectConfig.physicsComponents,
				projectRoot,
				hasObjTransform,
				ensureObjectTransform);

		VansScriptVehicleComponent* vehicleComponent =
			VansSceneVehicleComponentBuilder::AddVehiclePlaceholder(*obj, objectConfig.vehicleObject);
		vehicleObjectConfigs.push_back(objectConfig.vehicleObject);
		vehicleComponents.push_back(vehicleComponent);

		if (objectConfig.multiMeshRoot ||
			(objectConfig.animation && obj->GetComponent<VansScriptRenderComponent>() == nullptr))
		{
			ensureObjectTransform();
		}

		runtimeComponentBuildResults.lights =
			VansSceneLightComponentBuilder::BuildLights(
				*this,
				*obj,
				objectConfig.lightComponents,
				projectRoot,
				ensureObjectTransform);

		runtimeComponentBuildResults.cameraMedia =
			VansSceneCameraMediaComponentBuilder::BuildCameraAudioVideo(
				*this,
				*obj,
				objectConfig.cameraMediaComponents,
				ensureObjectTransform);

		if (objectConfig.audioReverbZone)
		{
			ensureObjectTransform();
			auto* reverbZone = new VansScriptAudioReverbZoneComponent();
			reverbZone->m_ComponentName = objectConfig.audioReverbZone->componentType;
			reverbZone->m_Shape = objectConfig.audioReverbZone->shape;
			reverbZone->m_Preset = objectConfig.audioReverbZone->preset;
			reverbZone->m_PresetAssetGuid = objectConfig.audioReverbZone->presetAssetGuid;
			reverbZone->m_PresetParameters = objectConfig.audioReverbZone->presetParameters;
			reverbZone->m_OverridePresetParameters = objectConfig.audioReverbZone->overridePresetParameters;
			reverbZone->m_Radius = objectConfig.audioReverbZone->radius;
			reverbZone->m_HalfExtentX = objectConfig.audioReverbZone->halfExtents[0];
			reverbZone->m_HalfExtentY = objectConfig.audioReverbZone->halfExtents[1];
			reverbZone->m_HalfExtentZ = objectConfig.audioReverbZone->halfExtents[2];
			reverbZone->m_FadeDistance = objectConfig.audioReverbZone->fadeDistance;
			reverbZone->m_WetGain = objectConfig.audioReverbZone->wetGain;
			reverbZone->m_Priority = objectConfig.audioReverbZone->priority;
			obj->AddComponent(reverbZone);
			runtimeComponentBuildResults.audioReverbZone = reverbZone;
		}

		if (objectConfig.animation)
		{
			VansSceneAnimationComponentBuilder::AddAnimationPlaceholder(
				*obj,
				*objectConfig.animation,
				pendingAnimComps);
		}

		if (objectConfig.particle)
		{
			runtimeComponentBuildResults.particle =
				VansSceneParticleComponentBuilder::BuildParticle(
				*this,
				device,
				*obj,
				*objectConfig.particle,
				projectRoot,
				hasObjTransform,
				objPos,
				objRot,
				objScl);
		}

		runtimeComponentBuildResults.scripts.uiControllers =
			VansSceneScriptComponentBuilder::BuildUIControllers(*obj, objectConfig.uiComponents);
		runtimeComponentBuildResults.scripts.scripts =
			VansSceneScriptComponentBuilder::BuildScripts(*obj, objectConfig.scriptComponents);
		VansSceneLightComponentBuilder::BindExplicitVideoComponentToRectLight(*this, *obj);
		ApplyRuntimeComponentGuids(*obj, objectConfig.componentGuids);

		if (objectConfig.parent)
		{
			ensureObjectTransform();
			if (obj->m_TransformID != UINT32_MAX)
			{
				ParentEntityLink link;
				link.childTransformID = obj->m_TransformID;
				link.childEntityGuid = objectConfig.entityGuid;
				link.childName = obj->m_ObjectName;
				link.parent = *objectConfig.parent;
				parentEntityLinks.push_back(std::move(link));
			}
		}

		obj->SetActive(objectConfig.active);
		m_SceneObjects.push_back(obj);
		m_RuntimeWorld->Commands().CreateEntity(
			{ objectConfig.entityGuid, objectConfig.name, Vans::VansEntityHandle{}, objectConfig.active });
		m_RuntimeWorld->FlushCommands();
		const Vans::VansEntityHandle runtimeEntity =
			m_RuntimeWorld->Entities().FindByGuid(objectConfig.entityGuid);
		if (!runtimeEntity.IsValid())
		{
			VANS_LOG_ERROR("[SceneBuild] Runtime command buffer did not create entity '"
				<< objectConfig.name << "' guid='" << objectConfig.entityGuid << "'");
		}
		std::string runtimeComponentError;
		if (!RegisterRuntimeComponents(
			*m_RuntimeWorld,
			runtimeEntity,
			*obj,
			objectConfig.componentGuids,
			runtimeComponentBuildResults,
			objectConfig.timeline,
			objectConfig.actionHost,
			m_GameplayRuntime.get(),
			runtimeComponentError))
		{
			VANS_LOG_ERROR("[SceneBuild] Could not register runtime components for entity '"
				<< objectConfig.name << "': " << runtimeComponentError);
			return false;
		}
	}

	// === [VansSceneLoadPass::Pass2_VehicleReference] ===
	vehicleDrivenTransformIDs = VansSceneVehicleComponentBuilder::ResolveVehicles(*this, vehicleObjectConfigs);
	RegisterDeferredVehicleRuntimeComponents(
		*m_RuntimeWorld,
		objectBuildPlan.objects,
		vehicleComponents);

	// === [VansSceneLoadPass::Pass3_TransformParent] ===
	for (const auto& link : parentLinks)
	{
		if (link.childName.empty() || link.parentName.empty())
			continue;

		VansRenderNode* childNode = FindRenderNodeByName(link.childName);
		VansRenderNode* parentNode = FindRenderNodeByName(link.parentName);
		if (childNode && parentNode)
		{
			if (vehicleDrivenTransformIDs.count(childNode->m_TransformID) > 0)
				continue;
			m_TransformGraph.SetParent(childNode->m_TransformID, parentNode->m_TransformID,
				Vans::VansTransformReparentMode::KeepLocal);
		}
	}

	for (const auto& link : parentEntityLinks)
	{
		const std::string parentEntityGuid = link.parent.entityGuid.ToString();
		VansScriptObject* parentObj = FindObjectByGuid(parentEntityGuid);
		if (parentObj && parentObj->m_TransformID != UINT32_MAX)
		{
			if (m_RuntimeWorld)
			{
				const Vans::VansEntityHandle child = m_RuntimeWorld->Entities().FindByGuid(link.childEntityGuid);
				const Vans::VansEntityHandle parent = m_RuntimeWorld->Entities().FindByGuid(parentEntityGuid);
				if (child.IsValid() && parent.IsValid())
				{
					m_RuntimeWorld->Commands().SetParent(child, parent);
					m_RuntimeWorld->FlushCommands();
				}
			}
			if (vehicleDrivenTransformIDs.count(link.childTransformID) == 0 && link.parent.IsEntity())
				m_TransformGraph.SetParent(link.childTransformID, parentObj->m_TransformID,
					Vans::VansTransformReparentMode::KeepLocal);
		}
		else
		{
			VANS_LOG_WARN("[TransformParent] Could not resolve parent entity for child='"
				<< link.childName << "' parentGuid='" << parentEntityGuid << "'");
		}
	}

	// === [VansSceneLoadPass::Pass3.5_MultiMeshGroupRebuild] ===
	for (const Vans::VansSceneObjectBuildConfig& objectConfig : objectBuildPlan.objects)
	{
		if (!objectConfig.multiMeshRoot)
			continue;

		const std::string& parentGuid = objectConfig.entityGuid;
		const std::string& parentName = objectConfig.name;
		if (parentGuid.empty() || parentName.empty())
			continue;

		const std::string& modelGuid = objectConfig.multiMeshRoot->modelGuid;
		VansMesh* sourceMesh = static_cast<VansMesh*>(GetMeshAsset(modelGuid));
		if (!sourceMesh || !sourceMesh->m_IsMultiMesh)
		{
			VANS_LOG_WARN("[MultiMeshGroup] Root '" << parentName
				<< "' references missing/non-multi mesh '" << modelGuid << "'");
			continue;
		}

		MultiMeshGroup& group = m_MultiMeshGroups[parentName];
		group.parentName = parentName;
		group.parentEntityGuid = parentGuid;
		group.sourceMesh = sourceMesh;
		group.childNodes.clear();
		group.ownsSharedTransform = false;
		const bool hasNodeTransformAnimation = sourceMesh->m_HasNodeTransformAnimation;

		VansScriptObject* parentObj = FindObjectByGuid(parentGuid);
		if (parentObj && parentObj->m_TransformID != UINT32_MAX)
			group.sharedTransformID = parentObj->m_TransformID;

		std::unordered_set<uint32_t> usedIndices;
		for (auto* childObj : m_SceneObjects)
		{
			if (!childObj || childObj->m_EntityGuid.empty())
				continue;
			auto* rc = childObj->GetComponent<VansScriptRenderComponent>();
			if (!rc || !rc->m_RenderNode)
				continue;

			VansRenderNode* node = rc->m_RenderNode;
			if (node->m_ParentEntityGuid != parentGuid)
				continue;
			if (node->m_SourceMesh != sourceMesh)
				continue;
			if (node->m_SubmeshIndex == UINT32_MAX)
				continue;
			if (!usedIndices.insert(node->m_SubmeshIndex).second)
			{
				VANS_LOG_WARN("[MultiMeshGroup] Duplicate submesh index " << node->m_SubmeshIndex
					<< " under root '" << parentName << "', keeping first node.");
				continue;
			}

			const uint32_t oldTransformID = node->m_TransformID;
			if (vehicleDrivenTransformIDs.count(oldTransformID) > 0)
			{
				if (m_TransformGraph.HasParent(oldTransformID))
					m_TransformGraph.ClearParent(oldTransformID);
				node->m_ParentGroupName = parentName;
				group.childNodes.push_back(node);
				continue;
			}

			if (hasNodeTransformAnimation)
			{
				if (m_TransformGraph.HasParent(oldTransformID))
					m_TransformGraph.ClearParent(oldTransformID);
				node->m_ParentGroupName = parentName;
				group.childNodes.push_back(node);
				continue;
			}

			if (oldTransformID != group.sharedTransformID)
			{
				if (m_TransformGraph.HasParent(oldTransformID))
					m_TransformGraph.ClearParent(oldTransformID);
				node->ShareTransform(group.sharedTransformID);
			}
			childObj->m_TransformID = group.sharedTransformID;
			childObj->m_OwnsTransform = false;

			node->m_ParentGroupName = parentName;
			group.childNodes.push_back(node);
		}

		std::sort(group.childNodes.begin(), group.childNodes.end(),
			[](const VansRenderNode* lhs, const VansRenderNode* rhs)
			{
				return lhs->m_SubmeshIndex < rhs->m_SubmeshIndex;
			});
	}

	// === [VansSceneLoadPass::Pass4_AnimationRagdoll] ===
	VansSceneAnimationComponentBuilder::ResolveAnimations(*this, pendingAnimComps, projectRoot);
	RegisterDeferredAnimationRuntimeComponents(*this, *m_RuntimeWorld, objectBuildPlan.objects);
	for (const ParentEntityLink& link : parentEntityLinks)
	{
		if (!link.parent.IsAnchor()
			|| vehicleDrivenTransformIDs.count(link.childTransformID) > 0)
			continue;
		VansScriptObject* owner = FindObjectByGuid(link.parent.entityGuid.ToString());
		if (!owner || owner->m_TransformID == UINT32_MAX
			|| !SetTransformAnchorReference(link.childTransformID, owner->m_TransformID, link.parent))
		{
			VANS_LOG_ERROR("[TransformGraph] Could not bind anchor parent for child='"
				<< link.childName << "' anchorGuid='" << link.parent.anchorGuid.ToString() << "'");
			return false;
		}
	}

	// === [VansSceneLoadPass::Pass5_ClothAnimationBinding] ===
	VansSceneClothAnimationBindingExecutor::Execute(*this);
	ConfigureTimelineRuntime();

	m_AudioManager.PlayAutoPlay();
	return true;
}
