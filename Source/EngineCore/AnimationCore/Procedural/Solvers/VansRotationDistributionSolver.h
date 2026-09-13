#pragma once

#include "VansConstraintMath.h"
#include "../VansPoseWorkspace.h"

namespace VansGraphics
{
	struct VansRotationDistributionState
	{
		float twistRadians = 0.0f;
		bool valid = false;
	};
	// 可接在任意位置 IK 后；只处理一个骨段的轴向旋转及显式辅助骨。
	class VansRotationDistributionSolver
	{
	public:
		static VansProceduralSolverResult Solve(VansPoseWorkspace& workspace,
			const VansCompiledAnimationRig& rig,
			const VansCompiledRotationDistribution& profile,
			const VansProceduralGoal& goal,
			VansRotationDistributionState& state);
	};
}
