#include "VansCharacterTrajectoryGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
	constexpr float kEpsilon = 0.0001f;
	constexpr float kLn2 = 0.6931471805599453f;

	float AngleDelta(float from, float to)
	{
		return std::remainder(to - from, 360.0f);
	}

	float AdvanceAngle(float current, float target, float halfLife,
	                   float maxRate, float deltaTime)
	{
		if (deltaTime <= 0.0f)
			return current;
		const float alpha = 1.0f - std::exp(-kLn2 * deltaTime /
			(std::max)(halfLife, kEpsilon));
		const float step = AngleDelta(current, target) * alpha;
		const float maxStep = (std::max)(0.0f, maxRate) * deltaTime;
		return current + glm::clamp(step, -maxStep, maxStep);
	}

	glm::vec3 ClampLength(const glm::vec3& value, float maximum)
	{
		const float length = glm::length(value);
		if (length <= maximum || length <= kEpsilon)
			return value;
		return value * (maximum / length);
	}
}

namespace Vans
{
	void VansCharacterTrajectoryGenerator::Reset(
		const glm::vec3& positionWorld, float facingYaw)
	{
		m_Trajectory = {};
		m_PlannedVelocityWorld = glm::vec3(0.0f);
		m_ActualVelocityWorld = glm::vec3(0.0f);
		m_RequestedVelocityWorld = glm::vec3(0.0f);
		m_MotionConsumptionRatio = 1.0f;
		m_PreviousMoveInputLocal = glm::vec2(0.0f);
		m_FilteredReferenceYaw = facingYaw;
		m_PreviousReferenceYaw = facingYaw;
		m_ReferenceYawRate = 0.0f;
		m_PlannedFacingYaw = facingYaw;
		m_PreviousDesiredFacingYaw = facingYaw;
		m_DesiredFacingYawRate = 0.0f;
		m_HistoryClock = 0.0f;
		m_HasActualVelocity = false;
		m_HasPreviousInput = false;
		m_HasPreviousReferenceYaw = false;
		m_HasPreviousDesiredFacing = false;
		m_Initialized = true;
		m_History.clear();
		m_History.push_back({ 0.0f, positionWorld });
	}

	void VansCharacterTrajectoryGenerator::RecordResolvedMotion(
		float deltaTime,
		const glm::vec3& positionWorld,
		const glm::vec3& actualVelocityWorld,
		const glm::vec3& requestedVelocityWorld)
	{
		if (!m_Initialized)
			Reset(positionWorld, 0.0f);
		m_ActualVelocityWorld = actualVelocityWorld;
		m_ActualVelocityWorld.y = 0.0f;
		m_RequestedVelocityWorld = requestedVelocityWorld;
		m_RequestedVelocityWorld.y = 0.0f;
		const float requestedSpeedSq = glm::dot(
			m_RequestedVelocityWorld, m_RequestedVelocityWorld);
		m_MotionConsumptionRatio = requestedSpeedSq > kEpsilon * kEpsilon
			? glm::clamp(glm::dot(m_ActualVelocityWorld, m_RequestedVelocityWorld) /
				requestedSpeedSq, 0.0f, 1.25f)
			: 1.0f;
		m_HasActualVelocity = true;
		m_HistoryClock += (std::max)(0.0f, deltaTime);
		m_History.push_back({ m_HistoryClock, positionWorld });
		while (m_History.size() > 2 &&
		       m_History.front().time < m_HistoryClock - 1.25f)
		{
			m_History.pop_front();
		}
	}

	glm::vec3 VansCharacterTrajectoryGenerator::ResolveDesiredVelocity(
		const VansCharacterMotionIntent& intent, float referenceYaw) const
	{
		glm::vec2 input = intent.moveInputLocal;
		const float length = glm::length(input);
		if (length > 1.0f)
			input /= length;
		const glm::vec3 local(input.x, 0.0f, input.y);
		return LocomotionLocalToWorldPlanar(local, referenceYaw) *
			(std::max)(0.0f, intent.desiredSpeed);
	}

	glm::vec3 VansCharacterTrajectoryGenerator::AdvanceVelocity(
		const glm::vec3& velocity,
		const glm::vec3& target,
		float deltaTime,
		const VansCharacterMotionSettings& settings) const
	{
		if (deltaTime <= 0.0f)
			return velocity;
		const float alpha = 1.0f - std::exp(-kLn2 * deltaTime /
			(std::max)(settings.velocityHalfLife, kEpsilon));
		const glm::vec3 requestedDelta = (target - velocity) * alpha;
		const bool decelerating = glm::length(target) + kEpsilon < glm::length(velocity) ||
			glm::dot(velocity, target) < 0.0f;
		const float limit = (decelerating ? settings.maxDeceleration : settings.maxAcceleration) *
			deltaTime;
		glm::vec3 result = velocity + ClampLength(requestedDelta, (std::max)(0.0f, limit));
		result.y = 0.0f;
		return result;
	}

	glm::vec3 VansCharacterTrajectoryGenerator::SampleHistoryPosition(
		float secondsAgo, const glm::vec3& fallbackPosition) const
	{
		if (m_History.empty())
			return fallbackPosition;
		const float targetTime = m_HistoryClock - secondsAgo;
		if (targetTime <= m_History.front().time)
			return m_History.front().positionWorld;
		for (std::size_t index = 1; index < m_History.size(); ++index)
		{
			const HistorySample& previous = m_History[index - 1];
			const HistorySample& next = m_History[index];
			if (targetTime <= next.time)
			{
				const float span = (std::max)(next.time - previous.time, kEpsilon);
				return glm::mix(previous.positionWorld, next.positionWorld,
					glm::clamp((targetTime - previous.time) / span, 0.0f, 1.0f));
			}
		}
		return m_History.back().positionWorld;
	}

	void VansCharacterTrajectoryGenerator::Update(
		float deltaTime,
		const VansCharacterMotionIntent& intent,
		const VansCharacterMotionSettings& settings,
		const glm::vec3& positionWorld,
		float currentFacingYaw)
	{
		const float dt = (std::max)(0.0f, deltaTime);
		if (!m_Initialized)
			Reset(positionWorld, currentFacingYaw);

		if (m_HasPreviousReferenceYaw && dt > kEpsilon)
		{
			const float rawRate = AngleDelta(
				m_PreviousReferenceYaw, intent.movementReferenceYaw) / dt;
			const float alpha = 1.0f - std::exp(-kLn2 * dt /
				(std::max)(settings.movementReferenceYawRateHalfLife, kEpsilon));
			m_ReferenceYawRate = glm::mix(m_ReferenceYawRate,
				glm::clamp(rawRate, -settings.maxFacingYawRate, settings.maxFacingYawRate), alpha);
		}
		else
			m_ReferenceYawRate = 0.0f;
		m_PreviousReferenceYaw = intent.movementReferenceYaw;
		m_HasPreviousReferenceYaw = true;
		m_FilteredReferenceYaw = AdvanceAngle(
			m_FilteredReferenceYaw, intent.movementReferenceYaw,
			settings.facingHalfLife, settings.maxFacingYawRate, dt);

		if (intent.hasFacing)
		{
			if (m_HasPreviousDesiredFacing && dt > kEpsilon)
			{
				const float rawRate = AngleDelta(
					m_PreviousDesiredFacingYaw, intent.desiredFacingYaw) / dt;
				const float alpha = 1.0f - std::exp(-kLn2 * dt /
					(std::max)(settings.facingVelocityHalfLife, kEpsilon));
				m_DesiredFacingYawRate = glm::mix(m_DesiredFacingYawRate,
					glm::clamp(rawRate, -settings.maxFacingYawRate, settings.maxFacingYawRate), alpha);
			}
			else
				m_DesiredFacingYawRate = 0.0f;
			m_PreviousDesiredFacingYaw = intent.desiredFacingYaw;
			m_HasPreviousDesiredFacing = true;
			m_PlannedFacingYaw = AdvanceAngle(
				m_PlannedFacingYaw, intent.desiredFacingYaw,
				settings.facingHalfLife, settings.maxFacingYawRate, dt);
		}
		else
		{
			m_DesiredFacingYawRate = 0.0f;
			m_HasPreviousDesiredFacing = false;
			m_PlannedFacingYaw = currentFacingYaw;
		}

		const glm::vec3 targetVelocity = ResolveDesiredVelocity(intent, m_FilteredReferenceYaw);
		m_PlannedVelocityWorld = AdvanceVelocity(
			m_PlannedVelocityWorld, targetVelocity, dt, settings);
		if (m_HasActualVelocity)
		{
			const float feedbackAlpha = 1.0f - std::exp(-kLn2 * dt /
				(std::max)(settings.actualVelocityFeedbackHalfLife, kEpsilon));
			const glm::vec3 correction = ClampLength(
				(m_ActualVelocityWorld - m_PlannedVelocityWorld) * feedbackAlpha,
				(std::max)(0.0f, settings.maxAcceleration) * dt * 0.5f);
			m_PlannedVelocityWorld += correction;
		}

		m_Trajectory = {};
		m_Trajectory.originWorld = positionWorld;
		m_Trajectory.currentVelocityWorld = m_HasActualVelocity
			? m_ActualVelocityWorld : m_PlannedVelocityWorld;
		m_Trajectory.requestedVelocityWorld = m_HasActualVelocity
			? m_RequestedVelocityWorld : m_PlannedVelocityWorld;
		m_Trajectory.plannedVelocityWorld = m_PlannedVelocityWorld;
		m_Trajectory.desiredVelocityWorld = ResolveDesiredVelocity(
			intent, intent.movementReferenceYaw);
		m_Trajectory.moveInputLocal = intent.moveInputLocal;
		m_Trajectory.movementReferenceYaw = m_FilteredReferenceYaw;
		m_Trajectory.movementReferenceYawRate = m_ReferenceYawRate;
		m_Trajectory.currentFacingYaw = currentFacingYaw;
		m_Trajectory.plannedFacingYaw = m_PlannedFacingYaw;
		m_Trajectory.desiredFacingYaw = intent.hasFacing
			? intent.desiredFacingYaw : currentFacingYaw;
		m_Trajectory.desiredFacingYawRate = intent.hasFacing ? m_DesiredFacingYawRate : 0.0f;
		m_Trajectory.hasFacing = intent.hasFacing;
		m_Trajectory.motionConsumptionRatio = m_HasActualVelocity
			? m_MotionConsumptionRatio : 1.0f;
		m_Trajectory.movementBlocked = m_HasActualVelocity &&
			glm::length(glm::vec2(m_RequestedVelocityWorld.x,
				m_RequestedVelocityWorld.z)) > 0.05f &&
			m_MotionConsumptionRatio < 0.25f;

		const float actualSpeed = glm::length(glm::vec2(
			m_Trajectory.currentVelocityWorld.x, m_Trajectory.currentVelocityWorld.z));
		const float desiredSpeed = glm::length(glm::vec2(
			m_Trajectory.desiredVelocityWorld.x, m_Trajectory.desiredVelocityWorld.z));
		if (actualSpeed > 0.05f && desiredSpeed > 0.05f)
		{
			const float directionDot = glm::clamp(glm::dot(
				m_Trajectory.currentVelocityWorld, m_Trajectory.desiredVelocityWorld) /
				(actualSpeed * desiredSpeed), -1.0f, 1.0f);
			m_Trajectory.directionChangeDegrees = glm::degrees(std::acos(directionDot));
			m_Trajectory.hasDirectionChange = true;
		}

		const float inputLength = glm::length(intent.moveInputLocal);
		const float previousInputLength = glm::length(m_PreviousMoveInputLocal);
		if (m_HasPreviousInput && inputLength > kEpsilon && previousInputLength > kEpsilon)
		{
			const float inputDot = glm::clamp(glm::dot(
				intent.moveInputLocal, m_PreviousMoveInputLocal) /
				(inputLength * previousInputLength), -1.0f, 1.0f);
			m_Trajectory.inputDirectionChangeDegrees = glm::degrees(std::acos(inputDot));
		}
		m_PreviousMoveInputLocal = intent.moveInputLocal;
		m_HasPreviousInput = inputLength > kEpsilon;

		constexpr std::array<float, 2> historyHorizons{ 0.30f, 0.15f };
		for (std::size_t index = 0; index < historyHorizons.size(); ++index)
		{
			auto& sample = m_Trajectory.history[index];
			sample.time = -historyHorizons[index];
			sample.positionWorld = SampleHistoryPosition(historyHorizons[index], positionWorld);
			sample.velocityWorld = m_Trajectory.currentVelocityWorld;
			sample.facingYaw = currentFacingYaw;
		}

		constexpr std::array<float, 3> horizons{ 0.25f, 0.5f, 1.0f };
		glm::vec3 predictedPosition = positionWorld;
		glm::vec3 predictedVelocity = m_PlannedVelocityWorld;
		float elapsed = 0.0f;
		std::size_t outputIndex = 0;
		const float configuredStep = glm::clamp(settings.predictionStep, 1.0f / 240.0f, 0.10f);
		while (outputIndex < horizons.size())
		{
			const float targetTime = horizons[outputIndex];
			const float step = (std::min)(configuredStep, targetTime - elapsed);
			const float sampleTime = elapsed + step;
			const float predictedReferenceYaw = PredictFacingYaw(
				m_FilteredReferenceYaw,
				intent.movementReferenceYaw,
				m_ReferenceYawRate,
				sampleTime,
				settings.facingHalfLife);
			const glm::vec3 futureTargetVelocity = ResolveDesiredVelocity(
				intent, predictedReferenceYaw);
			predictedVelocity = AdvanceVelocity(
				predictedVelocity, futureTargetVelocity, step, settings);
			predictedPosition += predictedVelocity * step;
			elapsed = sampleTime;
			if (elapsed + kEpsilon >= targetTime)
			{
				auto& sample = m_Trajectory.future[outputIndex];
				sample.time = targetTime;
				sample.positionWorld = predictedPosition;
				sample.velocityWorld = predictedVelocity;
				sample.facingYaw = intent.hasFacing
					? PredictFacingYaw(m_PlannedFacingYaw, intent.desiredFacingYaw,
						m_DesiredFacingYawRate, targetTime, settings.facingHalfLife)
					: currentFacingYaw;
				++outputIndex;
			}
		}

		glm::vec3 previousPosition = positionWorld;
		glm::vec3 previousVelocity = m_Trajectory.currentVelocityWorld;
		float previousPredictionTime = 0.0f;
		for (const auto& sample : m_Trajectory.future)
		{
			if (!m_Trajectory.hasPredictedPivot && desiredSpeed > 0.05f)
			{
				const float previousAlongDesired = glm::dot(
					previousVelocity, m_Trajectory.desiredVelocityWorld);
				const float nextAlongDesired = glm::dot(
					sample.velocityWorld, m_Trajectory.desiredVelocityWorld);
				if (previousAlongDesired < 0.0f && nextAlongDesired >= 0.0f)
				{
					const float denominator = nextAlongDesired - previousAlongDesired;
					const float alpha = denominator > kEpsilon
						? glm::clamp(-previousAlongDesired / denominator, 0.0f, 1.0f) : 0.0f;
					m_Trajectory.predictedPivotPositionWorld = glm::mix(
						previousPosition, sample.positionWorld, alpha);
					m_Trajectory.predictedPivotTime = glm::mix(
						previousPredictionTime, sample.time, alpha);
					m_Trajectory.hasPredictedPivot = true;
				}
			}
			previousPosition = sample.positionWorld;
			previousVelocity = sample.velocityWorld;
			previousPredictionTime = sample.time;
		}
		m_Trajectory.valid = true;
	}
}
