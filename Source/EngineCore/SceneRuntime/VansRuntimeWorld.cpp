#include "VansRuntimeWorld.h"

#include "../RuntimeCore/VansFramePhase.h"
#include "../RuntimeCore/VansThreadContract.h"

#include <cassert>

namespace Vans
{
bool VansRuntimeComponentTypeMatches(
	std::uint16_t typeId,
	const std::type_info& valueType)
{
	switch (typeId)
	{
	case VansRuntimeComponentType_Render: return valueType == typeid(VansRuntimeRenderComponent);
	case VansRuntimeComponentType_Physics: return valueType == typeid(VansRuntimePhysicsComponent);
	case VansRuntimeComponentType_Cloth: return valueType == typeid(VansRuntimeClothComponent);
	case VansRuntimeComponentType_CharacterController:
		return valueType == typeid(VansRuntimeCharacterControllerComponent);
	case VansRuntimeComponentType_DirectionalLight:
	case VansRuntimeComponentType_PointLight:
	case VansRuntimeComponentType_SpotLight:
	case VansRuntimeComponentType_RectLight:
		return valueType == typeid(VansRuntimeLightComponent);
	case VansRuntimeComponentType_Camera: return valueType == typeid(VansRuntimeCameraComponent);
	case VansRuntimeComponentType_Audio: return valueType == typeid(VansRuntimeAudioComponent);
	case VansRuntimeComponentType_AudioReverbZone:
	case VansRuntimeComponentType_AudioVolume:
		return valueType == typeid(VansRuntimeAudioReverbZoneComponent);
	case VansRuntimeComponentType_Video: return valueType == typeid(VansRuntimeVideoComponent);
	case VansRuntimeComponentType_Particle: return valueType == typeid(VansRuntimeParticleComponent);
	case VansRuntimeComponentType_Animation: return valueType == typeid(VansRuntimeAnimationComponent);
	case VansRuntimeComponentType_Ragdoll: return valueType == typeid(VansRuntimeRagdollComponent);
	case VansRuntimeComponentType_Vehicle: return valueType == typeid(VansRuntimeVehicleComponent);
	case VansRuntimeComponentType_UI: return valueType == typeid(VansRuntimeUIComponent);
	case VansRuntimeComponentType_Script: return valueType == typeid(VansRuntimeScriptComponent);
	case VansRuntimeComponentType_Transform: return valueType == typeid(VansRuntimeTransformComponent);
	case VansRuntimeComponentType_Timeline: return valueType == typeid(VansRuntimeTimelineComponent);
	case VansRuntimeComponentType_ActionHost: return valueType == typeid(VansRuntimeActionHostComponent);
	case VansRuntimeComponentType_NavigationAgent:
		return valueType == typeid(VansRuntimeNavigationAgentComponent);
	case VansRuntimeComponentType_AIAgent: return valueType == typeid(VansRuntimeAIAgentComponent);
	default: return true;
	}
}

VansEntityHandle VansRuntimeWorld::CreateEntity(const VansEntityCreateDesc& desc)
{
	const VansEntityHandle entity = m_Entities.CreateEntity(desc);
	RequestComponentEffectiveEnabledRecompute();
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
		RequestComponentEffectiveEnabledRecompute();
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
		RequestComponentEffectiveEnabledRecompute();
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

VansComponentHandle VansRuntimeWorld::FindComponentOwnedBy(VansEntityHandle entity, std::uint16_t typeId) const
{
	const IVansComponentStorage* storage = m_Entities.IsAlive(entity) ? FindStorage(typeId) : nullptr;
	return storage ? storage->FindFirstOwnedBy(entity) : VansComponentHandle{};
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
		RequestComponentEffectiveEnabledRecompute();
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

VansRuntimeCommandCommitResult VansRuntimeWorld::CommitCommands(
	VansRuntimeCommandCommitPoint commitPoint)
{
	VANS_ASSERT_MAIN_THREAD();
	VANS_ASSERT_FRAME_PHASE(VansFramePhase::GameLogic);
	assert(!m_CommandCommitInProgress);
	if (m_CommandCommitInProgress)
		return {};

	(void)commitPoint;
	std::vector<VansEntityCommand> commands = m_Commands.TakeCommands();
	VansRuntimeCommandCommitResult result;
	result.consumedCommandCount = commands.size();
	m_CommandCommitInProgress = true;
	m_ComponentEffectiveEnabledDirty = false;
	for (VansEntityCommand& command : commands)
		command(*this);
	m_CommandCommitInProgress = false;
	result.hierarchyActivityChanged = m_ComponentEffectiveEnabledDirty;
	if (m_ComponentEffectiveEnabledDirty)
	{
		m_ComponentEffectiveEnabledDirty = false;
		RecomputeComponentEffectiveEnabled();
	}
	return result;
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

void VansRuntimeWorld::RequestComponentEffectiveEnabledRecompute()
{
	if (m_CommandCommitInProgress)
	{
		m_ComponentEffectiveEnabledDirty = true;
		return;
	}
	RecomputeComponentEffectiveEnabled();
}

void VansRuntimeWorld::Clear()
{
	m_Commands.Clear();
	m_CommandCommitInProgress = false;
	m_ComponentEffectiveEnabledDirty = false;
	for (auto& entry : m_ComponentStorages)
		entry.second->Clear();
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
