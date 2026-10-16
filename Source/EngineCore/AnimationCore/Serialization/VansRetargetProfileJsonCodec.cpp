#include "VansRetargetProfileJsonCodec.h"

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

	bool VansRetargetProfileJsonCodec::Decode(
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
				  "targetModelSpaceAlignment", "limbMappings", "controlBones", "boneMappings", "outputRotation" },
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
			if (root.contains("outputRotation"))
			{
				const auto& rotation = root["outputRotation"];
				if (!rotation.is_array() || rotation.size()!=4) { error = "outputRotation requires XYZW"; return false; }
				for (const auto& value : rotation) if (!value.is_number()) { error="outputRotation must be numeric";return false; }
				asset.outputModelRotation={rotation[3].get<float>(),rotation[0].get<float>(),rotation[1].get<float>(),rotation[2].get<float>()};
				const auto& q=asset.outputModelRotation;
				if (!std::isfinite(q.w)||!std::isfinite(q.x)||!std::isfinite(q.y)||!std::isfinite(q.z)
					|| std::abs(glm::length(q)-1.0f)>.0001f) {error="outputRotation must be a finite unit quaternion";return false;}
			}
			if (root.contains("boneMappings"))
			{
				if (!root["boneMappings"].is_array()) { error = "boneMappings must be an array"; return false; }
				std::unordered_set<std::string> targets;
				for (const auto& value : root["boneMappings"])
				{
					if (!RequireFields(value, {"sourceBone", "targetBone", "rotationReferenceBone", "rotationBlend", "copyTranslationDelta"},
						{"sourceBone", "targetBone", "rotationReferenceBone", "rotationBlend", "copyTranslationDelta"}, "boneMapping", error)) return false;
					if (!value["sourceBone"].is_string() || !value["targetBone"].is_string()
						|| !value["rotationReferenceBone"].is_string() || !value["rotationBlend"].is_number()
						|| !value["copyTranslationDelta"].is_boolean()) { error = "Invalid boneMapping field types"; return false; }
					VansRetargetBoneMappingDesc mapping;
					mapping.sourceBone = value["sourceBone"].get<std::string>();
					mapping.targetBone = value["targetBone"].get<std::string>();
					mapping.rotationReferenceBone = value["rotationReferenceBone"].get<std::string>();
					mapping.rotationBlend = value["rotationBlend"].get<float>();
					mapping.copyTranslationDelta = value["copyTranslationDelta"].get<bool>();
					if (mapping.sourceBone.empty() || mapping.targetBone.empty() || !targets.insert(mapping.targetBone).second
						|| !std::isfinite(mapping.rotationBlend) || mapping.rotationBlend < 0 || mapping.rotationBlend > 1
						|| (!mapping.rotationReferenceBone.empty() && mapping.rotationReferenceBone == mapping.sourceBone)
						|| (mapping.rotationReferenceBone.empty() && mapping.rotationBlend != 1.0f))
					{ error = "boneMappings requires unique targets, valid source references and a finite blend in [0,1]"; return false; }
					asset.boneMappings.push_back(std::move(mapping));
				}
			}
			if (root.contains("controlBones"))
			{
				if (!root["controlBones"].is_array())
				{ error = "controlBones must be an array"; return false; }
				std::unordered_set<std::string> bones;
				for (const auto& control : root["controlBones"])
				{
					if (!RequireFields(control, {"bone", "referenceBone"},
						{"bone", "referenceBone"}, "controlBone", error)) return false;
					if (!control["bone"].is_string() || !control["referenceBone"].is_string()
						|| control["bone"].get<std::string>().empty()
						|| control["referenceBone"].get<std::string>().empty()
						|| control["bone"] == control["referenceBone"]
						|| !bones.insert(control["bone"].get<std::string>()).second)
					{ error = "controlBones requires unique bones and distinct non-empty references"; return false; }
					asset.controlBones.push_back({control["bone"].get<std::string>(),
						control["referenceBone"].get<std::string>()});
				}
			}
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
			else if (modelAlignment == "targetBindPose")
				asset.targetModelSpaceAlignment = VansRetargetModelSpaceAlignment::TargetBindPose;
			else
			{
				error = "Unknown targetModelSpaceAlignment '" + modelAlignment + "'";
				return false;
			}
			if (!asset.boneMappings.empty() && asset.targetModelSpaceAlignment == VansRetargetModelSpaceAlignment::None)
			{ error = "Explicit boneMappings requires model-space bind alignment"; return false; }

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

	bool VansRetargetProfileJsonCodec::Encode(
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
		case VansRetargetModelSpaceAlignment::TargetBindPose: modelAlignment = "targetBindPose"; break;
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
		if (!asset.controlBones.empty())
		{
			root["controlBones"] = json::array();
			for (const auto& control : asset.controlBones)
				root["controlBones"].push_back({{"bone", control.bone},
					{"referenceBone", control.referenceBone}});
		}
		if (!asset.boneMappings.empty())
		{
			root["boneMappings"] = json::array();
			for (const auto& mapping : asset.boneMappings)
				root["boneMappings"].push_back({{"sourceBone", mapping.sourceBone}, {"targetBone", mapping.targetBone},
					{"rotationReferenceBone", mapping.rotationReferenceBone}, {"rotationBlend", mapping.rotationBlend},
					{"copyTranslationDelta", mapping.copyTranslationDelta}});
		}
		if (asset.outputModelRotation != glm::quat(1,0,0,0))
			root["outputRotation"]={asset.outputModelRotation.x,asset.outputModelRotation.y,asset.outputModelRotation.z,asset.outputModelRotation.w};
		VansRetargetProfileAsset verified;
		return Decode(root, verified, error);
	}
}
