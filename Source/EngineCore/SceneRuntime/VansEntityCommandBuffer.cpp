#include "VansEntityCommandBuffer.h"

#include <utility>

namespace Vans
{
void VansEntityCommandBuffer::CreateEntity(VansEntityCreateDesc desc)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::CreateEntity;
	command.createDesc = std::move(desc);
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::DestroyEntity(VansEntityHandle entity, VansDestroyChildrenPolicy childrenPolicy)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::DestroyEntity;
	command.entity = entity;
	command.destroyChildrenPolicy = childrenPolicy;
	m_Commands.push_back(command);
}

void VansEntityCommandBuffer::AddTransformComponent(
	VansEntityHandle entity,
	std::string stableGuid,
	std::uint32_t transformStoreId,
	bool enabled)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::AddTransformComponent;
	command.entity = entity;
	command.transformComponent.transformStoreId = transformStoreId;
	command.componentStableGuid = std::move(stableGuid);
	command.boolValue = enabled;
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::AddRenderComponent(
	VansEntityHandle entity,
	std::string stableGuid,
	VansGraphics::VansRenderNode* renderNode,
	std::vector<VansGraphics::VansRenderNode*> renderNodes,
	bool enabled)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::AddRenderComponent;
	command.entity = entity;
	command.renderComponent.renderNode = renderNode;
	command.renderComponent.renderNodes = std::move(renderNodes);
	command.componentStableGuid = std::move(stableGuid);
	command.boolValue = enabled;
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::AddPhysicsComponent(
	VansEntityHandle entity,
	std::string stableGuid,
	VansEngine::VansPhysicsNode* physicsNode,
	bool enabled)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::AddPhysicsComponent;
	command.entity = entity;
	command.physicsComponent.physicsNode = physicsNode;
	command.componentStableGuid = std::move(stableGuid);
	command.boolValue = enabled;
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::AddClothComponent(
	VansEntityHandle entity,
	std::string stableGuid,
	VansEngine::VansClothNode* clothNode,
	std::string profilePath,
	bool enabled)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::AddClothComponent;
	command.entity = entity;
	command.clothComponent.clothNode = clothNode;
	command.clothComponent.profilePath = std::move(profilePath);
	command.componentStableGuid = std::move(stableGuid);
	command.boolValue = enabled;
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::AddCharacterControllerComponent(
	VansEntityHandle entity,
	std::string stableGuid,
	VansEngine::VansCharacterControllerNode* controllerNode,
	bool enabled)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::AddCharacterControllerComponent;
	command.entity = entity;
	command.characterControllerComponent.controllerNode = controllerNode;
	command.componentStableGuid = std::move(stableGuid);
	command.boolValue = enabled;
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::AddVehicleComponent(
	VansEntityHandle entity,
	std::string stableGuid,
	VansEngine::VansPhysicsVehicle* vehicle,
	bool enabled)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::AddVehicleComponent;
	command.entity = entity;
	command.vehicleComponent.vehicle = vehicle;
	command.componentStableGuid = std::move(stableGuid);
	command.boolValue = enabled;
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::AddAnimationComponent(
	VansEntityHandle entity,
	std::string stableGuid,
	VansGraphics::VansAnimationNode* animationNode,
	std::uint64_t skeletonInstanceId,
	std::uint32_t skeletonInstanceGeneration,
	bool enabled)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::AddAnimationComponent;
	command.entity = entity;
	command.animationComponent.animationNode = animationNode;
	command.animationComponent.skeletonInstanceId = skeletonInstanceId;
	command.animationComponent.skeletonInstanceGeneration = skeletonInstanceGeneration;
	command.componentStableGuid = std::move(stableGuid);
	command.boolValue = enabled;
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::AddRagdollComponent(
	VansEntityHandle entity,
	std::string stableGuid,
	VansGraphics::VansAnimationNode* animationNode,
	std::uint8_t initialDriveMode,
	std::string profilePath,
	std::string profileName,
	int configuredBodyCount,
	int configuredJointCount,
	bool enabled)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::AddRagdollComponent;
	command.entity = entity;
	command.ragdollComponent.animationNode = animationNode;
	command.ragdollComponent.initialDriveMode = initialDriveMode;
	command.ragdollComponent.profilePath = std::move(profilePath);
	command.ragdollComponent.profileName = std::move(profileName);
	command.ragdollComponent.configuredBodyCount = configuredBodyCount;
	command.ragdollComponent.configuredJointCount = configuredJointCount;
	command.componentStableGuid = std::move(stableGuid);
	command.boolValue = enabled;
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::AddAudioComponent(
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
	bool enabled)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::AddAudioComponent;
	command.entity = entity;
	command.audioComponent.audioNode = audioNode;
	command.audioComponent.sourceBinding = sourceBinding;
	command.audioComponent.sourceName = std::move(sourceName);
	command.audioComponent.coneSettings = coneSettings;
	command.audioComponent.dopplerEnabled = dopplerEnabled;
	command.audioComponent.hasLastAudioPosition = hasLastAudioPosition;
	command.audioComponent.lastAudioPositionX = lastAudioPositionX;
	command.audioComponent.lastAudioPositionY = lastAudioPositionY;
	command.audioComponent.lastAudioPositionZ = lastAudioPositionZ;
	command.audioComponent.occlusionSettings = std::move(occlusionSettings);
	command.audioComponent.occlusionState = occlusionState;
	command.componentStableGuid = std::move(stableGuid);
	command.boolValue = enabled;
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::AddAudioReverbZoneComponent(
	VansEntityHandle entity,
	std::uint16_t typeId,
	std::string stableGuid,
	VansRuntimeAudioReverbZoneComponent reverbZone,
	bool enabled)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::AddAudioReverbZoneComponent;
	command.entity = entity;
	command.componentTypeId = typeId;
	command.audioReverbZoneComponent = std::move(reverbZone);
	command.componentStableGuid = std::move(stableGuid);
	command.boolValue = enabled;
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::AddUIComponent(
	VansEntityHandle entity,
	std::string stableGuid,
	VansRuntimeUIComponent uiComponent,
	bool enabled)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::AddUIComponent;
	command.entity = entity;
	command.uiComponent = std::move(uiComponent);
	command.componentStableGuid = std::move(stableGuid);
	command.boolValue = enabled;
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::AddScriptComponent(
	VansEntityHandle entity,
	std::string stableGuid,
	VansRuntimeScriptComponent scriptComponent,
	bool enabled)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::AddScriptComponent;
	command.entity = entity;
	command.scriptComponent = std::move(scriptComponent);
	command.componentStableGuid = std::move(stableGuid);
	command.boolValue = enabled;
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::AddVideoComponent(
	VansEntityHandle entity,
	std::string stableGuid,
	VansGraphics::VansVideoTexture* videoTexture,
	VansGraphics::VansVideoManager* videoManager,
	int bindlessFirstSlot,
	bool enabled)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::AddVideoComponent;
	command.entity = entity;
	command.videoComponent.videoTexture = videoTexture;
	command.videoComponent.videoManager = videoManager;
	command.videoComponent.bindlessFirstSlot = bindlessFirstSlot;
	command.componentStableGuid = std::move(stableGuid);
	command.boolValue = enabled;
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::AddParticleComponent(
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
	bool enabled)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::AddParticleComponent;
	command.entity = entity;
	command.particleComponent.runtime = runtime;
	command.particleComponent.renderNode = renderNode;
	command.particleComponent.playOnAwake = playOnAwake;
	command.particleComponent.isPlaying = isPlaying;
	command.particleComponent.playTime = playTime;
	command.particleComponent.hasWorldPositionOverride = hasWorldPositionOverride;
	command.particleComponent.worldPositionOverrideX = worldPositionOverrideX;
	command.particleComponent.worldPositionOverrideY = worldPositionOverrideY;
	command.particleComponent.worldPositionOverrideZ = worldPositionOverrideZ;
	command.componentStableGuid = std::move(stableGuid);
	command.boolValue = enabled;
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::AddCameraComponent(
	VansEntityHandle entity,
	std::string stableGuid,
	VansGraphics::VansCamera* camera,
	bool enabled)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::AddCameraComponent;
	command.entity = entity;
	command.cameraComponent.camera = camera;
	command.componentStableGuid = std::move(stableGuid);
	command.boolValue = enabled;
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::AddLightComponent(
	VansEntityHandle entity,
	std::uint16_t typeId,
	std::string stableGuid,
	VansGraphics::VansLightManager* lightManager,
	int lightIndex,
	VansRuntimeLightKind kind,
	bool enabled)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::AddLightComponent;
	command.entity = entity;
	command.componentTypeId = typeId;
	command.lightComponent.lightManager = lightManager;
	command.lightComponent.lightIndex = lightIndex;
	command.lightComponent.kind = kind;
	command.componentStableGuid = std::move(stableGuid);
	command.boolValue = enabled;
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::AddTimelineComponent(
	VansEntityHandle entity,
	std::string stableGuid,
	VansRuntimeTimelineComponent timelineComponent,
	bool enabled)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::AddTimelineComponent;
	command.entity = entity;
	command.timelineComponent = std::move(timelineComponent);
	command.componentStableGuid = std::move(stableGuid);
	command.boolValue = enabled;
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::AddActionHostComponent(
	VansEntityHandle entity,
	std::string stableGuid,
	std::shared_ptr<VansActionHost> host,
	bool enabled)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::AddActionHostComponent;
	command.entity = entity;
	command.actionHostComponent.host = std::move(host);
	command.componentStableGuid = std::move(stableGuid);
	command.boolValue = enabled;
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::SetEntityActive(VansEntityHandle entity, bool active)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::SetEntityActive;
	command.entity = entity;
	command.boolValue = active;
	m_Commands.push_back(command);
}

void VansEntityCommandBuffer::SetEntityName(VansEntityHandle entity, std::string name)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::SetEntityName;
	command.entity = entity;
	command.stringValue = std::move(name);
	m_Commands.push_back(std::move(command));
}

void VansEntityCommandBuffer::SetComponentEnabled(VansComponentHandle component, bool enabled)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::SetComponentEnabled;
	command.component = component;
	command.boolValue = enabled;
	m_Commands.push_back(command);
}

void VansEntityCommandBuffer::RemoveComponent(VansComponentHandle component)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::RemoveComponent;
	command.component = component;
	m_Commands.push_back(command);
}

void VansEntityCommandBuffer::SetParent(VansEntityHandle entity, VansEntityHandle parent)
{
	VansEntityCommand command;
	command.type = VansEntityCommandType::SetParent;
	command.entity = entity;
	command.parent = parent;
	m_Commands.push_back(command);
}

std::vector<VansEntityCommand> VansEntityCommandBuffer::TakeCommands()
{
	std::vector<VansEntityCommand> commands;
	commands.swap(m_Commands);
	return commands;
}
}
