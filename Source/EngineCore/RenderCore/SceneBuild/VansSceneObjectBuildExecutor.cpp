#include "VansSceneAssembly.h"

#include "VansSceneAnimationComponentBuilder.h"
#include "VansSceneAudioReverbZoneComponentBuilder.h"
#include "VansSceneCameraMediaComponentBuilder.h"
#include "VansSceneClothAnimationBindingExecutor.h"
#include "VansSceneLightComponentBuilder.h"
#include "VansSceneLodGroupComponentBuilder.h"
#include "VansSceneMultiMeshGroupBuilder.h"
#include "VansSceneParticleComponentBuilder.h"
#include "VansScenePhysicsComponentBuilder.h"
#include "VansSceneRenderNodeBuilder.h"
#include "VansSceneScriptComponentBuilder.h"
#include "VansSceneVehicleComponentBuilder.h"
#include "../../SceneCore/VansSceneObjectBuildPlan.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../GameplayComposition/VansSceneGameplayComposition.h"
#include "../../GameplayActionCore/VansGameplayRuntime.h"
#include "../../AICore/VansAIWorld.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../SceneRuntime/Transform/VansTransformStore.h"
#include "../../Util/VansLog.h"
#include "../VulkanCore/VansMesh.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
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

struct VansRuntimeEntityPublishResult
{
	bool success = false;
	std::string error;
	Vans::VansEntityHandle entity;
};

struct VansDeferredRuntimePublishResult
{
	bool success = false;
	std::string error;
};

bool IsRuntimeComponentPublishedForEntity(
	const Vans::VansRuntimeWorld& runtimeWorld,
	Vans::VansEntityHandle entity,
	const std::string& componentGuid,
	std::uint16_t typeId);

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

VansDeferredRuntimePublishResult PublishDeferredAnimationRuntimeComponents(
	VansGraphics::VansScene& scene,
	Vans::VansRuntimeWorld& runtimeWorld,
	const std::vector<Vans::VansSceneObjectBuildConfig>& objectConfigs)
{
	struct VansPendingAnimationRuntimePublish
	{
		Vans::VansEntityHandle entity;
		VansScriptAnimationComponent* animation = nullptr;
		VansScriptRagdollComponent* ragdoll = nullptr;
		VansGraphics::VansSkeletonInstanceHandle skeletonInstance;
	};

	VansDeferredRuntimePublishResult result;
	std::vector<VansPendingAnimationRuntimePublish> pendingPublishes;
	std::unordered_set<std::string> pendingComponentGuids;
	for (const Vans::VansSceneObjectBuildConfig& objectConfig : objectConfigs)
	{
		if (!objectConfig.animation)
			continue;
		if (objectConfig.entityGuid.empty())
		{
			result.error = "Deferred Animation entity is missing its stable GUID";
			return result;
		}
		VansScriptObject* object = scene.FindObjectByGuid(objectConfig.entityGuid);
		const Vans::VansEntityHandle entity = runtimeWorld.Entities().FindByGuid(objectConfig.entityGuid);
		if (!object || !entity.IsValid())
		{
			result.error = "Deferred Animation owner is unavailable for entity '" +
				objectConfig.entityGuid + "'";
			return result;
		}

		auto* animationComponent = object->GetComponent<VansScriptAnimationComponent>();
		if (!animationComponent || !animationComponent->m_AnimNode)
		{
			result.error = "Deferred Animation runtime was not created for entity '" +
				objectConfig.entityGuid + "'";
			return result;
		}
		const std::string animationGuid =
			FindRuntimeComponentGuid(objectConfig.componentGuids, "animation");
		if (animationGuid.empty() || animationComponent->m_ComponentGuid != animationGuid)
		{
			result.error = "Deferred Animation component has no matching stable GUID for entity '" +
				objectConfig.entityGuid + "'";
			return result;
		}
		if (!pendingComponentGuids.insert(animationGuid).second ||
			runtimeWorld.FindComponentByGuid(animationGuid).IsValid())
		{
			result.error = "Deferred Animation component GUID is already used: '" +
				animationGuid + "'";
			return result;
		}

		auto* ragdollComponent = object->GetComponent<VansScriptRagdollComponent>();
		if (objectConfig.animation->ragdoll && !ragdollComponent)
		{
			result.error = "Deferred Ragdoll runtime was not created for entity '" +
				objectConfig.entityGuid + "'";
			return result;
		}
		if (ragdollComponent)
		{
			ragdollComponent->m_ComponentGuid = Vans::VansAssetGuid::FromStableName(
				"Animation.Ragdoll", animationGuid).ToString();
			if (!ragdollComponent->m_AnimNode ||
				!pendingComponentGuids.insert(ragdollComponent->m_ComponentGuid).second ||
				runtimeWorld.FindComponentByGuid(ragdollComponent->m_ComponentGuid).IsValid())
			{
				result.error = "Deferred Ragdoll component identity is invalid for entity '" +
					objectConfig.entityGuid + "'";
				return result;
			}
		}

		pendingPublishes.push_back({ entity, animationComponent, ragdollComponent, {} });
	}

	for (VansPendingAnimationRuntimePublish& pending : pendingPublishes)
	{
		pending.skeletonInstance = scene.RegisterSkeletonInstance(*pending.animation->m_AnimNode);
		if (!pending.skeletonInstance.IsValid())
		{
			for (VansPendingAnimationRuntimePublish& registered : pendingPublishes)
			{
				if (registered.skeletonInstance.IsValid())
					scene.UnregisterSkeletonInstance(registered.skeletonInstance);
			}
			result.error = "Could not register deferred Animation skeleton instance";
			return result;
		}
		runtimeWorld.Commands().AddAnimationComponent(
			pending.entity,
			pending.animation->m_ComponentGuid,
			pending.animation->m_AnimNode,
			pending.skeletonInstance.id,
			pending.skeletonInstance.generation,
			pending.animation->IsEnabled());
		if (pending.ragdoll)
		{
			runtimeWorld.Commands().AddRagdollComponent(
				pending.entity,
				pending.ragdoll->m_ComponentGuid,
				pending.ragdoll->m_AnimNode,
				static_cast<std::uint8_t>(pending.ragdoll->m_InitialDriveMode),
				pending.ragdoll->m_ProfileAssetGuid,
				pending.ragdoll->m_ProfileName,
				pending.ragdoll->m_ConfiguredBodyCount,
				pending.ragdoll->m_ConfiguredJointCount,
				pending.ragdoll->IsEnabled());
		}
	}
	runtimeWorld.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);

	for (const VansPendingAnimationRuntimePublish& pending : pendingPublishes)
	{
		const bool animationPublished = IsRuntimeComponentPublishedForEntity(
			runtimeWorld,
			pending.entity,
			pending.animation->m_ComponentGuid,
			Vans::VansRuntimeComponentType_Animation);
		const bool ragdollPublished = !pending.ragdoll || IsRuntimeComponentPublishedForEntity(
			runtimeWorld,
			pending.entity,
			pending.ragdoll->m_ComponentGuid,
			Vans::VansRuntimeComponentType_Ragdoll);
		if (!animationPublished || !ragdollPublished)
		{
			for (const VansPendingAnimationRuntimePublish& published : pendingPublishes)
			{
				const Vans::VansComponentHandle animationHandle = runtimeWorld.FindComponentByGuid(
					published.animation->m_ComponentGuid,
					Vans::VansRuntimeComponentType_Animation);
				if (animationHandle.IsValid())
					runtimeWorld.RemoveComponent(animationHandle);
				if (published.ragdoll)
				{
					const Vans::VansComponentHandle ragdollHandle = runtimeWorld.FindComponentByGuid(
						published.ragdoll->m_ComponentGuid,
						Vans::VansRuntimeComponentType_Ragdoll);
					if (ragdollHandle.IsValid())
						runtimeWorld.RemoveComponent(ragdollHandle);
				}
				scene.UnregisterSkeletonInstance(published.skeletonInstance);
			}
			result.error = "Deferred Animation/Ragdoll components were not published to their owners";
			return result;
		}
	}

	result.success = true;
	return result;
}

VansDeferredRuntimePublishResult PublishDeferredVehicleRuntimeComponents(
	Vans::VansRuntimeWorld& runtimeWorld,
	const std::vector<VansGraphics::VansSceneBuiltVehicleRuntime>& builtVehicles)
{
	struct VansPendingVehicleRuntimePublish
	{
		Vans::VansEntityHandle entity;
		VansScriptVehicleComponent* component = nullptr;
	};

	VansDeferredRuntimePublishResult result;
	std::vector<VansPendingVehicleRuntimePublish> pendingPublishes;
	std::unordered_set<std::string> pendingComponentGuids;
	for (const VansGraphics::VansSceneBuiltVehicleRuntime& built : builtVehicles)
	{
		const Vans::VansEntityHandle entity =
			runtimeWorld.Entities().FindByGuid(built.ownerEntityGuid);
		VansScriptVehicleComponent* vehicleComponent = built.component;
		if (!entity.IsValid() || !vehicleComponent || !vehicleComponent->m_Vehicle ||
			vehicleComponent->m_ComponentGuid.empty())
		{
			result.error = "Deferred Vehicle runtime is incomplete for entity '" +
				built.ownerEntityGuid + "'";
			return result;
		}
		if (!pendingComponentGuids.insert(vehicleComponent->m_ComponentGuid).second ||
			runtimeWorld.FindComponentByGuid(vehicleComponent->m_ComponentGuid).IsValid())
		{
			result.error = "Deferred Vehicle component GUID is already used: '" +
				vehicleComponent->m_ComponentGuid + "'";
			return result;
		}
		pendingPublishes.push_back({ entity, vehicleComponent });
	}

	for (const VansPendingVehicleRuntimePublish& pending : pendingPublishes)
	{
		runtimeWorld.Commands().AddVehicleComponent(
			pending.entity,
			pending.component->m_ComponentGuid,
			pending.component->m_Vehicle,
			pending.component->IsEnabled());
	}
	runtimeWorld.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	for (const VansPendingVehicleRuntimePublish& pending : pendingPublishes)
	{
		if (!IsRuntimeComponentPublishedForEntity(
			runtimeWorld,
			pending.entity,
			pending.component->m_ComponentGuid,
			Vans::VansRuntimeComponentType_Vehicle))
		{
			for (const VansPendingVehicleRuntimePublish& published : pendingPublishes)
			{
				const Vans::VansComponentHandle vehicleHandle = runtimeWorld.FindComponentByGuid(
					published.component->m_ComponentGuid,
					Vans::VansRuntimeComponentType_Vehicle);
				if (vehicleHandle.IsValid())
					runtimeWorld.RemoveComponent(vehicleHandle);
			}
			result.error = "Deferred Vehicle component was not published for stable GUID '" +
				pending.component->m_ComponentGuid + "'";
			return result;
		}
	}
	result.success = true;
	return result;
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
			cameraMedia.audio->m_Source.GetResourceNode(),
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
			cameraMedia.video->m_VideoAssetGuid,
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
			physicsBuild.cloth->m_ProfileAssetGuid,
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
		typeId == Vans::VansRuntimeComponentType_AudioVolume
			? Vans::VansRuntimeAudioEnvironmentKind::Volume
			: Vans::VansRuntimeAudioEnvironmentKind::ReverbZone,
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
		particleComponent->m_ParticleAssetGuid,
		particleComponent->m_Instance,
		particleComponent->m_PlayOnAwake,
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
		runtimeUI.autoOpenScreenAssetGuids = uiComponent->m_AutoOpenScreenAssetGuids;
		runtimeUI.preloadScreenAssetGuids = uiComponent->m_PreloadScreenAssetGuids;
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

bool IsRuntimeComponentPublishedForEntity(
	const Vans::VansRuntimeWorld& runtimeWorld,
	Vans::VansEntityHandle entity,
	const std::string& componentGuid,
	std::uint16_t typeId)
{
	const Vans::VansComponentHandle component =
		runtimeWorld.FindComponentByGuid(componentGuid, typeId);
	const Vans::VansComponentHeader* header = runtimeWorld.GetComponentHeader(component);
	return component.IsValid() && header && header->owner == entity;
}

VansRuntimeEntityPublishResult PublishRuntimeEntity(
	Vans::VansRuntimeWorld& runtimeWorld,
	const Vans::VansEntityCreateDesc& entityDesc,
	VansScriptObject& object,
	const std::unordered_map<std::string, std::string>& componentGuids,
	const RuntimeComponentBuildResults& buildResults,
	const std::optional<Vans::VansSceneTimelineComponentConfig>& timelineConfig,
	const std::optional<Vans::VansGameplayActionHostSetup>& actionHostConfig,
	const std::optional<Vans::VansSceneNavigationAgentConfig>& navigationAgentConfig,
	const std::optional<Vans::VansSceneAIAgentConfig>& aiAgentConfig,
	Vans::VansGameplayRuntime* gameplayRuntime)
{
	VansRuntimeEntityPublishResult result;
	if (entityDesc.stableGuid.empty())
	{
		result.error = "Runtime entity is missing its stable GUID";
		return result;
	}
	if (runtimeWorld.Entities().FindByGuid(entityDesc.stableGuid).IsValid())
	{
		result.error = "Runtime entity GUID is already published: '" + entityDesc.stableGuid + "'";
		return result;
	}

	std::unordered_set<std::string> uniqueComponentGuids;
	for (const auto& componentEntry : componentGuids)
	{
		const std::string& componentGuid = componentEntry.second;
		if (componentGuid.empty())
			continue;
		if (!uniqueComponentGuids.insert(componentGuid).second)
		{
			result.error = "Runtime component GUID is duplicated in entity '" +
				entityDesc.stableGuid + "': '" + componentGuid + "'";
			return result;
		}
		if (runtimeWorld.FindComponentByGuid(componentGuid).IsValid())
		{
			result.error = "Runtime component GUID is already published: '" + componentGuid + "'";
			return result;
		}
	}

	const auto transformGuid = componentGuids.find("transform");
	if (transformGuid != componentGuids.end() && !transformGuid->second.empty() &&
		ResolveRuntimeTransformStoreId(object) == UINT32_MAX)
	{
		result.error = "Runtime Transform component has no entity Transform";
		return result;
	}
	if (buildResults.audioReverbZone)
	{
		const std::string key = Vans::CanonicalRuntimeComponentKeyForName(
			buildResults.audioReverbZone->m_ComponentName);
		if (Vans::VansRuntimeComponentTypeIdForKey(key) == Vans::VansInvalidComponentTypeId)
		{
			result.error = "Audio reverb component has no runtime type id";
			return result;
		}
	}

	const std::string timelineComponentGuid = timelineConfig && timelineConfig->valid
		? FindRuntimeComponentGuid(componentGuids, "timeline") : std::string();
	if (timelineConfig && timelineConfig->valid && timelineComponentGuid.empty())
	{
		result.error = "Timeline component is missing its stable component GUID";
		return result;
	}
	const std::string actionHostComponentGuid = actionHostConfig
		? FindRuntimeComponentGuid(componentGuids, "action_host") : std::string();
	if (actionHostConfig && (!gameplayRuntime || !gameplayRuntime->IsInitialized()))
	{
		result.error = "ActionHost requires an initialized Gameplay Runtime";
		return result;
	}
	if (actionHostConfig && actionHostComponentGuid.empty())
	{
		result.error = "ActionHost component is missing its stable component GUID";
		return result;
	}
	const std::string navigationAgentComponentGuid = navigationAgentConfig
		? FindRuntimeComponentGuid(componentGuids, "navigation_agent") : std::string();
	if (navigationAgentConfig &&
		(navigationAgentComponentGuid.empty() ||
			navigationAgentConfig->runtime.navigationMeshGuid.empty()))
	{
		result.error = "NavigationAgent requires stable component and Navigation Mesh asset GUIDs";
		return result;
	}
	const std::string aiAgentComponentGuid = aiAgentConfig
		? FindRuntimeComponentGuid(componentGuids, "ai_agent") : std::string();
	if (aiAgentConfig &&
		(aiAgentComponentGuid.empty() || aiAgentConfig->runtime.behaviorGuid.empty()))
	{
		result.error = "AIAgent requires stable component and AI Behavior asset GUIDs";
		return result;
	}

	result.entity = runtimeWorld.CreateEntity(entityDesc);
	if (!result.entity.IsValid() ||
		runtimeWorld.Entities().FindByGuid(entityDesc.stableGuid) != result.entity)
	{
		result.entity = {};
		result.error = "RuntimeWorld did not create entity '" + entityDesc.stableGuid + "'";
		return result;
	}

	auto failPublication = [&](std::string error)
	{
		runtimeWorld.DestroyEntity(
			result.entity,
			Vans::VansDestroyChildrenPolicy::DestroyChildren);
		result.entity = {};
		result.error = std::move(error);
		return result;
	};

	std::shared_ptr<Vans::VansActionHost> actionHost;
	if (actionHostConfig)
	{
		actionHost = gameplayRuntime->CreateHost(
			result.entity,
			*actionHostConfig,
			result.error);
		if (!actionHost)
			return failPublication(result.error);
	}

	std::vector<std::pair<VansScriptComponent*, std::uint16_t>> registrationChecks;
	if (transformGuid != componentGuids.end() && !transformGuid->second.empty())
	{
		runtimeWorld.Commands().AddTransformComponent(
			result.entity,
			transformGuid->second,
			ResolveRuntimeTransformStoreId(object),
			true);
	}
	QueueRenderRuntimeComponent(
		runtimeWorld,
		result.entity,
		buildResults.render,
		registrationChecks);
	QueueCameraMediaRuntimeComponents(
		runtimeWorld,
		result.entity,
		buildResults.cameraMedia,
		registrationChecks);
	QueuePhysicsRuntimeComponents(
		runtimeWorld,
		result.entity,
		buildResults.physics,
		registrationChecks);
	QueueAudioReverbZoneRuntimeComponent(
		runtimeWorld,
		result.entity,
		buildResults.audioReverbZone,
		registrationChecks);
	QueueParticleRuntimeComponent(
		runtimeWorld,
		result.entity,
		buildResults.particle,
		registrationChecks);
	QueueScriptRuntimeComponents(
		runtimeWorld,
		result.entity,
		buildResults.scripts,
		registrationChecks);
	QueueLightRuntimeComponents(
		runtimeWorld,
		result.entity,
		buildResults.lights,
		registrationChecks);
	if (timelineConfig && timelineConfig->valid)
	{
		Vans::VansRuntimeTimelineComponent timeline;
		timeline.assetGuid = timelineConfig->timelineAssetGuid;
		timeline.assetPath = timelineConfig->timelineAssetPath;
		timeline.instance = timelineConfig->instance;
		runtimeWorld.Commands().AddTimelineComponent(
			result.entity,
			timelineComponentGuid,
			std::move(timeline),
			timelineConfig->enabled);
	}
	if (actionHostConfig)
	{
		runtimeWorld.Commands().AddActionHostComponent(
			result.entity,
			actionHostComponentGuid,
			std::move(actionHost),
			actionHostConfig->enabled);
	}
	if (navigationAgentConfig)
	{
		runtimeWorld.Commands().AddNavigationAgentComponent(
			result.entity, navigationAgentComponentGuid, navigationAgentConfig->runtime,
			navigationAgentConfig->enabled);
	}
	if (aiAgentConfig)
	{
		runtimeWorld.Commands().AddAIAgentComponent(
			result.entity, aiAgentComponentGuid, aiAgentConfig->runtime,
			aiAgentConfig->enabled);
	}
	runtimeWorld.CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);
	if (transformGuid != componentGuids.end() && !transformGuid->second.empty())
	{
		if (!IsRuntimeComponentPublishedForEntity(
			runtimeWorld,
			result.entity,
			transformGuid->second,
			Vans::VansRuntimeComponentType_Transform))
			return failPublication("RuntimeWorld did not publish Transform component '" +
				transformGuid->second + "' for its entity");
	}
	for (const auto& [component, typeId] : registrationChecks)
	{
		if (!IsRuntimeComponentPublishedForEntity(
			runtimeWorld,
			result.entity,
			component->m_ComponentGuid,
			typeId))
			return failPublication("RuntimeWorld did not publish component '" +
				component->m_ComponentName + "' guid='" + component->m_ComponentGuid +
				"' for its entity");
	}
	const auto validateConfiguredComponent = [&](const std::string& componentGuid, std::uint16_t typeId,
		const char* componentName)
	{
		if (componentGuid.empty())
			return true;
		if (IsRuntimeComponentPublishedForEntity(
			runtimeWorld, result.entity, componentGuid, typeId))
			return true;
		result.error = std::string("RuntimeWorld did not publish ") + componentName +
			" component '" + componentGuid + "' for its entity";
		return false;
	};
	if (!validateConfiguredComponent(
		timelineComponentGuid, Vans::VansRuntimeComponentType_Timeline, "Timeline") ||
		!validateConfiguredComponent(
			actionHostComponentGuid, Vans::VansRuntimeComponentType_ActionHost, "ActionHost") ||
		!validateConfiguredComponent(
			navigationAgentComponentGuid, Vans::VansRuntimeComponentType_NavigationAgent,
			"NavigationAgent") ||
		!validateConfiguredComponent(
			aiAgentComponentGuid, Vans::VansRuntimeComponentType_AIAgent, "AIAgent"))
	{
		return failPublication(result.error);
	}

	if (timelineConfig && timelineConfig->valid)
	{
		VANS_LOG("[Timeline] Registered component='" << timelineComponentGuid
			<< "' asset='" << timelineConfig->timelineAssetGuid << "'");
	}
	result.success = true;
	return result;
}
}

VansGraphics::VansSceneObjectBuildResult VansGraphics::VansSceneAssembly::BuildObjects(
	VansScene& scene,
	VkDevice& device,
	const Vans::VansSceneObjectBuildPlan& objectBuildPlan,
	const std::string& projectRoot)
{
	return VansSceneAssembly(scene).BuildObjectsInternal(
		device, objectBuildPlan, projectRoot);
}

VansGraphics::VansSceneObjectBuildResult
VansGraphics::VansSceneAssembly::BuildObjectsInternal(
	VkDevice& device,
	const Vans::VansSceneObjectBuildPlan& objectBuildPlan,
	const std::string& projectRoot)
{
	using namespace VansEngine;
	const auto failure = [](
		VansSceneObjectBuildFailure reason,
		std::string error)
	{
		return VansSceneObjectBuildResult{
			false, reason, std::move(error) };
	};

	struct VansPendingEntityParentLink
	{
		uint32_t childTransformId = UINT32_MAX;
		std::string childEntityGuid;
		std::string childName;
		Vans::VansSceneParentReference parent;
		VansScriptObject* parentObject = nullptr;
		Vans::VansEntityHandle childEntityHandle;
		Vans::VansEntityHandle parentEntityHandle;
		Vans::VansEntityHandle previousParentHandle;
		bool hasPreviousTransformLink = false;
		Vans::VansTransformGraphLink previousTransformLink;
		Vans::VansLocalTransform previousLocalTransform;
		bool hasAppliedTransformLink = false;
	};

	std::vector<VansPendingEntityParentLink> pendingEntityParentLinks;
	std::vector<VansSceneAnimationComponentBuilder::PendingAnimationComponent> pendingAnimComps;
	std::vector<VansSceneVehicleBuildRequest> vehicleBuildRequests;
	std::unordered_set<uint32_t> vehicleDrivenTransformIds;

	vehicleBuildRequests.reserve(objectBuildPlan.objects.size());
	std::string gameplayError;
	if (!Vans::VansSceneGameplayComposition::InitializeActionRuntime(m_Scene, gameplayError))
	{
		VANS_LOG_ERROR("[SceneBuild] Could not initialize Gameplay Runtime: " << gameplayError);
		return failure(
			VansSceneObjectBuildFailure::GameplaySetupFailed,
			std::move(gameplayError));
	}

	VansIESProfileManager& iesProfileManager = *m_Scene.GetIESProfileManager();
	if (!iesProfileManager.IsGPUResourcesCreated() && m_SceneObjects.empty())
		iesProfileManager.ClearProfiles();
	const Vans::VansAssetObjectRepository& assetObjectRepository =
		Vans::VansProjectManager::Get().GetAssetObjectRepository();
	std::vector<VansSceneLightDependencies> lightDependencies;
	lightDependencies.reserve(objectBuildPlan.objects.size());
	for (const Vans::VansSceneObjectBuildConfig& objectConfig : objectBuildPlan.objects)
	{
		lightDependencies.push_back(VansSceneLightComponentBuilder::ResolveDependencies(
			m_Scene,
			objectConfig.lightComponents,
			assetObjectRepository,
			iesProfileManager,
			objectConfig.name));
	}

	std::vector<VansSceneCameraMediaDependencies> cameraMediaDependencies;
	cameraMediaDependencies.reserve(objectBuildPlan.objects.size());
	for (const Vans::VansSceneObjectBuildConfig& objectConfig : objectBuildPlan.objects)
	{
		VansSceneCameraMediaDependencies dependencies =
			VansSceneCameraMediaComponentBuilder::ResolveDependencies(
				m_Scene, objectConfig.cameraMediaComponents);
		if (!dependencies.success)
		{
			VANS_LOG_ERROR("[SceneBuild] Could not resolve Camera/Media dependencies for entity '"
				<< objectConfig.name << "': " << dependencies.error);
			return failure(
				VansSceneObjectBuildFailure::DependencyResolutionFailed,
				"Camera/Media dependencies for entity '" + objectConfig.name +
					"': " + dependencies.error);
		}
		cameraMediaDependencies.push_back(std::move(dependencies));
	}
	std::vector<VansSceneAudioReverbZoneDependencies> audioReverbZoneDependencies;
	audioReverbZoneDependencies.reserve(objectBuildPlan.objects.size());
	for (const Vans::VansSceneObjectBuildConfig& objectConfig : objectBuildPlan.objects)
	{
		VansSceneAudioReverbZoneDependencies dependencies;
		if (objectConfig.audioReverbZone)
		{
			dependencies = VansSceneAudioReverbZoneComponentBuilder::ResolveDependencies(
				*objectConfig.audioReverbZone, assetObjectRepository);
			if (!dependencies.success)
			{
				VANS_LOG_ERROR("[SceneBuild] Could not resolve AudioReverbZone dependencies for entity '"
					<< objectConfig.name << "': " << dependencies.error);
				return failure(
					VansSceneObjectBuildFailure::DependencyResolutionFailed,
					"AudioReverbZone dependencies for entity '" + objectConfig.name +
						"': " + dependencies.error);
			}
		}
		audioReverbZoneDependencies.push_back(std::move(dependencies));
	}
	std::vector<VansSceneLodGroupDependencies> lodGroupDependencies;
	lodGroupDependencies.reserve(objectBuildPlan.objects.size());
	for (const Vans::VansSceneObjectBuildConfig& objectConfig : objectBuildPlan.objects)
	{
		VansSceneLodGroupDependencies dependencies;
		if (objectConfig.lodGroup)
		{
			if (!objectConfig.render)
			{
				VANS_LOG_ERROR("[SceneBuild] LODGroup requires ModelRenderer for entity '"
					<< objectConfig.name << "'");
				return failure(
					VansSceneObjectBuildFailure::DependencyResolutionFailed,
					"LODGroup requires ModelRenderer for entity '" +
						objectConfig.name + "'");
			}
			dependencies = VansSceneLodGroupComponentBuilder::ResolveDependencies(
				m_Scene, *objectConfig.lodGroup);
			if (!dependencies.success)
			{
				VANS_LOG_ERROR("[SceneBuild] Could not resolve LODGroup dependencies for entity '"
					<< objectConfig.name << "': " << dependencies.error);
				return failure(
					VansSceneObjectBuildFailure::DependencyResolutionFailed,
					"LODGroup dependencies for entity '" + objectConfig.name +
						"': " + dependencies.error);
			}
		}
		lodGroupDependencies.push_back(std::move(dependencies));
	}
	std::vector<VansSceneParticleReservation> particleReservations;
	particleReservations.reserve(objectBuildPlan.objects.size());
	for (const Vans::VansSceneObjectBuildConfig& objectConfig : objectBuildPlan.objects)
	{
		VansSceneParticleReservation reservation;
		if (objectConfig.particle)
		{
			reservation = VansSceneParticleComponentBuilder::Reserve(
				m_Scene, *objectConfig.particle);
			if (!reservation.IsValid())
			{
				VANS_LOG_ERROR("[SceneBuild] Could not reserve Particle for entity '"
					<< objectConfig.name << "': " << reservation.Error());
				return failure(
					VansSceneObjectBuildFailure::DependencyResolutionFailed,
					"Particle reservation for entity '" + objectConfig.name +
						"': " + reservation.Error());
			}
		}
		particleReservations.push_back(std::move(reservation));
	}

	// === Pass 1: component instantiation ===
	for (std::size_t objectIndex = 0; objectIndex < objectBuildPlan.objects.size(); ++objectIndex)
	{
		const Vans::VansSceneObjectBuildConfig& objectConfig = objectBuildPlan.objects[objectIndex];
		const VansSceneLightDependencies& lightDependency = lightDependencies[objectIndex];
		const VansSceneCameraMediaDependencies& cameraMediaDependency =
			cameraMediaDependencies[objectIndex];
		const VansSceneAudioReverbZoneDependencies& audioReverbZoneDependency =
			audioReverbZoneDependencies[objectIndex];
		const VansSceneLodGroupDependencies& lodGroupDependency =
			lodGroupDependencies[objectIndex];
		VansSceneParticleReservation& particleReservation =
			particleReservations[objectIndex];
		RuntimeComponentBuildResults runtimeComponentBuildResults;
		auto pendingObject = std::make_unique<VansScriptObject>();
		VansScriptObject* obj = pendingObject.get();
		obj->m_EntityGuid = objectConfig.entityGuid;
		obj->m_ObjectName = objectConfig.name;
		obj->m_ModelAssetGuid = objectConfig.ResolveModelAssetGuid();

		const bool hasObjTransform = objectConfig.transform.has_value();
		glm::vec3 objPos(0.0f), objRot(0.0f), objScl(1.0f);
		if (objectConfig.transform)
		{
			objPos = ToVec3(objectConfig.transform->position);
			objRot = ToVec3(objectConfig.transform->rotation);
			objScl = ToVec3(objectConfig.transform->scale);
		}

		bool objectTransformAllocated = obj->m_OwnsTransform;
		VansRenderNodeBuildResult renderBuild;
		auto ensureObjectTransform = [&]()
		{
			if (!objectTransformAllocated &&
				obj->GetComponent<VansScriptRenderComponent>() == nullptr)
			{
				obj->m_TransformID = Vans::VansTransformStore::Allocate();
				obj->m_OwnsTransform = true;
				if (objectConfig.transform)
				{
					Vans::VansTransform transform = Vans::VansTransformStore::Read(obj->m_TransformID);
					transform.m_Position = objPos;
					transform.m_Rotation = objRot;
					transform.m_Scale = objScl;
					Vans::VansTransformStore::Write(obj->m_TransformID, transform);
				}
				objectTransformAllocated = true;
			}
		};

		if (objectConfig.render)
		{
			Vans::VansSceneRenderNodeConfig renderConfig = *objectConfig.render;
			if (objectConfig.transform)
				renderConfig.transform = objectConfig.transform;

			renderBuild = VansSceneRenderNodeBuilder::BuildRenderNode(
				m_Scene, device, renderConfig);
			if (!renderBuild.success)
			{
				VANS_LOG_ERROR("[SceneBuild] Could not build render node for entity '"
					<< objectConfig.name << "': " << renderBuild.error);
				return failure(
					VansSceneObjectBuildFailure::ComponentBuildFailed,
					"Render node for entity '" + objectConfig.name +
						"': " + renderBuild.error);
			}
			VansRenderNode* rn = renderBuild.GetPrimaryNode();
			if (!rn)
			{
				VANS_LOG_ERROR("[SceneBuild] Render node build produced no node for entity '"
					<< objectConfig.name << "'");
				return failure(
					VansSceneObjectBuildFailure::ComponentBuildFailed,
					"Render node build produced no node for entity '" +
						objectConfig.name + "'");
			}

			if (rn)
			{
				if (hasObjTransform)
					rn->SetTransformData(objPos, objRot, objScl);

				auto* rc = new VansScriptRenderComponent();
				rc->m_ComponentName = "render";
				rc->m_RenderNode = rn;
				rc->m_RenderNodes = renderBuild.nodes;

				if (!objectConfig.renderEnabled)
				{
					for (auto* renderNode : rc->m_RenderNodes)
						if (renderNode) renderNode->SetEnabled(false);
				}
				rc->m_Enabled = objectConfig.renderEnabled;

				obj->AddComponent(rc);
				runtimeComponentBuildResults.render = rc;
				obj->m_TransformID = rn->m_TransformID;

			}
		}

		if (objectConfig.lodGroup)
		{
			const VansSceneLodGroupBuildResult lodGroupBuild =
				VansSceneLodGroupComponentBuilder::Build(
					*obj,
					*objectConfig.lodGroup,
					lodGroupDependency,
					runtimeComponentBuildResults.render,
					FindRuntimeComponentGuid(objectConfig.componentGuids, "lod_group"));
			if (!lodGroupBuild.success)
			{
				VANS_LOG_ERROR("[SceneBuild] Could not build LODGroup for entity '"
					<< objectConfig.name << "': " << lodGroupBuild.error);
				m_Scene.DiscardRenderNodeBuild(renderBuild);
				return failure(
					VansSceneObjectBuildFailure::ComponentBuildFailed,
					"LODGroup for entity '" + objectConfig.name +
						"': " + lodGroupBuild.error);
			}
		}
		// A scene Transform is a runtime component even when the object has no
		// render, physics, camera, or other component that would otherwise force
		// allocation. This is required for pure Transform targets such as
		// virtual cameras and camera focus markers.
		if (hasObjTransform)
			ensureObjectTransform();

		if (objectConfig.particle)
		{
			VansSceneParticleBuildResult particleBuild =
				VansSceneParticleComponentBuilder::Build(
					*obj,
					*objectConfig.particle,
					particleReservation,
					ensureObjectTransform);
			if (!particleBuild.success)
			{
				VANS_LOG_ERROR("[SceneBuild] Could not build Particle for entity '"
					<< objectConfig.name << "': " << particleBuild.error);
				m_Scene.DiscardRenderNodeBuild(renderBuild);
				return failure(
					VansSceneObjectBuildFailure::ComponentBuildFailed,
					"Particle for entity '" + objectConfig.name +
						"': " + particleBuild.error);
			}
			runtimeComponentBuildResults.particle = particleBuild.component;
		}

		runtimeComponentBuildResults.physics =
			VansScenePhysicsComponentBuilder::BuildPhysicsClothAndCharacter(
				m_Scene,
				*obj,
				objectConfig.physicsComponents,
				hasObjTransform,
				ensureObjectTransform);
		if (!runtimeComponentBuildResults.physics.success)
		{
			VANS_LOG_ERROR("[SceneBuild] Could not build Physics components for entity '"
				<< objectConfig.name << "': " << runtimeComponentBuildResults.physics.error);
			m_Scene.DiscardRenderNodeBuild(renderBuild);
			return failure(
				VansSceneObjectBuildFailure::ComponentBuildFailed,
				"Physics components for entity '" + objectConfig.name +
					"': " + runtimeComponentBuildResults.physics.error);
		}

		if (objectConfig.vehicleObject.vehicle)
		{
			const std::string vehicleGuid =
				FindRuntimeComponentGuid(objectConfig.componentGuids, "vehicle");
			if (vehicleGuid.empty())
			{
				m_Scene.DiscardRenderNodeBuild(renderBuild);
				return failure(
					VansSceneObjectBuildFailure::ComponentBuildFailed,
					"Vehicle component for entity '" + objectConfig.name +
						"' is missing its stable component GUID");
			}
			vehicleBuildRequests.push_back({
				objectConfig.entityGuid,
				*objectConfig.vehicleObject.vehicle,
				vehicleGuid });
		}

		if (objectConfig.multiMeshRoot ||
			(objectConfig.animation && obj->GetComponent<VansScriptRenderComponent>() == nullptr))
		{
			ensureObjectTransform();
		}

		runtimeComponentBuildResults.lights =
			VansSceneLightComponentBuilder::BuildLights(
				m_Scene,
				*obj,
				objectConfig.lightComponents,
				lightDependency,
				ensureObjectTransform);

		runtimeComponentBuildResults.cameraMedia =
			VansSceneCameraMediaComponentBuilder::Build(
				*obj,
				objectConfig.cameraMediaComponents,
				cameraMediaDependency,
				ensureObjectTransform);

		if (objectConfig.audioReverbZone)
		{
			const VansSceneAudioReverbZoneBuildResult audioReverbZoneBuild =
				VansSceneAudioReverbZoneComponentBuilder::Build(
					*obj,
					*objectConfig.audioReverbZone,
					audioReverbZoneDependency,
					ensureObjectTransform);
			if (!audioReverbZoneBuild.success)
			{
				VANS_LOG_ERROR("[SceneBuild] Could not build AudioReverbZone for entity '"
					<< objectConfig.name << "': " << audioReverbZoneBuild.error);
				m_Scene.DiscardRenderNodeBuild(renderBuild);
				return failure(
					VansSceneObjectBuildFailure::ComponentBuildFailed,
					"AudioReverbZone for entity '" + objectConfig.name +
						"': " + audioReverbZoneBuild.error);
			}
			runtimeComponentBuildResults.audioReverbZone =
				audioReverbZoneBuild.component;
		}

		if (objectConfig.localVolumetricFog)
		{
			ensureObjectTransform();
			auto* fogVolume = new VansScriptLocalVolumetricFogComponent();
			fogVolume->m_Settings = *objectConfig.localVolumetricFog;
			fogVolume->m_Enabled = objectConfig.localVolumetricFog->enabled;
			obj->AddComponent(fogVolume);
		}

		if (objectConfig.animation)
		{
			VansSceneAnimationComponentBuilder::AddAnimationPlaceholder(
				*obj,
				*objectConfig.animation,
				pendingAnimComps);
		}

		runtimeComponentBuildResults.scripts = VansSceneScriptComponentBuilder::Build(
			*obj,
			objectConfig.uiComponents,
			objectConfig.scriptComponents);
		if (!runtimeComponentBuildResults.scripts.success)
		{
			VANS_LOG_ERROR("[SceneBuild] Could not build Script/UI components for entity '"
				<< objectConfig.name << "': " << runtimeComponentBuildResults.scripts.error);
			m_Scene.DiscardRenderNodeBuild(renderBuild);
			return failure(
				VansSceneObjectBuildFailure::ComponentBuildFailed,
				"Script/UI components for entity '" + objectConfig.name +
					"': " + runtimeComponentBuildResults.scripts.error);
		}
		VansSceneLightComponentBuilder::BindVideo(m_Scene, *obj);
		ApplyRuntimeComponentGuids(*obj, objectConfig.componentGuids);

		if (objectConfig.parent)
		{
			ensureObjectTransform();
			if (obj->m_TransformID != UINT32_MAX)
			{
				VansPendingEntityParentLink link;
				link.childTransformId = obj->m_TransformID;
				link.childEntityGuid = objectConfig.entityGuid;
				link.childName = obj->m_ObjectName;
				link.parent = *objectConfig.parent;
				pendingEntityParentLinks.push_back(std::move(link));
			}
		}

		obj->SetActive(objectConfig.active);
		VansRuntimeEntityPublishResult runtimePublish = PublishRuntimeEntity(
			*m_RuntimeWorld,
			{ objectConfig.entityGuid, objectConfig.name,
				Vans::VansEntityHandle{}, objectConfig.active },
			*obj,
			objectConfig.componentGuids,
			runtimeComponentBuildResults,
			objectConfig.timeline,
			objectConfig.actionHost,
			objectConfig.navigationAgent,
			objectConfig.aiAgent,
			m_GameplayRuntime.get());
		if (!runtimePublish.success)
		{
			VANS_LOG_ERROR("[SceneBuild] Could not publish runtime entity '"
				<< objectConfig.name << "': " << runtimePublish.error);
			return failure(
				VansSceneObjectBuildFailure::EntityPublishFailed,
				"Runtime entity '" + objectConfig.name +
					"': " + runtimePublish.error);
		}
		m_SceneObjects.push_back(pendingObject.release());
		++m_SceneObjectCollectionGeneration;
	}

	// === Pass 2: vehicle reference resolution ===
	VansSceneVehicleBuildResult vehicleBuild =
		VansSceneVehicleComponentBuilder::BuildVehicles(m_Scene, vehicleBuildRequests);
	if (!vehicleBuild.success)
	{
		VANS_LOG_ERROR("[SceneBuild] Could not build Vehicle runtimes: "
			<< vehicleBuild.error);
		return failure(
			VansSceneObjectBuildFailure::VehicleBuildFailed,
			std::move(vehicleBuild.error));
	}
	vehicleDrivenTransformIds = std::move(vehicleBuild.drivenTransformIds);
	const VansDeferredRuntimePublishResult vehiclePublish = PublishDeferredVehicleRuntimeComponents(
		*m_RuntimeWorld,
		vehicleBuild.builtVehicles);
	if (!vehiclePublish.success)
	{
		VANS_LOG_ERROR("[SceneBuild] Could not publish deferred Vehicle components: "
			<< vehiclePublish.error);
		return failure(
			VansSceneObjectBuildFailure::VehiclePublishFailed,
			vehiclePublish.error);
	}
	std::vector<VansSceneMultiMeshGroupBuildPlan> multiMeshGroupPlans;
	multiMeshGroupPlans.reserve(objectBuildPlan.objects.size());
	for (const Vans::VansSceneObjectBuildConfig& objectConfig : objectBuildPlan.objects)
	{
		if (!objectConfig.multiMeshRoot)
			continue;
		VansSceneMultiMeshGroupBuildPlan plan =
			VansSceneMultiMeshGroupBuilder::Prepare(
				m_Scene, objectConfig, vehicleDrivenTransformIds);
		if (!plan.success)
		{
			VANS_LOG_ERROR("[SceneBuild] Could not prepare MultiMeshRoot for entity '"
				<< objectConfig.name << "': " << plan.error);
			return failure(
				VansSceneObjectBuildFailure::MultiMeshPreparationFailed,
				"MultiMeshRoot for entity '" + objectConfig.name +
					"': " + plan.error);
		}
		multiMeshGroupPlans.push_back(std::move(plan));
	}

	// === Pass 3: Entity transform parent publication ===
	std::unordered_set<std::string> parentedEntityGuids;
	for (VansPendingEntityParentLink& link : pendingEntityParentLinks)
	{
		if (!link.parent.IsValid() ||
			!parentedEntityGuids.insert(link.childEntityGuid).second)
		{
			VANS_LOG_ERROR("[TransformParent] Invalid or duplicate parent request for child='"
				<< link.childName << "'");
			return failure(
				VansSceneObjectBuildFailure::EntityParentFailed,
				"Invalid or duplicate parent request for child '" +
					link.childName + "'");
		}
		const std::string parentEntityGuid = link.parent.entityGuid.ToString();
		link.parentObject = m_Scene.FindObjectByGuid(parentEntityGuid);
		if (!link.parentObject || link.parentObject->m_TransformID == UINT32_MAX)
		{
			VANS_LOG_ERROR("[TransformParent] Could not resolve parent entity for child='"
				<< link.childName << "' parentGuid='" << parentEntityGuid << "'");
			return failure(
				VansSceneObjectBuildFailure::EntityParentFailed,
				"Could not resolve parent entity for child '" + link.childName +
					"' parentGuid='" + parentEntityGuid + "'");
		}
		link.childEntityHandle =
			m_RuntimeWorld->Entities().FindByGuid(link.childEntityGuid);
		link.parentEntityHandle =
			m_RuntimeWorld->Entities().FindByGuid(parentEntityGuid);
		const Vans::VansEntityRecord* childRecord =
			m_RuntimeWorld->Entities().Get(link.childEntityHandle);
		if (!childRecord || !link.parentEntityHandle.IsValid())
		{
			VANS_LOG_ERROR("[TransformParent] Runtime entity link is unavailable for child='"
				<< link.childName << "' parentGuid='" << parentEntityGuid << "'");
			return failure(
				VansSceneObjectBuildFailure::EntityParentFailed,
				"Runtime entity link is unavailable for child '" + link.childName +
					"' parentGuid='" + parentEntityGuid + "'");
		}
		link.previousParentHandle = childRecord->parent;

		if (vehicleDrivenTransformIds.count(link.childTransformId) == 0 && link.parent.IsEntity())
		{
			if (!Vans::VansTransformStore::IsAllocated(link.childTransformId) ||
				!Vans::VansTransformStore::IsAllocated(link.parentObject->m_TransformID) ||
				!m_TransformGraph.TryGetLocalTransform(
					link.childTransformId, link.previousLocalTransform))
			{
				VANS_LOG_ERROR("[TransformParent] Transform preflight failed for child='"
					<< link.childName << "' parentGuid='" << parentEntityGuid << "'");
				return failure(
					VansSceneObjectBuildFailure::EntityParentFailed,
					"Transform preflight failed for child '" + link.childName +
						"' parentGuid='" + parentEntityGuid + "'");
			}
			if (const Vans::VansTransformGraphLink* previous =
				m_TransformGraph.GetLink(link.childTransformId))
			{
				link.hasPreviousTransformLink = true;
				link.previousTransformLink = *previous;
			}
		}
	}

	for (const VansPendingEntityParentLink& link : pendingEntityParentLinks)
		m_RuntimeWorld->Commands().SetParent(link.childEntityHandle, link.parentEntityHandle);
	if (!pendingEntityParentLinks.empty())
		m_RuntimeWorld->CommitCommands(Vans::VansRuntimeCommandCommitPoint::SceneAssembly);

	const auto rollbackRuntimeParents = [&]()
	{
		bool restored = true;
		for (auto it = pendingEntityParentLinks.rbegin();
			it != pendingEntityParentLinks.rend(); ++it)
		{
			restored = m_RuntimeWorld->SetParent(
				it->childEntityHandle, it->previousParentHandle) && restored;
		}
		return restored;
	};
	const auto restoreTransformParent = [&](VansPendingEntityParentLink& link)
	{
		if (!link.hasAppliedTransformLink)
			return true;
		bool restored = false;
		if (!link.hasPreviousTransformLink)
		{
			restored = !m_TransformGraph.HasParent(link.childTransformId) ||
				m_TransformGraph.ClearParent(
					link.childTransformId, Vans::VansTransformReparentMode::KeepWorld);
		}
		else if (link.previousTransformLink.usesAnchor)
		{
			restored = m_TransformGraph.SetAnchorWithLocalTransform(
				link.childTransformId,
				link.previousTransformLink.parentTransformId,
				link.previousTransformLink.anchor,
				link.previousLocalTransform);
		}
		else
		{
			restored = m_TransformGraph.SetParent(
				link.childTransformId,
				link.previousTransformLink.parentTransformId,
				Vans::VansTransformReparentMode::KeepLocal) &&
				m_TransformGraph.SetLocalTransform(
					link.childTransformId, link.previousLocalTransform);
		}
		link.hasAppliedTransformLink = false;
		return restored;
	};
	const auto rollbackTransformParents = [&]()
	{
		bool restored = true;
		for (auto it = pendingEntityParentLinks.rbegin();
			it != pendingEntityParentLinks.rend(); ++it)
			restored = restoreTransformParent(*it) && restored;
		return restored;
	};
	for (const VansPendingEntityParentLink& link : pendingEntityParentLinks)
	{
		const Vans::VansEntityRecord* childRecord =
			m_RuntimeWorld->Entities().Get(link.childEntityHandle);
		if (!childRecord || childRecord->parent != link.parentEntityHandle)
		{
			const bool rollbackSucceeded = rollbackRuntimeParents();
			VANS_LOG_ERROR("[TransformParent] Runtime parent batch failed for child='"
				<< link.childName << "' rollback=" << rollbackSucceeded);
			return failure(
				VansSceneObjectBuildFailure::EntityParentFailed,
				"Runtime parent batch failed for child '" + link.childName +
					"' rollback=" + (rollbackSucceeded ? "true" : "false"));
		}
	}
	for (VansPendingEntityParentLink& link : pendingEntityParentLinks)
	{
		if (vehicleDrivenTransformIds.count(link.childTransformId) > 0 ||
			!link.parent.IsEntity())
			continue;
		if (!m_TransformGraph.SetParent(
			link.childTransformId,
			link.parentObject->m_TransformID,
			Vans::VansTransformReparentMode::KeepLocal))
		{
			const std::string transformError = m_TransformGraph.GetLastError();
			const bool transformRollbackSucceeded = rollbackTransformParents();
			const bool runtimeRollbackSucceeded = rollbackRuntimeParents();
			VANS_LOG_ERROR("[TransformParent] Could not link entity child='"
				<< link.childName << "' parentGuid='" << link.parent.entityGuid.ToString()
				<< "': " << transformError
				<< " transformRollback=" << transformRollbackSucceeded
				<< " runtimeRollback=" << runtimeRollbackSucceeded);
			return failure(
				VansSceneObjectBuildFailure::EntityParentFailed,
				"Could not link entity child '" + link.childName +
					"' parentGuid='" + link.parent.entityGuid.ToString() +
					"': " + transformError);
		}
		link.hasAppliedTransformLink = true;
		const Vans::VansTransformGraphLink* published =
			m_TransformGraph.GetLink(link.childTransformId);
		if (!published || published->usesAnchor ||
			published->parentTransformId != link.parentObject->m_TransformID)
		{
			const bool transformRollbackSucceeded = rollbackTransformParents();
			const bool runtimeRollbackSucceeded = rollbackRuntimeParents();
			VANS_LOG_ERROR("[TransformParent] Transform parent confirmation failed for child='"
				<< link.childName << "' transformRollback=" << transformRollbackSucceeded
				<< " runtimeRollback=" << runtimeRollbackSucceeded);
			return failure(
				VansSceneObjectBuildFailure::EntityParentFailed,
				"Transform parent confirmation failed for child '" +
					link.childName + "'");
		}
	}

	// === Pass 3.5: MultiMeshGroup commit ===
	for (const VansSceneMultiMeshGroupBuildPlan& plan : multiMeshGroupPlans)
		VansSceneMultiMeshGroupBuilder::Commit(m_Scene, plan);

	// === Pass 4: Animation and Ragdoll build ===
	const VansSceneAnimationBuildResult animationBuild =
		VansSceneAnimationComponentBuilder::BuildAnimations(
			m_Scene, pendingAnimComps, projectRoot);
	if (!animationBuild.success)
	{
		VANS_LOG_ERROR("[SceneBuild] Could not build Animation/Ragdoll runtimes: "
			<< animationBuild.error);
		return failure(
			VansSceneObjectBuildFailure::AnimationBuildFailed,
			animationBuild.error);
	}
	const VansDeferredRuntimePublishResult animationPublish =
		PublishDeferredAnimationRuntimeComponents(
			m_Scene, *m_RuntimeWorld, objectBuildPlan.objects);
	if (!animationPublish.success)
	{
		VANS_LOG_ERROR("[SceneBuild] Could not publish deferred Animation/Ragdoll components: "
			<< animationPublish.error);
		return failure(
			VansSceneObjectBuildFailure::AnimationPublishFailed,
			animationPublish.error);
	}
	auto* animationStorage = m_RuntimeWorld->FindStorage<
		Vans::VansRuntimeAnimationComponent>(Vans::VansRuntimeComponentType_Animation);
	std::vector<VansPendingEntityParentLink*> pendingAnchorParentLinks;
	for (VansPendingEntityParentLink& link : pendingEntityParentLinks)
	{
		if (!link.parent.IsAnchor()
			|| vehicleDrivenTransformIds.count(link.childTransformId) > 0)
			continue;
		const Vans::VansComponentHandle animationHandle = m_RuntimeWorld->FindComponentByGuid(
			link.parent.animationComponentGuid.ToString(),
			Vans::VansRuntimeComponentType_Animation);
		const Vans::VansComponentHeader* animationHeader =
			m_RuntimeWorld->GetComponentHeader(animationHandle);
		const Vans::VansRuntimeAnimationComponent* animation =
			animationStorage ? animationStorage->Get(animationHandle) : nullptr;
		if (!link.parentObject || link.parentObject->m_TransformID == UINT32_MAX ||
			!animationHeader || animationHeader->owner != link.parentEntityHandle ||
			!animation || animation->skeletonInstanceId == 0 ||
			animation->skeletonInstanceGeneration == 0 ||
			!Vans::VansTransformStore::IsAllocated(link.childTransformId) ||
			!Vans::VansTransformStore::IsAllocated(link.parentObject->m_TransformID) ||
			!m_TransformGraph.TryGetLocalTransform(
				link.childTransformId, link.previousLocalTransform))
		{
			VANS_LOG_ERROR("[TransformGraph] Anchor parent preflight failed for child='"
				<< link.childName << "' anchorGuid='"
				<< link.parent.anchorGuid.ToString() << "'");
			return failure(
				VansSceneObjectBuildFailure::AnchorParentFailed,
				"Anchor parent preflight failed for child '" + link.childName +
					"' anchorGuid='" + link.parent.anchorGuid.ToString() + "'");
		}
		if (const Vans::VansTransformGraphLink* previous =
			m_TransformGraph.GetLink(link.childTransformId))
		{
			link.hasPreviousTransformLink = true;
			link.previousTransformLink = *previous;
		}
		pendingAnchorParentLinks.push_back(&link);
	}
	const auto rollbackAnchorParents = [&]()
	{
		bool restored = true;
		for (auto it = pendingAnchorParentLinks.rbegin();
			it != pendingAnchorParentLinks.rend(); ++it)
			restored = restoreTransformParent(**it) && restored;
		return restored;
	};
	for (VansPendingEntityParentLink* link : pendingAnchorParentLinks)
	{
		const Vans::VansComponentHandle animationHandle = m_RuntimeWorld->FindComponentByGuid(
			link->parent.animationComponentGuid.ToString(),
			Vans::VansRuntimeComponentType_Animation);
		const Vans::VansRuntimeAnimationComponent* animation =
			animationStorage ? animationStorage->Get(animationHandle) : nullptr;
		if (!animation || !m_Scene.SetTransformAnchorReference(
			link->childTransformId,
			link->parentObject->m_TransformID,
			link->parent))
		{
			const bool rollbackSucceeded = rollbackAnchorParents();
			VANS_LOG_ERROR("[TransformGraph] Could not bind anchor parent for child='"
				<< link->childName << "' anchorGuid='"
				<< link->parent.anchorGuid.ToString()
				<< "' rollback=" << rollbackSucceeded);
			return failure(
				VansSceneObjectBuildFailure::AnchorParentFailed,
				"Could not bind anchor parent for child '" + link->childName +
					"' anchorGuid='" + link->parent.anchorGuid.ToString() + "'");
		}
		link->hasAppliedTransformLink = true;
		const Vans::VansTransformGraphLink* published =
			m_TransformGraph.GetLink(link->childTransformId);
		const Vans::VansTransformAnchorKind expectedKind =
			link->parent.kind == Vans::VansSceneParentKind::Bone
				? Vans::VansTransformAnchorKind::Bone
				: Vans::VansTransformAnchorKind::Socket;
		if (!published || !published->usesAnchor ||
			published->parentTransformId != link->parentObject->m_TransformID ||
			published->anchor.instanceId != animation->skeletonInstanceId ||
			published->anchor.instanceGeneration != animation->skeletonInstanceGeneration ||
			published->anchor.kind != expectedKind ||
			published->anchor.anchorGuid != link->parent.anchorGuid.ToString())
		{
			const bool rollbackSucceeded = rollbackAnchorParents();
			VANS_LOG_ERROR("[TransformGraph] Anchor parent confirmation failed for child='"
				<< link->childName << "' rollback=" << rollbackSucceeded);
			return failure(
				VansSceneObjectBuildFailure::AnchorParentFailed,
				"Anchor parent confirmation failed for child '" +
					link->childName + "'");
		}
	}

	// === Pass 5: Cloth animation binding ===
	const VansSceneClothAnimationBindingResult clothBinding =
		VansSceneClothAnimationBindingExecutor::Execute(m_Scene);
	if (!clothBinding.success)
	{
		VANS_LOG_ERROR("[SceneBuild] Could not bind Cloth animation: "
			<< clothBinding.error);
		return failure(
			VansSceneObjectBuildFailure::ClothBindingFailed,
			clothBinding.error);
	}
    // 批量追加对象不能重新初始化已在运行的 Timeline/AI 或重播已有资源音频。
    const bool initialRuntimeSetup = !m_AIWorld;
	if (initialRuntimeSetup)
	{
		std::string timelineError;
		if (!Vans::VansSceneGameplayComposition::ConfigureTimelineRuntime(
			m_Scene, timelineError))
		{
			VANS_LOG_ERROR("[SceneBuild] Could not configure Timeline runtime: " << timelineError);
			return failure(
				VansSceneObjectBuildFailure::TimelineSetupFailed,
				std::move(timelineError));
		}
	}
    if (!m_AIWorld)
    {
        auto ai = std::make_unique<Vans::VansAIWorld>();
        std::string aiError;
        if (!ai->Initialize(*m_RuntimeWorld, m_GameplayRuntime.get(),
            Vans::VansProjectManager::Get().GetAssetObjectRepository(), aiError))
        {
            VANS_LOG_ERROR("[SceneBuild] Could not initialize AI World: " << aiError);
			return failure(
				VansSceneObjectBuildFailure::AIInitializationFailed,
				std::move(aiError));
        }
        m_AIWorld = std::move(ai);
    }
    if (initialRuntimeSetup) m_AudioManager.PlayAutoPlay();

	return VansSceneObjectBuildResult{
		true, VansSceneObjectBuildFailure::None, {} };
}
