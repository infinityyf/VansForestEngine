#include "VansConstraintMath.h"

#include <algorithm>
#include <cmath>

namespace VansGraphics
{
	namespace
	{
		constexpr float kEpsilon = 1.0e-7f;
		constexpr float kPi = 3.14159265358979323846f;

		bool Finite(const glm::quat& value)
		{
			return std::isfinite(value.w) && std::isfinite(value.x)
				&& std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool Finite(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool Finite(const glm::vec2& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y);
		}

		glm::quat NormalizeHemisphere(glm::quat value)
		{
			value = glm::normalize(value);
			return value.w < 0.0f ? -value : value;
		}

		void DecomposeSwingTwist(const glm::quat& rotation,
		                         const glm::vec3& axis,
		                         glm::quat& swing,
		                         glm::quat& twist)
		{
			const glm::quat q = NormalizeHemisphere(rotation);
			const glm::vec3 imaginary(q.x, q.y, q.z);
			const glm::vec3 projected = axis * glm::dot(imaginary, axis);
			twist = glm::quat(q.w, projected.x, projected.y, projected.z);
			if (glm::dot(twist, twist) <= kEpsilon)
				twist = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			else
				twist = NormalizeHemisphere(twist);
			swing = NormalizeHemisphere(q * glm::inverse(twist));
		}

		float SignedTwistRadians(const glm::quat& twist, const glm::vec3& axis)
		{
			const glm::quat q = NormalizeHemisphere(twist);
			const float signedSinHalf = glm::dot(glm::vec3(q.x, q.y, q.z), axis);
			float angle = 2.0f * std::atan2(signedSinHalf, q.w);
			while (angle > kPi) angle -= 2.0f * kPi;
			while (angle < -kPi) angle += 2.0f * kPi;
			return angle;
		}
	}

	VansConstraintResult VansApplyJointLimit(
		const glm::quat& desiredLocalRotation,
		const VansCompiledRigJointLimit* limit)
	{
		VansConstraintResult result;
		if (!Finite(desiredLocalRotation) || glm::dot(desiredLocalRotation, desiredLocalRotation) <= kEpsilon)
			return result;
		result.rotation = glm::normalize(desiredLocalRotation);
		result.valid = true;
		if (!limit)
			return result;
		if ((limit->kind != VansJointLimitKind::Hinge
				&& limit->kind != VansJointLimitKind::SwingTwist
				&& limit->kind != VansJointLimitKind::Locked)
			|| !Finite(limit->restLocalRotation)
			|| glm::dot(limit->restLocalRotation, limit->restLocalRotation) <= kEpsilon)
		{
			result.valid = false;
			return result;
		}
		const glm::quat rest = glm::normalize(limit->restLocalRotation);
		if (limit->kind == VansJointLimitKind::Locked)
		{
			result.limited = VansQuaternionAngleDegrees(glm::inverse(rest) * result.rotation) > 1.0e-4f;
			result.rotation = rest;
			return result;
		}
		if (!Finite(limit->axisLocal)
			|| glm::dot(limit->axisLocal, limit->axisLocal) <= kEpsilon
			|| !std::isfinite(limit->minDegrees) || !std::isfinite(limit->maxDegrees)
			|| limit->minDegrees < -180.0f || limit->maxDegrees > 180.0f
			|| limit->minDegrees > limit->maxDegrees
			|| (limit->kind == VansJointLimitKind::SwingTwist
				&& (!Finite(limit->swingReferenceAxisLocal)
					|| glm::dot(limit->swingReferenceAxisLocal,
						limit->swingReferenceAxisLocal) <= kEpsilon
					|| std::abs(glm::dot(glm::normalize(limit->axisLocal),
						glm::normalize(limit->swingReferenceAxisLocal))) > 0.05f
					|| !Finite(limit->swingLimitDegrees)
					|| limit->swingLimitDegrees.x < 0.0f || limit->swingLimitDegrees.x > 180.0f
					|| limit->swingLimitDegrees.y < 0.0f || limit->swingLimitDegrees.y > 180.0f)))
		{
			result.valid = false;
			return result;
		}
		const glm::vec3 axis = glm::normalize(limit->axisLocal);

		const glm::quat relative = NormalizeHemisphere(glm::inverse(rest) * result.rotation);
		glm::quat swing;
		glm::quat twist;
		DecomposeSwingTwist(relative, axis, swing, twist);
		const float requestedTwist = glm::degrees(SignedTwistRadians(twist, axis));
		const float clampedTwist = std::clamp(requestedTwist, limit->minDegrees, limit->maxDegrees);
		glm::quat limitedSwing(1.0f, 0.0f, 0.0f, 0.0f);
		if (limit->kind == VansJointLimitKind::SwingTwist)
		{
			const float swingAngle = VansQuaternionAngleDegrees(swing);
			if (swingAngle > kEpsilon)
			{
				const glm::quat normalizedSwing = NormalizeHemisphere(swing);
				glm::vec3 swingAxis(normalizedSwing.x, normalizedSwing.y, normalizedSwing.z);
				swingAxis = glm::normalize(swingAxis);
				const glm::vec3 firstAxis = glm::normalize(limit->swingReferenceAxisLocal);
				const glm::vec3 secondAxis = glm::normalize(glm::cross(axis, firstAxis));
				float first = swingAngle * glm::dot(swingAxis, firstAxis);
				float second = swingAngle * glm::dot(swingAxis, secondAxis);
				const auto constrainZeroAxis = [&](float& component, float allowed)
				{
					if (allowed <= kEpsilon && std::abs(component) > kEpsilon)
					{
						component = 0.0f;
						result.limited = true;
					}
				};
				constrainZeroAxis(first, limit->swingLimitDegrees.x);
				constrainZeroAxis(second, limit->swingLimitDegrees.y);
				const float firstTerm = limit->swingLimitDegrees.x > kEpsilon
					? first / limit->swingLimitDegrees.x : 0.0f;
				const float secondTerm = limit->swingLimitDegrees.y > kEpsilon
					? second / limit->swingLimitDegrees.y : 0.0f;
				const float ellipseRadius = std::sqrt(
					firstTerm * firstTerm + secondTerm * secondTerm);
				if (ellipseRadius > 1.0f)
				{
					first /= ellipseRadius;
					second /= ellipseRadius;
					result.limited = true;
				}
				const glm::vec3 limitedVector = firstAxis * first + secondAxis * second;
				const float limitedAngle = glm::length(limitedVector);
				if (limitedAngle > kEpsilon)
					limitedSwing = glm::angleAxis(
						glm::radians(limitedAngle), limitedVector / limitedAngle);
			}
		}
		result.limited = result.limited || std::abs(clampedTwist - requestedTwist) > 1.0e-4f
			|| limit->kind == VansJointLimitKind::Hinge
				&& VansQuaternionAngleDegrees(swing) > 1.0e-4f;
		const glm::quat limitedTwist = glm::angleAxis(glm::radians(clampedTwist), axis);
		result.rotation = glm::normalize(rest * limitedSwing * limitedTwist);
		result.valid = Finite(result.rotation);
		return result;
	}

	glm::quat VansShortestArc(const glm::vec3& from, const glm::vec3& to)
	{
		const float fromLength = glm::length(from);
		const float toLength = glm::length(to);
		if (fromLength <= kEpsilon || toLength <= kEpsilon)
			return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		const glm::vec3 first = from / fromLength;
		const glm::vec3 second = to / toLength;
		const float dotValue = std::clamp(glm::dot(first, second), -1.0f, 1.0f);
		if (dotValue >= 1.0f - kEpsilon)
			return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		if (dotValue <= -1.0f + kEpsilon)
		{
			glm::vec3 axis = glm::cross(first, glm::vec3(1.0f, 0.0f, 0.0f));
			if (glm::length(axis) <= kEpsilon)
				axis = glm::cross(first, glm::vec3(0.0f, 1.0f, 0.0f));
			return glm::angleAxis(kPi, glm::normalize(axis));
		}
		return glm::normalize(glm::quat(1.0f + dotValue, glm::cross(first, second)));
	}

	float VansQuaternionAngleDegrees(const glm::quat& rotation)
	{
		if (!Finite(rotation) || glm::dot(rotation, rotation) <= kEpsilon)
			return 0.0f;
		const glm::quat q = NormalizeHemisphere(rotation);
		return glm::degrees(2.0f * std::acos(std::clamp(q.w, -1.0f, 1.0f)));
	}
}
