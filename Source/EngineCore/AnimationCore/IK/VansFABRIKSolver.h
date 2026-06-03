#pragma once

// ────────────────────────────────────────────────────────────────────
//  VansFABRIKSolver — FABRIK (Forward And Backward Reaching IK)
//
//  无角度约束的位置投影迭代法。
//  适用于尾巴/触手/绳索等非关节链场景，姿态自然。
// ────────────────────────────────────────────────────────────────────

#include "VansIKSolver.h"

namespace VansGraphics
{
	class VansFABRIKSolver : public VansIKSolver
	{
	public:
		IKSolveResult Solve(
			std::vector<glm::mat4>&       localTransforms,
			const std::vector<glm::mat4>& globalTransforms,
			const Skeleton&               skeleton,
			const IKChainDefinition&      chain,
			const IKTarget&               target,
			float                         deltaTime) override;
	};

}  // namespace VansGraphics
