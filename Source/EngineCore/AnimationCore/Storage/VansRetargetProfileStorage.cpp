#include "VansRetargetProfileStorage.h"

#include "../../AssetCore/Storage/VansJsonFileStorage.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <unordered_set>

namespace VansGraphics
{
	using json = nlohmann::json;

	namespace
	{
		bool RequireFields(const json& value,
			std::initializer_list<const char*> allowed,
			std::initializer_list<const char*> required,
			const std::string& label,
			std::string& error)
		{
			if (!value.is_object())
			{
				error = label + " must be an object";
				return false;
			}
			std::unordered_set<std::string> allowedFields;
			for (const char* field : allowed)
				allowedFields.emplace(field);
			for (const auto& item : value.items())
			{
				if (allowedFields.find(item.key()) == allowedFields.end())
				{
					error = "Unknown " + label + " field '" + item.key() + "'";
					return false;
				}
			}
			for (const char* field : required)
			{
				if (!value.contains(field))
				{
					error = label + " is missing required field '" + field + "'";
					return false;
				}
			}
			return true;
		}

		bool ReadTranslationScale(const json& value,
			VansRetargetProfileAsset& asset,
			std::string& error)
		{
			if (!RequireFields(value, { "mode", "value" }, { "mode" },
				"translationScale", error) || !value["mode"].is_string())
				return false;
			const std::string mode = value["mode"].get<std::string>();
			if (mode == "autoPelvis")
				asset.translationScaleMode = VansRetargetTranslationScaleMode::AutoPelvis;
			else if (mode == "compatibleSkeleton")
				asset.translationScaleMode = VansRetargetTranslationScaleMode::CompatibleSkeleton;
			else if (mode == "explicit")
				asset.translationScaleMode = VansRetargetTranslationScaleMode::Explicit;
			else
			{
				error = "Unknown translationScale mode '" + mode + "'";
				return false;
			}
			if (asset.translationScaleMode == VansRetargetTranslationScaleMode::Explicit)
			{
				if (!value.contains("value") || !value["value"].is_number())
				{
					error = "Explicit translationScale requires numeric value";
					return false;
				}
				asset.explicitTranslationScale = value["value"].get<float>();
				if (!std::isfinite(asset.explicitTranslationScale)
					|| asset.explicitTranslationScale <= 0.0f)
				{
					error = "Explicit translationScale value must be finite and positive";
					return false;
				}
			}
			else if (value.contains("value"))
			{
				error = "translationScale value is only valid for explicit mode";
				return false;
			}
			return true;
		}
	}

	bool VansRetargetProfileStorage::DeserializeFromJsonObject(
		const json& root,
		VansRetargetProfileAsset& asset,
		std::string& error)
	{
		asset = {};
		error.clear();
		try
		{
			if (!RequireFields(root,
				{ "assetKind", "name", "translationScale", "rootAlignment",
				  "targetModelSpaceAlignment", "limbMappings" },
				{ "assetKind", "name", "translationScale", "rootAlignment",
				  "targetModelSpaceAlignment", "limbMappings" },
				"Retarget Profile", error))
				return false;
			if (!root["assetKind"].is_string()
				|| root["assetKind"].get<std::string>() != "retargetProfile"
				|| !root["name"].is_string() || root["name"].get<std::string>().empty()
				|| !root["rootAlignment"].is_string()
				|| !root["targetModelSpaceAlignment"].is_string()
				|| !root["limbMappings"].is_array())
			{
				error = "Retarget Profile canonical field types are invalid";
				return false;
			}
			asset.name = root["name"].get<std::string>();
			if (!ReadTranslationScale(root["translationScale"], asset, error))
				return false;

			const std::string rootAlignment = root["rootAlignment"].get<std::string>();
			if (rootAlignment == "none")
				asset.rootAlignment = VansRetargetRootAlignment::None;
			else if (rootAlignment == "feetToOwner")
				asset.rootAlignment = VansRetargetRootAlignment::FeetToOwner;
			else
			{
				error = "Unknown rootAlignment '" + rootAlignment + "'";
				return false;
			}

			const std::string modelAlignment = root["targetModelSpaceAlignment"].get<std::string>();
			if (modelAlignment == "none")
				asset.targetModelSpaceAlignment = VansRetargetModelSpaceAlignment::None;
			else if (modelAlignment == "sourceBindPose")
				asset.targetModelSpaceAlignment = VansRetargetModelSpaceAlignment::SourceBindPose;
			else
			{
				error = "Unknown targetModelSpaceAlignment '" + modelAlignment + "'";
				return false;
			}

			std::unordered_set<std::string> names;
			std::unordered_set<std::string> targetChains;
			for (const json& value : root["limbMappings"])
			{
				if (!RequireFields(value,
					{ "id", "sourceBones", "targetChain", "positionWeight" },
					{ "id", "sourceBones", "targetChain", "positionWeight" },
					"limbMapping", error)
					|| !value["id"].is_string() || !value["sourceBones"].is_array()
					|| value["sourceBones"].size() != 3 || !value["targetChain"].is_string()
					|| !value["positionWeight"].is_number())
				{
					if (error.empty()) error = "limbMapping canonical field types are invalid";
					return false;
				}
				VansRetargetLimbChainDesc mapping;
				mapping.name = value["id"].get<std::string>();
				mapping.targetChainId = value["targetChain"].get<std::string>();
				for (const json& bone : value["sourceBones"])
					if (!bone.is_string() || bone.get<std::string>().empty())
					{
						error = "limbMapping sourceBones must contain three non-empty strings";
						return false;
					}
				mapping.sourceRoot = value["sourceBones"][0].get<std::string>();
				mapping.sourceMid = value["sourceBones"][1].get<std::string>();
				mapping.sourceTip = value["sourceBones"][2].get<std::string>();
				mapping.positionWeight = value["positionWeight"].get<float>();
				if (mapping.name.empty() || mapping.targetChainId.empty()
					|| !names.insert(mapping.name).second
					|| !targetChains.insert(mapping.targetChainId).second
					|| !std::isfinite(mapping.positionWeight)
					|| mapping.positionWeight <= 0.0f || mapping.positionWeight > 1.0f)
				{
					error = "limbMapping IDs/chains must be unique and positionWeight must be in (0, 1]";
					return false;
				}
				asset.limbChains.push_back(std::move(mapping));
			}
		}
		catch (const json::exception& exception)
		{
			error = exception.what();
			return false;
		}
		return true;
	}

	bool VansRetargetProfileStorage::SerializeToJsonObject(
		const VansRetargetProfileAsset& asset,
		json& root,
		std::string& error)
	{
		error.clear();
		const char* scaleMode = nullptr;
		switch (asset.translationScaleMode)
		{
		case VansRetargetTranslationScaleMode::AutoPelvis: scaleMode = "autoPelvis"; break;
		case VansRetargetTranslationScaleMode::CompatibleSkeleton: scaleMode = "compatibleSkeleton"; break;
		case VansRetargetTranslationScaleMode::Explicit: scaleMode = "explicit"; break;
		default:
			error = "Retarget Profile contains an invalid translationScale mode enum";
			return false;
		}
		const char* rootAlignment = nullptr;
		switch (asset.rootAlignment)
		{
		case VansRetargetRootAlignment::None: rootAlignment = "none"; break;
		case VansRetargetRootAlignment::FeetToOwner: rootAlignment = "feetToOwner"; break;
		default:
			error = "Retarget Profile contains an invalid rootAlignment enum";
			return false;
		}
		const char* modelAlignment = nullptr;
		switch (asset.targetModelSpaceAlignment)
		{
		case VansRetargetModelSpaceAlignment::None: modelAlignment = "none"; break;
		case VansRetargetModelSpaceAlignment::SourceBindPose: modelAlignment = "sourceBindPose"; break;
		default:
			error = "Retarget Profile contains an invalid targetModelSpaceAlignment enum";
			return false;
		}
		json scale = { { "mode", scaleMode } };
		if (asset.translationScaleMode == VansRetargetTranslationScaleMode::Explicit)
			scale["value"] = asset.explicitTranslationScale;
		root = {
			{ "assetKind", "retargetProfile" },
			{ "name", asset.name },
			{ "translationScale", std::move(scale) },
			{ "rootAlignment", rootAlignment },
			{ "targetModelSpaceAlignment", modelAlignment },
			{ "limbMappings", json::array() }
		};
		for (const VansRetargetLimbChainDesc& mapping : asset.limbChains)
		{
			root["limbMappings"].push_back({
				{ "id", mapping.name },
				{ "sourceBones", { mapping.sourceRoot, mapping.sourceMid, mapping.sourceTip } },
				{ "targetChain", mapping.targetChainId },
				{ "positionWeight", mapping.positionWeight }
			});
		}
		VansRetargetProfileAsset verified;
		return DeserializeFromJsonObject(root, verified, error);
	}

	bool VansRetargetProfileStorage::Load(
		const std::filesystem::path& path,
		VansRetargetProfileAsset& asset,
		std::string& error)
	{
		json root;
		return Vans::VansJsonFileStorage::Read(path, root, error)
			&& DeserializeFromJsonObject(root, asset, error);
	}

	bool VansRetargetProfileStorage::SaveAtomic(
		const std::filesystem::path& path,
		const VansRetargetProfileAsset& asset,
		std::string& error)
	{
		json root;
		return SerializeToJsonObject(asset, root, error)
			&& Vans::VansJsonFileStorage::WriteAtomic(path, root, error);
	}
}
