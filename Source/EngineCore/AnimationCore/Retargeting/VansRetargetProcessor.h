#pragma once

#include "../VansAnimationTypes.h"

#include <string>
#include <vector>

namespace VansGraphics
{
	struct VansRetargetRuntimeDesc
	{
		std::string profilePath;
		std::string sourceModelPath;
		std::string sourceAnimatorPath;
		std::string runtimeMode = "source_proxy";
		std::string cachePolicy = "read_or_build";
		std::string translationScaleMode = "auto_from_pelvis_local_translation";
		std::string rootAlignmentMode = "none";
		glm::vec3 modelSpaceRotationDegrees = glm::vec3(0.0f);
		float translationScale = 1.0f;
		bool hasExplicitTranslationScale = false;
		bool debugDraw = false;
	};

	struct VansRetargetBuildStats
	{
		uint32_t sourceBoneCount = 0;
		uint32_t targetBoneCount = 0;
		uint32_t mappedBoneCount = 0;
		uint32_t unmappedTargetBoneCount = 0;
		float translationScale = 1.0f;
	};

	class VansRetargetProcessor
	{
	public:
		bool Build(const Skeleton& sourceSkeleton,
		           const Skeleton& targetSkeleton,
		           const VansRetargetRuntimeDesc& desc = {});
		bool IsValid() const { return m_Valid; }
		const VansRetargetBuildStats& GetStats() const { return m_Stats; }

		bool Process(const std::vector<glm::mat4>& sourceModelTransforms,
		             const Skeleton& sourceSkeleton,
		             const Skeleton& targetSkeleton,
		             std::vector<glm::mat4>& outTargetModelTransforms) const;

	private:
		struct BoneMapEntry
		{
			int sourceIndex = -1;
			int targetIndex = -1;
			bool copyTranslationDelta = false;
		};

		std::vector<BoneMapEntry> m_BoneMap;
		VansRetargetBuildStats m_Stats;
		bool m_Valid = false;

		static glm::mat4 ComposeTransform(
			const glm::vec3& translation,
			const glm::quat& rotation,
			const glm::vec3& scale);
		static bool DecomposeTransform(
			const glm::mat4& transform,
			glm::vec3& translation,
			glm::quat& rotation,
			glm::vec3& scale);
		static std::vector<glm::mat4> BuildLocalFromModel(
			const std::vector<glm::mat4>& modelTransforms,
			const Skeleton& skeleton);
		static void BuildModelFromLocal(
			const std::vector<glm::mat4>& localTransforms,
			const Skeleton& skeleton,
			std::vector<glm::mat4>& outModelTransforms);
		static int FindBone(const Skeleton& skeleton, const char* name);
	};
}
