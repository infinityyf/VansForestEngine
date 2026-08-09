#include "VansIKSolver.h"
#include "../VansAnimationFrameMemory.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>
#include <../../GLM/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

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

	std::vector<glm::mat4> IK_BuildModelSpaceTransforms(
		const Skeleton& skeleton,
		const std::vector<glm::mat4>& localTransforms)
	{
		const size_t count = skeleton.bones.size();
		if (localTransforms.size() != count)
			return {};

		std::vector<glm::mat4> result = localTransforms;
		if (!skeleton.topologicalOrder.empty())
		{
			for (int boneIndex : skeleton.topologicalOrder)
			{
				if (boneIndex < 0 || boneIndex >= static_cast<int>(count))
					continue;
				const int parentIndex = skeleton.bones[boneIndex].parentIndex;
				result[boneIndex] = parentIndex >= 0 && parentIndex < static_cast<int>(count)
					? result[parentIndex] * localTransforms[boneIndex]
					: localTransforms[boneIndex];
			}
			return result;
		}

		// Robust fallback for imported/test skeletons that have not built a
		// topological order. Resolve each parent chain once and reject cycles.
		std::vector<unsigned char> state(count, 0);
		std::function<bool(int)> resolve = [&](int boneIndex)
		{
			if (boneIndex < 0 || boneIndex >= static_cast<int>(count)) return false;
			if (state[boneIndex] == 2) return true;
			if (state[boneIndex] == 1) return false;
			state[boneIndex] = 1;
			const int parentIndex = skeleton.bones[boneIndex].parentIndex;
			if (parentIndex >= 0 && parentIndex < static_cast<int>(count))
			{
				if (!resolve(parentIndex)) return false;
				result[boneIndex] = result[parentIndex] * localTransforms[boneIndex];
			}
			else
			{
				result[boneIndex] = localTransforms[boneIndex];
			}
			state[boneIndex] = 2;
			return true;
		};

		for (int boneIndex = 0; boneIndex < static_cast<int>(count); ++boneIndex)
			if (!resolve(boneIndex)) return {};
		return result;
	}

	static int ResolveReferenceBone(int index, std::string_view name, const Skeleton& skeleton)
	{
		if (index >= 0 && index < static_cast<int>(skeleton.bones.size())) return index;
		if (!name.empty())
		{
			for (std::size_t boneIndex = 0; boneIndex < skeleton.bones.size(); ++boneIndex)
				if (skeleton.bones[boneIndex].name == name)
					return static_cast<int>(boneIndex);
		}
		return -1;
	}

	glm::vec3 IK_ResolvePointToModelSpace(
		const glm::vec3& point,
		IKCoordinateSpace space,
		int referenceBoneIndex,
		std::string_view referenceBoneName,
		const IKSolveContext& context,
		const std::vector<glm::mat4>& modelTransforms,
		const Skeleton& skeleton)
	{
		if (space == IKCoordinateSpace::Model) return point;
		if (space == IKCoordinateSpace::World)
			return glm::vec3(glm::inverse(context.ownerWorldTransform) * glm::vec4(point, 1.0f));

		int referenceIndex = ResolveReferenceBone(referenceBoneIndex, referenceBoneName, skeleton);
		if (space == IKCoordinateSpace::ParentBone && referenceIndex >= 0)
			referenceIndex = skeleton.bones[referenceIndex].parentIndex;
		if (referenceIndex < 0 || referenceIndex >= static_cast<int>(modelTransforms.size()))
			return point;
		return glm::vec3(modelTransforms[referenceIndex] * glm::vec4(point, 1.0f));
	}

	IKTarget IK_ResolveTargetToModelSpace(
		const IKTarget& target,
		const IKSolveContext& context,
		const std::vector<glm::mat4>& modelTransforms,
		const Skeleton& skeleton)
	{
		IKTarget result = target;
		result.position = IK_ResolvePointToModelSpace(
			target.position, target.positionSpace, target.referenceBoneIndex,
			target.referenceBoneName, context, modelTransforms, skeleton);

		glm::quat referenceRotation(1.0f, 0.0f, 0.0f, 0.0f);
		if (target.rotationSpace == IKCoordinateSpace::World)
		{
			referenceRotation = glm::conjugate(IK_ExtractRotation(context.ownerWorldTransform));
		}
		else if (target.rotationSpace == IKCoordinateSpace::Bone ||
		         target.rotationSpace == IKCoordinateSpace::ParentBone)
		{
			int referenceIndex = ResolveReferenceBone(target.referenceBoneIndex, target.referenceBoneName, skeleton);
			if (target.rotationSpace == IKCoordinateSpace::ParentBone && referenceIndex >= 0)
				referenceIndex = skeleton.bones[referenceIndex].parentIndex;
			if (referenceIndex >= 0 && referenceIndex < static_cast<int>(modelTransforms.size()))
				referenceRotation = IK_ExtractRotation(modelTransforms[referenceIndex]);
		}
		result.rotation = glm::normalize(referenceRotation * target.rotation);
		result.positionSpace = IKCoordinateSpace::Model;
		result.rotationSpace = IKCoordinateSpace::Model;
		result.referenceBoneIndex = -1;
		result.referenceBoneName = {};
		return result;
	}

	bool IK_ValidateChain(
		const Skeleton& skeleton,
		const IKChainDefinition& chain,
		bool requireDirectParenting,
		std::string* outError)
	{
		auto fail = [&](const char* message)
		{
			if (outError) *outError = message;
			return false;
		};
		if (chain.bones.size() < 2) return fail("IK chain needs at least two bones");
		for (size_t i = 0; i < chain.bones.size(); ++i)
		{
			const int boneIndex = chain.bones[i].boneIndex;
			if (boneIndex < 0 || boneIndex >= static_cast<int>(skeleton.bones.size()))
				return fail("IK chain contains an unresolved bone");
			if (i == 0) continue;
			const int previous = chain.bones[i - 1].boneIndex;
			int parent = skeleton.bones[boneIndex].parentIndex;
			if (requireDirectParenting)
			{
				if (parent != previous) return fail("IK chain is not directly parented root-to-tip");
			}
			else
			{
				bool ancestorFound = false;
				while (parent >= 0 && parent < static_cast<int>(skeleton.bones.size()))
				{
					if (parent == previous) { ancestorFound = true; break; }
					parent = skeleton.bones[parent].parentIndex;
				}
				if (!ancestorFound) return fail("IK chain is not a root-to-tip hierarchy");
			}
		}
		return true;
	}

	float IK_QuaternionAngularErrorDeg(const glm::quat& a, const glm::quat& b)
	{
		const glm::quat delta = glm::normalize(glm::conjugate(glm::normalize(a)) * glm::normalize(b));
		const float cosine = glm::clamp(std::abs(delta.w), 0.0f, 1.0f);
		return glm::degrees(2.0f * std::acos(cosine));
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
		if (localTransforms.size() != skeleton.bones.size() ||
		    globalTransforms.size() != skeleton.bones.size())
			return;
		if (skeleton.topologicalOrder.empty())
		{
			for (std::size_t boneIndex = 0; boneIndex < skeleton.bones.size(); ++boneIndex)
			{
				const int parentIndex = skeleton.bones[boneIndex].parentIndex;
				globalTransforms[boneIndex] = parentIndex >= 0
					&& parentIndex < static_cast<int>(globalTransforms.size())
					? globalTransforms[static_cast<std::size_t>(parentIndex)] * localTransforms[boneIndex]
					: localTransforms[boneIndex];
			}
			return;
		}

		// 按拓扑序遍历所有骨骼，仅更新 root 子树
		// 用一个标记数组避免反复检查父链
		const size_t N = skeleton.bones.size();
		VansAnimationFrameVector<char> dirty(N, 0);
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
