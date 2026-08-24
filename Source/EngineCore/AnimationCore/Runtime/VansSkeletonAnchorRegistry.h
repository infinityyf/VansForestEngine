#pragma once

#include "../../SceneRuntime/Transform/VansTransformGraph.h"

#include <cstdint>
#include <queue>
#include <vector>

namespace VansGraphics
{
class VansAnimationNode;

struct VansSkeletonInstanceHandle
{
	std::uint64_t id = 0;
	std::uint32_t generation = 0;

	bool IsValid() const { return id != 0 && generation != 0; }
};

class VansSkeletonAnchorRegistry final : public Vans::IVansTransformAnchorProvider
{
public:
	VansSkeletonInstanceHandle RegisterInstance(VansAnimationNode& animationNode);
	bool UnregisterInstance(VansSkeletonInstanceHandle handle);
	Vans::VansTransformAnchorHandle MakeAnchorHandle(
		VansSkeletonInstanceHandle instance,
		Vans::VansTransformAnchorKind kind,
		std::string anchorGuid) const;

	bool ResolveModelSpaceTransform(
		const Vans::VansTransformAnchorHandle& handle,
		glm::mat4& outModelTransform,
		std::uint64_t& outPoseRevision) const override;
	void Clear();

private:
	struct Slot
	{
		VansAnimationNode* animationNode = nullptr;
		std::uint32_t generation = 1;
	};

	const Slot* ResolveSlot(std::uint64_t id, std::uint32_t generation) const;
	std::vector<Slot> m_Slots;
	std::queue<std::uint32_t> m_FreeSlots;
};
}
