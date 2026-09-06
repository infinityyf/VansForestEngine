#pragma once

#include "../VansAnimationTypes.h"
#include "../Procedural/VansAnimationRig.h"
#include "../Procedural/VansPoseWorkspace.h"
#include "VansRetargetProfile.h"

#include <string>
#include <vector>

namespace VansGraphics
{
	struct VansRetargetRuntimeDesc
	{
		std::string profileAssetGuid;
		std::string sourceModelAssetGuid;
		std::string sourceAnimatorAssetGuid;
		VansRetargetTranslationScaleMode translationScaleMode =
			VansRetargetTranslationScaleMode::AutoPelvis;
		VansRetargetRootAlignment rootAlignment = VansRetargetRootAlignment::None;
		VansRetargetModelSpaceAlignment targetModelSpaceAlignment =
			VansRetargetModelSpaceAlignment::None;
		float translationScale = 1.0f;
		bool debugDraw = false;
		std::vector<VansRetargetLimbChainDesc> limbChains;
	};

	struct VansRetargetBuildStats
	{
		uint32_t sourceBoneCount = 0;
		uint32_t targetBoneCount = 0;
		uint32_t mappedBoneCount = 0;
		uint32_t unmappedTargetBoneCount = 0;
		uint32_t limbChainCount = 0;
		float translationScale = 1.0f;
	};

	class VansRetargetProcessor
	{
	public:
		bool Build(const Skeleton& sourceSkeleton,
		           const Skeleton& targetSkeleton,
		           const VansCompiledAnimationRig& targetRig,
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

		struct CompiledLimbChain
		{
			std::string name;
			int sourceRoot = -1;
			int sourceMid = -1;
			int sourceTip = -1;
			int targetChainIndex = -1;
			float positionWeight = 1.0f;
		};

		std::vector<BoneMapEntry> m_BoneMap;
		std::vector<CompiledLimbChain> m_LimbChains;
		VansCompiledAnimationRig m_TargetRig;
		std::vector<glm::mat4> m_SourceBindModelTransforms;
		std::vector<glm::mat4> m_TargetBindModelTransforms;
		VansRetargetBuildStats m_Stats;
		const Skeleton* m_SourceSkeleton = nullptr;
		const Skeleton* m_TargetSkeleton = nullptr;
		glm::mat4 m_TargetModelSpaceCorrection = glm::mat4(1.0f);
		glm::mat4 m_RootAlignmentCorrection = glm::mat4(1.0f);
		bool m_HasTargetModelSpaceCorrection = false;
		bool m_HasRootAlignmentCorrection = false;
		bool m_Valid = false;
		mutable std::vector<glm::mat4> m_SourceLocalScratch;
		mutable std::vector<glm::mat4> m_TargetLocalScratch;
		mutable std::vector<glm::quat> m_DesiredModelRotationsScratch;
		mutable std::vector<bool> m_MappedTargetBonesScratch;
		mutable std::vector<glm::mat4> m_ResolvedModelScratch;
		mutable std::vector<VansBoneTransform> m_LocalPoseScratch;
		mutable VansPoseWorkspace m_PoseWorkspaceScratch;

		static glm::mat4 ComposeTransform(
			const glm::vec3& translation,
			const glm::quat& rotation,
			const glm::vec3& scale);
		static bool DecomposeTransform(
			const glm::mat4& transform,
			glm::vec3& translation,
			glm::quat& rotation,
			glm::vec3& scale);
		static void BuildLocalFromModel(
			const std::vector<glm::mat4>& modelTransforms,
			const Skeleton& skeleton,
			std::vector<glm::mat4>& outLocalTransforms);
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
