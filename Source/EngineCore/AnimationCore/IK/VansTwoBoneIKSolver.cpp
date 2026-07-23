#include "VansTwoBoneIKSolver.h"
#include "VansIKConstraint.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtc/constants.hpp>
#include <../../GLM/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace VansGraphics
{
	namespace
	{
		constexpr float kEpsilon = 1e-5f;

		glm::vec3 SafeNormalize(const glm::vec3& value, const glm::vec3& fallback)
		{
			const float length = glm::length(value);
			return length > kEpsilon ? value / length : fallback;
		}

		glm::vec3 OrthogonalDirection(const glm::vec3& direction)
		{
			const glm::vec3 n = SafeNormalize(direction, glm::vec3(0.0f, 1.0f, 0.0f));
			const glm::vec3 reference = std::abs(n.y) < 0.75f
				? glm::vec3(0.0f, 1.0f, 0.0f)
				: glm::vec3(1.0f, 0.0f, 0.0f);
			return SafeNormalize(glm::cross(reference, n), glm::vec3(0.0f, 0.0f, 1.0f));
		}

		glm::quat RotationBetween(const glm::vec3& from, const glm::vec3& to)
		{
			const glm::vec3 fromDirection = SafeNormalize(from, glm::vec3(1.0f, 0.0f, 0.0f));
			const glm::vec3 toDirection = SafeNormalize(to, fromDirection);
			const float dotValue = glm::clamp(glm::dot(fromDirection, toDirection), -1.0f, 1.0f);
			if (dotValue > 1.0f - 1e-6f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			if (dotValue < -1.0f + 1e-6f)
				return glm::angleAxis(glm::pi<float>(), OrthogonalDirection(fromDirection));
			return glm::rotation(fromDirection, toDirection);
		}

		glm::vec3 BlendPoleDirection(const glm::vec3& current,
		                             const glm::vec3& desired,
		                             const glm::vec3& reachAxis,
		                             float weight)
		{
			weight = glm::clamp(weight, 0.0f, 1.0f);
			if (weight <= kEpsilon) return current;
			const float cosine = glm::clamp(glm::dot(current, desired), -1.0f, 1.0f);
			const float sine = glm::dot(reachAxis, glm::cross(current, desired));
			float angle = std::atan2(sine, cosine);
			if (std::abs(sine) < kEpsilon && cosine < 0.0f) angle = glm::pi<float>();
			return SafeNormalize(glm::angleAxis(angle * weight, reachAxis) * current, desired);
		}

		bool ApplyAimRotation(int boneIndex,
		                      const glm::vec3& currentDirection,
		                      const glm::vec3& desiredDirection,
		                      const JointConstraint& constraint,
		                      const Skeleton& skeleton,
		                      std::vector<glm::mat4>& localTransforms,
		                      std::vector<glm::mat4>& modelTransforms)
		{
			const BoneInfo& bone = skeleton.bones[boneIndex];
			const glm::quat parentRotation = bone.parentIndex >= 0
				? IK_ExtractRotation(modelTransforms[bone.parentIndex])
				: glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			const glm::quat modelDelta = RotationBetween(currentDirection, desiredDirection);
			const glm::quat localDelta = IK_WorldDeltaToLocal(parentRotation, modelDelta);
			const glm::quat currentLocal = IK_ExtractRotation(localTransforms[boneIndex]);
			const glm::quat desiredLocal = glm::normalize(localDelta * currentLocal);
			const glm::quat constrainedLocal = IK_ApplyJointConstraint(desiredLocal, constraint);
			const bool limited = IK_QuaternionAngularErrorDeg(desiredLocal, constrainedLocal) > 0.01f;
			IK_SetRotation(localTransforms[boneIndex], constrainedLocal);
			IK_UpdateGlobalsForSubtree(boneIndex, localTransforms, modelTransforms, skeleton);
			return limited;
		}
	}

	IKSolveResult VansTwoBoneIKSolver::Solve(
		std::vector<glm::mat4>& localTransforms,
		const std::vector<glm::mat4>& globalTransforms,
		const Skeleton& skeleton,
		const IKChainDefinition& chain,
		const IKTarget& target,
		const IKSolveContext& context)
	{
		IKSolveResult result;
		if (chain.bones.size() != 3 ||
		    localTransforms.size() != skeleton.bones.size() ||
		    globalTransforms.size() != skeleton.bones.size() ||
		    !IK_ValidateChain(skeleton, chain, true))
			return result;

		const int rootIndex = chain.bones[0].boneIndex;
		const int midIndex = chain.bones[1].boneIndex;
		const int tipIndex = chain.bones[2].boneIndex;
		std::vector<glm::mat4> modelTransforms = globalTransforms;
		const IKTarget modelTarget = IK_ResolveTargetToModelSpace(target, context, modelTransforms, skeleton);
		const float positionWeight = glm::clamp(modelTarget.positionWeight, 0.0f, 1.0f);
		const float rotationWeight = glm::clamp(modelTarget.rotationWeight, 0.0f, 1.0f);
		if (positionWeight <= kEpsilon && rotationWeight <= kEpsilon)
		{
			result.status = IKSolveStatus::NoEffect;
			result.converged = true;
			return result;
		}

		const glm::quat originalTipRotation = IK_ExtractRotation(modelTransforms[tipIndex]);
		if (positionWeight > kEpsilon)
		{
			glm::vec3 root = IK_ExtractTranslation(modelTransforms[rootIndex]);
			glm::vec3 mid = IK_ExtractTranslation(modelTransforms[midIndex]);
			glm::vec3 tip = IK_ExtractTranslation(modelTransforms[tipIndex]);
			float upperLength = glm::distance(root, mid);
			float lowerLength = glm::distance(mid, tip);
			if (upperLength <= kEpsilon || lowerLength <= kEpsilon) return result;

			glm::vec3 desiredTip = glm::mix(tip, modelTarget.position, positionWeight);
			float requestedDistance = glm::distance(root, desiredTip);
			const float originalReach = upperLength + lowerLength;
			if (chain.allowStretch && originalReach > kEpsilon &&
			    requestedDistance > originalReach * glm::clamp(chain.startStretchRatio, 0.0f, 1.0f))
			{
				const float stretchScale = glm::clamp(requestedDistance / originalReach,
				                                            1.0f,
				                                            std::max(1.0f, chain.maxStretchScale));
				if (stretchScale > 1.0f + kEpsilon)
				{
					localTransforms[midIndex][3] = glm::vec4(
						glm::vec3(localTransforms[midIndex][3]) * stretchScale, 1.0f);
					localTransforms[tipIndex][3] = glm::vec4(
						glm::vec3(localTransforms[tipIndex][3]) * stretchScale, 1.0f);
					IK_UpdateGlobalsForSubtree(rootIndex, localTransforms, modelTransforms, skeleton);
					mid = IK_ExtractTranslation(modelTransforms[midIndex]);
					tip = IK_ExtractTranslation(modelTransforms[tipIndex]);
					upperLength = glm::distance(root, mid);
					lowerLength = glm::distance(mid, tip);
				}
			}

			glm::vec3 rootToTarget = desiredTip - root;
			const glm::vec3 currentRootToTip = tip - root;
			const glm::vec3 targetDirection = SafeNormalize(
				rootToTarget, SafeNormalize(currentRootToTip, OrthogonalDirection(mid - root)));
			const float minimumReach = std::max(std::abs(upperLength - lowerLength) + kEpsilon, kEpsilon);
			const float maximumReach = std::max(upperLength + lowerLength - kEpsilon, minimumReach);
			const float targetDistance = glm::clamp(glm::length(rootToTarget), minimumReach, maximumReach);
			result.positionLimited = std::abs(targetDistance - glm::length(rootToTarget)) > kEpsilon;
			desiredTip = root + targetDirection * targetDistance;

			glm::vec3 currentPole = mid - root - targetDirection * glm::dot(mid - root, targetDirection);
			currentPole = SafeNormalize(currentPole, OrthogonalDirection(targetDirection));
			glm::vec3 poleDirection = currentPole;
			if (chain.poleWeight > kEpsilon)
			{
				const glm::vec3 polePoint = IK_ResolvePointToModelSpace(
					chain.poleVector, chain.poleSpace, chain.poleReferenceBoneIndex,
					chain.poleReferenceBoneName, context, modelTransforms, skeleton);
				glm::vec3 desiredPole = polePoint - root;
				desiredPole -= targetDirection * glm::dot(desiredPole, targetDirection);
				desiredPole = SafeNormalize(desiredPole, currentPole);
				poleDirection = BlendPoleDirection(currentPole, desiredPole, targetDirection, chain.poleWeight);
			}

			const float midAlongTarget = glm::clamp(
				(upperLength * upperLength + targetDistance * targetDistance - lowerLength * lowerLength) /
				(2.0f * std::max(targetDistance, kEpsilon)), 0.0f, upperLength);
			const float midSide = std::sqrt(std::max(
				upperLength * upperLength - midAlongTarget * midAlongTarget, 0.0f));
			const glm::vec3 desiredMid = root + targetDirection * midAlongTarget + poleDirection * midSide;

			result.rotationLimited |= ApplyAimRotation(
				rootIndex, mid - root, desiredMid - root, chain.bones[0].constraint,
				skeleton, localTransforms, modelTransforms);
			const glm::vec3 solvedMid = IK_ExtractTranslation(modelTransforms[midIndex]);
			const glm::vec3 solvedTip = IK_ExtractTranslation(modelTransforms[tipIndex]);
			result.rotationLimited |= ApplyAimRotation(
				midIndex, solvedTip - solvedMid, desiredTip - solvedMid, chain.bones[1].constraint,
				skeleton, localTransforms, modelTransforms);
			result.iterationsUsed = 1;
		}

		IKTarget effectorTarget = modelTarget;
		if (chain.maintainEffectorGlobalRotation && rotationWeight <= kEpsilon)
		{
			effectorTarget.rotation = originalTipRotation;
			effectorTarget.rotationWeight = 1.0f;
		}
		IK_ApplyEffectorRotationTarget(localTransforms, modelTransforms, skeleton, tipIndex, effectorTarget);

		const glm::vec3 finalTip = IK_ExtractTranslation(modelTransforms[tipIndex]);
		const glm::vec3 effectivePosition = glm::mix(
			IK_ExtractTranslation(globalTransforms[tipIndex]), modelTarget.position, positionWeight);
		result.finalPosError = glm::distance(finalTip, effectivePosition);
		if (effectorTarget.rotationWeight > kEpsilon)
			result.finalRotError = IK_QuaternionAngularErrorDeg(
				IK_ExtractRotation(modelTransforms[tipIndex]), effectorTarget.rotation);

		const bool positionConverged = positionWeight <= kEpsilon || result.finalPosError <= chain.positionTolerance;
		const bool rotationConverged = effectorTarget.rotationWeight <= kEpsilon ||
		                               result.finalRotError <= chain.rotationTolerance;
		result.converged = positionConverged && rotationConverged;
		if (result.rotationLimited) result.status = IKSolveStatus::ReachedLimit;
		else if (result.positionLimited && !result.converged) result.status = IKSolveStatus::Unreachable;
		else result.status = result.converged ? IKSolveStatus::Solved : IKSolveStatus::ReachedLimit;
		return result;
	}
}
