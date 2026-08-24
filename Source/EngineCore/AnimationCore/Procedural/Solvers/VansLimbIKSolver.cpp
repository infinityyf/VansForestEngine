#include "VansLimbIKSolver.h"

#include "../../VansPoseMath.h"

#include <algorithm>
#include <cmath>

namespace VansGraphics
{
	namespace
	{
		constexpr float kEpsilon = 1.0e-6f;

		bool Finite(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool Finite(const glm::quat& value)
		{
			return std::isfinite(value.w) && std::isfinite(value.x)
				&& std::isfinite(value.y) && std::isfinite(value.z);
		}

		glm::vec3 Orthogonal(const glm::vec3& direction)
		{
			glm::vec3 axis = std::abs(direction.y) < 0.8f
				? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
			return glm::normalize(glm::cross(direction, axis));
		}

		float SoftReach(float requested, float naturalReach, float startRatio)
		{
			const float start = naturalReach * startRatio;
			if (requested <= start || naturalReach <= kEpsilon)
				return requested;
			const float zone = naturalReach - start;
			return start + zone * (1.0f - std::exp(-(requested - start) / zone));
		}

		bool ApplyLimit(VansPoseWorkspace& workspace,
		                const VansCompiledAnimationRig& rig,
		                int boneIndex,
		                bool& limited)
		{
			const VansConstraintResult constrained = VansApplyJointLimit(
				workspace.GetLocal(boneIndex).rotation, rig.FindJointLimit(boneIndex));
			if (!constrained.valid || !workspace.SetLocalRotation(boneIndex, constrained.rotation))
				return false;
			limited = limited || constrained.limited;
			return true;
		}
	}

	VansProceduralSolverResult VansLimbIKSolver::Solve(
		VansPoseWorkspace& workspace,
		const VansCompiledAnimationRig& rig,
		const VansCompiledRigChain& chain,
		const VansProceduralGoal& goal,
		const VansLimbIKSettings& settings)
	{
		VansProceduralSolverResult result;
		if (chain.solver != VansRigSolverKind::Limb || chain.boneIndices.size() != 3
			|| !goal.valid || !workspace.IsValidBone(chain.boneIndices[0])
			|| !workspace.IsValidBone(chain.boneIndices[1])
			|| !workspace.IsValidBone(chain.boneIndices[2])
			|| !Finite(goal.positionModel) || !Finite(goal.rotationModel)
			|| glm::dot(goal.rotationModel, goal.rotationModel) <= kEpsilon * kEpsilon
			|| !std::isfinite(goal.positionWeight) || !std::isfinite(goal.rotationWeight)
			|| goal.positionWeight < 0.0f || goal.positionWeight > 1.0f
			|| goal.rotationWeight < 0.0f || goal.rotationWeight > 1.0f
			|| !std::isfinite(settings.positionTolerance) || settings.positionTolerance <= 0.0f
			|| !std::isfinite(settings.weight) || settings.weight < 0.0f || settings.weight > 1.0f
			|| (settings.tipRotationMode != VansLimbTipRotationMode::PreserveInput
				&& settings.tipRotationMode != VansLimbTipRotationMode::MatchGoal
				&& settings.tipRotationMode != VansLimbTipRotationMode::FollowChain))
			return result;
		const float positionWeight = std::clamp(goal.positionWeight * settings.weight, 0.0f, 1.0f);
		const float rotationWeight = std::clamp(goal.rotationWeight * settings.weight, 0.0f, 1.0f);
		if (positionWeight <= kEpsilon && rotationWeight <= kEpsilon)
		{
			result.status = VansProceduralSolverStatus::NoEffect;
			return result;
		}

		const int rootIndex = chain.boneIndices[0];
		const int midIndex = chain.boneIndices[1];
		const int tipIndex = chain.boneIndices[2];
		const std::array<VansBoneTransform, 3> original = {
			workspace.GetLocal(rootIndex), workspace.GetLocal(midIndex), workspace.GetLocal(tipIndex)
		};
		auto restore = [&]()
		{
			workspace.SetLocal(rootIndex, original[0]);
			workspace.SetLocal(midIndex, original[1]);
			workspace.SetLocal(tipIndex, original[2]);
		};
		const glm::quat originalTipComponentRotation = workspace.GetComponentRotation(tipIndex);
		if (positionWeight <= kEpsilon)
		{
			if (settings.tipRotationMode != VansLimbTipRotationMode::MatchGoal
				|| rotationWeight <= kEpsilon)
			{
				result.status = VansProceduralSolverStatus::NoEffect;
				return result;
			}
			glm::quat target = glm::normalize(goal.rotationModel);
			if (glm::dot(originalTipComponentRotation, target) < 0.0f) target = -target;
			if (!workspace.SetComponentRotation(tipIndex, glm::normalize(glm::slerp(
				originalTipComponentRotation, target, rotationWeight)))) return result;
			bool tipLimited = false;
			if (!ApplyLimit(workspace, rig, tipIndex, tipLimited))
			{
				restore();
				return {};
			}
			result.rotationErrorDegrees = VansQuaternionAngleDegrees(
				glm::inverse(workspace.GetComponentRotation(tipIndex)) * goal.rotationModel);
			result.status = tipLimited
				? VansProceduralSolverStatus::Clamped : VansProceduralSolverStatus::Solved;
			if (tipLimited) result.limitReason |= VansProceduralLimitReason::Joint;
			if (tipLimited && !settings.commitClampedPose) restore();
			result.iterations = 1;
			return result;
		}
		const glm::vec3 root = workspace.GetComponentPosition(rootIndex);
		const glm::vec3 mid = workspace.GetComponentPosition(midIndex);
		const glm::vec3 tip = workspace.GetComponentPosition(tipIndex);
		float upperLength = glm::length(mid - root);
		float lowerLength = glm::length(tip - mid);
		if (upperLength <= kEpsilon || lowerLength <= kEpsilon)
			return result;

		const glm::vec3 requestedDelta = goal.positionModel - root;
		const float requestedDistance = glm::length(requestedDelta);
		const glm::vec3 targetDirection = requestedDistance > kEpsilon
			? requestedDelta / requestedDistance
			: glm::normalize(tip - root);
		const float naturalReach = upperLength + lowerLength;
		const float minimumReach = std::abs(upperLength - lowerLength);
		const float maximumReach = naturalReach * chain.maxStretchScale;
		const float softenedDistance = SoftReach(requestedDistance, naturalReach,
			chain.softReachStartRatio);
		result.softReachApplied = softenedDistance + kEpsilon < requestedDistance;
		float stretchAmount = 0.0f;
		bool reachLimited = requestedDistance + settings.positionTolerance < minimumReach
			|| requestedDistance > maximumReach + settings.positionTolerance;
		if (requestedDistance > naturalReach && chain.maxStretchScale > 1.0f)
		{
			stretchAmount = std::min(requestedDistance - naturalReach,
				naturalReach * (chain.maxStretchScale - 1.0f));
			const float stretchScale = 1.0f + stretchAmount / naturalReach;
			VansBoneTransform midLocal = workspace.GetLocal(midIndex);
			VansBoneTransform tipLocal = workspace.GetLocal(tipIndex);
			midLocal.translation *= stretchScale;
			tipLocal.translation *= stretchScale;
			if (!workspace.SetLocal(midIndex, midLocal) || !workspace.SetLocal(tipIndex, tipLocal))
			{
				restore();
				return {};
			}
			upperLength *= stretchScale;
			lowerLength *= stretchScale;
			result.stretchApplied = stretchAmount > kEpsilon;
		}
		float effectiveDistance = softenedDistance + stretchAmount;
		effectiveDistance = std::clamp(effectiveDistance,
			std::abs(upperLength - lowerLength) + kEpsilon, upperLength + lowerLength - kEpsilon);
		const glm::vec3 effectiveTarget = root + targetDirection * effectiveDistance;

		// The authored pole is compiled into the chain-root parent's bind space.
		// Following the parent keeps the knee aligned with the character while
		// preventing Motion Matching changes in the source thigh twist from moving
		// the bend plane sideways for an otherwise stable planted-foot goal.
		const int rootParent = rig.skeleton->bones[
			static_cast<std::size_t>(rootIndex)].parentIndex;
		const glm::quat poleFrame = workspace.IsValidBone(rootParent)
			? workspace.GetComponentRotation(rootParent)
			: glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		glm::vec3 pole = poleFrame * chain.poleAxisParentLocal;
		pole -= targetDirection * glm::dot(pole, targetDirection);
		if (glm::length(pole) <= kEpsilon)
			pole = mid - root - targetDirection * glm::dot(mid - root, targetDirection);
		pole = glm::length(pole) > kEpsilon ? glm::normalize(pole) : Orthogonal(targetDirection);
		const float along = (upperLength * upperLength - lowerLength * lowerLength
			+ effectiveDistance * effectiveDistance) / (2.0f * effectiveDistance);
		const float height = std::sqrt(std::max(0.0f, upperLength * upperLength - along * along));
		const glm::vec3 desiredMid = root + targetDirection * along + pole * height;

		if (!workspace.ApplyComponentRotationDelta(rootIndex,
			VansShortestArc(mid - root, desiredMid - root)))
			return result;
		bool jointLimited = false;
		if (!ApplyLimit(workspace, rig, rootIndex, jointLimited))
		{
			restore();
			return {};
		}
		const glm::vec3 solvedMid = workspace.GetComponentPosition(midIndex);
		const glm::vec3 solvedTip = workspace.GetComponentPosition(tipIndex);
		if (!workspace.ApplyComponentRotationDelta(midIndex,
			VansShortestArc(solvedTip - solvedMid, effectiveTarget - solvedMid)))
		{
			restore();
			return {};
		}
		if (!ApplyLimit(workspace, rig, midIndex, jointLimited))
		{
			restore();
			return {};
		}
		const float solvedEffectiveError = glm::length(
			workspace.GetComponentPosition(tipIndex) - effectiveTarget);

		const std::array<VansBoneTransform, 3> solved = {
			workspace.GetLocal(rootIndex), workspace.GetLocal(midIndex), workspace.GetLocal(tipIndex)
		};
		if (positionWeight < 1.0f - kEpsilon)
		{
			workspace.SetLocal(rootIndex, VansPoseMath::BlendTransforms(original[0], solved[0], positionWeight));
			workspace.SetLocal(midIndex, VansPoseMath::BlendTransforms(original[1], solved[1], positionWeight));
			workspace.SetLocal(tipIndex, VansPoseMath::BlendTransforms(original[2], solved[2], positionWeight));
		}
		if (settings.tipRotationMode == VansLimbTipRotationMode::PreserveInput)
			workspace.SetComponentRotation(tipIndex, originalTipComponentRotation);
		else if (settings.tipRotationMode == VansLimbTipRotationMode::MatchGoal && rotationWeight > kEpsilon)
		{
			glm::quat current = workspace.GetComponentRotation(tipIndex);
			glm::quat target = glm::normalize(goal.rotationModel);
			if (glm::dot(current, target) < 0.0f) target = -target;
			if (!workspace.SetComponentRotation(tipIndex,
				glm::normalize(glm::slerp(current, target, rotationWeight))))
			{
				restore();
				return {};
			}
			if (!ApplyLimit(workspace, rig, tipIndex, jointLimited))
			{
				restore();
				return {};
			}
		}

		if (!workspace.IsFinite())
		{
			restore();
			return result;
		}
		result.requestedPositionError = glm::length(workspace.GetComponentPosition(tipIndex) - goal.positionModel);
		result.effectivePositionError = glm::length(workspace.GetComponentPosition(tipIndex) - effectiveTarget);
		result.rotationErrorDegrees = VansQuaternionAngleDegrees(
			glm::inverse(workspace.GetComponentRotation(tipIndex)) * goal.rotationModel);
		result.iterations = 1;
		if (reachLimited || jointLimited
			|| solvedEffectiveError > std::max(settings.positionTolerance, kEpsilon))
		{
			result.status = reachLimited
				? VansProceduralSolverStatus::Unreachable : VansProceduralSolverStatus::Clamped;
			if (reachLimited) result.limitReason |= VansProceduralLimitReason::Reach;
			if (jointLimited) result.limitReason |= VansProceduralLimitReason::Joint;
			if (!settings.commitClampedPose)
				restore();
		}
		else
			result.status = VansProceduralSolverStatus::Solved;
		return result;
	}
}
