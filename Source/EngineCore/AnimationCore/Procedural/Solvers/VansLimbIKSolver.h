#pragma once

#include "VansConstraintMath.h"
#include "../VansPoseWorkspace.h"

namespace VansGraphics
{
	enum class VansLimbTipRotationMode { PreserveInput, MatchGoal, FollowChain };

	struct VansLimbIKSettings
	{
		VansLimbTipRotationMode tipRotationMode = VansLimbTipRotationMode::MatchGoal;
		float positionTolerance = 0.001f;
		float weight = 1.0f;
		bool commitClampedPose = true;
	};

	class VansLimbIKSolver
	{
	public:
		static VansProceduralSolverResult Solve(
			VansPoseWorkspace& workspace,
			const VansCompiledAnimationRig& rig,
			const VansCompiledRigChain& chain,
			const VansProceduralGoal& goal,
			const VansLimbIKSettings& settings = {});
	};
}
