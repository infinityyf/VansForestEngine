#include "VansAnimationTargetResolver.h"
#include "../../AnimationCore/VansPoseMath.h"
#include <unordered_set>

namespace Vans
{
bool VansAnimationTargetResolver::Resolve(const VansTransformGraph& graph, std::uint32_t targetTransform,
	const AnchorResolver& resolveAnchor, VansGraphics::VansResolvedAnimationTarget& target)
{
	target.valid = false;
	target.diagnostic.clear();
	glm::mat4 localChain(1.0f);
	std::unordered_set<std::uint32_t> visited;
	for (std::uint32_t current = targetTransform; current != UINT32_MAX;)
	{
		if (!visited.insert(current).second) { target.diagnostic = "Target transform parent cycle"; return false; }
		VansLocalTransform local;
		if (!graph.TryGetLocalTransform(current, local)) { target.diagnostic = "Target transform is unavailable"; return false; }
		if (glm::any(glm::lessThanEqual(local.scale, glm::vec3(1.0e-6f))))
		{ target.diagnostic = "Target transforms require positive nonsingular scale"; return false; }
		localChain = local.ToMatrix() * localChain;
		const auto* link = graph.GetLink(current);
		if (!link || link->usesAnchor)
		{
			if (link)
			{
				VansAnimationTargetAnchor anchor;
				if (!resolveAnchor || !resolveAnchor(*link, anchor))
				{ target.diagnostic = "Target bone, socket, or pose checkpoint is unavailable"; return false; }
				if (anchor.sameSkeleton)
				{
					target.space = VansGraphics::VansAnimationTargetSpace::Pose;
					target.sourceBoneIndex = anchor.boneIndex;
					target.poseCheckpoint = link->anchor.poseCheckpoint;
					target.sourceLocal = anchor.boneToAnchor * localChain;
					target.valid = true;
					return true;
				}
				localChain = anchor.world * localChain;
			}
			VansGraphics::VansBoneTransform world;
			if (!VansGraphics::VansPoseMath::TryDecompose(localChain, world))
			{ target.diagnostic = "Target world transform cannot be decomposed"; return false; }
			target.space = VansGraphics::VansAnimationTargetSpace::World;
			target.positionWorld = world.translation;
			target.rotationWorld = world.rotation;
			target.valid = true;
			return true;
		}
		current = link->parentTransformId;
	}
	target.diagnostic = "Target transform is missing";
	return false;
}
}
