#pragma once

#include "../VansAnimationTypes.h"

#include <string>
#include <vector>

namespace VansGraphics
{
	struct VansRetargetTwoBoneChainDesc
	{
		std::string name;
		std::string sourceRoot;
		std::string sourceMid;
		std::string sourceTip;
		std::string targetRoot;
		std::string targetMid;
		std::string targetTip;
		float positionWeight = 1.0f;
	};

	struct VansRetargetRuntimeDesc
	{
		std::string profilePath;
		std::string sourceModelPath;
		std::string sourceAnimatorPath;
		std::string runtimeMode = "source_proxy";
		std::string cachePolicy = "read_or_build";
		std::string translationScaleMode = "auto_from_pelvis_local_translation";
		std::string rootAlignmentMode = "none";
		std::string targetModelSpaceAlignmentMode = "none";
		float translationScale = 1.0f;
		bool hasExplicitTranslationScale = false;
		bool debugDraw = false;
		std::vector<VansRetargetTwoBoneChainDesc> twoBoneChains;
	};

	struct VansRetargetBuildStats
	{
		uint32_t sourceBoneCount = 0;
		uint32_t targetBoneCount = 0;
		uint32_t mappedBoneCount = 0;
		uint32_t unmappedTargetBoneCount = 0;
		uint32_t twoBoneChainCount = 0;
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

		struct CompiledTwoBoneChain
		{
			std::string name;
			int sourceRoot = -1;
			int sourceMid = -1;
			int sourceTip = -1;
			int targetRoot = -1;
			int targetMid = -1;
			int targetTip = -1;
			float positionWeight = 1.0f;
		};

		std::vector<BoneMapEntry> m_BoneMap;
		std::vector<CompiledTwoBoneChain> m_TwoBoneChains;
		std::vector<glm::mat4> m_SourceBindModelTransforms;
		std::vector<glm::mat4> m_TargetBindModelTransforms;
		VansRetargetBuildStats m_Stats;
		glm::mat4 m_TargetModelSpaceCorrection = glm::mat4(1.0f);
		bool m_HasTargetModelSpaceCorrection = false;
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
		static std::vector<glm::mat4> BuildBindModelTransforms(const Skeleton& skeleton);
		static bool TryBuildHumanoidBasis(
			const Skeleton& skeleton,
			const std::vector<glm::mat4>& modelTransforms,
			glm::mat3& outBasis);
		static int FindBone(const Skeleton& skeleton, const char* name);
	};
}
