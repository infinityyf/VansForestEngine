#pragma once

#include "VansAnimationTypes.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
	enum class VansBoneMaskRuleMode { Include, Exclude };
	enum class VansBoneMaskFalloff { Constant, Linear, SmoothStep };
	enum class VansBoneMaskDiagnosticSeverity { Warning, Error };

	struct VansBoneMaskBranchRule
	{
		std::string id;
		VansBoneMaskRuleMode mode = VansBoneMaskRuleMode::Include;
		std::string rootBone;
		bool includeDescendants = true;
		int maxDepth = -1;
		float rootWeight = 1.0f;
		float endWeight = 1.0f;
		VansBoneMaskFalloff falloff = VansBoneMaskFalloff::Linear;
	};

	struct VansBoneMaskAsset
	{
		std::string id;
		std::string name;
		std::string previewSkeletonGuid;
		std::string previewSkeletonPathHint;
		float defaultWeight = 0.0f;
		std::vector<VansBoneMaskBranchRule> branchRules;
		std::unordered_map<std::string, float> explicitWeights;
		std::vector<std::string> editorExpandedBones;
	};

	struct VansBoneMaskDiagnostic
	{
		VansBoneMaskDiagnosticSeverity severity = VansBoneMaskDiagnosticSeverity::Warning;
		std::string ruleId;
		std::string message;
	};

	struct VansCompiledBoneMask
	{
		std::string assetId;
		std::uint64_t skeletonSignature = 0;
		std::vector<float> weights;
		std::vector<std::uint32_t> activeBones;
		std::vector<VansBoneMaskDiagnostic> diagnostics;
		float rootWeight = 0.0f;
		bool allZero = true;
		bool allOne = false;
		bool valid = false;
	};

	class VansBoneMaskCompiler
	{
	public:
		static VansCompiledBoneMask Compile(const VansBoneMaskAsset& asset,
		                                    const Skeleton& skeleton);
		static std::uint64_t ComputeSkeletonSignature(const Skeleton& skeleton);
	};
}
