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
		float                         deltaTime)
	{
		IKSolveResult result;
		const int N = static_cast<int>(chain.bones.size());
		if (N < 1 || target.positionWeight < 1e-4f) return result;
		if (globalTransformsIn.size() != skeleton.bones.size()) return result;

		std::vector<glm::mat4> globals = globalTransformsIn;

		// 计算所有 link 的累计权重（用于权重归一化）
		float totalWeight = 0.0f;
		for (const auto& l : chain.bones) totalWeight += l.stiffnessWeight;
		if (totalWeight < 1e-6f) totalWeight = static_cast<float>(N);

		float maxAngleRad = (m_MaxAnglePerBoneDeg > 0.0f)
			? glm::radians(m_MaxAnglePerBoneDeg) : 1e9f;

		for (int i = 0; i < N; ++i)
		{
			const IKBoneLink& link = chain.bones[i];
			if (link.boneIndex < 0) continue;
			if (link.constraint.type == JointConstraintType::Locked) continue;

			glm::mat4& boneGlobal = globals[link.boneIndex];
			glm::vec3 jointPos = IK_ExtractTranslation(boneGlobal);
			glm::quat boneRot  = IK_ExtractRotation(boneGlobal);

			// 当前前向方向
			glm::vec3 curDir = glm::normalize(boneRot * m_ForwardAxis);
			glm::vec3 desDir = target.position - jointPos;
			float dl = glm::length(desDir);
			if (dl < 1e-6f) continue;
			desDir /= dl;

			float d = glm::clamp(glm::dot(curDir, desDir), -1.0f, 1.0f);
			if (d > 0.99999f) continue;

			float angle = std::acos(d);
			// 单骨骼角度限制
			if (angle > maxAngleRad) angle = maxAngleRad;

			// 按权重分摊
			float weight = (link.stiffnessWeight / totalWeight) * target.positionWeight;
			angle *= weight;

			glm::vec3 axis = glm::cross(curDir, desDir);
			float al = glm::length(axis);
			if (al < 1e-6f) continue;
			axis /= al;

			glm::quat worldDelta = glm::angleAxis(angle, axis);

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

		result.converged = true;
		result.iterationsUsed = 1;
		return result;
	}

}  // namespace VansGraphics
