#include "VansEntityCommandBuffer.h"

#include "VansRuntimeWorld.h"

#include <utility>

namespace Vans
{
namespace
{
std::uint16_t RuntimeTypeId(VansRuntimeAudioEnvironmentKind kind)
{
	return kind == VansRuntimeAudioEnvironmentKind::Volume
		? VansRuntimeComponentType_AudioVolume
		: VansRuntimeComponentType_AudioReverbZone;
}

std::uint16_t RuntimeTypeId(VansRuntimeLightKind kind)
{
	switch (kind)
	{
	case VansRuntimeLightKind::Directional: return VansRuntimeComponentType_DirectionalLight;
	case VansRuntimeLightKind::Point: return VansRuntimeComponentType_PointLight;
	case VansRuntimeLightKind::Spot: return VansRuntimeComponentType_SpotLight;
	case VansRuntimeLightKind::Rect: return VansRuntimeComponentType_RectLight;
	}
	return VansInvalidComponentTypeId;
}
}

template <typename T>
void VansEntityCommandBuffer::AddComponentCommand(
	VansEntityHandle entity,
	std::uint16_t typeId,
	std::string stableGuid,
	T component,
	bool enabled)
{
	m_Commands.emplace_back(
		[entity, typeId, stableGuid = std::move(stableGuid), component = std::move(component), enabled]
		(VansRuntimeWorld& world) mutable
		{
			world.AddComponent(
				entity,
				typeId,
				std::move(component),
				std::move(stableGuid),
				enabled);
		});
}

void VansEntityCommandBuffer::CreateEntity(VansEntityCreateDesc desc)
{
	m_Commands.emplace_back(
		[desc = std::move(desc)](VansRuntimeWorld& world) mutable
		{ world.CreateEntity(desc); });
}

void VansEntityCommandBuffer::DestroyEntity(
	VansEntityHandle entity,
	VansDestroyChildrenPolicy childrenPolicy)
{
	m_Commands.emplace_back(
		[entity, childrenPolicy](VansRuntimeWorld& world)
		{ world.DestroyEntity(entity, childrenPolicy); });
}

void VansEntityCommandBuffer::AddTransformComponent(
	VansEntityHandle entity, std::string stableGuid, std::uint32_t transformStoreId, bool enabled)
{
	AddComponentCommand(entity, VansRuntimeComponentType_Transform, std::move(stableGuid),
		VansRuntimeTransformComponent{ transformStoreId }, enabled);
}

void VansEntityCommandBuffer::AddRenderComponent(
	VansEntityHandle entity, std::string stableGuid,
	VansGraphics::VansRenderNode* renderNode,
	std::vector<VansGraphics::VansRenderNode*> renderNodes,
	bool enabled)
{
	VansRuntimeRenderComponent component;
	component.renderNode = renderNode;
	component.renderNodes = std::move(renderNodes);
	AddComponentCommand(entity, VansRuntimeComponentType_Render, std::move(stableGuid),
		std::move(component), enabled);
}

void VansEntityCommandBuffer::AddPhysicsComponent(
	VansEntityHandle entity, std::string stableGuid,
	VansEngine::VansPhysicsNode* physicsNode, bool enabled)
{
	AddComponentCommand(entity, VansRuntimeComponentType_Physics, std::move(stableGuid),
		VansRuntimePhysicsComponent{ physicsNode }, enabled);
}

void VansEntityCommandBuffer::AddClothComponent(
	VansEntityHandle entity, std::string stableGuid,
	VansEngine::VansClothNode* clothNode, std::string profileAssetGuid, bool enabled)
{
	VansRuntimeClothComponent component;
	component.clothNode = clothNode;
	component.profileAssetGuid = std::move(profileAssetGuid);
	AddComponentCommand(entity, VansRuntimeComponentType_Cloth, std::move(stableGuid),
		std::move(component), enabled);
}

void VansEntityCommandBuffer::AddCharacterControllerComponent(
	VansEntityHandle entity, std::string stableGuid,
	VansEngine::VansCharacterControllerNode* controllerNode, bool enabled)
{
	AddComponentCommand(entity, VansRuntimeComponentType_CharacterController, std::move(stableGuid),
		VansRuntimeCharacterControllerComponent{ controllerNode }, enabled);
}

void VansEntityCommandBuffer::AddVehicleComponent(
	VansEntityHandle entity, std::string stableGuid,
	VansEngine::VansPhysicsVehicle* vehicle, bool enabled)
{
	AddComponentCommand(entity, VansRuntimeComponentType_Vehicle, std::move(stableGuid),
		VansRuntimeVehicleComponent{ vehicle }, enabled);
}

void VansEntityCommandBuffer::AddAnimationComponent(
	VansEntityHandle entity, std::string stableGuid,
	VansGraphics::VansAnimationNode* animationNode,
	std::uint64_t skeletonInstanceId,
	std::uint32_t skeletonInstanceGeneration,
	bool enabled)
{
	VansRuntimeAnimationComponent component;
	component.animationNode = animationNode;
	component.skeletonInstanceId = skeletonInstanceId;
	component.skeletonInstanceGeneration = skeletonInstanceGeneration;
	AddComponentCommand(entity, VansRuntimeComponentType_Animation, std::move(stableGuid),
		std::move(component), enabled);
}

void VansEntityCommandBuffer::AddRagdollComponent(
	VansEntityHandle entity, std::string stableGuid,
	VansGraphics::VansAnimationNode* animationNode,
	std::uint8_t initialDriveMode,
	std::string profileAssetGuid,
	std::string profileName,
	int configuredBodyCount,
	int configuredJointCount,
	bool enabled)
{
	VansRuntimeRagdollComponent component;
	component.animationNode = animationNode;
	component.initialDriveMode = initialDriveMode;
	component.profileAssetGuid = std::move(profileAssetGuid);
	component.profileName = std::move(profileName);
	component.configuredBodyCount = configuredBodyCount;
	component.configuredJointCount = configuredJointCount;
	AddComponentCommand(entity, VansRuntimeComponentType_Ragdoll, std::move(stableGuid),
		std::move(component), enabled);
}

void VansEntityCommandBuffer::AddAudioComponent(
	VansEntityHandle entity, std::string stableGuid,
	VansEngine::VansAudioNode* audioNode,
	VansEngine::VansAudioSourceBinding* sourceBinding,
	std::string assetGuid,
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
	VansRuntimeAudioComponent component;
	component.audioNode = audioNode;
	component.sourceBinding = sourceBinding;
	component.assetGuid = std::move(assetGuid);
	component.coneSettings = coneSettings;
	component.dopplerEnabled = dopplerEnabled;
	component.hasLastAudioPosition = hasLastAudioPosition;
	component.lastAudioPositionX = lastAudioPositionX;
	component.lastAudioPositionY = lastAudioPositionY;
	component.lastAudioPositionZ = lastAudioPositionZ;
	component.occlusionSettings = std::move(occlusionSettings);
	component.occlusionState = occlusionState;
	AddComponentCommand(entity, VansRuntimeComponentType_Audio, std::move(stableGuid),
		std::move(component), enabled);
}

void VansEntityCommandBuffer::AddAudioReverbZoneComponent(
	VansEntityHandle entity,
	VansRuntimeAudioEnvironmentKind kind,
	std::string stableGuid,
	VansRuntimeAudioReverbZoneComponent reverbZone,
	bool enabled)
{
	AddComponentCommand(entity, RuntimeTypeId(kind), std::move(stableGuid),
		std::move(reverbZone), enabled);
}

void VansEntityCommandBuffer::AddUIComponent(
	VansEntityHandle entity, std::string stableGuid,
	VansRuntimeUIComponent component, bool enabled)
{
	AddComponentCommand(entity, VansRuntimeComponentType_UI, std::move(stableGuid),
		std::move(component), enabled);
}

void VansEntityCommandBuffer::AddScriptComponent(
	VansEntityHandle entity, std::string stableGuid,
	VansRuntimeScriptComponent component, bool enabled)
{
	AddComponentCommand(entity, VansRuntimeComponentType_Script, std::move(stableGuid),
		std::move(component), enabled);
}

void VansEntityCommandBuffer::AddVideoComponent(
	VansEntityHandle entity, std::string stableGuid, std::string assetGuid,
	VansGraphics::VansVideoTexture* videoTexture,
	VansGraphics::VansVideoManager* videoManager,
	int bindlessFirstSlot, bool enabled)
{
	VansRuntimeVideoComponent component;
	component.assetGuid = std::move(assetGuid);
	component.videoTexture = videoTexture;
	component.videoManager = videoManager;
	component.bindlessFirstSlot = bindlessFirstSlot;
	AddComponentCommand(entity, VansRuntimeComponentType_Video, std::move(stableGuid),
		std::move(component), enabled);
}

void VansEntityCommandBuffer::AddParticleComponent(
	VansEntityHandle entity, std::string stableGuid, std::string assetGuid,
	VansGenerationHandle instance, bool playOnAwake,
	bool hasWorldPositionOverride, float worldPositionOverrideX,
	float worldPositionOverrideY, float worldPositionOverrideZ, bool enabled)
{
	VansRuntimeParticleComponent component;
	component.assetGuid = std::move(assetGuid);
	component.instance = instance;
	component.playOnAwake = playOnAwake;
	component.hasWorldPositionOverride = hasWorldPositionOverride;
	component.worldPositionOverrideX = worldPositionOverrideX;
	component.worldPositionOverrideY = worldPositionOverrideY;
	component.worldPositionOverrideZ = worldPositionOverrideZ;
	AddComponentCommand(entity, VansRuntimeComponentType_Particle, std::move(stableGuid),
		std::move(component), enabled);
}

void VansEntityCommandBuffer::AddCameraComponent(
	VansEntityHandle entity, std::string stableGuid,
	VansGraphics::VansCamera* camera, bool enabled)
{
	AddComponentCommand(entity, VansRuntimeComponentType_Camera, std::move(stableGuid),
		VansRuntimeCameraComponent{ camera }, enabled);
}

void VansEntityCommandBuffer::AddLightComponent(
	VansEntityHandle entity, std::string stableGuid,
	VansGraphics::VansLightManager* lightManager,
	int lightIndex, VansRuntimeLightKind kind, bool enabled)
{
	VansRuntimeLightComponent component;
	component.lightManager = lightManager;
	component.lightIndex = lightIndex;
	component.kind = kind;
	AddComponentCommand(entity, RuntimeTypeId(kind), std::move(stableGuid),
		std::move(component), enabled);
}

void VansEntityCommandBuffer::AddTimelineComponent(
	VansEntityHandle entity, std::string stableGuid,
	VansRuntimeTimelineComponent component, bool enabled)
{
	AddComponentCommand(entity, VansRuntimeComponentType_Timeline, std::move(stableGuid),
		std::move(component), enabled);
}

void VansEntityCommandBuffer::AddActionHostComponent(
	VansEntityHandle entity, std::string stableGuid,
	std::shared_ptr<VansActionHost> host, bool enabled)
{
	AddComponentCommand(entity, VansRuntimeComponentType_ActionHost, std::move(stableGuid),
		VansRuntimeActionHostComponent{ std::move(host) }, enabled);
}

void VansEntityCommandBuffer::AddNavigationAgentComponent(
	VansEntityHandle entity, std::string stableGuid,
	VansRuntimeNavigationAgentComponent component, bool enabled)
{
	AddComponentCommand(entity, VansRuntimeComponentType_NavigationAgent, std::move(stableGuid),
		std::move(component), enabled);
}

void VansEntityCommandBuffer::AddAIAgentComponent(
	VansEntityHandle entity, std::string stableGuid,
	VansRuntimeAIAgentComponent component, bool enabled)
{
	AddComponentCommand(entity, VansRuntimeComponentType_AIAgent, std::move(stableGuid),
		std::move(component), enabled);
}

void VansEntityCommandBuffer::SetEntityActive(VansEntityHandle entity, bool active)
{
	m_Commands.emplace_back(
		[entity, active](VansRuntimeWorld& world)
		{ world.SetEntityActive(entity, active); });
}

void VansEntityCommandBuffer::SetEntityName(VansEntityHandle entity, std::string name)
{
	m_Commands.emplace_back(
		[entity, name = std::move(name)](VansRuntimeWorld& world)
		{ world.SetEntityName(entity, name); });
}

void VansEntityCommandBuffer::SetComponentEnabled(VansComponentHandle component, bool enabled)
{
	m_Commands.emplace_back(
		[component, enabled](VansRuntimeWorld& world)
		{ world.SetComponentEnabled(component, enabled); });
}

void VansEntityCommandBuffer::RemoveComponent(VansComponentHandle component)
{
	m_Commands.emplace_back(
		[component](VansRuntimeWorld& world)
		{ world.RemoveComponent(component); });
}

void VansEntityCommandBuffer::SetParent(VansEntityHandle entity, VansEntityHandle parent)
{
	m_Commands.emplace_back(
		[entity, parent](VansRuntimeWorld& world)
		{ world.SetParent(entity, parent); });
}

std::vector<VansEntityCommand> VansEntityCommandBuffer::TakeCommands()
{
	std::vector<VansEntityCommand> commands;
	commands.swap(m_Commands);
	return commands;
}
}
