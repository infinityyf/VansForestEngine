#include "VansRootMotionReconciler.h"

#include <../../GLM/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float kLn2 = 0.6931471805599453f;
	constexpr float kEpsilon = 0.0001f;

	glm::vec3 ClampLength(const glm::vec3& value, float maximumLength)
	{
		const float length = glm::length(value);
		return length > maximumLength && length > kEpsilon
			? value * (maximumLength / length)
			: value;
	}

	float RootYawDegrees(const glm::quat& rotation)
	{
		return glm::degrees(glm::eulerAngles(rotation).z);
	}
}

namespace VansGraphics
{
	void VansRootMotionReconciler::Configure(
		const RootMotionReconciliationSettings& settings)
	{
		m_Settings = settings;
		Reset();
	}

	void VansRootMotionReconciler::Reset()
	{
		m_OutgoingVelocityAnimation = glm::vec3(0.0f);
		m_LinearVelocityOffset = glm::vec3(0.0f);
		m_OutgoingYawRateDegreesPerSecond = 0.0f;
		m_AngularVelocityOffsetDegreesPerSecond = 0.0f;
		m_Elapsed = 0.0f;
		m_Pending = false;
		m_Active = false;
	}

	void VansRootMotionReconciler::RequestTransition(
		const glm::vec3& outgoingVelocityAnimation,
		float outgoingYawRateDegreesPerSecond)
	{
		if (!m_Settings.enabled)
		{
			Reset();
			return;
		}
		m_OutgoingVelocityAnimation = outgoingVelocityAnimation;
		m_OutgoingYawRateDegreesPerSecond = outgoingYawRateDegreesPerSecond;
		m_Elapsed = 0.0f;
		m_Pending = true;
		m_Active = false;
	}

	RootMotionReconciliationResult VansRootMotionReconciler::Apply(
		float deltaTime,
		glm::vec3& inOutTranslation,
		glm::quat& inOutRotation)
	{
		RootMotionReconciliationResult result;
		const float dt = std::max(0.0f, deltaTime);
		if (!m_Settings.enabled || dt <= kEpsilon)
			return result;

		result.targetVelocityAnimation = inOutTranslation / dt;
		result.targetYawRateDegreesPerSecond = RootYawDegrees(inOutRotation) / dt;
		if (m_Pending)
		{
			m_LinearVelocityOffset = ClampLength(
				m_OutgoingVelocityAnimation - result.targetVelocityAnimation,
				std::max(0.0f, m_Settings.maxLinearVelocityCorrection));
			m_AngularVelocityOffsetDegreesPerSecond = glm::clamp(
				m_OutgoingYawRateDegreesPerSecond - result.targetYawRateDegreesPerSecond,
				-std::max(0.0f, m_Settings.maxAngularVelocityCorrectionDegreesPerSecond),
				 std::max(0.0f, m_Settings.maxAngularVelocityCorrectionDegreesPerSecond));
			m_Pending = false;
			m_Active = true;
			m_Elapsed = 0.0f;
		}

		if (!m_Active)
		{
			result.appliedVelocityAnimation = result.targetVelocityAnimation;
			result.appliedYawRateDegreesPerSecond = result.targetYawRateDegreesPerSecond;
			return result;
		}

		result.active = true;
		result.appliedVelocityAnimation =
			result.targetVelocityAnimation + m_LinearVelocityOffset;
		result.appliedYawRateDegreesPerSecond =
			result.targetYawRateDegreesPerSecond +
			m_AngularVelocityOffsetDegreesPerSecond;
		inOutTranslation = result.appliedVelocityAnimation * dt;
		const float correctionDegrees =
			m_AngularVelocityOffsetDegreesPerSecond * dt;
		if (std::abs(correctionDegrees) > kEpsilon)
		{
			const glm::quat correction = glm::angleAxis(
				glm::radians(correctionDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
			inOutRotation = glm::normalize(correction * inOutRotation);
		}

		const float linearHalfLife = std::max(m_Settings.linearVelocityHalfLife, kEpsilon);
		const float angularHalfLife = std::max(m_Settings.angularVelocityHalfLife, kEpsilon);
		m_LinearVelocityOffset *= std::exp(-kLn2 * dt / linearHalfLife);
		m_AngularVelocityOffsetDegreesPerSecond *=
			std::exp(-kLn2 * dt / angularHalfLife);
		m_Elapsed += dt;
		if (m_Elapsed >= std::max(0.0f, m_Settings.maxDuration) ||
			(glm::length(m_LinearVelocityOffset) < 0.01f &&
			 std::abs(m_AngularVelocityOffsetDegreesPerSecond) < 0.05f))
		{
			m_Active = false;
			m_LinearVelocityOffset = glm::vec3(0.0f);
			m_AngularVelocityOffsetDegreesPerSecond = 0.0f;
		}
		return result;
	}
}
