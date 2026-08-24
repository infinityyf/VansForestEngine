#include "VansSkeletonAnchorRegistry.h"

#include "../VansAnimationController.h"
#include "../VansAnimationNode.h"
#include "../Procedural/VansAnimationRig.h"

namespace VansGraphics
{
VansSkeletonInstanceHandle VansSkeletonAnchorRegistry::RegisterInstance(
	VansAnimationNode& animationNode)
{
	std::uint32_t index = 0;
	if (!m_FreeSlots.empty())
	{
		index = m_FreeSlots.front();
		m_FreeSlots.pop();
	}
	else
	{
		index = static_cast<std::uint32_t>(m_Slots.size());
		m_Slots.emplace_back();
	}
	Slot& slot = m_Slots[index];
	slot.animationNode = &animationNode;
	return { static_cast<std::uint64_t>(index) + 1u, slot.generation };
}

bool VansSkeletonAnchorRegistry::UnregisterInstance(VansSkeletonInstanceHandle handle)
{
	if (!handle.IsValid() || handle.id > m_Slots.size())
		return false;
	const std::uint32_t index = static_cast<std::uint32_t>(handle.id - 1u);
	Slot& slot = m_Slots[index];
	if (slot.generation != handle.generation || !slot.animationNode)
		return false;
	slot.animationNode = nullptr;
	if (++slot.generation == 0)
		++slot.generation;
	m_FreeSlots.push(index);
	return true;
}

Vans::VansTransformAnchorHandle VansSkeletonAnchorRegistry::MakeAnchorHandle(
	VansSkeletonInstanceHandle instance,
	Vans::VansTransformAnchorKind kind,
	std::string anchorGuid) const
{
	return { instance.id, instance.generation, kind, std::move(anchorGuid) };
}

const VansSkeletonAnchorRegistry::Slot* VansSkeletonAnchorRegistry::ResolveSlot(
	std::uint64_t id,
	std::uint32_t generation) const
{
	if (id == 0 || id > m_Slots.size())
		return nullptr;
	const Slot& slot = m_Slots[static_cast<std::size_t>(id - 1u)];
	return slot.generation == generation && slot.animationNode ? &slot : nullptr;
}

bool VansSkeletonAnchorRegistry::ResolveModelSpaceTransform(
	const Vans::VansTransformAnchorHandle& handle,
	glm::mat4& outModelTransform,
	std::uint64_t& outPoseRevision) const
{
	const Slot* slot = ResolveSlot(handle.instanceId, handle.instanceGeneration);
	if (!slot)
		return false;
	const VansSkeletonPoseView pose = slot->animationNode->GetFinalPoseView();
	if (!pose.IsValid())
		return false;

	if (handle.kind == Vans::VansTransformAnchorKind::Bone)
	{
		const auto bone = pose.skeleton->boneGuidToIndex.find(handle.anchorGuid);
		if (bone == pose.skeleton->boneGuidToIndex.end())
			return false;
		outModelTransform = (*pose.modelTransforms)[static_cast<std::size_t>(bone->second)];
		outPoseRevision = pose.revision;
		return true;
	}

	const VansAnimationController* controller = slot->animationNode->GetController();
	const VansCompiledAnimationRig* rig = controller ? controller->GetAnimationRig() : nullptr;
	if (!rig)
		return false;
	const int socketIndex = rig->FindSocketByGuid(handle.anchorGuid);
	if (socketIndex < 0 || socketIndex >= static_cast<int>(rig->sockets.size()))
		return false;
	const VansCompiledRigSocket& socket = rig->sockets[static_cast<std::size_t>(socketIndex)];
	if (socket.boneIndex < 0
		|| socket.boneIndex >= static_cast<int>(pose.modelTransforms->size()))
		return false;
	outModelTransform = (*pose.modelTransforms)[static_cast<std::size_t>(socket.boneIndex)]
		* socket.localTransform;
	outPoseRevision = pose.revision;
	return true;
}

void VansSkeletonAnchorRegistry::Clear()
{
	m_Slots.clear();
	m_FreeSlots = {};
}
}
