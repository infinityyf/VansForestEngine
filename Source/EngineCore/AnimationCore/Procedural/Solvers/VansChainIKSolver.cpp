#include "VansChainIKSolver.h"

#include "../../VansPoseMath.h"

#include <algorithm>
#include <array>
#include <cstddef>
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

		glm::vec3 SafeDirection(const glm::vec3& value, const glm::vec3& fallback)
		{
			const float length = glm::length(value);
			return length > kEpsilon ? value / length : fallback;
		}

		float MinimumReach(float totalLength, float longestSegment)
		{
			return std::max(0.0f, longestSegment - (totalLength - longestSegment));
		}

		void Restore(VansPoseWorkspace& workspace,
		             const std::vector<int>& bones,
		             const std::array<VansBoneTransform, VansMaxProceduralChainBones>& original)
		{
			for (std::size_t index = 0; index < bones.size(); ++index)
				workspace.SetLocal(bones[index], original[index]);
		}

		void Blend(VansPoseWorkspace& workspace,
		           const std::vector<int>& bones,
		           const std::array<VansBoneTransform, VansMaxProceduralChainBones>& original,
		           float weight)
		{
			for (std::size_t index = 0; index < bones.size(); ++index)
			{
				workspace.SetLocal(bones[index], VansPoseMath::BlendTransforms(
					original[index], workspace.GetLocal(bones[index]), weight));
			}
		}

		bool ApplyLimit(VansPoseWorkspace& workspace,
		                const VansCompiledAnimationRig& rig,
		                int bone,
		                bool& limited)
		{
			const VansConstraintResult value = VansApplyJointLimit(
				workspace.GetLocal(bone).rotation, rig.FindJointLimit(bone));
			if (!value.valid || !workspace.SetLocalRotation(bone, value.rotation))
				return false;
			limited = limited || value.limited;
			return true;
		}
	}

	VansProceduralSolverResult VansChainIKSolver::Solve(
		VansPoseWorkspace& workspace,
		const VansCompiledAnimationRig& rig,
		const VansCompiledRigChain& chain,
		const VansProceduralGoal& goal,
		const VansChainIKSettings& settings)
	{
		if (chain.solver == VansRigSolverKind::CCD)
			return SolveCCD(workspace, rig, chain, goal, settings);
		if (chain.solver == VansRigSolverKind::FABRIK)
			return SolveFABRIK(workspace, rig, chain, goal, settings);
		return {};
	}

	VansProceduralSolverResult VansChainIKSolver::SolveCCD(
		VansPoseWorkspace& workspace,
		const VansCompiledAnimationRig& rig,
		const VansCompiledRigChain& chain,
		const VansProceduralGoal& goal,
		const VansChainIKSettings& settings)
	{
		VansProceduralSolverResult result;
		if (chain.boneIndices.size() < 2
			|| chain.boneIndices.size() > VansMaxProceduralChainBones || !goal.valid
			|| chain.solveWeights.size() + 1 != chain.boneIndices.size()
			|| !std::isfinite(chain.maxStepDegrees)
			|| chain.maxStepDegrees <= 0.0f || chain.maxStepDegrees > 180.0f
			|| std::any_of(chain.solveWeights.begin(), chain.solveWeights.end(),
				[](float value)
				{ return !std::isfinite(value) || value < 0.0f || value > 1.0f; })
			|| settings.maxIterations < 1 || settings.maxIterations > 64
			|| !Finite(goal.positionModel) || !std::isfinite(goal.positionWeight)
			|| goal.positionWeight < 0.0f || goal.positionWeight > 1.0f
			|| !std::isfinite(settings.positionTolerance) || settings.positionTolerance <= 0.0f
			|| !std::isfinite(settings.weight) || settings.weight < 0.0f || settings.weight > 1.0f)
			return result;
		const float weight = std::clamp(settings.weight * goal.positionWeight, 0.0f, 1.0f);
		if (weight <= kEpsilon)
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
		const glm::vec3 rootPosition = workspace.GetComponentPosition(chain.boneIndices.front());
		float totalLength = 0.0f;
		float longestSegment = 0.0f;
		for (std::size_t index = 1; index < chain.boneIndices.size(); ++index)
		{
			const float length = glm::length(
				workspace.GetComponentPosition(chain.boneIndices[index])
				- workspace.GetComponentPosition(chain.boneIndices[index - 1]));
			if (!std::isfinite(length) || length <= kEpsilon)
			{
				Restore(workspace, chain.boneIndices, original);
				return {};
			}
			totalLength += length;
			longestSegment = std::max(longestSegment, length);
		}
		const glm::vec3 requestedDelta = goal.positionModel - rootPosition;
		const float requestedDistance = glm::length(requestedDelta);
		const float minimumReach = MinimumReach(totalLength, longestSegment);
		const bool reachLimited = requestedDistance + settings.positionTolerance < minimumReach
			|| requestedDistance > totalLength + settings.positionTolerance;
		const glm::vec3 fallbackDirection = SafeDirection(
			workspace.GetComponentPosition(tip) - rootPosition, glm::vec3(0.0f, 0.0f, 1.0f));
		const glm::vec3 targetDirection = SafeDirection(requestedDelta, fallbackDirection);
		const float effectiveDistance = std::clamp(requestedDistance, minimumReach, totalLength);
		const glm::vec3 effectiveTarget = rootPosition + targetDirection * effectiveDistance;
		bool limited = false;
		for (int iteration = 0; iteration < settings.maxIterations; ++iteration)
		{
			result.iterations = iteration + 1;
			if (glm::length(workspace.GetComponentPosition(tip) - effectiveTarget)
				<= settings.positionTolerance)
				break;
			for (std::size_t reverse = chain.boneIndices.size() - 1; reverse-- > 0;)
			{
				const int bone = chain.boneIndices[reverse];
				const float solveWeight = chain.solveWeights[reverse];
				if (solveWeight <= kEpsilon) continue;
				const glm::vec3 pivot = workspace.GetComponentPosition(bone);
				const glm::vec3 effector = workspace.GetComponentPosition(tip);
				glm::quat delta = VansShortestArc(effector - pivot, effectiveTarget - pivot);
				const float deltaAngle = VansQuaternionAngleDegrees(delta);
				if (deltaAngle > chain.maxStepDegrees)
					delta = glm::normalize(glm::slerp(
						glm::quat(1.0f, 0.0f, 0.0f, 0.0f), delta,
						chain.maxStepDegrees / deltaAngle));
				delta = glm::normalize(glm::slerp(
					glm::quat(1.0f, 0.0f, 0.0f, 0.0f), delta, solveWeight));
				if (!workspace.ApplyComponentRotationDelta(bone, delta)
					|| !ApplyLimit(workspace, rig, bone, limited))
				{
					Restore(workspace, chain.boneIndices, original);
					return {};
				}
			}
		}
		const float solvedEffectiveError = glm::length(
			workspace.GetComponentPosition(tip) - effectiveTarget);
		Blend(workspace, chain.boneIndices, original, weight);
		if (!workspace.IsFinite())
		{
			Restore(workspace, chain.boneIndices, original);
			return {};
		}
		result.requestedPositionError = glm::length(workspace.GetComponentPosition(tip) - goal.positionModel);
		result.effectivePositionError = glm::length(
			workspace.GetComponentPosition(tip) - effectiveTarget);
		result.status = reachLimited ? VansProceduralSolverStatus::Unreachable
			: solvedEffectiveError <= settings.positionTolerance
				? VansProceduralSolverStatus::Solved : VansProceduralSolverStatus::Clamped;
		if (reachLimited) result.limitReason |= VansProceduralLimitReason::Reach;
		if (limited)
		{
			if (!reachLimited) result.status = VansProceduralSolverStatus::Clamped;
			result.limitReason |= VansProceduralLimitReason::Joint;
		}
		if ((result.status == VansProceduralSolverStatus::Clamped
			|| result.status == VansProceduralSolverStatus::Unreachable)
			&& !settings.commitClampedPose)
			Restore(workspace, chain.boneIndices, original);
		return result;
	}

	VansProceduralSolverResult VansChainIKSolver::SolveFABRIK(
		VansPoseWorkspace& workspace,
		const VansCompiledAnimationRig& rig,
		const VansCompiledRigChain& chain,
		const VansProceduralGoal& goal,
		const VansChainIKSettings& settings)
	{
		VansProceduralSolverResult result;
		const std::size_t count = chain.boneIndices.size();
		if (count < 2 || count > VansMaxProceduralChainBones || !goal.valid
			|| chain.solveWeights.size() + 1 != count
			|| std::any_of(chain.solveWeights.begin(), chain.solveWeights.end(),
				[](float value)
				{ return !std::isfinite(value) || value < 0.0f || value > 1.0f; })
			|| settings.maxIterations < 1 || settings.maxIterations > 64
			|| !Finite(goal.positionModel) || !std::isfinite(goal.positionWeight)
			|| goal.positionWeight < 0.0f || goal.positionWeight > 1.0f
			|| !std::isfinite(settings.positionTolerance) || settings.positionTolerance <= 0.0f
			|| !std::isfinite(settings.weight) || settings.weight < 0.0f || settings.weight > 1.0f)
			return result;
		const float weight = settings.weight * goal.positionWeight;
		if (weight <= kEpsilon)
		{
			result.status = VansProceduralSolverStatus::NoEffect;
			return result;
		}
		std::array<VansBoneTransform, VansMaxProceduralChainBones> original{};
		std::array<glm::vec3, VansMaxProceduralChainBones> positions{};
		std::array<float, VansMaxProceduralChainBones - 1> lengths{};
		std::array<glm::vec3, VansMaxProceduralChainBones - 1> referenceDirections{};
		float longestSegment = 0.0f;
		for (std::size_t index = 0; index < count; ++index)
		{
			const int bone = chain.boneIndices[index];
			if (!workspace.IsValidBone(bone)) return result;
			original[index] = workspace.GetLocal(bone);
			positions[index] = workspace.GetComponentPosition(bone);
			if (index > 0)
			{
				lengths[index - 1] = glm::length(positions[index] - positions[index - 1]);
				if (!std::isfinite(lengths[index - 1]) || lengths[index - 1] <= kEpsilon)
					return result;
				referenceDirections[index - 1] =
					(positions[index] - positions[index - 1]) / lengths[index - 1];
				longestSegment = std::max(longestSegment, lengths[index - 1]);
			}
		}
		const glm::vec3 root = positions.front();
		const float totalLength = std::accumulate(
			lengths.begin(), lengths.begin() + static_cast<std::ptrdiff_t>(count - 1), 0.0f);
		const float requestedDistance = glm::length(goal.positionModel - root);
		const float minimumReach = MinimumReach(totalLength, longestSegment);
		const bool reachLimited = requestedDistance + settings.positionTolerance < minimumReach
			|| requestedDistance > totalLength + settings.positionTolerance;
		const glm::vec3 fallbackDirection = SafeDirection(
			positions[count - 1] - root, referenceDirections[0]);
		const glm::vec3 targetDirection = SafeDirection(
			goal.positionModel - root, fallbackDirection);
		const float effectiveDistance = std::clamp(requestedDistance, minimumReach, totalLength);
		const glm::vec3 effectiveTarget = root + targetDirection * effectiveDistance;
		bool jointLimited = false;
		const int tip = chain.boneIndices.back();
		for (int iteration = 0; iteration < settings.maxIterations; ++iteration)
		{
			result.iterations = iteration + 1;
			for (std::size_t index = 0; index < count; ++index)
				positions[index] = workspace.GetComponentPosition(chain.boneIndices[index]);
			if (effectiveDistance >= totalLength - kEpsilon)
			{
				positions.front() = root;
				for (std::size_t index = 1; index < count; ++index)
					positions[index] = positions[index - 1]
						+ targetDirection * lengths[index - 1];
			}
			else
			{
				positions[count - 1] = effectiveTarget;
				for (std::size_t reverse = count - 1; reverse-- > 0;)
				{
					const glm::vec3 direction = SafeDirection(
						positions[reverse] - positions[reverse + 1],
						-referenceDirections[reverse]);
					positions[reverse] = positions[reverse + 1]
						+ direction * lengths[reverse];
				}
				positions.front() = root;
				for (std::size_t index = 1; index < count; ++index)
				{
					const glm::vec3 direction = SafeDirection(
						positions[index] - positions[index - 1],
						referenceDirections[index - 1]);
					positions[index] = positions[index - 1]
						+ direction * lengths[index - 1];
				}
			}

			// Constraints are part of every FABRIK projection pass. Applying them only
			// after an unconstrained solve produces a pose whose descendants no longer
			// match the solved point chain.
			for (std::size_t index = 0; index + 1 < count; ++index)
			{
				const int bone = chain.boneIndices[index];
				const float solveWeight = chain.solveWeights[index];
				if (solveWeight <= kEpsilon) continue;
				const glm::vec3 current = workspace.GetComponentPosition(
					chain.boneIndices[index + 1]) - workspace.GetComponentPosition(bone);
				const glm::quat delta = glm::normalize(glm::slerp(
					glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
					VansShortestArc(current, positions[index + 1] - positions[index]),
					solveWeight));
				if (!workspace.ApplyComponentRotationDelta(bone, delta)
					|| !ApplyLimit(workspace, rig, bone, jointLimited))
				{
					Restore(workspace, chain.boneIndices, original);
					return {};
				}
			}
			if (glm::length(workspace.GetComponentPosition(tip) - effectiveTarget)
				<= settings.positionTolerance)
				break;
		}
		const float solvedEffectiveError = glm::length(
			workspace.GetComponentPosition(tip) - effectiveTarget);
		Blend(workspace, chain.boneIndices, original, weight);
		result.requestedPositionError = glm::length(workspace.GetComponentPosition(tip) - goal.positionModel);
		result.effectivePositionError = glm::length(
			workspace.GetComponentPosition(tip) - effectiveTarget);
		result.status = reachLimited ? VansProceduralSolverStatus::Unreachable
			: solvedEffectiveError <= settings.positionTolerance
				? VansProceduralSolverStatus::Solved : VansProceduralSolverStatus::Clamped;
		if (reachLimited) result.limitReason |= VansProceduralLimitReason::Reach;
		if (jointLimited)
		{
			if (!reachLimited) result.status = VansProceduralSolverStatus::Clamped;
			result.limitReason |= VansProceduralLimitReason::Joint;
		}
		if (!workspace.IsFinite())
		{
			Restore(workspace, chain.boneIndices, original);
			return {};
		}
		if ((result.status == VansProceduralSolverStatus::Clamped
			|| result.status == VansProceduralSolverStatus::Unreachable)
			&& !settings.commitClampedPose)
			Restore(workspace, chain.boneIndices, original);
		return result;
	}
}
