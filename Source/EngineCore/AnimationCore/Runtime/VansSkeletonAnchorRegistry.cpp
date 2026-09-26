#include "../../SceneRuntime/Transform/VansTransform.h"
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

	const VansAnimationController* controller = slot->animationNode->GetController();
	int boneIndex = -1;
	glm::mat4 local(1.0f);
	if (handle.kind == Vans::VansTransformAnchorKind::Bone)
	{
		const auto bone = pose.skeleton->boneGuidToIndex.find(handle.anchorGuid);
		if (bone == pose.skeleton->boneGuidToIndex.end()) return false;
		boneIndex = bone->second;
	}
	else
	{
		const auto* rig = controller ? controller->GetAnimationRig() : nullptr;
		const int socket = rig ? rig->FindSocketByGuid(handle.anchorGuid) : -1;
		if (socket < 0) return false;
		boneIndex = rig->sockets[socket].boneIndex;
		local = rig->sockets[socket].localTransform;
	}
	if (boneIndex < 0 || boneIndex >= static_cast<int>(pose.modelTransforms->size())) return false;
	if (handle.poseCheckpoint.empty()) outModelTransform = (*pose.modelTransforms)[boneIndex];
	else if (!controller || !controller->TryGetPoseCheckpointTransform(handle.poseCheckpoint, boneIndex, outModelTransform))
		return false;
	outModelTransform *= local;
	outPoseRevision = pose.revision;
	return true;
}

VansAnimationNode* VansSkeletonAnchorRegistry::FindAnimationNode(const Vans::VansTransformAnchorHandle& handle) const
{
	const auto* slot = ResolveSlot(handle.instanceId, handle.instanceGeneration);
	return slot ? slot->animationNode : nullptr;
}

void VansSkeletonAnchorRegistry::Clear()
{
	m_Slots.clear();
	m_FreeSlots = {};
}
}
