#pragma once

#include "VansConstraintMath.h"
#include "../VansPoseWorkspace.h"

namespace VansGraphics
{
	struct VansAimConstraintSettings
	{
		glm::vec2 yawLimitDegrees{ -85.0f, 85.0f };
		glm::vec2 pitchLimitDegrees{ -45.0f, 60.0f };
		float maxAngularSpeedDegrees = 540.0f;
		float weight = 1.0f;
	};

	class VansAimConstraintSolver
	{
	public:
		static VansProceduralSolverResult Solve(
			VansPoseWorkspace& workspace,
			const VansCompiledAnimationRig& rig,
			const VansCompiledRigChain& chain,
			const VansProceduralGoal& goal,
			float deltaTime,
			const VansAimConstraintSettings& settings = {});
	};
}
