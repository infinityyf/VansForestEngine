#pragma once

// ────────────────────────────────────────────────────────────────────
//  VansIKConstraint — 关节约束应用工具
//
//  对一个"期望的局部旋转"应用 BallSocket / Hinge / AngleLimit 等约束，
//  返回修正后的局部旋转。所有约束都在骨骼局部空间内运算。
// ────────────────────────────────────────────────────────────────────

#include "VansIKTypes.h"
#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>

namespace VansGraphics
{
	// 把 desiredLocalRot 按约束修正到合法范围；如果约束类型为 None，原样返回。
	glm::quat IK_ApplyJointConstraint(
		const glm::quat& desiredLocalRot,
		const JointConstraint& constraint);

	// Swing-Twist 分解：
	//   将 q 分解为 swing 和 twist，twist 绕 axis 旋转，swing 垂直于 axis。
	//   q == swing * twist
	void IK_DecomposeSwingTwist(
		const glm::quat& q,
		const glm::vec3& axis,
		glm::quat& outSwing,
		glm::quat& outTwist);

	// 把世界空间的 delta 旋转转换到局部空间（基于父全局变换的旋转分量）。
	glm::quat IK_WorldDeltaToLocal(
		const glm::quat& parentGlobalRot,
		const glm::quat& worldDeltaRot);

}  // namespace VansGraphics
