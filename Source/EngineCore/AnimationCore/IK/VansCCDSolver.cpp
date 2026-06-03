#include "VansCCDSolver.h"
#include "VansIKConstraint.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace VansGraphics
{
	IKSolveResult VansCCDSolver::Solve(
		std::vector<glm::mat4>&       localTransforms,
		const std::vector<glm::mat4>& globalTransformsIn,
		const Skeleton&               skeleton,
		const IKChainDefinition&      chain,
		const IKTarget&               target,
		float                         deltaTime)
	{
		IKSolveResult result;

		const int N = static_cast<int>(chain.bones.size());
		if (N < 2 || target.positionWeight < 1e-4f)
			return result;

		// 全局变换的可写副本（求解过程中需要不断更新）
		std::vector<glm::mat4> globals = globalTransformsIn;
		if (globals.size() != skeleton.bones.size())
			return result;

		// 迭代直到收敛
		float lastError = 1e9f;
		for (int it = 0; it < chain.maxIterations; ++it)
		{
			float err = PerformIteration(chain, target, localTransforms, globals, skeleton);
			result.iterationsUsed = it + 1;
			result.finalPosError  = err;

			if (err < chain.positionTolerance)
			{
				result.converged = true;
				break;
			}
			// 防止震荡：若误差停滞则退出
			if (std::abs(err - lastError) < 1e-6f)
				break;
			lastError = err;
		}

		// 极向量修正（仅在三关节链且权重 > 0 时有效）
		if (chain.poleWeight > 1e-4f && N >= 3)
		{
			ApplyPoleVector(chain, localTransforms, globals, skeleton);
		}

		return result;
	}

	float VansCCDSolver::PerformIteration(
		const IKChainDefinition&      chain,
		const IKTarget&               target,
		std::vector<glm::mat4>&       localTransforms,
		std::vector<glm::mat4>&       globals,
		const Skeleton&               skeleton)
	{
		const int N = static_cast<int>(chain.bones.size());
		const int effectorIdx = chain.bones[N - 1].boneIndex;
		if (effectorIdx < 0 || effectorIdx >= static_cast<int>(skeleton.bones.size()))
			return 1e9f;

		// 从末端的父节点开始，向根扫描（不动末端骨骼自身）
		for (int linkPos = N - 2; linkPos >= 0; --linkPos)
		{
			const IKBoneLink& link = chain.bones[linkPos];
			if (link.constraint.type == JointConstraintType::Locked) continue;
			if (link.boneIndex < 0) continue;

			glm::vec3 jointPos    = IK_ExtractTranslation(globals[link.boneIndex]);
			glm::vec3 effectorPos = IK_ExtractTranslation(globals[effectorIdx]);
			glm::vec3 targetPos   = target.position;

			glm::vec3 toEff = effectorPos - jointPos;
			glm::vec3 toTar = targetPos   - jointPos;

			float lenE = glm::length(toEff);
			float lenT = glm::length(toTar);
			if (lenE < 1e-6f || lenT < 1e-6f) continue;

			toEff /= lenE;
			toTar /= lenT;

			float d = glm::clamp(glm::dot(toEff, toTar), -1.0f, 1.0f);
			if (d > 0.99999f) continue;

			float angle = std::acos(d) * link.stiffnessWeight;
			glm::vec3 axis = glm::cross(toEff, toTar);
			float axisLen = glm::length(axis);
			if (axisLen < 1e-6f) continue;
			axis /= axisLen;

			glm::quat worldDelta = glm::angleAxis(angle, axis);

			// 把世界 delta 转到该关节的父全局空间下
			const BoneInfo& bone = skeleton.bones[link.boneIndex];
			glm::quat parentRot = (bone.parentIndex >= 0)
				? IK_ExtractRotation(globals[bone.parentIndex])
				: glm::quat(1, 0, 0, 0);

			glm::quat localDelta = IK_WorldDeltaToLocal(parentRot, worldDelta);
			glm::quat curLocalRot = IK_ExtractRotation(localTransforms[link.boneIndex]);
			glm::quat newLocalRot = glm::normalize(localDelta * curLocalRot);

			// 应用关节约束
			if (link.constraint.type != JointConstraintType::None)
			{
				newLocalRot = IK_ApplyJointConstraint(newLocalRot, link.constraint);
			}

			IK_SetRotation(localTransforms[link.boneIndex], newLocalRot);

			// 更新该关节子树的全局变换
			IK_UpdateGlobalsForSubtree(link.boneIndex, localTransforms, globals, skeleton);
		}

		// 末端误差
		glm::vec3 effectorPos = IK_ExtractTranslation(globals[effectorIdx]);
		return glm::distance(effectorPos, target.position);
	}

	void VansCCDSolver::ApplyPoleVector(
		const IKChainDefinition&      chain,
		std::vector<glm::mat4>&       localTransforms,
		std::vector<glm::mat4>&       globals,
		const Skeleton&               skeleton)
	{
		const int N = static_cast<int>(chain.bones.size());
		// 三关节链：root(0) - mid(1) - tip(N-1)
		// 对于多关节链取首/中/末三个作为参考
		int rootIdx = chain.bones.front().boneIndex;
		int midIdx  = chain.bones[N / 2].boneIndex;
		int tipIdx  = chain.bones.back().boneIndex;
		if (rootIdx < 0 || midIdx < 0 || tipIdx < 0) return;

		glm::vec3 rootPos = IK_ExtractTranslation(globals[rootIdx]);
		glm::vec3 midPos  = IK_ExtractTranslation(globals[midIdx]);
		glm::vec3 tipPos  = IK_ExtractTranslation(globals[tipIdx]);

		glm::vec3 rootToTip = tipPos - rootPos;
		float l2 = glm::dot(rootToTip, rootToTip);
		if (l2 < 1e-6f) return;
		glm::vec3 axis = rootToTip / std::sqrt(l2);

		// mid 在垂直于 axis 平面上的偏移方向
		glm::vec3 midOffset = midPos - rootPos;
		midOffset = midOffset - axis * glm::dot(midOffset, axis);
		float midLen = glm::length(midOffset);
		if (midLen < 1e-6f) return;
		midOffset /= midLen;

		// pole 在垂直于 axis 平面上的方向
		glm::vec3 poleOffset = chain.poleVector - rootPos;
		poleOffset = poleOffset - axis * glm::dot(poleOffset, axis);
		float poleLen = glm::length(poleOffset);
		if (poleLen < 1e-6f) return;
		poleOffset /= poleLen;

		float d = glm::clamp(glm::dot(midOffset, poleOffset), -1.0f, 1.0f);
		if (d > 0.99999f) return;
		float angle = std::acos(d) * chain.poleWeight;

		glm::vec3 rotAxis = glm::cross(midOffset, poleOffset);
		float al = glm::length(rotAxis);
		if (al < 1e-6f) return;
		rotAxis /= al;

		// 这个旋转只能加在 root 关节上（绕 root-tip 轴）
		glm::quat worldDelta = glm::angleAxis(angle, rotAxis);
		const BoneInfo& bone = skeleton.bones[rootIdx];
		glm::quat parentRot = (bone.parentIndex >= 0)
			? IK_ExtractRotation(globals[bone.parentIndex])
			: glm::quat(1, 0, 0, 0);

		glm::quat localDelta = IK_WorldDeltaToLocal(parentRot, worldDelta);
		glm::quat curLocalRot = IK_ExtractRotation(localTransforms[rootIdx]);
		glm::quat newLocalRot = glm::normalize(localDelta * curLocalRot);

		IK_SetRotation(localTransforms[rootIdx], newLocalRot);
		IK_UpdateGlobalsForSubtree(rootIdx, localTransforms, globals, skeleton);
	}

}  // namespace VansGraphics
