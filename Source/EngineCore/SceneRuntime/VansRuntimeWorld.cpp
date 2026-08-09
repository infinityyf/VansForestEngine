#include "VansRuntimeWorld.h"

namespace Vans
{
VansEntityHandle VansRuntimeWorld::CreateEntity(const VansEntityCreateDesc& desc)
{
	const VansEntityHandle entity = m_Entities.CreateEntity(desc);
	RecomputeComponentEffectiveEnabled();
	return entity;
}

bool VansRuntimeWorld::DestroyEntity(VansEntityHandle entity, VansDestroyChildrenPolicy childrenPolicy)
{
	std::vector<VansEntityHandle> destroyedEntities;
	CollectEntitiesForDestroy(entity, childrenPolicy, destroyedEntities);
	const bool destroyed = m_Entities.DestroyEntity(entity, childrenPolicy);
	if (destroyed)
	{
		for (VansEntityHandle destroyedEntity : destroyedEntities)
			RemoveComponentsOwnedBy(destroyedEntity);
		RecomputeComponentEffectiveEnabled();
	}
	return destroyed;
}

bool VansRuntimeWorld::SetEntityName(VansEntityHandle entity, const std::string& name)
{
	return m_Entities.SetName(entity, name);
}

bool VansRuntimeWorld::SetEntityActive(VansEntityHandle entity, bool active)
{
	const bool changed = m_Entities.SetSelfActive(entity, active);
	if (changed)
		RecomputeComponentEffectiveEnabled();
	return changed;
}

bool VansRuntimeWorld::SetComponentEnabled(VansComponentHandle component, bool enabled)
{
	IVansComponentStorage* storage = FindStorage(component.typeId);
	if (!storage)
		return false;
	return storage->SetEnabled(
		component,
		enabled,
		[this](VansEntityHandle entity)
		{
			return m_Entities.IsHierarchyActive(entity);
		});
}

bool VansRuntimeWorld::RemoveComponent(VansComponentHandle component)
{
	IVansComponentStorage* storage = FindStorage(component.typeId);
	return storage ? storage->Remove(component) : false;
}

VansComponentHandle VansRuntimeWorld::FindComponentByGuid(
	const std::string& componentGuid,
	std::uint16_t typeId) const
{
	if (componentGuid.empty())
		return VansComponentHandle{};
	if (typeId != VansInvalidComponentTypeId)
	{
		const IVansComponentStorage* storage = FindStorage(typeId);
		return storage ? storage->FindByStableGuid(componentGuid) : VansComponentHandle{};
	}
	for (const auto& entry : m_ComponentStorages)
	{
		VansComponentHandle handle = entry.second->FindByStableGuid(componentGuid);
		if (handle.IsValid())
			return handle;
	}
	return VansComponentHandle{};
}

const VansComponentHeader* VansRuntimeWorld::GetComponentHeader(VansComponentHandle component) const
{
	const IVansComponentStorage* storage = FindStorage(component.typeId);
	return storage ? storage->GetHeaderUntyped(component) : nullptr;
}

bool VansRuntimeWorld::IsComponentSelfEnabled(VansComponentHandle component) const
{
	const VansComponentHeader* header = GetComponentHeader(component);
	return header ? header->selfEnabled : false;
}

bool VansRuntimeWorld::IsComponentEffectivelyEnabled(VansComponentHandle component) const
{
	const VansComponentHeader* header = GetComponentHeader(component);
	return header ? header->effectiveEnabled : false;
}

std::vector<VansComponentHandle> VansRuntimeWorld::CollectComponentsOwnedBy(VansEntityHandle entity) const
{
	std::vector<VansComponentHandle> components;
	if (!m_Entities.IsAlive(entity))
		return components;
	for (const auto& entry : m_ComponentStorages)
		entry.second->CollectOwnedBy(entity, components);
	return components;
}

std::vector<VansComponentHandle> VansRuntimeWorld::CollectComponentsInSubtree(VansEntityHandle entity) const
{
	std::vector<VansComponentHandle> components;
	if (!m_Entities.IsAlive(entity))
		return components;

	std::vector<VansEntityHandle> stack;
	stack.push_back(entity);
	while (!stack.empty())
	{
		const VansEntityHandle current = stack.back();
		stack.pop_back();
		for (const auto& entry : m_ComponentStorages)
			entry.second->CollectOwnedBy(current, components);

		const VansEntityRecord* record = m_Entities.Get(current);
		if (!record)
			continue;
		for (VansEntityHandle child : record->children)
			stack.push_back(child);
	}
	return components;
}

bool VansRuntimeWorld::SetParent(VansEntityHandle entity, VansEntityHandle parent)
{
	const bool changed = m_Entities.SetParent(entity, parent);
	if (changed)
		RecomputeComponentEffectiveEnabled();
	return changed;
}

IVansComponentStorage* VansRuntimeWorld::FindStorage(std::uint16_t typeId)
{
	const auto it = m_ComponentStorages.find(typeId);
	return it != m_ComponentStorages.end() ? it->second.get() : nullptr;
}

const IVansComponentStorage* VansRuntimeWorld::FindStorage(std::uint16_t typeId) const
{
	const auto it = m_ComponentStorages.find(typeId);
	return it != m_ComponentStorages.end() ? it->second.get() : nullptr;
}

void VansRuntimeWorld::FlushCommands()
{
	for (const VansEntityCommand& command : m_Commands.TakeCommands())
	{
		switch (command.type)
		{
		case VansEntityCommandType::CreateEntity:
			CreateEntity(command.createDesc);
			break;
		case VansEntityCommandType::DestroyEntity:
			DestroyEntity(command.entity, command.destroyChildrenPolicy);
			break;
		case VansEntityCommandType::AddTransformComponent:
			AddComponent(
				command.entity,
				VansRuntimeComponentType_Transform,
				command.transformComponent,
				command.componentStableGuid,
				command.boolValue);
			break;
		case VansEntityCommandType::AddRenderComponent:
			AddComponent(
				command.entity,
				VansRuntimeComponentType_Render,
				command.renderComponent,
				command.componentStableGuid,
				command.boolValue);
			break;
		case VansEntityCommandType::AddPhysicsComponent:
			AddComponent(
				command.entity,
				VansRuntimeComponentType_Physics,
				command.physicsComponent,
				command.componentStableGuid,
				command.boolValue);
			break;
		case VansEntityCommandType::AddClothComponent:
			AddComponent(
				command.entity,
				VansRuntimeComponentType_Cloth,
				command.clothComponent,
				command.componentStableGuid,
				command.boolValue);
			break;
		case VansEntityCommandType::AddCharacterControllerComponent:
			AddComponent(
				command.entity,
				VansRuntimeComponentType_CharacterController,
				command.characterControllerComponent,
				command.componentStableGuid,
				command.boolValue);
			break;
		case VansEntityCommandType::AddVehicleComponent:
			AddComponent(
				command.entity,
				VansRuntimeComponentType_Vehicle,
				command.vehicleComponent,
				command.componentStableGuid,
				command.boolValue);
			break;
		case VansEntityCommandType::AddAnimationComponent:
			AddComponent(
				command.entity,
				VansRuntimeComponentType_Animation,
				command.animationComponent,
				command.componentStableGuid,
				command.boolValue);
			break;
		case VansEntityCommandType::AddRagdollComponent:
			AddComponent(
				command.entity,
				VansRuntimeComponentType_Ragdoll,
				command.ragdollComponent,
				command.componentStableGuid,
				command.boolValue);
			break;
		case VansEntityCommandType::AddAudioComponent:
			AddComponent(
				command.entity,
				VansRuntimeComponentType_Audio,
				command.audioComponent,
				command.componentStableGuid,
				command.boolValue);
			break;
		case VansEntityCommandType::AddAudioReverbZoneComponent:
			AddComponent(
				command.entity,
				command.componentTypeId,
				command.audioReverbZoneComponent,
				command.componentStableGuid,
				command.boolValue);
			break;
		case VansEntityCommandType::AddUIComponent:
			AddComponent(
				command.entity,
				VansRuntimeComponentType_UI,
				command.uiComponent,
				command.componentStableGuid,
				command.boolValue);
			break;
		case VansEntityCommandType::AddScriptComponent:
			AddComponent(
				command.entity,
				VansRuntimeComponentType_Script,
				command.scriptComponent,
				command.componentStableGuid,
				command.boolValue);
			break;
		case VansEntityCommandType::AddVideoComponent:
			AddComponent(
				command.entity,
				VansRuntimeComponentType_Video,
				command.videoComponent,
				command.componentStableGuid,
				command.boolValue);
			break;
		case VansEntityCommandType::AddParticleComponent:
			AddComponent(
				command.entity,
				VansRuntimeComponentType_Particle,
				command.particleComponent,
				command.componentStableGuid,
				command.boolValue);
			break;
		case VansEntityCommandType::AddCameraComponent:
			AddComponent(
				command.entity,
				VansRuntimeComponentType_Camera,
				command.cameraComponent,
				command.componentStableGuid,
				command.boolValue);
			break;
		case VansEntityCommandType::AddLightComponent:
			AddComponent(
				command.entity,
				command.componentTypeId,
				command.lightComponent,
				command.componentStableGuid,
				command.boolValue);
			break;
		case VansEntityCommandType::AddTimelineComponent:
			AddComponent(
				command.entity,
				VansRuntimeComponentType_Timeline,
				command.timelineComponent,
				command.componentStableGuid,
				command.boolValue);
			break;
		case VansEntityCommandType::SetEntityActive:
			SetEntityActive(command.entity, command.boolValue);
			break;
		case VansEntityCommandType::SetEntityName:
			SetEntityName(command.entity, command.stringValue);
			break;
		case VansEntityCommandType::SetComponentEnabled:
			SetComponentEnabled(command.component, command.boolValue);
			break;
		case VansEntityCommandType::RemoveComponent:
			RemoveComponent(command.component);
			break;
		case VansEntityCommandType::SetParent:
			SetParent(command.entity, command.parent);
			break;
		default:
			break;
		}
	}
}

void VansRuntimeWorld::RecomputeComponentEffectiveEnabled()
{
	for (auto& entry : m_ComponentStorages)
	{
		entry.second->RecomputeEffectiveEnabled(
			[this](VansEntityHandle entity)
			{
				return m_Entities.IsHierarchyActive(entity);
			});
	}
}

void VansRuntimeWorld::Clear()
{
	m_Commands.Clear();
	m_ComponentStorages.clear();
	m_Entities.Clear();
}

void VansRuntimeWorld::CollectEntitiesForDestroy(
	VansEntityHandle entity,
	VansDestroyChildrenPolicy childrenPolicy,
	std::vector<VansEntityHandle>& outEntities) const
{
	const VansEntityRecord* record = m_Entities.Get(entity);
	if (!record)
		return;
	outEntities.push_back(entity);
	if (childrenPolicy != VansDestroyChildrenPolicy::DestroyChildren)
		return;
	const std::vector<VansEntityHandle> children = record->children;
	for (VansEntityHandle child : children)
		CollectEntitiesForDestroy(child, childrenPolicy, outEntities);
}

void VansRuntimeWorld::RemoveComponentsOwnedBy(VansEntityHandle entity)
{
	for (auto& entry : m_ComponentStorages)
		entry.second->RemoveOwnedBy(entity);
}
}
