#include "VansRootMotionSteering.h"
#include "VansRootMotionYaw.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float kLn2 = 0.6931471805599453f;
	constexpr float kEpsilon = 0.0001f;

	float ClampSignedMagnitude(float value, float maximumMagnitude)
	{
		return glm::clamp(value, -maximumMagnitude, maximumMagnitude);
	}
}

namespace VansGraphics
{
	void VansRootMotionSteering::Configure(const RootMotionSteeringSettings& settings)
	{
		m_Settings = settings;
		Reset();
	}

	void VansRootMotionSteering::Reset()
	{
		m_CorrectionYawRateDegreesPerSecond = 0.0f;
	}

	RootMotionSteeringResult VansRootMotionSteering::Apply(
		float deltaTime,
		float movementSpeed,
		float currentFacingYawDegrees,
		float targetFacingYawDegrees,
		float authoredFutureYawDegrees,
		glm::quat& inOutRootMotionRotation)
	{
		RootMotionSteeringResult result;
		const float dt = (std::max)(0.0f, deltaTime);
		if (!m_Settings.enabled || dt <= 0.0f ||
			movementSpeed < (std::max)(0.0f, m_Settings.minMovementSpeed))
		{
			Reset();
			return result;
		}

		const float horizon = (std::max)(m_Settings.predictionTime, kEpsilon);
		result.targetFacingDeltaDegrees = std::remainder(
			targetFacingYawDegrees - currentFacingYawDegrees, 360.0f);
		result.authoredFacingDeltaDegrees = std::remainder(
			authoredFutureYawDegrees, 360.0f);
		const float rawCorrection = std::remainder(
			result.targetFacingDeltaDegrees - result.authoredFacingDeltaDegrees,
			360.0f);
		const float maxCorrection = glm::clamp(
			m_Settings.maxCorrectionAngleDegrees, 0.0f, 180.0f);
		result.requestedCorrectionDegrees = ClampSignedMagnitude(
			rawCorrection, maxCorrection);
		result.limited = std::abs(rawCorrection - result.requestedCorrectionDegrees) > 0.001f;

		const float maximumRate = (std::max)(
			0.0f, m_Settings.maxCorrectionYawRateDegreesPerSecond);
		const float requestedRate = ClampSignedMagnitude(
			result.requestedCorrectionDegrees / horizon, maximumRate);
		const float halfLife = (std::max)(
			m_Settings.correctionHalfLife, kEpsilon);
		const float alpha = 1.0f - std::exp(-kLn2 * dt / halfLife);
		m_CorrectionYawRateDegreesPerSecond = glm::mix(
			m_CorrectionYawRateDegreesPerSecond, requestedRate, alpha);

		float applied = m_CorrectionYawRateDegreesPerSecond * dt;
		if (std::abs(applied) > std::abs(result.requestedCorrectionDegrees) &&
			applied * result.requestedCorrectionDegrees >= 0.0f)
		{
			applied = result.requestedCorrectionDegrees;
		}
		result.appliedCorrectionDegrees = applied;
		result.appliedYawRateDegreesPerSecond =
			dt > kEpsilon ? applied / dt : 0.0f;
		result.active = std::abs(applied) > 0.0001f;
		if (result.active)
			ApplyRootMotionYawCorrection(applied, inOutRootMotionRotation);
		return result;
	}
}
