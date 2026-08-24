#include "VansAimConstraintSolver.h"

#include "../../VansPoseMath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace VansGraphics
{
	namespace
	{
		constexpr float kEpsilon = 1.0e-6f;

		bool Finite(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		glm::quat ClampDelta(const glm::quat& delta, float maxDegrees, bool& clamped)
		{
			const float angle = VansQuaternionAngleDegrees(delta);
			if (angle <= maxDegrees || angle <= kEpsilon)
				return delta;
			clamped = true;
			return glm::normalize(glm::slerp(
				glm::quat(1.0f, 0.0f, 0.0f, 0.0f), delta, maxDegrees / angle));
		}

		glm::vec3 ProjectedDirection(
			const glm::vec3& value,
			const glm::vec3& normal,
			const glm::vec3& fallback)
		{
			glm::vec3 projected = value - normal * glm::dot(value, normal);
			float length = glm::length(projected);
			if (length > kEpsilon) return projected / length;
			projected = fallback - normal * glm::dot(fallback, normal);
			length = glm::length(projected);
			if (length > kEpsilon) return projected / length;
			const glm::vec3 axis = std::abs(normal.y) < 0.8f
				? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
			return glm::normalize(glm::cross(normal, axis));
		}

		glm::quat AlignFrame(
			const glm::vec3& currentForward,
			const glm::vec3& currentUp,
			const glm::vec3& targetForward,
			const glm::vec3& targetUp)
		{
			const glm::quat forwardDelta = VansShortestArc(currentForward, targetForward);
			const glm::vec3 alignedUp = ProjectedDirection(
				forwardDelta * currentUp, targetForward, targetUp);
			const float roll = std::atan2(
				glm::dot(glm::cross(alignedUp, targetUp), targetForward),
				std::clamp(glm::dot(alignedUp, targetUp), -1.0f, 1.0f));
			return glm::normalize(glm::angleAxis(roll, targetForward) * forwardDelta);
		}
	}

	VansProceduralSolverResult VansAimConstraintSolver::Solve(
		VansPoseWorkspace& workspace,
		const VansCompiledAnimationRig& rig,
		const VansCompiledRigChain& chain,
		const VansProceduralGoal& goal,
		float deltaTime,
		const VansAimConstraintSettings& settings)
	{
		VansProceduralSolverResult result;
		if (chain.solver != VansRigSolverKind::Aim || chain.boneIndices.empty()
			|| chain.boneIndices.size() > VansMaxProceduralChainBones
			|| chain.weights.size() != chain.boneIndices.size() || !goal.valid
			|| !Finite(goal.positionModel) || !std::isfinite(goal.positionWeight)
			|| goal.positionWeight < 0.0f || goal.positionWeight > 1.0f
			|| deltaTime < 0.0f || !std::isfinite(deltaTime)
			|| !std::isfinite(settings.yawLimitDegrees.x)
			|| !std::isfinite(settings.yawLimitDegrees.y)
			|| settings.yawLimitDegrees.x > settings.yawLimitDegrees.y
			|| settings.yawLimitDegrees.x < -180.0f || settings.yawLimitDegrees.y > 180.0f
			|| !std::isfinite(settings.pitchLimitDegrees.x)
			|| !std::isfinite(settings.pitchLimitDegrees.y)
			|| settings.pitchLimitDegrees.x > settings.pitchLimitDegrees.y
			|| settings.pitchLimitDegrees.x < -180.0f || settings.pitchLimitDegrees.y > 180.0f
			|| !std::isfinite(settings.maxAngularSpeedDegrees)
			|| settings.maxAngularSpeedDegrees <= 0.0f
			|| !std::isfinite(settings.weight) || settings.weight < 0.0f || settings.weight > 1.0f)
			return result;
		const float totalWeight = std::clamp(settings.weight * goal.positionWeight, 0.0f, 1.0f);
		if (totalWeight <= kEpsilon)
		{
			result.status = VansProceduralSolverStatus::NoEffect;
			return result;
		}
		std::array<VansBoneTransform, VansMaxProceduralChainBones> original{};
		for (std::size_t index = 0; index < chain.boneIndices.size(); ++index)
		{
			const int bone = chain.boneIndices[index];
			if (!workspace.IsValidBone(bone)) return result;
			original[index] = workspace.GetLocal(bone);
		}

		const int tip = chain.boneIndices.back();
		const glm::vec3 modelForward = rig.modelForward;
		const glm::vec3 modelUp = rig.modelUp;
		const glm::vec3 modelRight = glm::normalize(glm::cross(modelUp, modelForward));
		const glm::quat initialTipRotation = workspace.GetComponentRotation(tip);
		const glm::vec3 currentForward = initialTipRotation
			* chain.forwardAxisLocal;
		const glm::vec3 currentUp = ProjectedDirection(
			initialTipRotation * chain.upAxisLocal,
			currentForward, modelUp);
		bool angleLimited = false;
		bool speedLimited = false;
		auto computeTargetTipRotation = [&](const glm::vec3& origin, glm::quat& targetRotation)
		{
			glm::vec3 desired = goal.positionModel - origin;
			if (!Finite(desired) || glm::length(desired) <= kEpsilon)
				return false;
			desired = glm::normalize(desired);
			const float requestedYaw = glm::degrees(std::atan2(
				glm::dot(desired, modelRight), glm::dot(desired, modelForward)));
			const float requestedPitch = glm::degrees(std::asin(
				std::clamp(glm::dot(desired, modelUp), -1.0f, 1.0f)));
			const float yaw = std::clamp(requestedYaw,
				settings.yawLimitDegrees.x, settings.yawLimitDegrees.y);
			const float pitch = std::clamp(requestedPitch,
				settings.pitchLimitDegrees.x, settings.pitchLimitDegrees.y);
			angleLimited = angleLimited || std::abs(yaw - requestedYaw) > 1.0e-4f
				|| std::abs(pitch - requestedPitch) > 1.0e-4f;
			const glm::quat yawRotation = glm::angleAxis(glm::radians(yaw), modelUp);
			const glm::vec3 yawedRight = yawRotation * modelRight;
			desired = glm::normalize(glm::angleAxis(glm::radians(-pitch), yawedRight)
				* (yawRotation * modelForward));
			const glm::vec3 targetUp = ProjectedDirection(modelUp, desired, currentUp);
			glm::quat targetDelta = AlignFrame(
				currentForward, currentUp, desired, targetUp);
			bool clampedBySpeed = false;
			targetDelta = ClampDelta(targetDelta,
				settings.maxAngularSpeedDegrees * deltaTime, clampedBySpeed);
			speedLimited = speedLimited || clampedBySpeed;
			targetDelta = glm::normalize(glm::slerp(
				glm::quat(1.0f, 0.0f, 0.0f, 0.0f), targetDelta, totalWeight));
			targetRotation = glm::normalize(targetDelta * initialTipRotation);
			return true;
		};
		glm::quat targetTipRotation;
		if (!computeTargetTipRotation(workspace.GetComponentPosition(tip), targetTipRotation))
		{
			result.status = VansProceduralSolverStatus::NoEffect;
			return result;
		}

		bool jointLimited = false;
		constexpr int kMaximumConstraintPasses = 16;
		constexpr float kRotationToleranceDegrees = 0.05f;
		for (int pass = 0; pass < kMaximumConstraintPasses; ++pass)
		{
			// Ancestor rotations move the aim origin. Recompute the point direction on
			// every pass instead of solving a stale orientation from the input pose.
			if (!computeTargetTipRotation(
				workspace.GetComponentPosition(tip), targetTipRotation))
				break;
			const float passStartError = VansQuaternionAngleDegrees(glm::normalize(
				targetTipRotation * glm::inverse(workspace.GetComponentRotation(tip))));
			if (passStartError <= kRotationToleranceDegrees)
				break;
			float remainingWeight = 1.0f;
			for (std::size_t index = 0; index < chain.boneIndices.size(); ++index)
			{
				const int bone = chain.boneIndices[index];
				if (chain.weights[index] <= kEpsilon) continue;
				glm::quat delta = glm::normalize(
					targetTipRotation * glm::inverse(workspace.GetComponentRotation(tip)));
				const float share = remainingWeight > kEpsilon
					? std::clamp(chain.weights[index] / remainingWeight, 0.0f, 1.0f) : 1.0f;
				remainingWeight = std::max(0.0f, remainingWeight - chain.weights[index]);
				delta = glm::normalize(glm::slerp(
					glm::quat(1.0f, 0.0f, 0.0f, 0.0f), delta, share));
				if (!workspace.ApplyComponentRotationDelta(bone, delta))
				{
					for (std::size_t restore = 0; restore < chain.boneIndices.size(); ++restore)
						workspace.SetLocal(chain.boneIndices[restore], original[restore]);
					return {};
				}
				const VansConstraintResult constrained = VansApplyJointLimit(
					workspace.GetLocal(bone).rotation, rig.FindJointLimit(bone));
				if (!constrained.valid || !workspace.SetLocalRotation(bone, constrained.rotation))
				{
					for (std::size_t restore = 0; restore < chain.boneIndices.size(); ++restore)
						workspace.SetLocal(chain.boneIndices[restore], original[restore]);
					return {};
				}
				jointLimited = jointLimited || constrained.limited;
			}
			result.iterations = pass + 1;
			if (!computeTargetTipRotation(
				workspace.GetComponentPosition(tip), targetTipRotation))
				break;
			const float passEndError = VansQuaternionAngleDegrees(glm::normalize(
				targetTipRotation * glm::inverse(workspace.GetComponentRotation(tip))));
			if (passEndError <= kRotationToleranceDegrees)
				break;
		}
		computeTargetTipRotation(workspace.GetComponentPosition(tip), targetTipRotation);
		result.rotationErrorDegrees = VansQuaternionAngleDegrees(glm::normalize(
			targetTipRotation * glm::inverse(workspace.GetComponentRotation(tip))));
		result.status = angleLimited || speedLimited || jointLimited
			|| result.rotationErrorDegrees > kRotationToleranceDegrees
			? VansProceduralSolverStatus::Clamped : VansProceduralSolverStatus::Solved;
		if (angleLimited || jointLimited) result.limitReason |= VansProceduralLimitReason::Joint;
		if (speedLimited) result.limitReason |= VansProceduralLimitReason::AngularSpeed;
		if (!workspace.IsFinite())
		{
			for (std::size_t restore = 0; restore < chain.boneIndices.size(); ++restore)
				workspace.SetLocal(chain.boneIndices[restore], original[restore]);
			return {};
		}
		return result;
	}
}
