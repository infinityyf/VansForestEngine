#pragma once

#include "../Transform/VansTransformGraph.h"
#include "../../AnimationCore/Procedural/VansProceduralTypes.h"
#include <functional>

namespace Vans
{
struct VansAnimationTargetAnchor
{
	bool sameSkeleton = false;
	int boneIndex = -1;
	glm::mat4 boneToAnchor{ 1.0f };
	glm::mat4 world{ 1.0f };
};

// 仅解析目标父链。场景提供骨架身份与当前姿态，求解器不接触实体存储。
class VansAnimationTargetResolver
{
public:
	using AnchorResolver = std::function<bool(const VansTransformGraphLink&, VansAnimationTargetAnchor&)>;
	static bool Resolve(const VansTransformGraph& graph, std::uint32_t targetTransform,
		const AnchorResolver& resolveAnchor, VansGraphics::VansResolvedAnimationTarget& target);
};
}
