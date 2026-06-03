#include "VansIKConstraint.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace VansGraphics
{
	// ─── Swing-Twist 分解 ──────────────────────────────────────────────
	// 参考：https://stackoverflow.com/questions/3684269/component-of-a-quaternion-rotation-around-an-axis
	void IK_DecomposeSwingTwist(
		const glm::quat& q,
		const glm::vec3& axisIn,
		glm::quat& outSwing,
		glm::quat& outTwist)
	{
		glm::vec3 axis = glm::length(axisIn) > 1e-6f ? glm::normalize(axisIn) : glm::vec3(0, 1, 0);

		glm::vec3 r(q.x, q.y, q.z);                  // 四元数虚部
		glm::vec3 p = glm::dot(r, axis) * axis;      // r 在 axis 上的投影

		glm::quat twist(q.w, p.x, p.y, p.z);
		float lenSq = twist.w * twist.w + p.x * p.x + p.y * p.y + p.z * p.z;
		if (lenSq < 1e-12f)
		{
			outTwist = glm::quat(1, 0, 0, 0);
		}
		else
		{
			float invLen = 1.0f / std::sqrt(lenSq);
			twist.w *= invLen;
			twist.x *= invLen;
			twist.y *= invLen;
			twist.z *= invLen;
			outTwist = twist;
		}

		outSwing = glm::normalize(q * glm::conjugate(outTwist));
	}

	glm::quat IK_WorldDeltaToLocal(
		const glm::quat& parentGlobalRot,
		const glm::quat& worldDeltaRot)
	{
		glm::quat invP = glm::conjugate(glm::normalize(parentGlobalRot));
		return glm::normalize(invP * worldDeltaRot * parentGlobalRot);
	}

	// ─── 角度钳制工具 ────────────────────────────────────────────────
	static float ClampAngleDeg(float a, float minDeg, float maxDeg, float stiffness)
	{
		float clamped = std::min(std::max(a, minDeg), maxDeg);
		return clamped * stiffness + a * (1.0f - stiffness);
	}

	// 提取四元数的旋转角度（带符号），相对给定参考轴
	static float QuatAngleAroundAxis(const glm::quat& q, const glm::vec3& axisIn)
	{
		glm::quat swing, twist;
		IK_DecomposeSwingTwist(q, axisIn, swing, twist);

		// twist.w = cos(angle/2)，twist.xyz 沿 axis 方向（带符号）
		glm::vec3 axis = glm::length(axisIn) > 1e-6f ? glm::normalize(axisIn) : glm::vec3(0, 1, 0);
		float w = std::min(std::max(twist.w, -1.0f), 1.0f);
		float angle = 2.0f * std::acos(w);  // 0..PI
		// 用 twist 的虚部与 axis 同向/反向决定符号
		glm::vec3 v(twist.x, twist.y, twist.z);
		if (glm::dot(v, axis) < 0.0f)
			angle = -angle;
		return angle;
	}

	// ─── BallSocket: 锥形角度限制 ────────────────────────────────────
	static glm::quat ApplyBallSocket(
		const glm::quat& desired,
		const JointConstraint& c)
	{
		// 计算相对 rest pose 的偏移
		glm::quat invRest = glm::conjugate(glm::normalize(c.restRotation));
		glm::quat delta   = glm::normalize(desired * invRest);

		// swing-twist 分解：twist 绕主弯曲轴
		glm::quat swing, twist;
		IK_DecomposeSwingTwist(delta, c.localYAxis, swing, twist);

		// 限制 swing 的角度（锥形限制）
		float w = std::min(std::max(swing.w, -1.0f), 1.0f);
		float swingAngle = 2.0f * std::acos(std::abs(w));   // 取绝对值得到 0..PI 的偏角
		float swingAngleDeg = glm::degrees(swingAngle);

		if (swingAngleDeg > c.coneAngleDeg && swingAngleDeg > 1e-4f)
		{
			float scaled = c.coneAngleDeg * c.stiffness + swingAngleDeg * (1.0f - c.stiffness);
			float ratio = scaled / swingAngleDeg;
			// 用 slerp 把 swing 拉回锥形内
			swing = glm::slerp(glm::quat(1, 0, 0, 0), swing, ratio);
		}

		// 限制 twist
		float twistAngle = QuatAngleAroundAxis(twist, c.localYAxis);
		float twistDeg   = glm::degrees(twistAngle);
		float clampedTwistDeg = ClampAngleDeg(twistDeg, c.minAngleX, c.maxAngleX, c.stiffness);
		twist = glm::angleAxis(glm::radians(clampedTwistDeg), glm::normalize(c.localYAxis));

		glm::quat clampedDelta = glm::normalize(swing * twist);
		return glm::normalize(clampedDelta * c.restRotation);
	}

	// ─── Hinge: 单轴铰链 ────────────────────────────────────────────
	static glm::quat ApplyHinge(
		const glm::quat& desired,
		const JointConstraint& c)
	{
		glm::quat invRest = glm::conjugate(glm::normalize(c.restRotation));
		glm::quat delta   = glm::normalize(desired * invRest);

		// 把 delta 中绕铰链轴的分量提取出来，丢弃其余分量
		glm::vec3 axis = glm::length(c.localYAxis) > 1e-6f ? glm::normalize(c.localYAxis) : glm::vec3(0, 1, 0);
		glm::quat swing, twist;
		IK_DecomposeSwingTwist(delta, axis, swing, twist);

		float angle = QuatAngleAroundAxis(twist, axis);
		float angleDeg = glm::degrees(angle);
		float clampedDeg = ClampAngleDeg(angleDeg, c.minAngleY, c.maxAngleY, c.stiffness);

		glm::quat hinge = glm::angleAxis(glm::radians(clampedDeg), axis);
		return glm::normalize(hinge * c.restRotation);
	}

	// ─── AngleLimit: 三轴独立角度限制 ───────────────────────────────
	static glm::quat ApplyAngleLimit(
		const glm::quat& desired,
		const JointConstraint& c)
	{
		// 把 delta 转为绕 X/Y/Z 三轴的连续旋转（近似 Euler）
		glm::quat invRest = glm::conjugate(glm::normalize(c.restRotation));
		glm::quat delta   = glm::normalize(desired * invRest);

		glm::vec3 e = glm::eulerAngles(delta);   // pitch/yaw/roll(rad)
		float xDeg = glm::degrees(e.x);
		float yDeg = glm::degrees(e.y);
		float zDeg = glm::degrees(e.z);

		xDeg = ClampAngleDeg(xDeg, c.minAngleX, c.maxAngleX, c.stiffness);
		yDeg = ClampAngleDeg(yDeg, c.minAngleY, c.maxAngleY, c.stiffness);
		zDeg = ClampAngleDeg(zDeg, c.minAngleZ, c.maxAngleZ, c.stiffness);

		glm::quat clamped = glm::quat(glm::vec3(
			glm::radians(xDeg), glm::radians(yDeg), glm::radians(zDeg)));
		return glm::normalize(clamped * c.restRotation);
	}

	// ─── 主入口 ────────────────────────────────────────────────────
	glm::quat IK_ApplyJointConstraint(
		const glm::quat& desiredLocalRot,
		const JointConstraint& constraint)
	{
		switch (constraint.type)
		{
		case JointConstraintType::None:
			return desiredLocalRot;
		case JointConstraintType::Locked:
			return constraint.restRotation;
		case JointConstraintType::BallSocket:
			return ApplyBallSocket(desiredLocalRot, constraint);
		case JointConstraintType::Hinge:
			return ApplyHinge(desiredLocalRot, constraint);
		case JointConstraintType::AngleLimit:
			return ApplyAngleLimit(desiredLocalRot, constraint);
		case JointConstraintType::TwistLimit:
		{
			// 仅限制扭转轴，其余分量保留
			glm::quat invRest = glm::conjugate(glm::normalize(constraint.restRotation));
			glm::quat delta   = glm::normalize(desiredLocalRot * invRest);
			glm::quat swing, twist;
			IK_DecomposeSwingTwist(delta, constraint.localXAxis, swing, twist);
			float angle = QuatAngleAroundAxis(twist, constraint.localXAxis);
			float clampedDeg = ClampAngleDeg(
				glm::degrees(angle),
				constraint.minAngleX, constraint.maxAngleX,
				constraint.stiffness);
			glm::quat newTwist = glm::angleAxis(
				glm::radians(clampedDeg),
				glm::normalize(constraint.localXAxis));
			return glm::normalize(swing * newTwist * constraint.restRotation);
		}
		}
		return desiredLocalRot;
	}

}  // namespace VansGraphics
