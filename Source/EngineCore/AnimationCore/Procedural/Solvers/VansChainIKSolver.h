#pragma once

#include "VansConstraintMath.h"
#include "../VansPoseWorkspace.h"

namespace VansGraphics
{
	struct VansChainIKSettings
	{
		int maxIterations = 16;
		float positionTolerance = 0.001f;
		float weight = 1.0f;
		bool commitClampedPose = true;
	};

	class VansChainIKSolver
	{
	public:
		static VansProceduralSolverResult Solve(
			VansPoseWorkspace& workspace,
			const VansCompiledAnimationRig& rig,
			const VansCompiledRigChain& chain,
			const VansProceduralGoal& goal,
			const VansChainIKSettings& settings = {});

	private:
		static VansProceduralSolverResult SolveCCD(
			VansPoseWorkspace& workspace,
			const VansCompiledAnimationRig& rig,
			const VansCompiledRigChain& chain,
			const VansProceduralGoal& goal,
			const VansChainIKSettings& settings);
		static VansProceduralSolverResult SolveFABRIK(
			VansPoseWorkspace& workspace,
			const VansCompiledAnimationRig& rig,
			const VansCompiledRigChain& chain,
			const VansProceduralGoal& goal,
			const VansChainIKSettings& settings);
	};
}
