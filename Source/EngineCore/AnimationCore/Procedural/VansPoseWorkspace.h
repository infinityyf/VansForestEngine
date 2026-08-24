#pragma once

#include "VansProceduralTypes.h"

#include <vector>

namespace VansGraphics
{
	// 程序化动画唯一姿态工作区。Local TRS 是事实源；Component TRS 是按脏子树更新的缓存。
	class VansPoseWorkspace
	{
	public:
		bool Initialize(const Skeleton& skeleton,
		                const VansAnimationFrameVector<VansBoneTransform>& localPose);
		bool Initialize(const Skeleton& skeleton,
		                const std::vector<VansBoneTransform>& localPose);

		const Skeleton* GetSkeleton() const { return m_Skeleton; }
		std::size_t Size() const { return m_LocalPose.size(); }
		bool IsValidBone(int boneIndex) const;

		const VansBoneTransform& GetLocal(int boneIndex) const;
		bool SetLocal(int boneIndex, const VansBoneTransform& transform);
		bool SetLocalRotation(int boneIndex, const glm::quat& rotation);
		bool SetLocalTranslation(int boneIndex, const glm::vec3& translation);

		glm::vec3 GetComponentPosition(int boneIndex);
		glm::quat GetComponentRotation(int boneIndex);
		glm::vec3 GetComponentScale(int boneIndex);
		bool SetComponentRotation(int boneIndex, const glm::quat& rotation);
		bool ApplyComponentRotationDelta(int boneIndex, const glm::quat& delta);

		void MarkSubtreeDirty(int boneIndex);
		bool IsFinite() const;
		const std::vector<VansBoneTransform>& GetLocalPose() const { return m_LocalPose; }
		void Export(VansAnimationFrameVector<VansBoneTransform>& outPose) const;

	private:
		bool InitializeInternal(
			const Skeleton& skeleton,
			const VansBoneTransform* localPose,
			std::size_t poseSize);
		void EnsureComponent(int boneIndex);
		void RebuildAllComponents();
		static bool IsFiniteTransform(const VansBoneTransform& transform);

		const Skeleton* m_Skeleton = nullptr;
		std::vector<VansBoneTransform> m_LocalPose;
		std::vector<glm::vec3> m_ComponentPositions;
		std::vector<glm::quat> m_ComponentRotations;
		std::vector<glm::vec3> m_ComponentScales;
		std::vector<bool> m_ComponentDirty;
		std::vector<int> m_DirtyTraversalStack;
	};
}
