#pragma once

#include "VansIKSolver.h"

namespace VansGraphics
{
	// Closed-form two-segment solver for limbs. The chain must contain exactly
	// root, mid and tip bones in direct parent order.
	class VansTwoBoneIKSolver final : public VansIKSolver
	{
	public:
		IKSolveResult Solve(
			std::vector<glm::mat4>& localTransforms,
			const std::vector<glm::mat4>& globalTransforms,
			const Skeleton& skeleton,
			const IKChainDefinition& chain,
			const IKTarget& target,
			const IKSolveContext& context) override;
	};
}
