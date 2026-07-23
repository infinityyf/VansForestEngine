#include "VansFABRIKSolver.h"
#include "VansIKConstraint.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>
#include <cmath>

namespace VansGraphics
{
	// 基于求解前后骨骼"全局位置 + 朝向"差异，反推每根骨骼的局部旋转。
	// 返回该 link 关节相对父的局部旋转。
	static glm::quat ComputeLocalRotationFromDirection(
		const glm::vec3& fromGlobalPos,
		const glm::vec3& toGlobalPos,
		const glm::vec3& origDirGlobal,
		const glm::quat& parentGlobalRot,
		const glm::quat& origGlobalRot)
	{
		glm::vec3 newDir = toGlobalPos - fromGlobalPos;
		float lN = glm::length(newDir);
		float lO = glm::length(origDirGlobal);
		if (lN < 1e-6f || lO < 1e-6f)
			return glm::normalize(glm::conjugate(parentGlobalRot) * origGlobalRot);

		newDir /= lN;
		glm::vec3 oldDir = origDirGlobal / lO;

		float d = glm::clamp(glm::dot(oldDir, newDir), -1.0f, 1.0f);
		if (d > 0.99999f)
			return glm::normalize(glm::conjugate(parentGlobalRot) * origGlobalRot);

		glm::quat worldDelta;
		if (d < -0.99999f)
		{
			// 180° 翻转：选一个垂直轴
			glm::vec3 perp = std::abs(oldDir.x) < 0.9f
				? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
			glm::vec3 axis = glm::normalize(glm::cross(oldDir, perp));
			worldDelta = glm::angleAxis(3.14159265f, axis);
		}
		else
		{
			float angle = std::acos(d);
			glm::vec3 axis = glm::normalize(glm::cross(oldDir, newDir));
			worldDelta = glm::angleAxis(angle, axis);
		}

		// Reconstruct the desired global rotation from the original global frame,
		// then convert it through the already-solved new parent frame. Applying a
		// delta derived from the original direction to the new parent/local pair
		// would accumulate a parent-space error along the chain.
		const glm::quat desiredGlobal = glm::normalize(worldDelta * origGlobalRot);
		return glm::normalize(glm::conjugate(glm::normalize(parentGlobalRot)) * desiredGlobal);
	}

	IKSolveResult VansFABRIKSolver::Solve(
		std::vector<glm::mat4>&       localTransforms,
		const std::vector<glm::mat4>& globalTransformsIn,
		const Skeleton&               skeleton,
		const IKChainDefinition&      chain,
		const IKTarget&               target,
		const IKSolveContext&         context)
	{
		IKSolveResult result;
		const int N = static_cast<int>(chain.bones.size());
		if (N < 2 || localTransforms.size() != skeleton.bones.size()) return result;
		if (globalTransformsIn.size() != skeleton.bones.size()) return result;
		if (!IK_ValidateChain(skeleton, chain, true)) return result;
		std::vector<glm::mat4> globals = globalTransformsIn;
		const IKTarget modelTarget = IK_ResolveTargetToModelSpace(target, context, globals, skeleton);

		// 初始位置（求解前的全局位置）
		std::vector<glm::vec3> origPos(N);
		std::vector<glm::vec3> pos(N);
		std::vector<float>     boneLengths(N - 1);
		for (int i = 0; i < N; ++i)
		{
			int bi = chain.bones[i].boneIndex;
			if (bi < 0) return result;
			origPos[i] = IK_ExtractTranslation(globalTransformsIn[bi]);
			pos[i] = origPos[i];
		}
		float totalLen = 0.0f;
		for (int i = 0; i < N - 1; ++i)
		{
			boneLengths[i] = glm::distance(origPos[i + 1], origPos[i]);
			if (boneLengths[i] <= 1e-6f) return result;
			totalLen += boneLengths[i];
		}

		std::vector<glm::vec3> localSegmentDirections(N - 1);
		for (int i = 0; i < N - 1; ++i)
		{
			const glm::quat originalRotation = IK_ExtractRotation(globalTransformsIn[chain.bones[i].boneIndex]);
			localSegmentDirections[i] = glm::normalize(
				glm::conjugate(originalRotation) * (origPos[i + 1] - origPos[i]));
		}

		auto applyPositionConstraints = [&]()
		{
			std::vector<glm::quat> passGlobalRotations(N - 1);
			for (int i = 0; i < N - 1; ++i)
			{
				const int boneIndex = chain.bones[i].boneIndex;
				const BoneInfo& bone = skeleton.bones[boneIndex];
				const glm::quat parentRotation = i > 0
					? passGlobalRotations[i - 1]
					: (bone.parentIndex >= 0
						? IK_ExtractRotation(globalTransformsIn[bone.parentIndex])
						: glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
				const glm::quat desiredLocal = ComputeLocalRotationFromDirection(
					pos[i], pos[i + 1], origPos[i + 1] - origPos[i], parentRotation,
					IK_ExtractRotation(globalTransformsIn[boneIndex]));
				const glm::quat constrainedLocal = IK_ApplyJointConstraint(desiredLocal, chain.bones[i].constraint);
				passGlobalRotations[i] = glm::normalize(parentRotation * constrainedLocal);
				const glm::vec3 constrainedDirection = glm::normalize(passGlobalRotations[i] * localSegmentDirections[i]);
				pos[i + 1] = pos[i] + constrainedDirection * boneLengths[i];
			}
		};

		glm::vec3 rootPos = pos[0];
		const float positionWeight = glm::clamp(modelTarget.positionWeight, 0.0f, 1.0f);
		const float rotationWeight = glm::clamp(modelTarget.rotationWeight, 0.0f, 1.0f);
		if (positionWeight < 1e-4f && rotationWeight < 1e-4f)
		{
			result.status = IKSolveStatus::NoEffect;
			result.converged = true;
			return result;
		}
		glm::vec3 targetPos = glm::mix(origPos[N - 1], modelTarget.position, positionWeight);
		float distRootTarget = glm::distance(rootPos, targetPos);

		// 不可达：直接拉直指向目标
		if (distRootTarget > totalLen)
		{
			glm::vec3 dir = (targetPos - rootPos);
			float lD = glm::length(dir);
			if (lD < 1e-6f) return result;
			dir /= lD;
			for (int i = 0; i < N - 1; ++i)
				pos[i + 1] = pos[i] + dir * boneLengths[i];
			applyPositionConstraints();
			result.positionLimited = true;
			result.iterationsUsed = 1;
			result.finalPosError = glm::distance(pos[N - 1], targetPos);
		}
		else
		{
			// FABRIK 迭代
			for (int it = 0; it < chain.maxIterations; ++it)
			{
				// Forward: 末端 → 根
				pos[N - 1] = targetPos;
				for (int i = N - 2; i >= 0; --i)
				{
					glm::vec3 d = pos[i] - pos[i + 1];
					float lD = glm::length(d);
					if (lD < 1e-6f) continue;
					d /= lD;
					pos[i] = pos[i + 1] + d * boneLengths[i];
				}
				// Backward: 根 → 末端
				pos[0] = rootPos;
				for (int i = 0; i < N - 1; ++i)
				{
					glm::vec3 d = pos[i + 1] - pos[i];
					float lD = glm::length(d);
					if (lD < 1e-6f) continue;
					d /= lD;
					pos[i + 1] = pos[i] + d * boneLengths[i];
				}
				applyPositionConstraints();

				float err = glm::distance(pos[N - 1], targetPos);
				result.iterationsUsed = it + 1;
				result.finalPosError  = err;
				if (err < chain.positionTolerance)
				{
					result.converged = true;
					break;
				}
			}
		}

		// 把求解出的全局位置反算回各骨骼的局部旋转
		// 维护一个滚动的全局旋转：parentGlobalRot
		// 求解骨骼 i 的方向 = pos[i+1] - pos[i]，原始方向 = origPos[i+1] - origPos[i]
		std::vector<glm::quat> newGlobalRot(N);
		for (int i = 0; i < N; ++i)
		{
			int bi = chain.bones[i].boneIndex;
			newGlobalRot[i] = IK_ExtractRotation(globalTransformsIn[bi]);
		}

		for (int i = 0; i < N - 1; ++i)
		{
			int bi = chain.bones[i].boneIndex;
			const BoneInfo& bone = skeleton.bones[bi];

			glm::quat parentRot;
			if (i == 0)
			{
				parentRot = (bone.parentIndex >= 0)
					? IK_ExtractRotation(globalTransformsIn[bone.parentIndex])
					: glm::quat(1, 0, 0, 0);
			}
			else
			{
				parentRot = newGlobalRot[i - 1];
			}

			glm::vec3 origDirGlobal = origPos[i + 1] - origPos[i];
			glm::quat origGlobalRot = IK_ExtractRotation(globalTransformsIn[bi]);

			glm::quat newLocalRot = ComputeLocalRotationFromDirection(
				pos[i], pos[i + 1], origDirGlobal, parentRot, origGlobalRot);
			newLocalRot = IK_ApplyJointConstraint(newLocalRot, chain.bones[i].constraint);

			IK_SetRotation(localTransforms[bi], newLocalRot);
			newGlobalRot[i] = glm::normalize(parentRot * newLocalRot);
			IK_UpdateGlobalsForSubtree(bi, localTransforms, globals, skeleton);
		}

		IK_ApplyEffectorRotationTarget(
			localTransforms, globals, skeleton, chain.bones[N - 1].boneIndex, modelTarget);

		const int effectorIndex = chain.bones[N - 1].boneIndex;
		result.finalPosError = glm::distance(IK_ExtractTranslation(globals[effectorIndex]), targetPos);
		if (rotationWeight > 1e-4f)
			result.finalRotError = IK_QuaternionAngularErrorDeg(
				IK_ExtractRotation(globals[effectorIndex]), modelTarget.rotation);
		const bool positionConverged = positionWeight < 1e-4f || result.finalPosError <= chain.positionTolerance;
		const bool rotationConverged = rotationWeight < 1e-4f || result.finalRotError <= chain.rotationTolerance;
		result.converged = positionConverged && rotationConverged;
		result.status = result.converged ? IKSolveStatus::Solved
			: (result.positionLimited ? IKSolveStatus::Unreachable : IKSolveStatus::ReachedLimit);

		return result;
	}

}  // namespace VansGraphics
