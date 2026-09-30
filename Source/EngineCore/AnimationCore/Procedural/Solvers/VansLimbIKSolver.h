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
		// Blend the fully solved local chain using linear TRS and normalized linear rotation.
		bool linearPoseBlend = false;
		// Optional model-space pole supplied by retargeting. Normal graph IK keeps
		// using the authored chain pole; retargeting can override it from the
		// source pose so the target elbow follows the source bend plane.
		glm::vec3 poleDirectionModel{ 0.0f };
		bool hasPoleDirectionModel = false;
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
