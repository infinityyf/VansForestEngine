#pragma once

#include "VansEntityRegistry.h"
#include "VansRuntimeComponentTypes.h"

#include <string>
#include <vector>

namespace Vans
{
enum class VansEntityCommandType
{
	CreateEntity,
	DestroyEntity,
	AddTransformComponent,
	AddRenderComponent,
	AddPhysicsComponent,
	AddClothComponent,
	AddCharacterControllerComponent,
	AddVehicleComponent,
	AddAnimationComponent,
	AddRagdollComponent,
	AddAudioComponent,
	AddAudioReverbZoneComponent,
	AddUIComponent,
	AddScriptComponent,
	AddVideoComponent,
	AddParticleComponent,
	AddCameraComponent,
	AddLightComponent,
	AddTimelineComponent,
	AddActionHostComponent,
	SetEntityActive,
	SetEntityName,
	SetComponentEnabled,
	RemoveComponent,
	SetParent
};

struct VansEntityCommand
{
	VansEntityCommandType type = VansEntityCommandType::CreateEntity;
	VansEntityCreateDesc createDesc;
	VansEntityHandle entity;
	VansComponentHandle component;
	VansEntityHandle parent;
	VansRuntimeTransformComponent transformComponent;
	VansRuntimeRenderComponent renderComponent;
	VansRuntimePhysicsComponent physicsComponent;
	VansRuntimeClothComponent clothComponent;
	VansRuntimeCharacterControllerComponent characterControllerComponent;
	VansRuntimeVehicleComponent vehicleComponent;
	VansRuntimeAnimationComponent animationComponent;
	VansRuntimeRagdollComponent ragdollComponent;
	VansRuntimeAudioComponent audioComponent;
	VansRuntimeAudioReverbZoneComponent audioReverbZoneComponent;
	VansRuntimeUIComponent uiComponent;
	VansRuntimeScriptComponent scriptComponent;
	VansRuntimeVideoComponent videoComponent;
	VansRuntimeParticleComponent particleComponent;
	VansRuntimeCameraComponent cameraComponent;
	VansRuntimeLightComponent lightComponent;
	VansRuntimeTimelineComponent timelineComponent;
	VansRuntimeActionHostComponent actionHostComponent;
	std::uint16_t componentTypeId = VansInvalidComponentTypeId;
	std::string componentStableGuid;
	std::string stringValue;
	bool boolValue = false;
	VansDestroyChildrenPolicy destroyChildrenPolicy = VansDestroyChildrenPolicy::DestroyChildren;
};

class VansEntityCommandBuffer
{
public:
	void CreateEntity(VansEntityCreateDesc desc);
	void DestroyEntity(
		VansEntityHandle entity,
		VansDestroyChildrenPolicy childrenPolicy = VansDestroyChildrenPolicy::DestroyChildren);
	void AddTransformComponent(
		VansEntityHandle entity,
		std::string stableGuid,
		std::uint32_t transformStoreId,
		bool enabled);
	void AddRenderComponent(
		VansEntityHandle entity,
		std::string stableGuid,
		VansGraphics::VansRenderNode* renderNode,
		std::vector<VansGraphics::VansRenderNode*> renderNodes,
		bool enabled);
	void AddPhysicsComponent(
		VansEntityHandle entity,
		std::string stableGuid,
		VansEngine::VansPhysicsNode* physicsNode,
		bool enabled);
	void AddClothComponent(
		VansEntityHandle entity,
		std::string stableGuid,
		VansEngine::VansClothNode* clothNode,
		std::string profilePath,
		bool enabled);
	void AddCharacterControllerComponent(
		VansEntityHandle entity,
		std::string stableGuid,
		VansEngine::VansCharacterControllerNode* controllerNode,
		bool enabled);
	void AddVehicleComponent(
		VansEntityHandle entity,
		std::string stableGuid,
		VansEngine::VansPhysicsVehicle* vehicle,
		bool enabled);
	void AddAnimationComponent(
		VansEntityHandle entity,
		std::string stableGuid,
		VansGraphics::VansAnimationNode* animationNode,
		bool enabled);
	void AddRagdollComponent(
		VansEntityHandle entity,
		std::string stableGuid,
		VansGraphics::VansAnimationNode* animationNode,
		std::uint8_t initialDriveMode,
		std::string profilePath,
		std::string profileName,
		int configuredBodyCount,
		int configuredJointCount,
		bool enabled);
	void AddAudioComponent(
		VansEntityHandle entity,
		std::string stableGuid,
		VansEngine::VansAudioNode* audioNode,
		VansEngine::VansAudioSourceBinding* sourceBinding,
		std::string sourceName,
		VansEngine::AudioConeSettings coneSettings,
		bool dopplerEnabled,
		bool hasLastAudioPosition,
		float lastAudioPositionX,
		float lastAudioPositionY,
		float lastAudioPositionZ,
		VansEngine::AudioOcclusionSettings occlusionSettings,
		VansEngine::AudioOcclusionState occlusionState,
		bool enabled);
	void AddAudioReverbZoneComponent(
		VansEntityHandle entity,
		std::uint16_t typeId,
		std::string stableGuid,
		VansRuntimeAudioReverbZoneComponent reverbZone,
		bool enabled);
	void AddUIComponent(
		VansEntityHandle entity,
		std::string stableGuid,
		VansRuntimeUIComponent uiComponent,
		bool enabled);
	void AddScriptComponent(
		VansEntityHandle entity,
		std::string stableGuid,
		VansRuntimeScriptComponent scriptComponent,
		bool enabled);
	void AddVideoComponent(
		VansEntityHandle entity,
		std::string stableGuid,
		VansGraphics::VansVideoTexture* videoTexture,
		VansGraphics::VansVideoManager* videoManager,
		int bindlessFirstSlot,
		bool enabled);
	void AddParticleComponent(
		VansEntityHandle entity,
		std::string stableGuid,
		VansGraphics::VansParticleRuntime* runtime,
		VansGraphics::VansParticleRenderNode* renderNode,
		bool playOnAwake,
		bool isPlaying,
		float playTime,
		bool hasWorldPositionOverride,
		float worldPositionOverrideX,
		float worldPositionOverrideY,
		float worldPositionOverrideZ,
		bool enabled);
	void AddCameraComponent(
		VansEntityHandle entity,
		std::string stableGuid,
		VansGraphics::VansCamera* camera,
		bool enabled);
	void AddLightComponent(
		VansEntityHandle entity,
		std::uint16_t typeId,
		std::string stableGuid,
		VansGraphics::VansLightManager* lightManager,
		int lightIndex,
		VansRuntimeLightKind kind,
		bool enabled);
	void AddTimelineComponent(
		VansEntityHandle entity,
		std::string stableGuid,
		VansRuntimeTimelineComponent timelineComponent,
		bool enabled);
	void AddActionHostComponent(
		VansEntityHandle entity,
		std::string stableGuid,
		std::shared_ptr<VansActionHost> host,
		bool enabled);
	void SetEntityActive(VansEntityHandle entity, bool active);
	void SetEntityName(VansEntityHandle entity, std::string name);
	void SetComponentEnabled(VansComponentHandle component, bool enabled);
	void RemoveComponent(VansComponentHandle component);
	void SetParent(VansEntityHandle entity, VansEntityHandle parent);

	std::size_t PendingCount() const { return m_Commands.size(); }
	bool Empty() const { return m_Commands.empty(); }
	void Clear() { m_Commands.clear(); }
	std::vector<VansEntityCommand> TakeCommands();

private:
	std::vector<VansEntityCommand> m_Commands;
};
}
