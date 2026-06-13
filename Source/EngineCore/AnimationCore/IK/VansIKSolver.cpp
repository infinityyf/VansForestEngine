#include "VansIKSolver.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>
#include <../../GLM/gtc/matrix_transform.hpp>

namespace VansGraphics
{
	glm::vec3 IK_ExtractTranslation(const glm::mat4& m)
	{
		return glm::vec3(m[3]);
	}

	glm::vec3 IK_ExtractScale(const glm::mat4& m)
	{
		return glm::vec3(
			glm::length(glm::vec3(m[0])),
			glm::length(glm::vec3(m[1])),
			glm::length(glm::vec3(m[2])));
	}

	glm::quat IK_ExtractRotation(const glm::mat4& m)
	{
		// 去除缩放后再转 quat
		glm::vec3 s = IK_ExtractScale(m);
		if (s.x < 1e-6f) s.x = 1.0f;
		if (s.y < 1e-6f) s.y = 1.0f;
		if (s.z < 1e-6f) s.z = 1.0f;

		glm::mat3 r;
		r[0] = glm::vec3(m[0]) / s.x;
		r[1] = glm::vec3(m[1]) / s.y;
		r[2] = glm::vec3(m[2]) / s.z;

		return glm::normalize(glm::quat_cast(r));
	}

	glm::mat4 IK_ComposeMatrix(const glm::vec3& t, const glm::quat& r, const glm::vec3& s)
	{
		glm::mat4 result = glm::mat4_cast(glm::normalize(r));
		result[0] *= s.x;
		result[1] *= s.y;
		result[2] *= s.z;
		result[3] = glm::vec4(t, 1.0f);
		return result;
	}

	void IK_SetRotation(glm::mat4& m, const glm::quat& r)
	{
		glm::vec3 t = IK_ExtractTranslation(m);
		glm::vec3 s = IK_ExtractScale(m);
		m = IK_ComposeMatrix(t, r, s);
	}

	void IK_ApplyEffectorRotationTarget(
		std::vector<glm::mat4>& localTransforms,
		std::vector<glm::mat4>& globalTransforms,
		const Skeleton&         skeleton,
		int                     effectorBoneIdx,
		const IKTarget&         target)
	{
		if (target.rotationWeight < 1e-4f)
			return;
		if (effectorBoneIdx < 0 ||
		    effectorBoneIdx >= static_cast<int>(skeleton.bones.size()) ||
		    effectorBoneIdx >= static_cast<int>(localTransforms.size()) ||
		    effectorBoneIdx >= static_cast<int>(globalTransforms.size()))
			return;

		const BoneInfo& bone = skeleton.bones[effectorBoneIdx];
		glm::quat parentRot = (bone.parentIndex >= 0 &&
		                       bone.parentIndex < static_cast<int>(globalTransforms.size()))
			? IK_ExtractRotation(globalTransforms[bone.parentIndex])
			: glm::quat(1, 0, 0, 0);

		glm::quat desiredGlobal = glm::normalize(target.rotation);
		glm::quat desiredLocal = glm::normalize(glm::conjugate(parentRot) * desiredGlobal);
		glm::quat currentLocal = IK_ExtractRotation(localTransforms[effectorBoneIdx]);
		float w = glm::clamp(target.rotationWeight, 0.0f, 1.0f);

		IK_SetRotation(localTransforms[effectorBoneIdx],
		               glm::normalize(glm::slerp(currentLocal, desiredLocal, w)));
		IK_UpdateGlobalsForSubtree(effectorBoneIdx, localTransforms, globalTransforms, skeleton);
	}

	void IK_UpdateGlobalsForSubtree(
		int                           rootBoneIdx,
		const std::vector<glm::mat4>& localTransforms,
		std::vector<glm::mat4>&       globalTransforms,
		const Skeleton&               skeleton)
	{
		if (rootBoneIdx < 0 || rootBoneIdx >= static_cast<int>(skeleton.bones.size()))
			return;

		// 按拓扑序遍历所有骨骼，仅更新 root 子树
		// 用一个标记数组避免反复检查父链
		const size_t N = skeleton.bones.size();
		std::vector<char> dirty(N, 0);
		dirty[rootBoneIdx] = 1;

		for (int bi : skeleton.topologicalOrder)
		{
			if (bi == rootBoneIdx)
			{
				const BoneInfo& bone = skeleton.bones[bi];
				if (bone.parentIndex >= 0)
					globalTransforms[bi] = globalTransforms[bone.parentIndex] * localTransforms[bi];
				else
					globalTransforms[bi] = localTransforms[bi];
				continue;
			}

			const BoneInfo& bone = skeleton.bones[bi];
			if (bone.parentIndex >= 0 && dirty[bone.parentIndex])
			{
				dirty[bi] = 1;
				globalTransforms[bi] = globalTransforms[bone.parentIndex] * localTransforms[bi];
			}
		}
	}

}  // namespace VansGraphics
