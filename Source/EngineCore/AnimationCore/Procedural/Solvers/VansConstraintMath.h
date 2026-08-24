#pragma once

#include "../VansAnimationRig.h"

namespace VansGraphics
{
	struct VansConstraintResult
	{
		glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		bool valid = false;
		bool limited = false;
	};

	VansConstraintResult VansApplyJointLimit(
		const glm::quat& desiredLocalRotation,
		const VansCompiledRigJointLimit* limit);
	glm::quat VansShortestArc(const glm::vec3& from, const glm::vec3& to);
	float VansQuaternionAngleDegrees(const glm::quat& rotation);
}
