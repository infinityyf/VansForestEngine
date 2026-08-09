#include "VansBoneMask.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <utility>

namespace VansGraphics
{
	namespace
	{
		struct BoneAtDepth
		{
			int index = -1;
			int depth = 0;
		};

		float RuleWeight(const VansBoneMaskBranchRule& rule, int depth, int deepest)
		{
			if (rule.falloff == VansBoneMaskFalloff::Constant || deepest <= 0)
				return rule.rootWeight;
			float alpha = std::clamp(static_cast<float>(depth) / static_cast<float>(deepest), 0.0f, 1.0f);
			if (rule.falloff == VansBoneMaskFalloff::SmoothStep)
				alpha = alpha * alpha * (3.0f - 2.0f * alpha);
			return glm::mix(rule.rootWeight, rule.endWeight, alpha);
		}

		std::vector<BoneAtDepth> CollectBranch(const Skeleton& skeleton, int root,
		                                       bool descendants, int maxDepth)
		{
			std::vector<BoneAtDepth> result;
			std::queue<BoneAtDepth> pending;
			pending.push({ root, 0 });
			while (!pending.empty())
			{
				const BoneAtDepth item = pending.front();
				pending.pop();
				if (item.index < 0 || item.index >= static_cast<int>(skeleton.bones.size()))
					continue;
				result.push_back(item);
				if (!descendants || (maxDepth >= 0 && item.depth >= maxDepth))
					continue;
				for (int child : skeleton.bones[item.index].children)
					pending.push({ child, item.depth + 1 });
			}
			return result;
		}
	}

	std::uint64_t VansBoneMaskCompiler::ComputeSkeletonSignature(const Skeleton& skeleton)
	{
		std::uint64_t hash = 14695981039346656037ull;
		auto addByte = [&](unsigned char byte)
		{
			hash ^= byte;
			hash *= 1099511628211ull;
		};
		for (size_t index = 0; index < skeleton.bones.size(); ++index)
		{
			for (unsigned char character : skeleton.bones[index].name)
				addByte(character);
			addByte(0xff);
			const std::uint32_t parent = static_cast<std::uint32_t>(skeleton.bones[index].parentIndex + 1);
			for (int shift = 0; shift < 32; shift += 8)
				addByte(static_cast<unsigned char>((parent >> shift) & 0xffu));
		}
		return hash;
	}

	VansCompiledBoneMask VansBoneMaskCompiler::Compile(const VansBoneMaskAsset& asset,
	                                                   const Skeleton& skeleton)
	{
		VansCompiledBoneMask result;
		result.assetId = asset.id;
		result.skeletonSignature = ComputeSkeletonSignature(skeleton);
		result.weights.assign(skeleton.bones.size(), std::clamp(asset.defaultWeight, 0.0f, 1.0f));
		bool hasError = false;

		for (const VansBoneMaskBranchRule& rule : asset.branchRules)
		{
			auto root = skeleton.boneNameToIndex.find(rule.rootBone);
			if (root == skeleton.boneNameToIndex.end()
			    || root->second < 0 || root->second >= static_cast<int>(skeleton.bones.size()))
			{
				const bool error = rule.mode == VansBoneMaskRuleMode::Include;
				result.diagnostics.push_back({
					error ? VansBoneMaskDiagnosticSeverity::Error : VansBoneMaskDiagnosticSeverity::Warning,
					rule.id,
					"Bone mask rule '" + rule.id + "' references missing root bone '" + rule.rootBone + "'"
				});
				hasError = hasError || error;
				continue;
			}
			std::vector<BoneAtDepth> branch = CollectBranch(
				skeleton, root->second, rule.includeDescendants, rule.maxDepth);
			int deepest = 0;
			for (const BoneAtDepth& item : branch)
				deepest = std::max(deepest, item.depth);
			for (const BoneAtDepth& item : branch)
			{
				const float candidate = std::clamp(RuleWeight(rule, item.depth, deepest), 0.0f, 1.0f);
				float& weight = result.weights[static_cast<size_t>(item.index)];
				if (rule.mode == VansBoneMaskRuleMode::Include)
					weight = std::max(weight, candidate);
				else
					weight *= 1.0f - candidate;
			}
		}

		for (const auto& [boneName, explicitWeight] : asset.explicitWeights)
		{
			auto bone = skeleton.boneNameToIndex.find(boneName);
			if (bone == skeleton.boneNameToIndex.end()
			    || bone->second < 0 || bone->second >= static_cast<int>(skeleton.bones.size()))
			{
				result.diagnostics.push_back({ VansBoneMaskDiagnosticSeverity::Warning, {},
					"Bone mask explicit weight references missing bone '" + boneName + "'" });
				continue;
			}
			result.weights[static_cast<size_t>(bone->second)] = std::clamp(explicitWeight, 0.0f, 1.0f);
		}

		int rootIndex = -1;
		for (size_t index = 0; index < skeleton.bones.size(); ++index)
			if (skeleton.bones[index].parentIndex < 0) { rootIndex = static_cast<int>(index); break; }
		result.rootWeight = rootIndex >= 0 ? result.weights[static_cast<size_t>(rootIndex)] : 0.0f;
		result.allZero = true;
		result.allOne = !result.weights.empty();
		for (size_t index = 0; index < result.weights.size(); ++index)
		{
			float& weight = result.weights[index];
			weight = std::clamp(weight, 0.0f, 1.0f);
			if (weight > 1.0e-6f)
			{
				result.allZero = false;
				result.activeBones.push_back(static_cast<std::uint32_t>(index));
			}
			if (weight < 1.0f - 1.0e-6f)
				result.allOne = false;
		}
		if (result.allZero)
			result.diagnostics.push_back({ VansBoneMaskDiagnosticSeverity::Warning, {},
				"Bone mask compiles to all-zero weights" });
		result.valid = !hasError;
		return result;
	}
}
