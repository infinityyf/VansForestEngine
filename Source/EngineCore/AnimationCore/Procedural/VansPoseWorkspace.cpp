#include "VansPoseWorkspace.h"

#include <algorithm>
#include <cmath>

namespace VansGraphics
{
	namespace
	{
		constexpr float kTransformEpsilon = 1.0e-8f;

		bool Finite(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool Finite(const glm::quat& value)
		{
			return std::isfinite(value.w) && std::isfinite(value.x)
				&& std::isfinite(value.y) && std::isfinite(value.z);
		}

		glm::quat SafeQuaternion(const glm::quat& value)
		{
			return Finite(value) && glm::dot(value, value) > kTransformEpsilon
				? glm::normalize(value)
				: glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		}
	}

	bool VansPoseWorkspace::Initialize(
		const Skeleton& skeleton,
		const VansAnimationFrameVector<VansBoneTransform>& localPose)
	{
		return InitializeInternal(skeleton, localPose.data(), localPose.size());
	}

	bool VansPoseWorkspace::Initialize(
		const Skeleton& skeleton,
		const std::vector<VansBoneTransform>& localPose)
	{
		return InitializeInternal(skeleton, localPose.data(), localPose.size());
	}

	bool VansPoseWorkspace::InitializeInternal(
		const Skeleton& skeleton,
		const VansBoneTransform* localPose,
		std::size_t poseSize)
	{
		if (poseSize == 0 || poseSize != skeleton.bones.size() || !localPose)
			return false;
		for (std::size_t index = 0; index < poseSize; ++index)
		{
			if (!IsFiniteTransform(localPose[index]))
				return false;
		}

		m_Skeleton = &skeleton;
		m_LocalPose.assign(localPose, localPose + poseSize);
		m_ComponentPositions.assign(poseSize, glm::vec3(0.0f));
		m_ComponentRotations.assign(poseSize, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
		m_ComponentScales.assign(poseSize, glm::vec3(1.0f));
		m_ComponentDirty.assign(poseSize, true);
		m_DirtyTraversalStack.clear();
		m_DirtyTraversalStack.reserve(poseSize);
		RebuildAllComponents();
		return true;
	}

	bool VansPoseWorkspace::IsValidBone(int boneIndex) const
	{
		return m_Skeleton && boneIndex >= 0
			&& boneIndex < static_cast<int>(m_LocalPose.size());
	}

	const VansBoneTransform& VansPoseWorkspace::GetLocal(int boneIndex) const
	{
		static const VansBoneTransform identity;
		return IsValidBone(boneIndex) ? m_LocalPose[static_cast<std::size_t>(boneIndex)] : identity;
	}

	bool VansPoseWorkspace::SetLocal(int boneIndex, const VansBoneTransform& transform)
	{
		if (!IsValidBone(boneIndex) || !IsFiniteTransform(transform))
			return false;
		VansBoneTransform normalized = transform;
		normalized.rotation = SafeQuaternion(normalized.rotation);
		m_LocalPose[static_cast<std::size_t>(boneIndex)] = normalized;
		MarkSubtreeDirty(boneIndex);
		return true;
	}

	bool VansPoseWorkspace::SetLocalRotation(int boneIndex, const glm::quat& rotation)
	{
		if (!IsValidBone(boneIndex) || !Finite(rotation)
			|| glm::dot(rotation, rotation) <= kTransformEpsilon)
			return false;
		m_LocalPose[static_cast<std::size_t>(boneIndex)].rotation = glm::normalize(rotation);
		MarkSubtreeDirty(boneIndex);
		return true;
	}

	bool VansPoseWorkspace::SetLocalTranslation(int boneIndex, const glm::vec3& translation)
	{
		if (!IsValidBone(boneIndex) || !Finite(translation))
			return false;
		m_LocalPose[static_cast<std::size_t>(boneIndex)].translation = translation;
		MarkSubtreeDirty(boneIndex);
		return true;
	}

	glm::vec3 VansPoseWorkspace::GetComponentPosition(int boneIndex)
	{
		EnsureComponent(boneIndex);
		return IsValidBone(boneIndex)
			? m_ComponentPositions[static_cast<std::size_t>(boneIndex)] : glm::vec3(0.0f);
	}

	glm::quat VansPoseWorkspace::GetComponentRotation(int boneIndex)
	{
		EnsureComponent(boneIndex);
		return IsValidBone(boneIndex)
			? m_ComponentRotations[static_cast<std::size_t>(boneIndex)]
			: glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	}

	glm::vec3 VansPoseWorkspace::GetComponentScale(int boneIndex)
	{
		EnsureComponent(boneIndex);
		return IsValidBone(boneIndex)
			? m_ComponentScales[static_cast<std::size_t>(boneIndex)] : glm::vec3(1.0f);
	}

	bool VansPoseWorkspace::SetComponentRotation(int boneIndex, const glm::quat& rotation)
	{
		if (!IsValidBone(boneIndex) || !Finite(rotation)
			|| glm::dot(rotation, rotation) <= kTransformEpsilon)
			return false;
		const int parent = m_Skeleton->bones[static_cast<std::size_t>(boneIndex)].parentIndex;
		const glm::quat parentRotation = IsValidBone(parent)
			? GetComponentRotation(parent) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		return SetLocalRotation(boneIndex,
			glm::normalize(glm::inverse(parentRotation) * glm::normalize(rotation)));
	}

	bool VansPoseWorkspace::ApplyComponentRotationDelta(int boneIndex, const glm::quat& delta)
	{
		if (!IsValidBone(boneIndex) || !Finite(delta)
			|| glm::dot(delta, delta) <= kTransformEpsilon)
			return false;
		return SetComponentRotation(boneIndex,
			glm::normalize(glm::normalize(delta) * GetComponentRotation(boneIndex)));
	}

	void VansPoseWorkspace::MarkSubtreeDirty(int boneIndex)
	{
		if (!IsValidBone(boneIndex))
			return;
		m_DirtyTraversalStack.clear();
		m_DirtyTraversalStack.push_back(boneIndex);
		while (!m_DirtyTraversalStack.empty())
		{
			const int current = m_DirtyTraversalStack.back();
			m_DirtyTraversalStack.pop_back();
			if (!IsValidBone(current))
				continue;
			m_ComponentDirty[static_cast<std::size_t>(current)] = true;
			for (int child : m_Skeleton->bones[static_cast<std::size_t>(current)].children)
				m_DirtyTraversalStack.push_back(child);
		}
	}

	bool VansPoseWorkspace::IsFinite() const
	{
		return std::all_of(m_LocalPose.begin(), m_LocalPose.end(), IsFiniteTransform);
	}

	void VansPoseWorkspace::Export(VansAnimationFrameVector<VansBoneTransform>& outPose) const
	{
		outPose.assign(m_LocalPose.begin(), m_LocalPose.end());
	}

	void VansPoseWorkspace::EnsureComponent(int boneIndex)
	{
		if (!IsValidBone(boneIndex))
			return;
		const std::size_t index = static_cast<std::size_t>(boneIndex);
		if (!m_ComponentDirty[index])
			return;

		const int parent = m_Skeleton->bones[index].parentIndex;
		if (IsValidBone(parent))
			EnsureComponent(parent);
		const VansBoneTransform& local = m_LocalPose[index];
		if (IsValidBone(parent))
		{
			const std::size_t parentIndex = static_cast<std::size_t>(parent);
			m_ComponentPositions[index] = m_ComponentPositions[parentIndex]
				+ m_ComponentRotations[parentIndex]
				* (m_ComponentScales[parentIndex] * local.translation);
			m_ComponentRotations[index] = glm::normalize(
				m_ComponentRotations[parentIndex] * local.rotation);
			m_ComponentScales[index] = m_ComponentScales[parentIndex] * local.scale;
		}
		else
		{
			m_ComponentPositions[index] = local.translation;
			m_ComponentRotations[index] = SafeQuaternion(local.rotation);
			m_ComponentScales[index] = local.scale;
		}
		m_ComponentDirty[index] = false;
	}

	void VansPoseWorkspace::RebuildAllComponents()
	{
		if (!m_Skeleton)
			return;
		if (!m_Skeleton->topologicalOrder.empty())
		{
			for (int boneIndex : m_Skeleton->topologicalOrder)
				EnsureComponent(boneIndex);
		}
		else
		{
			for (int boneIndex = 0; boneIndex < static_cast<int>(m_LocalPose.size()); ++boneIndex)
				EnsureComponent(boneIndex);
		}
	}

	bool VansPoseWorkspace::IsFiniteTransform(const VansBoneTransform& transform)
	{
		return Finite(transform.translation) && Finite(transform.rotation)
			&& Finite(transform.scale)
			&& glm::dot(transform.rotation, transform.rotation) > kTransformEpsilon;
	}
}
