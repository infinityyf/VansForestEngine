#include "VansBoneMaskStorage.h"

#include "../../AssetCore/VansAssetGuid.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace VansGraphics
{
	using json = nlohmann::json;

	namespace
	{
		bool HasOnlyFields(const json& value,
		                   std::initializer_list<const char*> allowed,
		                   std::string& unknown)
		{
			if (!value.is_object())
				return false;
			std::unordered_set<std::string> names;
			for (const char* name : allowed)
				names.emplace(name);
			for (const auto& item : value.items())
			{
				if (names.find(item.key()) == names.end())
				{
					unknown = item.key();
					return false;
				}
			}
			return true;
		}

		const char* ModeName(VansBoneMaskRuleMode mode)
		{
			return mode == VansBoneMaskRuleMode::Include ? "include" : "exclude";
		}

		const char* FalloffName(VansBoneMaskFalloff falloff)
		{
			switch (falloff)
			{
			case VansBoneMaskFalloff::Constant: return "constant";
			case VansBoneMaskFalloff::SmoothStep: return "smoothStep";
			case VansBoneMaskFalloff::Linear: return "linear";
			}
			return "linear";
		}

		bool ParseMode(const std::string& value, VansBoneMaskRuleMode& mode)
		{
			if (value == "include") { mode = VansBoneMaskRuleMode::Include; return true; }
			if (value == "exclude") { mode = VansBoneMaskRuleMode::Exclude; return true; }
			return false;
		}

		bool ParseFalloff(const std::string& value, VansBoneMaskFalloff& falloff)
		{
			if (value == "constant") { falloff = VansBoneMaskFalloff::Constant; return true; }
			if (value == "linear") { falloff = VansBoneMaskFalloff::Linear; return true; }
			if (value == "smoothStep") { falloff = VansBoneMaskFalloff::SmoothStep; return true; }
			return false;
		}

		bool IsUnitWeight(float value)
		{
			return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
		}
	}

	bool VansBoneMaskStorage::DeserializeFromJsonObject(const json& root,
	                                                   VansBoneMaskAsset& asset,
	                                                   std::string& error)
	{
		asset = {};
		try
		{
			static const std::unordered_set<std::string> allowed = {
				"magic", "id", "name", "previewSkeleton", "defaultWeight",
				"branchRules", "explicitWeights", "editor"
			};
			if (!root.is_object()) { error = "Bone mask root must be an object"; return false; }
			for (const char* forbidden : { "version", "schemaVersion", "formatVersion" })
				if (root.contains(forbidden)) { error = std::string("Forbidden generation field '") + forbidden + "'"; return false; }
			for (const auto& item : root.items())
				if (allowed.find(item.key()) == allowed.end()) { error = "Unknown bone mask field '" + item.key() + "'"; return false; }
			if (root.value("magic", "") != "VBONEMASK" || !root.contains("id") || !root["id"].is_string()
			    || !root.contains("name") || !root["name"].is_string()
			    || !root.contains("branchRules") || !root["branchRules"].is_array()
			    || !root.contains("explicitWeights") || !root["explicitWeights"].is_object())
			{
				error = "Bone mask is missing required canonical fields";
				return false;
			}
			asset.id = root["id"].get<std::string>();
			asset.name = root["name"].get<std::string>();
			if (!root.contains("defaultWeight") || !root["defaultWeight"].is_number())
			{
				error = "Bone mask is missing numeric defaultWeight";
				return false;
			}
			asset.defaultWeight = root["defaultWeight"].get<float>();
			if (asset.id.empty() || asset.name.empty() || !IsUnitWeight(asset.defaultWeight))
			{
				error = "Bone mask identity must be non-empty and defaultWeight must be in [0, 1]";
				return false;
			}
			if (root.contains("previewSkeleton"))
			{
				const json& reference = root["previewSkeleton"];
				if (!reference.is_object()
					|| !reference.contains("guid") || !reference["guid"].is_string()
					|| !reference.contains("pathHint") || !reference["pathHint"].is_string())
				{
					error = "previewSkeleton must contain string guid/pathHint fields";
					return false;
				}
				std::string unknown;
				if (!HasOnlyFields(reference, { "guid", "pathHint" }, unknown))
				{
					error = "Unknown previewSkeleton field '" + unknown + "'";
					return false;
				}
				asset.previewSkeletonGuid = reference["guid"].get<std::string>();
				asset.previewSkeletonPathHint = reference["pathHint"].get<std::string>();
				Vans::VansAssetGuid previewGuid;
				if (!asset.previewSkeletonGuid.empty()
					&& !Vans::VansAssetGuid::TryParse(asset.previewSkeletonGuid, previewGuid))
				{
					error = "previewSkeleton guid must be empty or a valid asset GUID";
					return false;
				}
			}
			std::unordered_set<std::string> ruleIds;
			for (const json& value : root["branchRules"])
			{
				if (!value.is_object()) { error = "Bone mask rule must be an object"; return false; }
				std::string unknown;
				if (!HasOnlyFields(value, { "id", "mode", "rootBone", "includeDescendants",
					"maxDepth", "rootWeight", "endWeight", "falloff" }, unknown))
				{
					error = "Unknown bone mask rule field '" + unknown + "'";
					return false;
				}
				VansBoneMaskBranchRule rule;
				rule.id = value.value("id", "");
				rule.rootBone = value.value("rootBone", "");
				if (rule.id.empty() || rule.rootBone.empty() || !ruleIds.insert(rule.id).second
				    || !ParseMode(value.value("mode", ""), rule.mode)
				    || !ParseFalloff(value.value("falloff", "linear"), rule.falloff))
				{
					error = "Bone mask rule identity or enum is invalid";
					return false;
				}
				rule.includeDescendants = value.value("includeDescendants", true);
				rule.maxDepth = value.value("maxDepth", -1);
				rule.rootWeight = value.value("rootWeight", 1.0f);
				rule.endWeight = value.value("endWeight", 1.0f);
				if (rule.maxDepth < -1 || !IsUnitWeight(rule.rootWeight) || !IsUnitWeight(rule.endWeight))
				{
					error = "Bone mask rule weights must be in [0, 1] and maxDepth must be -1 or greater";
					return false;
				}
				asset.branchRules.push_back(std::move(rule));
			}
			for (const auto& item : root["explicitWeights"].items())
			{
				if (!item.value().is_number()) { error = "Explicit bone weights must be numbers"; return false; }
				const float weight = item.value().get<float>();
				if (item.key().empty() || !IsUnitWeight(weight))
				{
					error = "Explicit bone names must be non-empty and weights must be in [0, 1]";
					return false;
				}
				asset.explicitWeights[item.key()] = weight;
			}
			if (root.contains("editor"))
			{
				const json& editor = root["editor"];
				std::string unknown;
				if (!editor.is_object() || !HasOnlyFields(editor, { "expandedBones" }, unknown)
					|| !editor.contains("expandedBones") || !editor["expandedBones"].is_array())
				{
					error = unknown.empty() ? "Bone mask editor data must contain expandedBones"
						: "Unknown Bone mask editor field '" + unknown + "'";
					return false;
				}
				std::unordered_set<std::string> expandedBones;
				for (const json& bone : editor["expandedBones"])
				{
					if (!bone.is_string() || bone.get<std::string>().empty()
						|| !expandedBones.insert(bone.get<std::string>()).second)
					{
						error = "Bone mask expandedBones must contain unique non-empty names";
						return false;
					}
					asset.editorExpandedBones.push_back(bone.get<std::string>());
				}
			}
		}
		catch (const json::exception& exception)
		{
			error = exception.what();
			return false;
		}
		return true;
	}

	bool VansBoneMaskStorage::SerializeToJsonObject(const VansBoneMaskAsset& asset,
	                                               json& root,
	                                               std::string& error)
	{
		Vans::VansAssetGuid previewGuid;
		if (asset.id.empty() || asset.name.empty()
			|| !IsUnitWeight(asset.defaultWeight)
			|| (!asset.previewSkeletonGuid.empty()
				&& !Vans::VansAssetGuid::TryParse(asset.previewSkeletonGuid, previewGuid)))
		{
			error = "Bone mask identity, preview Skeleton GUID, or default weight is invalid";
			return false;
		}
		root = {
			{ "magic", "VBONEMASK" },
			{ "id", asset.id },
			{ "name", asset.name },
			{ "previewSkeleton", {
				{ "guid", asset.previewSkeletonGuid },
				{ "pathHint", asset.previewSkeletonPathHint }
			} },
			{ "defaultWeight", asset.defaultWeight },
			{ "branchRules", json::array() },
			{ "explicitWeights", json::object() },
			{ "editor", { { "expandedBones", json::array() } } }
		};
		std::unordered_set<std::string> ids;
		for (const VansBoneMaskBranchRule& rule : asset.branchRules)
		{
			if (rule.id.empty() || rule.rootBone.empty() || !ids.insert(rule.id).second
				|| rule.maxDepth < -1 || !IsUnitWeight(rule.rootWeight) || !IsUnitWeight(rule.endWeight))
			{
				error = "Bone mask rules require unique identities, valid roots, depth, and [0, 1] weights";
				return false;
			}
			root["branchRules"].push_back({
				{ "id", rule.id }, { "mode", ModeName(rule.mode) }, { "rootBone", rule.rootBone },
				{ "includeDescendants", rule.includeDescendants }, { "maxDepth", rule.maxDepth },
				{ "rootWeight", rule.rootWeight },
				{ "endWeight", rule.endWeight },
				{ "falloff", FalloffName(rule.falloff) }
			});
		}
		std::vector<std::pair<std::string, float>> explicitWeights(
			asset.explicitWeights.begin(), asset.explicitWeights.end());
		std::sort(explicitWeights.begin(), explicitWeights.end());
		for (const auto& [bone, weight] : explicitWeights)
		{
			if (bone.empty() || !IsUnitWeight(weight))
			{
				error = "Explicit bone names must be non-empty and weights must be in [0, 1]";
				return false;
			}
			root["explicitWeights"][bone] = weight;
		}
		std::vector<std::string> expandedBones = asset.editorExpandedBones;
		std::sort(expandedBones.begin(), expandedBones.end());
		if (std::adjacent_find(expandedBones.begin(), expandedBones.end()) != expandedBones.end()
			|| std::any_of(expandedBones.begin(), expandedBones.end(),
				[](const std::string& bone) { return bone.empty(); }))
		{
			error = "Bone mask expandedBones must contain unique non-empty names";
			return false;
		}
		for (const std::string& bone : expandedBones)
			root["editor"]["expandedBones"].push_back(bone);
		return true;
	}

	bool VansBoneMaskStorage::Load(const std::filesystem::path& path,
	                              VansBoneMaskAsset& asset,
	                              std::string& error)
	{
		json root;
		if (!Vans::VansJsonFileStorage::Read(path, root, error))
			return false;
		return DeserializeFromJsonObject(root, asset, error);
	}

	bool VansBoneMaskStorage::SaveAtomic(const std::filesystem::path& path,
	                                    const VansBoneMaskAsset& asset,
	                                    std::string& error)
	{
		json root;
		if (!SerializeToJsonObject(asset, root, error))
			return false;
		return Vans::VansJsonFileStorage::WriteAtomic(path, root, error);
	}
}
