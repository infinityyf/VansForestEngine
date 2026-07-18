#include "VansSceneRuntimeProjection.h"

#include "../AssetCore/VansAssetDatabase.h"
#include "../AssetCore/VansAssetMeta.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../Util/VansLog.h"
#include "VansSceneSchema.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>
#include <unordered_map>

namespace Vans
{
namespace
{
using json = nlohmann::json;

const json* FindComponent(const json& entity, const char* type)
{
	if (!entity.contains("components") || !entity["components"].is_array())
		return nullptr;

	for (const json& component : entity["components"])
	{
		if (component.value("type", "") == type)
			return &component;
	}
	return nullptr;
}

std::string ReadStringField(const json& object, const char* key)
{
	if (!object.is_object())
		return {};

	const auto found = object.find(key);
	return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

std::string LowerAsciiCopy(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

std::string ResolveTextureGuidFromAlias(const std::string& textureName, const std::string& preferredRoot)
{
	if (textureName.empty())
		return {};

	VansAssetGuid parsed;
	if (VansAssetGuid::TryParse(textureName, parsed))
		return textureName;

	VansAssetDatabase* database = VansProjectManager::Get().GetAssetDatabase();
	if (database == nullptr)
		return {};

	const std::string wantedStem = LowerAsciiCopy(std::filesystem::path(textureName).stem().string());
	const std::string wantedFile = LowerAsciiCopy(textureName);
	const std::string preferredToken = LowerAsciiCopy(preferredRoot);
	std::string fallbackGuid;
	for (const VansAssetRecord& candidate : database->All())
	{
		if (candidate.type != VansAssetType::Texture || candidate.state == VansAssetState::Missing)
			continue;

		const std::string candidateStem = LowerAsciiCopy(candidate.sourcePath.stem().string());
		const std::string candidateFile = LowerAsciiCopy(candidate.sourcePath.filename().string());
		if (candidateStem != wantedStem && candidateFile != wantedFile)
			continue;

		if (fallbackGuid.empty())
			fallbackGuid = candidate.guid.ToString();

		const std::string candidatePath = LowerAsciiCopy(candidate.sourcePath.generic_string());
		if (!preferredToken.empty() && candidatePath.find(preferredToken) != std::string::npos)
			return candidate.guid.ToString();
	}
	return fallbackGuid;
}

std::string RuntimeAssetNameFromReference(const json& reference)
{
	if (reference.is_string())
		return reference.get<std::string>();
	if (!reference.is_object())
		return {};

	const std::string guid = ReadStringField(reference, "guid");
	if (guid.empty())
		return {};

	auto* database = VansProjectManager::Get().GetAssetDatabase();
	if (!database)
		return guid;

	for (const VansAssetRecord& record : database->All())
	{
		if (record.guid.ToString() != guid)
			continue;

		VansAssetMeta meta;
		std::string error;
		if (VansAssetMeta::Load(record.metaPath, meta, error) && meta.settings.is_object())
		{
			const std::string runtimeName = ReadStringField(meta.settings, "runtimeName");
			if (!runtimeName.empty())
				return runtimeName;
		}

		if (record.type == VansAssetType::Shader)
		{
			std::ifstream shaderInput(record.sourcePath);
			const json shader = shaderInput ? json::parse(shaderInput, nullptr, false) : json();
			const std::string shaderName = ReadStringField(shader, "name");
			if (!shaderName.empty())
				return shaderName;
		}
		return guid;
	}
	return guid;
}

json RuntimeShaderAssetFromReference(const json& reference)
{
	if (!reference.is_object())
		return json::object();

	const std::string guidText = ReadStringField(reference, "guid");
	if (guidText.empty())
		return json::object();

	VansAssetGuid guid;
	if (!VansAssetGuid::TryParse(guidText, guid))
		return json::object();

	auto* database = VansProjectManager::Get().GetAssetDatabase();
	if (!database)
		return json::object();

	std::optional<VansAssetRecord> record = database->Find(guid);
	if (!record || record->type != VansAssetType::Shader)
		return json::object();

	std::ifstream shaderInput(record->sourcePath);
	json shader = shaderInput ? json::parse(shaderInput, nullptr, false) : json();
	return shader.is_object() ? shader : json::object();
}

json MergeShaderParameterDefaults(const json& shader, const json& materialParameters)
{
	json merged = shader.contains("parameters") && shader["parameters"].is_object()
		? shader["parameters"]
		: json::object();

	if (materialParameters.is_object())
	{
		for (const auto& [name, value] : materialParameters.items())
		{
			if (merged.contains(name) && merged[name].is_object() && value.is_object())
			{
				for (const auto& [field, overrideValue] : value.items())
					merged[name][field] = overrideValue;
			}
			else
			{
				merged[name] = value;
			}
		}
	}
	return merged;
}

json MergeShaderTextureDefaults(const json& shader, const json& materialTextures)
{
	json merged = shader.contains("textures") && shader["textures"].is_object()
		? shader["textures"]
		: json::object();

	if (materialTextures.is_object())
	{
		for (const auto& [name, value] : materialTextures.items())
		{
			if (!merged.contains(name) || !merged[name].is_object())
				merged[name] = json::object();

			if (value.is_string())
				merged[name]["value"] = value;
			else if (value.is_object() && value.contains("guid"))
				merged[name]["value"] = RuntimeAssetNameFromReference(value);
			else if (value.is_object())
			{
				for (const auto& [field, overrideValue] : value.items())
					merged[name][field] = overrideValue;
			}
		}
	}
	return merged;
}

std::string RuntimeTextureNameFromAssetReference(const json& reference, const std::string& preferredRoot)
{
	if (reference.is_object() && reference.contains("guid"))
		return RuntimeAssetNameFromReference(reference);

	if (reference.is_string())
	{
		const std::string textureName = reference.get<std::string>();
		const std::string resolvedGuid = ResolveTextureGuidFromAlias(textureName, preferredRoot);
		return resolvedGuid.empty() ? textureName : resolvedGuid;
	}
	return {};
}

json RuntimeMaterialFromAsset(const VansAssetRecord& record)
{
	std::ifstream input(record.sourcePath);
	if (!input)
		return {};

	const json asset = json::parse(input, nullptr, false);
	if (asset.is_discarded() || !asset.is_object())
		return {};

	const std::string materialType = asset.value("materialType", "pbr");
	const std::string preferredRoot = asset.value("importSource", json::object()).value("model", "");
	const bool customShaderMaterial = materialType == "customShader" || materialType == "custom";
	const json shaderAsset = asset.contains("shader") ? RuntimeShaderAssetFromReference(asset["shader"]) : json::object();
	json material = customShaderMaterial ? json::object() : asset.value("parameters", json::object());
	material["name"] = record.guid.ToString();
	material["type"] = materialType;

	if (asset.contains("shader"))
	{
		if (asset["shader"].is_string())
			material["shader"] = asset["shader"];
		else if (asset["shader"].is_object() && asset["shader"].contains("guid"))
			material["shader"] = RuntimeAssetNameFromReference(asset["shader"]);
		else if (asset["shader"].is_object())
			material["shader"] = asset["shader"];
	}

	if (asset.contains("shaderPasses") && asset["shaderPasses"].is_object())
	{
		material["shaderPasses"] = json::object();
		for (const auto& [passName, shaderRef] : asset["shaderPasses"].items())
		{
			if (shaderRef.is_string())
				material["shaderPasses"][passName] = shaderRef;
			else if (shaderRef.is_object() && shaderRef.contains("guid"))
				material["shaderPasses"][passName] = RuntimeAssetNameFromReference(shaderRef);
		}
	}

	if (materialType == "transparent" && asset.contains("textures") && asset["textures"].is_array())
	{
		material["textures"] = json::array();
		for (const auto& entry : asset["textures"])
		{
			if (!entry.is_object())
				continue;

			json runtimeEntry;
			runtimeEntry["slot"] = entry.value("slot", "");
			if (entry.contains("texture"))
				runtimeEntry["texture"] = RuntimeTextureNameFromAssetReference(entry["texture"], preferredRoot);
			if (!runtimeEntry.value("slot", "").empty() && runtimeEntry.contains("texture"))
				material["textures"].push_back(std::move(runtimeEntry));
		}
	}
	else if (asset.contains("textures") && asset["textures"].is_object())
	{
		if (customShaderMaterial)
		{
			material["customTextures"] = MergeShaderTextureDefaults(shaderAsset, asset["textures"]);
		}
		else
		{
			for (const auto& [slot, reference] : asset["textures"].items())
			{
				const std::string textureName = RuntimeTextureNameFromAssetReference(reference, preferredRoot);
				if (!textureName.empty())
					material[slot + "_texture"] = textureName;
			}
		}
	}

	if (customShaderMaterial)
	{
		material["customParameters"] = MergeShaderParameterDefaults(
			shaderAsset,
			asset.contains("parameters") ? asset["parameters"] : json::object());
	}
	else if (asset.contains("customParameters") && asset["customParameters"].is_object())
	{
		material["customParameters"] = asset["customParameters"];
	}

	if (asset.contains("customTextures") && asset["customTextures"].is_object())
	{
		if (!material.contains("customTextures") || !material["customTextures"].is_object())
			material["customTextures"] = json::object();

		for (const auto& [slot, reference] : asset["customTextures"].items())
		{
			if (reference.is_string())
				material["customTextures"][slot] = reference;
			else if (reference.is_object() && reference.contains("guid"))
				material["customTextures"][slot] = RuntimeAssetNameFromReference(reference);
		}
	}
	return material;
}

std::string RuntimeComponentKey(const std::string& type)
{
	static const std::unordered_map<std::string, std::string> keys = {
		{ "Physics", "physics" }, { "Camera", "camera" }, { "Animation", "animation" },
		{ "CharacterController", "charController" }, { "DirectionalLight", "directional_light" },
		{ "PointLight", "point_light" }, { "SpotLight", "spot_light" }, { "RectLight", "rect_light" },
		{ "Audio", "audio" }, { "Video", "video" }, { "Particle", "particle" },
		{ "Cloth", "cloth" }, { "Vehicle", "vehicle" }
	};

	const auto found = keys.find(type);
	if (found != keys.end())
		return found->second;

	if (type.empty())
		return {};

	std::string result = type;
	result.front() = static_cast<char>(std::tolower(static_cast<unsigned char>(result.front())));
	return result;
}
}

bool VansSceneRuntimeProjection::BuildRuntimeScene(json& sceneData)
{
	if (sceneData.value("schemaVersion", 0u) != VansSceneSchemaVersion)
		return false;
	if (!sceneData.contains("entities") || !sceneData["entities"].is_array())
		return false;

	json projected = sceneData.value("settings", json::object());
	projected["material"] = json::array();
	if (VansAssetDatabase* database = VansProjectManager::Get().GetAssetDatabase())
	{
		for (const VansAssetRecord& record : database->All())
		{
			if (record.type != VansAssetType::Material || record.state == VansAssetState::Missing)
				continue;

			json material = RuntimeMaterialFromAsset(record);
			if (!material.empty())
				projected["material"].push_back(std::move(material));
		}
	}

	json objects = json::array();
	json renderNodes = json::array();
	auto resolveMaterialOverride = [](const json& data) -> std::string
	{
		if (!data.contains("materialOverrides") || !data["materialOverrides"].is_object())
			return {};

		const json& overrides = data["materialOverrides"];
		auto readGuid = [&](const std::string& key) -> std::string
		{
			if (!overrides.contains(key) || !overrides[key].is_object())
				return {};
			return overrides[key].value("guid", "");
		};

		std::string materialGuid = readGuid("default");
		if (!materialGuid.empty())
			return materialGuid;

		materialGuid = readGuid("0");
		if (!materialGuid.empty())
			return materialGuid;

		if (data.contains("submesh") && data["submesh"].is_object())
		{
			const std::string slotName = data["submesh"].value("slotName", "");
			if (!slotName.empty())
			{
				materialGuid = readGuid(slotName);
				if (!materialGuid.empty())
					return materialGuid;
			}
		}

		if (!overrides.empty())
			return overrides.begin().value().value("guid", "");
		return {};
	};

	for (const json& entity : sceneData["entities"])
	{
		const json* transformComponent = FindComponent(entity, "Transform");
		const json* rendererComponent = FindComponent(entity, "ModelRenderer");
		const json* animationComponent = FindComponent(entity, "Animation");
		const std::string entityGuid = entity.value("id", "");
		const std::string parentEntityGuid = entity.contains("parent") && entity["parent"].is_string()
			? entity["parent"].get<std::string>()
			: std::string{};

		json transform = {
			{ "position", { 0.0f, 0.0f, 0.0f } },
			{ "rotation", { 0.0f, 0.0f, 0.0f } },
			{ "scale", { 1.0f, 1.0f, 1.0f } }
		};
		if (transformComponent && transformComponent->value("enabled", true) && transformComponent->contains("data"))
		{
			const json& data = (*transformComponent)["data"];
			transform["position"] = data.value("position", transform["position"]);
			transform["scale"] = data.value("scale", transform["scale"]);
			if (data.contains("rotation") && data["rotation"].is_array() && data["rotation"].size() == 4)
			{
				const glm::quat rotation(
					data["rotation"][3].get<float>(),
					data["rotation"][0].get<float>(),
					data["rotation"][1].get<float>(),
					data["rotation"][2].get<float>());
				const glm::vec3 euler = glm::degrees(glm::eulerAngles(rotation));
				transform["rotation"] = { euler.x, euler.y, euler.z };
			}
		}

		json runtimeRender;
		bool specialRenderNode = false;
		if (rendererComponent && rendererComponent->value("enabled", true) && rendererComponent->contains("data"))
		{
			const json& data = (*rendererComponent)["data"];
			const std::string modelGuid = data.value("model", json::object()).value("guid", "");
			std::string materialGuid = resolveMaterialOverride(data);
			runtimeRender = {
				{ "entityGuid", entityGuid },
				{ "parentEntityGuid", parentEntityGuid },
				{ "name", animationComponent && animationComponent->value("enabled", true)
					? (*animationComponent)["data"].value("name", entity.value("name", ""))
					: entity.value("name", "") },
				{ "mesh", modelGuid },
				{ "material", materialGuid },
				{ "type", data.value("renderType", "opaque") },
				{ "support_shadow", data.value("castShadows", true) }
			};

			if (data.contains("submesh") && data["submesh"].is_object())
			{
				const json& submesh = data["submesh"];
				runtimeRender["submesh"] = submesh.value("index", 0u);
				runtimeRender["submeshSlotName"] = submesh.value("slotName", "");
				runtimeRender["submeshSourceNode"] = submesh.value("sourceNode", "");
				runtimeRender["submeshSourceMaterial"] = submesh.value("sourceMaterial", "");
			}
			if (data.contains("sourceNode") && data["sourceNode"].is_string())
				runtimeRender["parent"] = data["sourceNode"];
			specialRenderNode = data.contains("renderRole") && data["renderRole"].is_string();
		}

		if (specialRenderNode)
		{
			renderNodes.push_back(std::move(runtimeRender));
			continue;
		}

		json object = {
			{ "entityGuid", entityGuid },
			{ "parentEntityGuid", parentEntityGuid },
			{ "name", entity.value("name", "") },
			{ "transform", std::move(transform) },
			{ "components", json::object() }
		};
		if (!runtimeRender.empty())
			object["components"]["render"] = std::move(runtimeRender);
		object["pyScripts"] = json::array();

		for (const json& component : entity["components"])
		{
			const std::string type = component.value("type", "");
			if (type == "Transform" || type == "ModelRenderer")
				continue;

			if (type == "Script")
			{
				if (component.value("enabled", true))
					object["pyScripts"].push_back(component.value("data", json::object()));
				continue;
			}

			json data = component.value("data", json::object());
			if ((type == "Audio" || type == "Video") && data.contains("source"))
				data["source"] = RuntimeAssetNameFromReference(data["source"]);

			data["enabled"] = component.value("enabled", true);
			object["components"][RuntimeComponentKey(type)] = std::move(data);
		}

		if (object["pyScripts"].empty())
			object.erase("pyScripts");
		objects.push_back(std::move(object));
	}

	projected["scene"] = json::array({ {
		{ "objects", std::move(objects) },
		{ "rendernode", std::move(renderNodes) }
	} });
	sceneData = std::move(projected);
	return true;
}
}
