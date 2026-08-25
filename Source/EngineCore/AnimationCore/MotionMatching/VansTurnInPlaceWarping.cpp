#include "VansTurnInPlaceWarping.h"

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
	void VansTurnInPlaceWarping::Configure(const TurnInPlaceWarpingSettings& settings)
	{
		m_Settings = settings;
		Reset();
	}

	void VansTurnInPlaceWarping::Reset()
	{
		m_ActiveClipId = 0;
		m_LastAnimationTimeSeconds = -1.0f;
		m_AdditiveYawRateDegreesPerSecond = 0.0f;
		m_AccumulatedAdditiveCorrectionDegrees = 0.0f;
	}

	TurnInPlaceReachability VansTurnInPlaceWarping::EvaluateCandidate(
		float targetDeltaDegrees,
		float authoredRemainingYawDegrees,
		float availableCorrectionTimeSeconds) const
	{
		TurnInPlaceReachability result;
		result.authoredRemainingYawDegrees = authoredRemainingYawDegrees;
		if (!m_Settings.enabled)
		{
			result.reason = "Disabled";
			return result;
		}
		if (!std::isfinite(targetDeltaDegrees) ||
			!std::isfinite(authoredRemainingYawDegrees) ||
			!std::isfinite(availableCorrectionTimeSeconds))
		{
			result.reason = "NonFiniteInput";
			return result;
		}

		const float rootYawThreshold = (std::max)(
			m_Settings.rootYawThresholdDegrees, kEpsilon);
		if (std::abs(authoredRemainingYawDegrees) <= rootYawThreshold)
		{
			result.reason = "AuthoredYawExhausted";
			return result;
		}
		if (targetDeltaDegrees * authoredRemainingYawDegrees <= 0.0f)
		{
			result.reason = "WrongDirection";
			return result;
		}

		const float minimumScale = (std::min)(
			m_Settings.minRootYawScaleRatio, m_Settings.maxRootYawScaleRatio);
		const float maximumScale = (std::max)(
			m_Settings.minRootYawScaleRatio, m_Settings.maxRootYawScaleRatio);
		result.scaleRatio = glm::clamp(
			targetDeltaDegrees / authoredRemainingYawDegrees,
			minimumScale,
			maximumScale);
		result.residualDegrees = targetDeltaDegrees -
			result.scaleRatio * authoredRemainingYawDegrees;

		const float maxAdditive = (std::max)(
			0.0f, m_Settings.maxAdditiveCorrectionDegrees);
		if (std::abs(result.residualDegrees) > maxAdditive + kEpsilon)
		{
			result.reason = "AdditiveLimit";
			return result;
		}
		const float maximumRate = (std::max)(
			0.0f, m_Settings.maxAdditiveYawRateDegreesPerSecond);
		if (std::abs(result.residualDegrees) > kEpsilon &&
			(maximumRate <= kEpsilon ||
			 std::abs(result.residualDegrees) / maximumRate >
				availableCorrectionTimeSeconds + kEpsilon))
		{
			result.reason = "YawRateLimit";
			return result;
		}

		const float residualDenominator = (std::max)(maxAdditive, 1.0f);
		result.endpointCost =
			(std::max)(0.0f, m_Settings.endpointScaleCostWeight) *
				(result.scaleRatio - 1.0f) * (result.scaleRatio - 1.0f) +
			(std::max)(0.0f, m_Settings.endpointResidualCostWeight) *
				(result.residualDegrees / residualDenominator) *
				(result.residualDegrees / residualDenominator);
		result.reachable = true;
		result.reason = "None";
		return result;
	}

	TurnInPlaceWarpingResult VansTurnInPlaceWarping::Apply(
		float deltaTime,
		std::uint64_t sourceClipId,
		float animationTimeSeconds,
		float targetDeltaDegrees,
		float authoredRemainingYawDegrees,
		float remainingClipTimeSeconds,
		glm::quat& inOutRootMotionRotation)
	{
		TurnInPlaceWarpingResult result;
		result.targetDeltaDegrees = targetDeltaDegrees;
		result.authoredRemainingYawDegrees = authoredRemainingYawDegrees;
		const float dt = (std::max)(0.0f, deltaTime);
		if (!m_Settings.enabled || dt <= 0.0f)
		{
			Reset();
			result.reason = m_Settings.enabled ? "InvalidDeltaTime" : "Disabled";
			return result;
		}

		if (sourceClipId != m_ActiveClipId ||
			animationTimeSeconds + kEpsilon < m_LastAnimationTimeSeconds)
		{
			Reset();
			m_ActiveClipId = sourceClipId;
		}
		m_LastAnimationTimeSeconds = animationTimeSeconds;
		result.authoredFrameYawDegrees =
			ExtractRootMotionYawDegrees(inOutRootMotionRotation);

		const float tolerance = (std::max)(
			0.0f, m_Settings.finalToleranceDegrees);
		if (std::abs(targetDeltaDegrees) <= tolerance)
		{
			result.reachable = true;
			result.reason = "Aligned";
			// Turn pose 仍可能处于根旋转区间。若直接停用修正，本帧 authored yaw
			// 会把已经对齐的 Transform 再次推离目标；端点锁定只抵消本帧 Yaw，
			// 不改位移，也不会把大残差伪装成 snap。
			result.appliedAdditiveCorrectionDegrees =
				targetDeltaDegrees - result.authoredFrameYawDegrees;
			result.appliedYawRateDegreesPerSecond =
				result.appliedAdditiveCorrectionDegrees / dt;
			ApplyRootMotionYawCorrection(
				result.appliedAdditiveCorrectionDegrees,
				inOutRootMotionRotation);
			result.active =
				std::abs(result.appliedAdditiveCorrectionDegrees) > kEpsilon;
			result.limited = result.active;
			result.accumulatedAdditiveCorrectionDegrees =
				m_AccumulatedAdditiveCorrectionDegrees;
			return result;
		}

		const float rootYawThreshold = (std::max)(
			m_Settings.rootYawThresholdDegrees, kEpsilon);
		TurnInPlaceReachability reachability;
		if (std::abs(authoredRemainingYawDegrees) > rootYawThreshold)
		{
			reachability = EvaluateCandidate(
				targetDeltaDegrees,
				authoredRemainingYawDegrees,
				(std::max)(remainingClipTimeSeconds, dt));
		}
		else
		{
			reachability.authoredRemainingYawDegrees = authoredRemainingYawDegrees;
			reachability.scaleRatio = 1.0f;
			reachability.residualDegrees = targetDeltaDegrees;
			const float remainingBudget = (std::max)(0.0f,
				m_Settings.maxAdditiveCorrectionDegrees -
				std::abs(m_AccumulatedAdditiveCorrectionDegrees));
			const float maxRate = (std::max)(
				0.0f, m_Settings.maxAdditiveYawRateDegreesPerSecond);
			reachability.reachable =
				std::abs(targetDeltaDegrees) <= remainingBudget + kEpsilon &&
				maxRate > kEpsilon &&
				std::abs(targetDeltaDegrees) / maxRate <=
					(std::max)(remainingClipTimeSeconds, dt) + kEpsilon;
			reachability.reason = reachability.reachable
				? "ProceduralSettle" : "AuthoredYawExhausted";
		}

		static_cast<TurnInPlaceReachability&>(result) = reachability;
		if (!reachability.reachable)
		{
			result.needsReplan = true;
			result.accumulatedAdditiveCorrectionDegrees =
				m_AccumulatedAdditiveCorrectionDegrees;
			return result;
		}

		result.appliedScaleCorrectionDegrees =
			(reachability.scaleRatio - 1.0f) * result.authoredFrameYawDegrees;
		const float correctionHorizon = (std::max)(
			dt,
			(std::min)(
				(std::max)(m_Settings.proceduralTargetTime, dt),
				(std::max)(remainingClipTimeSeconds, dt)));
		const float maxRate = (std::max)(
			0.0f, m_Settings.maxAdditiveYawRateDegreesPerSecond);
		const float requestedRate = ClampSignedMagnitude(
			reachability.residualDegrees / correctionHorizon, maxRate);
		const float halfLife = (std::max)(
			m_Settings.correctionHalfLife, kEpsilon);
		const float alpha = 1.0f - std::exp(-kLn2 * dt / halfLife);
		m_AdditiveYawRateDegreesPerSecond = glm::mix(
			m_AdditiveYawRateDegreesPerSecond, requestedRate, alpha);

		float additiveStep = m_AdditiveYawRateDegreesPerSecond * dt;
		const bool finalFrame = remainingClipTimeSeconds <= dt + kEpsilon;
		if (finalFrame || std::abs(reachability.residualDegrees) <= std::abs(additiveStep))
			additiveStep = reachability.residualDegrees;
		const float additiveLimit = (std::max)(
			0.0f, m_Settings.maxAdditiveCorrectionDegrees);
		const float nextAccumulated = glm::clamp(
			m_AccumulatedAdditiveCorrectionDegrees + additiveStep,
			-additiveLimit,
			additiveLimit);
		result.appliedAdditiveCorrectionDegrees =
			nextAccumulated - m_AccumulatedAdditiveCorrectionDegrees;
		m_AccumulatedAdditiveCorrectionDegrees = nextAccumulated;
		result.accumulatedAdditiveCorrectionDegrees =
			m_AccumulatedAdditiveCorrectionDegrees;
		result.appliedYawRateDegreesPerSecond =
			result.appliedAdditiveCorrectionDegrees / dt;
		result.limited =
			std::abs(additiveStep - result.appliedAdditiveCorrectionDegrees) > kEpsilon;

		const float totalCorrection =
			result.appliedScaleCorrectionDegrees +
			result.appliedAdditiveCorrectionDegrees;
		ApplyRootMotionYawCorrection(totalCorrection, inOutRootMotionRotation);
		result.active = std::abs(totalCorrection) > kEpsilon;
		if (result.limited &&
			std::abs(reachability.residualDegrees) > tolerance)
		{
			result.needsReplan = true;
			result.reason = "AdditiveBudgetExhausted";
		}
		return result;
	}
}
