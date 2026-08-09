#include "VansLookAtSolver.h"
#include "VansIKConstraint.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace VansGraphics
{
	IKSolveResult VansLookAtSolver::Solve(
		std::vector<glm::mat4>&       localTransforms,
		const std::vector<glm::mat4>& globalTransformsIn,
		const Skeleton&               skeleton,
		const IKChainDefinition&      chain,
		const IKTarget&               target,
		const IKSolveContext&         context)
	{
		IKSolveResult result;
		const int N = static_cast<int>(chain.bones.size());
		if (N < 1 || target.positionWeight < 1e-4f) { result.status = IKSolveStatus::NoEffect; return result; }
		if (globalTransformsIn.size() != skeleton.bones.size()) return result;
		if (localTransforms.size() != skeleton.bones.size()) return result;

		m_GlobalScratch.assign(globalTransformsIn.begin(), globalTransformsIn.end());
		auto& globals = m_GlobalScratch;
		const IKTarget modelTarget = IK_ResolveTargetToModelSpace(target, context, globals, skeleton);

		// 计算所有 link 的累计权重（用于权重归一化）
		float totalWeight = 0.0f;
		for (const auto& link : chain.bones)
			if (link.boneIndex >= 0 && link.boneIndex < static_cast<int>(skeleton.bones.size()) &&
			    link.constraint.type != JointConstraintType::Locked)
				totalWeight += std::max(0.0f, link.stiffnessWeight);
		if (totalWeight < 1e-6f) return result;

		float maxAngleRad = (m_MaxAnglePerBoneDeg > 0.0f)
			? glm::radians(m_MaxAnglePerBoneDeg) : 1e9f;

		// 是否使用 root-local 前向参考（每骨骼自动从绑定姿态推导 local 前向）
		const bool useWorldForward = glm::length(m_WorldForward) > 1e-3f;
		glm::vec3  worldForwardN   = useWorldForward
			? glm::normalize(m_WorldForward) : glm::vec3(0.0f, 0.0f, -1.0f);
		const bool useModelUp = glm::length(m_ModelUp) > 1e-3f && m_UpWeight > 1e-4f;
		const glm::vec3 modelUpN = useModelUp ? glm::normalize(m_ModelUp) : glm::vec3(0.0f, 1.0f, 0.0f);

		for (int i = 0; i < N; ++i)
		{
			const IKBoneLink& link = chain.bones[i];
			if (link.boneIndex < 0) continue;
			if (link.constraint.type == JointConstraintType::Locked) continue;

			glm::mat4& boneGlobal = globals[link.boneIndex];
			glm::vec3 jointPos = IK_ExtractTranslation(boneGlobal);
			glm::quat boneRot  = IK_ExtractRotation(boneGlobal);

			glm::vec3 localForward = m_ForwardAxis;
			if (useWorldForward)
			{
				// m_WorldForward is retained for asset compatibility but is interpreted
				// as a model-space bind reference. Convert it to a per-bone local axis,
				// then evaluate that axis through the current input pose.
				const glm::quat bindModelRotation = IK_ExtractRotation(glm::inverse(skeleton.bones[link.boneIndex].offsetMatrix));
				localForward = glm::normalize(glm::conjugate(bindModelRotation) * worldForwardN);
			}
			glm::vec3 curDir = glm::normalize(boneRot * localForward);
			glm::vec3 desDir = modelTarget.position - jointPos;
			float dl = glm::length(desDir);
			if (dl < 1e-6f) continue;
			desDir /= dl;

			float d = glm::clamp(glm::dot(curDir, desDir), -1.0f, 1.0f);

			float weight = (std::max(0.0f, link.stiffnessWeight) / totalWeight) *
			               glm::clamp(modelTarget.positionWeight, 0.0f, 1.0f);
			glm::quat worldDelta(1.0f, 0.0f, 0.0f, 0.0f);
			if (d < 0.99999f)
			{
				const float angle = std::min(std::acos(d) * weight, maxAngleRad);
				glm::vec3 axis = glm::cross(curDir, desDir);
				const float axisLength = glm::length(axis);
				if (axisLength < 1e-6f)
				{
					const glm::vec3 reference = std::abs(curDir.y) < 0.75f
						? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
					axis = glm::normalize(glm::cross(curDir, reference));
				}
				else
				{
					axis /= axisLength;
				}
				worldDelta = glm::angleAxis(angle, axis);
			}

			if (useModelUp)
			{
				const glm::quat bindModelRotation = IK_ExtractRotation(glm::inverse(skeleton.bones[link.boneIndex].offsetMatrix));
				const glm::vec3 localUp = glm::normalize(glm::conjugate(bindModelRotation) * modelUpN);
				const glm::quat aimedRotation = glm::normalize(worldDelta * boneRot);
				glm::vec3 currentUp = aimedRotation * localUp;
				currentUp -= desDir * glm::dot(currentUp, desDir);
				glm::vec3 desiredUp = modelUpN - desDir * glm::dot(modelUpN, desDir);
				const float currentUpLength = glm::length(currentUp);
				const float desiredUpLength = glm::length(desiredUp);
				if (currentUpLength > 1e-5f && desiredUpLength > 1e-5f)
				{
					currentUp /= currentUpLength;
					desiredUp /= desiredUpLength;
					const float signedRoll = std::atan2(glm::dot(desDir, glm::cross(currentUp, desiredUp)),
					                                    glm::clamp(glm::dot(currentUp, desiredUp), -1.0f, 1.0f));
					const float roll = glm::clamp(signedRoll * weight * glm::clamp(m_UpWeight, 0.0f, 1.0f),
					                              -maxAngleRad, maxAngleRad);
					worldDelta = glm::normalize(glm::angleAxis(roll, desDir) * worldDelta);
				}
			}

			const BoneInfo& bone = skeleton.bones[link.boneIndex];
			glm::quat parentRot = (bone.parentIndex >= 0)
				? IK_ExtractRotation(globals[bone.parentIndex])
				: glm::quat(1, 0, 0, 0);

			glm::quat localDelta = IK_WorldDeltaToLocal(parentRot, worldDelta);
			glm::quat curLocal   = IK_ExtractRotation(localTransforms[link.boneIndex]);
			glm::quat newLocal   = glm::normalize(localDelta * curLocal);

			if (link.constraint.type != JointConstraintType::None)
				newLocal = IK_ApplyJointConstraint(newLocal, link.constraint);

			IK_SetRotation(localTransforms[link.boneIndex], newLocal);
			IK_UpdateGlobalsForSubtree(link.boneIndex, localTransforms, globals, skeleton);
		}

		const IKBoneLink& effector = chain.bones.back();
		if (effector.boneIndex >= 0 && effector.boneIndex < static_cast<int>(globals.size()))
		{
			const glm::quat finalRotation = IK_ExtractRotation(globals[effector.boneIndex]);
			glm::vec3 localForward = m_ForwardAxis;
			if (useWorldForward)
			{
				const glm::quat bindRotation = IK_ExtractRotation(glm::inverse(skeleton.bones[effector.boneIndex].offsetMatrix));
				localForward = glm::normalize(glm::conjugate(bindRotation) * worldForwardN);
			}
			const glm::vec3 finalDirection = glm::normalize(finalRotation * localForward);
			const glm::vec3 desired = glm::normalize(modelTarget.position - IK_ExtractTranslation(globals[effector.boneIndex]));
			result.finalRotError = glm::degrees(std::acos(glm::clamp(glm::dot(finalDirection, desired), -1.0f, 1.0f)));
			if (useModelUp)
			{
				const glm::quat bindRotation = IK_ExtractRotation(glm::inverse(skeleton.bones[effector.boneIndex].offsetMatrix));
				const glm::vec3 localUp = glm::normalize(glm::conjugate(bindRotation) * modelUpN);
				glm::vec3 currentUp = finalRotation * localUp;
				currentUp -= desired * glm::dot(currentUp, desired);
				glm::vec3 desiredUp = modelUpN - desired * glm::dot(modelUpN, desired);
				if (glm::length(currentUp) > 1e-5f && glm::length(desiredUp) > 1e-5f)
				{
					currentUp = glm::normalize(currentUp);
					desiredUp = glm::normalize(desiredUp);
					const float rollError = glm::degrees(std::acos(glm::clamp(glm::dot(currentUp, desiredUp), -1.0f, 1.0f)));
					result.finalRotError = std::max(result.finalRotError, rollError);
				}
			}
		}
		result.converged = result.finalRotError <= std::max(chain.rotationTolerance, 0.01f);
		result.iterationsUsed = 1;
		result.status = result.converged ? IKSolveStatus::Solved : IKSolveStatus::ReachedLimit;
		return result;
	}

}  // namespace VansGraphics
