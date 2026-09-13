#pragma once

#include "VansConstraintMath.h"
#include "../VansPoseWorkspace.h"

namespace VansGraphics
{
	enum class VansAimConstraintMode { LookAtPoint, LookAtDirection, PitchOffset };

	struct VansAimConstraintTarget
	{
		glm::vec3 valueModel{ 0.0f };
		float weight = 1.0f;
		bool valid = false;
	};

	// 保存瞄准目标/修正的连续状态，不缓存或反馈已求解骨骼姿态。
	struct VansAimConstraintState
	{
		glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		bool valid = false;
	};

	struct VansAimConstraintSettings
	{
		VansAimConstraintMode mode = VansAimConstraintMode::LookAtPoint;
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
			const VansAimConstraintTarget& goal,
			float deltaTime,
			VansAimConstraintState& state,
			const VansAimConstraintSettings& settings = {},
			int pivotBoneIndex = -1);
	};
}
