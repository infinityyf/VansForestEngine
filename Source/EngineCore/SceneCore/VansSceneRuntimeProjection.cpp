#include "VansSceneRuntimeProjection.h"

#include "../AssetCore/VansAssetDatabase.h"
#include "../AssetCore/VansAssetMeta.h"
#include "../AssetCore/VansMaterialAuthoringAsset.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../AssetCore/Storage/VansAssetMetaStorage.h"
#include "../AssetCore/Storage/VansMaterialAuthoringAssetStorage.h"
#include "../AssetCore/Storage/VansShaderAuthoringAssetStorage.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../Util/VansLog.h"
#include "../ScriptCore/VansPythonScriptComponentReader.h"
#include "VansSceneAnimationComponentReader.h"
#include "VansSceneCameraMediaComponentReader.h"
#include "VansSceneContentBuildPlan.h"
#include "VansSceneEnvironmentNodeConfigReader.h"
#include "VansSceneLightComponentReader.h"
#include "VansSceneParticleComponentReader.h"
#include "VansScenePhysicsComponentReader.h"
#include "VansSceneReflectionProbeConfigReader.h"
#include "VansSceneRenderSettingsConfigReader.h"
#include "VansSceneRuntimeComponentKey.h"
#include "VansSceneSchema.h"
#include "VansSceneVehicleComponentReader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>
#include <system_error>
#include <unordered_map>

namespace Vans
{
namespace
{
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

std::string RuntimeAssetNameFromGuid(const std::string& guid)
{
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
		if (VansAssetMetaStorage::Load(record.metaPath, meta, error) && meta.HasObjectSettings())
		{
			const std::string runtimeName = meta.ReadStringSetting("runtimeName");
			if (!runtimeName.empty())
				return runtimeName;
		}

		if (record.type == VansAssetType::Shader)
		{
			VansShaderAuthoringAsset shader;
			if (VansShaderAuthoringAssetStorage::Load(record.sourcePath, shader, error) && !shader.name.empty())
				return shader.name;
		}
		return guid;
	}
	return guid;
}

std::string ProjectRelativeAssetPathFromGuid(
	const std::string& guidText,
	VansAssetType expectedType,
	const std::string& projectRoot,
	bool leadingSlash)
{
	if (guidText.empty())
		return {};

	VansAssetGuid guid;
	if (!VansAssetGuid::TryParse(guidText, guid))
		return guidText;

	VansAssetDatabase* database = VansProjectManager::Get().GetAssetDatabase();
	if (!database)
		return guidText;

	std::optional<VansAssetRecord> record = database->Find(guid);
	if (!record || record->type != expectedType || record->state == VansAssetState::Missing)
		return guidText;

	if (projectRoot.empty())
		return record->sourcePath.generic_string();

	std::error_code ec;
	std::filesystem::path relative = std::filesystem::relative(
		record->sourcePath,
		std::filesystem::path(projectRoot),
		ec);
	if (ec || relative.empty())
		return record->sourcePath.generic_string();

	std::string result = relative.generic_string();
	if (leadingSlash && !result.empty() && result.front() != '/')
		result.insert(result.begin(), '/');
	return result;
}

std::string RuntimeAssetNameFromReference(const VansSerializedValue& reference)
{
	if (reference.kind == VansSerializedValue::Kind::String)
		return reference.stringValue;
	if (reference.kind != VansSerializedValue::Kind::Object)
		return {};

	const std::string guid = ReadSerializedStringField(reference, "guid");
	return RuntimeAssetNameFromGuid(guid);
}

VansSerializedValue RuntimeShaderAssetFromReference(const VansSerializedValue& reference)
{
	if (reference.kind != VansSerializedValue::Kind::Object)
		return VansSerializedValue::Object({});

	const std::string guidText = ReadSerializedStringField(reference, "guid");
	if (guidText.empty())
		return VansSerializedValue::Object({});

	VansAssetGuid guid;
	if (!VansAssetGuid::TryParse(guidText, guid))
		return VansSerializedValue::Object({});

	auto* database = VansProjectManager::Get().GetAssetDatabase();
	if (!database)
		return VansSerializedValue::Object({});

	std::optional<VansAssetRecord> record = database->Find(guid);
	if (!record || record->type != VansAssetType::Shader)
		return VansSerializedValue::Object({});

	VansShaderAuthoringAsset shader;
	std::string error;
	if (!VansShaderAuthoringAssetStorage::Load(record->sourcePath, shader, error))
	{
		VANS_LOG_ERROR("[SceneRuntimeProjection] Cannot read shader asset: " << record->sourcePath.string() << " (" << error << ")");
		return VansSerializedValue::Object({});
	}
	return shader.root.kind == VansSerializedValue::Kind::Object
		? shader.root
		: VansSerializedValue::Object({});
}

VansSerializedValue SerializedObjectOrEmpty(const VansSerializedValue& value)
{
	return value.kind == VansSerializedValue::Kind::Object
		? value
		: VansSerializedValue::Object({});
}

VansSerializedValue MergeShaderParameterDefaults(
	const VansSerializedValue& shader,
	const VansSerializedValue& materialParameters)
{
	VansSerializedValue merged = VansSerializedValue::Object({});
	if (const VansSerializedValue* parameters = FindObjectField(shader, "parameters");
		parameters && parameters->kind == VansSerializedValue::Kind::Object)
	{
		merged = *parameters;
	}

	if (materialParameters.kind == VansSerializedValue::Kind::Object)
	{
		for (const auto& [name, value] : materialParameters.objectFields)
		{
			VansSerializedValue* existing = FindObjectField(merged, name);
			if (existing && existing->kind == VansSerializedValue::Kind::Object &&
				value.kind == VansSerializedValue::Kind::Object)
			{
				for (const auto& [field, overrideValue] : value.objectFields)
					SetSerializedObjectField(*existing, field, overrideValue);
			}
			else
			{
				SetSerializedObjectField(merged, name, value);
			}
		}
	}
	return merged;
}

VansSerializedValue MergeShaderTextureDefaults(
	const VansSerializedValue& shader,
	const VansSerializedValue& materialTextures)
{
	VansSerializedValue merged = VansSerializedValue::Object({});
	if (const VansSerializedValue* textures = FindObjectField(shader, "textures");
		textures && textures->kind == VansSerializedValue::Kind::Object)
	{
		merged = *textures;
	}

	if (materialTextures.kind == VansSerializedValue::Kind::Object)
	{
		for (const auto& [name, value] : materialTextures.objectFields)
		{
			VansSerializedValue* existing = FindObjectField(merged, name);
			if (!existing || existing->kind != VansSerializedValue::Kind::Object)
			{
				SetSerializedObjectField(merged, name, VansSerializedValue::Object({}));
				existing = FindObjectField(merged, name);
			}
			if (!existing)
				continue;

			if (value.kind == VansSerializedValue::Kind::String)
			{
				SetSerializedObjectField(*existing, "value", value);
			}
			else if (value.kind == VansSerializedValue::Kind::Object && FindObjectField(value, "guid"))
			{
				SetSerializedObjectField(
					*existing,
					"value",
					VansSerializedValue::String(RuntimeAssetNameFromReference(value)));
			}
			else if (value.kind == VansSerializedValue::Kind::Object)
			{
				for (const auto& [field, overrideValue] : value.objectFields)
					SetSerializedObjectField(*existing, field, overrideValue);
			}
		}
	}
	return merged;
}

std::string RuntimeTextureNameFromAssetReference(const VansSerializedValue& reference, const std::string& preferredRoot)
{
	if (reference.kind == VansSerializedValue::Kind::Object && FindObjectField(reference, "guid"))
		return RuntimeAssetNameFromReference(reference);

	if (reference.kind == VansSerializedValue::Kind::String)
	{
		const std::string& textureName = reference.stringValue;
		const std::string resolvedGuid = ResolveTextureGuidFromAlias(textureName, preferredRoot);
		return resolvedGuid.empty() ? textureName : resolvedGuid;
	}
	return {};
}

VansSerializedValue RuntimeMaterialShaderValue(const VansSerializedValue& shader)
{
	if (shader.kind == VansSerializedValue::Kind::String)
		return shader;
	if (shader.kind == VansSerializedValue::Kind::Object && FindObjectField(shader, "guid"))
		return VansSerializedValue::String(RuntimeAssetNameFromReference(shader));
	if (shader.kind == VansSerializedValue::Kind::Object)
		return shader;
	return VansSerializedValue::Null();
}

std::optional<VansSceneMaterialConfig> RuntimeMaterialConfigFromAsset(const VansAssetRecord& record)
{
	VansMaterialAuthoringAsset asset;
	std::string error;
	if (!VansMaterialAuthoringAssetStorage::Load(record.sourcePath, asset, error))
	{
		VANS_LOG_ERROR("[SceneRuntimeProjection] Cannot read material asset: " << record.sourcePath.string() << " (" << error << ")");
		return std::nullopt;
	}

	const std::string materialType = MaterialAuthoringTypeOrDefault(asset.materialType);
	const std::string preferredRoot = asset.preferredImportModel;
	const bool customShaderMaterial = IsCustomShaderMaterialAuthoringType(materialType);
	const VansSerializedValue shaderAsset = asset.shader.IsNull()
		? VansSerializedValue::Object({})
		: RuntimeShaderAssetFromReference(asset.shader);
	VansSerializedValue material = customShaderMaterial
		? VansSerializedValue::Object({})
		: SerializedObjectOrEmpty(asset.parameters);
	SetSerializedObjectField(material, "name", VansSerializedValue::String(record.guid.ToString()));
	SetSerializedObjectField(material, "type", VansSerializedValue::String(materialType));

	if (!asset.shader.IsNull())
	{
		VansSerializedValue shaderValue = RuntimeMaterialShaderValue(asset.shader);
		if (!shaderValue.IsNull())
			SetSerializedObjectField(material, "shader", std::move(shaderValue));
	}

	if (asset.shaderPasses.kind == VansSerializedValue::Kind::Object)
	{
		VansSerializedValue shaderPasses = VansSerializedValue::Object({});
		for (const auto& [passName, shaderRef] : asset.shaderPasses.objectFields)
		{
			if (shaderRef.kind == VansSerializedValue::Kind::String)
				SetSerializedObjectField(shaderPasses, passName, shaderRef);
			else if (shaderRef.kind == VansSerializedValue::Kind::Object && FindObjectField(shaderRef, "guid"))
				SetSerializedObjectField(
					shaderPasses,
					passName,
					VansSerializedValue::String(RuntimeAssetNameFromReference(shaderRef)));
		}
		SetSerializedObjectField(material, "shaderPasses", std::move(shaderPasses));
	}

	if (IsTransparentMaterialAuthoringType(materialType) && asset.textures.kind == VansSerializedValue::Kind::Array)
	{
		std::vector<VansSerializedValue> runtimeTextures;
		for (const VansSerializedValue& entry : asset.textures.arrayItems)
		{
			if (entry.kind != VansSerializedValue::Kind::Object)
				continue;

			const std::string slot = ReadSerializedStringField(entry, "slot");
			std::string textureName;
			if (const VansSerializedValue* texture = FindObjectField(entry, "texture"))
				textureName = RuntimeTextureNameFromAssetReference(*texture, preferredRoot);
			if (!slot.empty() && !textureName.empty())
			{
				runtimeTextures.push_back(VansSerializedValue::Object({
					{ "slot", VansSerializedValue::String(slot) },
					{ "texture", VansSerializedValue::String(textureName) }
				}));
			}
		}
		SetSerializedObjectField(material, "textures", VansSerializedValue::Array(std::move(runtimeTextures)));
	}
	else if (asset.textures.kind == VansSerializedValue::Kind::Object)
	{
		if (customShaderMaterial)
		{
			SetSerializedObjectField(
				material,
				"customTextures",
				MergeShaderTextureDefaults(shaderAsset, asset.textures));
		}
		else
		{
			for (const auto& [slot, reference] : asset.textures.objectFields)
			{
				const std::string textureName = RuntimeTextureNameFromAssetReference(reference, preferredRoot);
				if (!textureName.empty())
					SetSerializedObjectField(
						material,
						slot + "_texture",
						VansSerializedValue::String(textureName));
			}
		}
	}

	if (customShaderMaterial)
	{
		SetSerializedObjectField(
			material,
			"customParameters",
			MergeShaderParameterDefaults(shaderAsset, asset.parameters));
	}
	else if (asset.customParameters.kind == VansSerializedValue::Kind::Object)
	{
		SetSerializedObjectField(material, "customParameters", asset.customParameters);
	}

	if (asset.customTextures.kind == VansSerializedValue::Kind::Object)
	{
		VansSerializedValue& customTextures = EnsureSerializedObjectField(material, "customTextures");

		for (const auto& [slot, reference] : asset.customTextures.objectFields)
		{
			if (reference.kind == VansSerializedValue::Kind::String)
			{
				SetSerializedObjectField(customTextures, slot, reference);
			}
			else if (reference.kind == VansSerializedValue::Kind::Object && FindObjectField(reference, "guid"))
			{
				SetSerializedObjectField(
					customTextures,
					slot,
					VansSerializedValue::String(RuntimeAssetNameFromReference(reference)));
			}
		}
	}

	VansSceneMaterialConfig config;
	config.root = std::move(material);
	return config;
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

bool IsLightComponentType(const std::string& type)
{
	return type == "DirectionalLight" ||
		type == "PointLight" ||
		type == "SpotLight" ||
		type == "RectLight";
}

bool IsCameraMediaComponentType(const std::string& type)
{
	return type == "Camera" || type == "Audio" || type == "Video";
}

bool IsParticleComponentType(const std::string& type)
{
	return type == "Particle";
}

bool IsMultiMeshRootComponentType(const std::string& type)
{
	return type == "MultiMeshRoot";
}

bool IsPhysicsComponentType(const std::string& type)
{
	return type == "Physics" || type == "Cloth" || type == "CharacterController";
}

bool IsVehicleComponentType(const std::string& type)
{
	return type == "Vehicle";
}

bool IsAnimationComponentType(const std::string& type)
{
	return type == "Animation";
}

const VansSerializedValue* FindSerializedObjectField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Object ? field : nullptr;
}

const VansSerializedValue* FindSerializedArrayField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Array ? field : nullptr;
}

const VansSerializedValue* FindComponent(const VansSerializedValue& entity, const char* type)
{
	const VansSerializedValue* components = FindSerializedArrayField(entity, "components");
	if (!components)
		return nullptr;

	for (const VansSerializedValue& component : components->arrayItems)
	{
		if (ReadSerializedStringField(component, "type") == type)
			return &component;
	}
	return nullptr;
}

std::optional<std::uint32_t> ReadSerializedUInt32Field(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	if (!field)
		return std::nullopt;

	if (field->kind == VansSerializedValue::Kind::Int && field->intValue >= 0)
		return static_cast<std::uint32_t>(field->intValue);
	if (field->kind == VansSerializedValue::Kind::Float && field->floatValue >= 0.0)
		return static_cast<std::uint32_t>(field->floatValue);
	return std::nullopt;
}

std::array<float, 3> ReadSerializedFloat3ArrayField(
	const VansSerializedValue& object,
	const char* key,
	const std::array<float, 3>& fallback)
{
	const VansSerializedValue* value = FindObjectField(object, key);
	if (!value || value->kind != VansSerializedValue::Kind::Array || value->arrayItems.size() < 3)
		return fallback;

	return std::array<float, 3>{
		static_cast<float>(ReadSerializedNumber(value->arrayItems[0], fallback[0])),
		static_cast<float>(ReadSerializedNumber(value->arrayItems[1], fallback[1])),
		static_cast<float>(ReadSerializedNumber(value->arrayItems[2], fallback[2]))
	};
}

VansSceneTransformConfig BuildAuthoringObjectTransform(const VansSerializedValue* transformComponent)
{
	VansSceneTransformConfig transform;
	if (!transformComponent || !ReadSerializedBoolField(*transformComponent, "enabled", true))
		return transform;

	const VansSerializedValue* data = FindSerializedObjectField(*transformComponent, "data");
	if (!data)
		return transform;

	transform.position = ReadSerializedFloat3ArrayField(*data, "position", transform.position);
	transform.scale = ReadSerializedFloat3ArrayField(*data, "scale", transform.scale);

	const VansSerializedValue* rotation = FindObjectField(*data, "rotation");
	if (rotation && rotation->kind == VansSerializedValue::Kind::Array && rotation->arrayItems.size() == 4)
	{
		const glm::quat quatRotation(
			static_cast<float>(ReadSerializedNumber(rotation->arrayItems[3], 1.0)),
			static_cast<float>(ReadSerializedNumber(rotation->arrayItems[0], 0.0)),
			static_cast<float>(ReadSerializedNumber(rotation->arrayItems[1], 0.0)),
			static_cast<float>(ReadSerializedNumber(rotation->arrayItems[2], 0.0)));
		const glm::vec3 euler = glm::degrees(glm::eulerAngles(quatRotation));
		transform.rotation = { euler.x, euler.y, euler.z };
	}

	return transform;
}

std::string ReadObjectReferenceGuid(const VansSerializedValue& reference)
{
	if (reference.kind == VansSerializedValue::Kind::String)
		return reference.stringValue;
	return ReadSerializedStringField(reference, "guid");
}

std::string ResolveMaterialOverride(const VansSerializedValue& data)
{
	const VansSerializedValue* overrides = FindSerializedObjectField(data, "materialOverrides");
	if (!overrides)
		return {};

	auto readGuid = [&](const std::string& key) -> std::string
	{
		const VansSerializedValue* binding = FindObjectField(*overrides, key);
		return binding && binding->kind == VansSerializedValue::Kind::Object
			? ReadSerializedStringField(*binding, "guid")
			: std::string{};
	};

	std::string materialGuid = readGuid("default");
	if (!materialGuid.empty())
		return materialGuid;

	materialGuid = readGuid("0");
	if (!materialGuid.empty())
		return materialGuid;

	if (const VansSerializedValue* submesh = FindSerializedObjectField(data, "submesh"))
	{
		const std::string slotName = ReadSerializedStringField(*submesh, "slotName");
		if (!slotName.empty())
		{
			materialGuid = readGuid(slotName);
			if (!materialGuid.empty())
				return materialGuid;
		}
	}

	if (!overrides->objectFields.empty())
	{
		const VansSerializedValue& binding = overrides->objectFields.front().second;
		if (binding.kind == VansSerializedValue::Kind::Object)
			return ReadSerializedStringField(binding, "guid");
	}
	return {};
}

std::unordered_map<std::string, std::string> DecodeSubmeshMaterialOverrides(const VansSerializedValue& data)
{
	std::unordered_map<std::string, std::string> overrides;
	const VansSerializedValue* overrideValue = FindSerializedObjectField(data, "submeshMaterialOverrides");
	if (!overrideValue)
		return overrides;

	for (const auto& [slot, reference] : overrideValue->objectFields)
	{
		const std::string materialGuid = ReadObjectReferenceGuid(reference);
		if (!materialGuid.empty())
			overrides[slot] = materialGuid;
	}
	return overrides;
}

bool TryBuildAuthoringRenderNode(
	const VansSerializedValue& entity,
	const VansSerializedValue* rendererComponent,
	const VansSerializedValue* animationComponent,
	const std::string& entityGuid,
	const std::string& parentEntityGuid,
	VansSceneRenderNodeConfig& outRender,
	bool& outSpecialRenderNode)
{
	outRender = {};
	outSpecialRenderNode = false;

	if (!rendererComponent || !ReadSerializedBoolField(*rendererComponent, "enabled", true))
		return false;

	const VansSerializedValue* data = FindSerializedObjectField(*rendererComponent, "data");
	if (!data)
		return false;

	const VansSerializedValue* model = FindSerializedObjectField(*data, "model");
	const VansSerializedValue* animationData =
		animationComponent && ReadSerializedBoolField(*animationComponent, "enabled", true)
		? FindSerializedObjectField(*animationComponent, "data")
		: nullptr;

	outRender.entityGuid = entityGuid;
	outRender.parentEntityGuid = parentEntityGuid;
	outRender.name = animationData
		? ReadSerializedStringField(*animationData, "name", ReadSerializedStringField(entity, "name"))
		: ReadSerializedStringField(entity, "name");
	outRender.mesh = model ? ReadSerializedStringField(*model, "guid") : std::string{};
	outRender.material = ResolveMaterialOverride(*data);
	outRender.type = ReadSerializedStringField(*data, "renderType", "opaque");
	outRender.rayTracingMode = ReadSerializedStringField(*data, "rayTracingMode", "auto");
	outRender.supportShadow = ReadSerializedBoolField(*data, "castShadows", true);
	outRender.shadowCasterMask = ReadSerializedUInt32Field(*data, "shadowCasterMask").value_or(0xffffffffu);
	outRender.submeshMaterialOverrides = DecodeSubmeshMaterialOverrides(*data);

	if (const VansSerializedValue* submesh = FindSerializedObjectField(*data, "submesh"))
	{
		outRender.submesh = ReadSerializedUInt32Field(*submesh, "index").value_or(0u);
		outRender.submeshSlotName = ReadSerializedStringField(*submesh, "slotName");
	}
	outRender.parent = ReadSerializedStringField(*data, "sourceNode");

	const VansSerializedValue* renderRole = FindObjectField(*data, "renderRole");
	outSpecialRenderNode = renderRole && renderRole->kind == VansSerializedValue::Kind::String;
	return true;
}

void CollectAuthoringRuntimeComponentMetadata(
	const VansSerializedValue& entity,
	VansPythonScriptComponentDescriptors& outPythonScripts,
	std::unordered_map<std::string, std::string>& outComponentGuids)
{
	outPythonScripts.clear();

	const VansSerializedValue* authoringComponents = FindSerializedArrayField(entity, "components");
	if (!authoringComponents)
		return;

	for (const VansSerializedValue& component : authoringComponents->arrayItems)
	{
		const std::string type = ReadSerializedStringField(component, "type");
		if (type == "Transform" || type == "ModelRenderer")
			continue;

		const bool enabled = ReadSerializedBoolField(component, "enabled", true);
		const VansSerializedValue* dataValue = FindObjectField(component, "data");
		const std::string componentGuid = ReadSerializedStringField(component, "id");

		if (type == "Script")
		{
			if (enabled && dataValue)
			{
				VansPythonScriptComponentDescriptor descriptor;
				if (VansPythonScriptComponentReader::TryReadScriptComponent(
					*dataValue,
					componentGuid,
					descriptor))
				{
					outPythonScripts.push_back(std::move(descriptor));
				}
			}
			continue;
		}

		const std::string runtimeKey = RuntimeComponentKey(type);
		if (!componentGuid.empty())
			outComponentGuids[CanonicalRuntimeComponentKeyForName(runtimeKey)] = componentGuid;
	}
}

VansSceneLightComponentConfig ReadAuthoringLightComponents(const VansSerializedValue& entity)
{
	VansSceneLightComponentConfig config;
	const VansSerializedValue* authoringComponents = FindSerializedArrayField(entity, "components");
	if (!authoringComponents)
		return config;

	for (const VansSerializedValue& component : authoringComponents->arrayItems)
	{
		const std::string type = ReadSerializedStringField(component, "type");
		if (!IsLightComponentType(type))
			continue;

		VansSerializedValue emptyData = VansSerializedValue::Object({});
		const VansSerializedValue* data = FindSerializedObjectField(component, "data");
		const VansSerializedValue& lightData = data ? *data : emptyData;
		if (type == "DirectionalLight")
			config.directionalLight = VansSceneLightComponentReader::ReadDirectionalLight(lightData);
		else if (type == "PointLight")
			config.pointLight = VansSceneLightComponentReader::ReadPointLight(lightData);
		else if (type == "SpotLight")
			config.spotLight = VansSceneLightComponentReader::ReadSpotLight(lightData);
		else if (type == "RectLight")
			config.rectLight = VansSceneLightComponentReader::ReadRectLight(lightData);
	}
	return config;
}

std::optional<VansSceneParticleComponentConfig> ReadAuthoringParticleComponent(
	const VansSerializedValue& entity)
{
	const VansSerializedValue* particleComponent = FindComponent(entity, "Particle");
	if (!particleComponent)
		return std::nullopt;

	VansSerializedValue emptyData = VansSerializedValue::Object({});
	const VansSerializedValue* data = FindSerializedObjectField(*particleComponent, "data");
	return VansSceneParticleComponentReader::ReadParticle(data ? *data : emptyData);
}

VansSceneCameraMediaComponentConfig ReadAuthoringCameraMediaComponents(const VansSerializedValue& entity)
{
	VansSceneCameraMediaComponentConfig config;
	const VansSerializedValue* authoringComponents = FindSerializedArrayField(entity, "components");
	if (!authoringComponents)
		return config;

	for (const VansSerializedValue& component : authoringComponents->arrayItems)
	{
		const std::string type = ReadSerializedStringField(component, "type");
		if (!IsCameraMediaComponentType(type))
			continue;

		VansSerializedValue emptyData = VansSerializedValue::Object({});
		const VansSerializedValue* data = FindSerializedObjectField(component, "data");
		const VansSerializedValue& componentData = data ? *data : emptyData;
		if (type == "Camera")
		{
			config.camera = VansSceneCameraMediaComponentReader::ReadCamera(componentData);
		}
		else if (type == "Audio")
		{
			config.audio = VansSceneCameraMediaComponentReader::ReadAudio(
				componentData,
				[](const VansSerializedValue& source) { return RuntimeAssetNameFromReference(source); });
		}
		else if (type == "Video")
		{
			config.video = VansSceneCameraMediaComponentReader::ReadVideo(
				componentData,
				[](const VansSerializedValue& source) { return RuntimeAssetNameFromReference(source); });
		}
	}
	return config;
}

std::optional<VansSceneMultiMeshRootConfig> ReadAuthoringMultiMeshRootComponent(
	const VansSerializedValue& entity)
{
	const VansSerializedValue* rootComponent = FindComponent(entity, "MultiMeshRoot");
	if (!rootComponent)
		return std::nullopt;

	VansSceneMultiMeshRootConfig config;
	if (const VansSerializedValue* data = FindSerializedObjectField(*rootComponent, "data"))
	{
		if (const VansSerializedValue* model = FindSerializedObjectField(*data, "model"))
			config.modelGuid = ReadSerializedStringField(*model, "guid");
	}
	return config;
}

bool AppendAuthoringEntityToContentPlan(
	const VansSerializedValue& entity,
	VansSceneContentBuildPlan& plan,
	const std::string& projectRoot)
{
	if (entity.kind != VansSerializedValue::Kind::Object)
		return true;

	const VansSerializedValue* transformComponent = FindComponent(entity, "Transform");
	const VansSerializedValue* rendererComponent = FindComponent(entity, "ModelRenderer");
	const VansSerializedValue* animationComponent = FindComponent(entity, "Animation");
	const std::string entityGuid = ReadSerializedStringField(entity, "id");
	const VansSerializedValue* parent = FindObjectField(entity, "parent");
	const std::string parentEntityGuid = parent && parent->kind == VansSerializedValue::Kind::String
		? parent->stringValue
		: std::string{};

	VansSceneRenderNodeConfig render;
	bool specialRenderNode = false;
	const bool hasRender = TryBuildAuthoringRenderNode(
		entity,
		rendererComponent,
		animationComponent,
		entityGuid,
		parentEntityGuid,
		render,
		specialRenderNode);

	if (specialRenderNode)
	{
		plan.renderNodes.push_back(std::move(render));
		return true;
	}

	VansSceneObjectBuildConfig objectConfig;
	objectConfig.entityGuid = entityGuid;
	objectConfig.name = ReadSerializedStringField(entity, "name");
	objectConfig.transform = BuildAuthoringObjectTransform(transformComponent);
	if (hasRender)
	{
		objectConfig.render = std::move(render);
		const std::string renderGuid = rendererComponent
			? ReadSerializedStringField(*rendererComponent, "id")
			: std::string{};
		if (!renderGuid.empty())
			objectConfig.componentGuids[CanonicalRuntimeComponentKeyForName("render")] = renderGuid;
	}

	VansPythonScriptComponentDescriptors pythonScripts;
	CollectAuthoringRuntimeComponentMetadata(
		entity,
		pythonScripts,
		objectConfig.componentGuids);
	objectConfig.multiMeshRoot = ReadAuthoringMultiMeshRootComponent(entity);
	objectConfig.physicsComponents = VansScenePhysicsComponentReader::ReadAuthoringComponents(entity);
	if (objectConfig.physicsComponents.cloth && objectConfig.physicsComponents.cloth->profilePath)
	{
		objectConfig.physicsComponents.cloth->profilePath = ProjectRelativeAssetPathFromGuid(
			*objectConfig.physicsComponents.cloth->profilePath,
			VansAssetType::ClothProfile,
			projectRoot,
			true);
	}
	objectConfig.vehicleObject = VansSceneVehicleComponentReader::ReadAuthoringComponents(entity);
	objectConfig.lightComponents = ReadAuthoringLightComponents(entity);
	objectConfig.cameraMediaComponents = ReadAuthoringCameraMediaComponents(entity);
	objectConfig.animation = VansSceneAnimationComponentReader::ReadFromAuthoringEntity(entity);
	if (objectConfig.animation)
	{
		objectConfig.animation->animator = ProjectRelativeAssetPathFromGuid(
			objectConfig.animation->animator,
			VansAssetType::AnimatorController,
			projectRoot,
			true);
		if (objectConfig.animation->ragdoll)
		{
			objectConfig.animation->ragdoll->profile = ProjectRelativeAssetPathFromGuid(
				objectConfig.animation->ragdoll->profile,
				VansAssetType::RagdollProfile,
				projectRoot,
				true);
		}
	}
	objectConfig.particle = ReadAuthoringParticleComponent(entity);
	if (objectConfig.particle)
	{
		objectConfig.particle->assetPath = ProjectRelativeAssetPathFromGuid(
			objectConfig.particle->assetPath,
			VansAssetType::Particle,
			projectRoot,
			false);
	}
	objectConfig.pythonScripts = std::move(pythonScripts);

	plan.objects.objects.push_back(std::move(objectConfig));
	return true;
}
}

bool VansSceneRuntimeProjection::BuildRuntimeSceneContentPlan(
	const VansSerializedValue& sceneRoot,
	const std::string& projectRoot,
	VansSceneContentBuildPlan& outPlan,
	std::string& outError)
{
	outPlan = {};
	outError.clear();

	const VansSerializedValue* entities = FindSerializedArrayField(sceneRoot, "entities");
	if (ReadSerializedIntField(sceneRoot, "schemaVersion", 0) != VansSceneSchemaVersion || !entities)
	{
		outError = "Invalid Scene document";
		return false;
	}

	const VansSerializedValue* settings = FindSerializedObjectField(sceneRoot, "settings");
	if (settings)
	{
		outPlan.renderSettings = VansSceneRenderSettingsConfigReader::Read(*settings);
		outPlan.reflectionProbes = VansSceneReflectionProbeConfigReader::Read(*settings);
	}

	if (VansAssetDatabase* database = VansProjectManager::Get().GetAssetDatabase())
	{
		for (const VansAssetRecord& record : database->All())
		{
			if (record.type != VansAssetType::Material || record.state == VansAssetState::Missing)
				continue;

			std::optional<VansSceneMaterialConfig> material = RuntimeMaterialConfigFromAsset(record);
			if (!material)
				continue;

			outPlan.materials.push_back(std::move(*material));
		}
	}

	outPlan.objects.objects.reserve(entities->arrayItems.size());
	for (const VansSerializedValue& entity : entities->arrayItems)
	{
		if (!AppendAuthoringEntityToContentPlan(entity, outPlan, projectRoot))
		{
			outError = "Invalid Scene entity";
			outPlan = {};
			return false;
		}
	}

	if (settings)
	{
		if (const VansSerializedValue* terrain = FindSerializedObjectField(*settings, "terrain"))
			outPlan.terrain = VansSceneEnvironmentNodeConfigReader::ReadTerrain(*terrain);
		if (const VansSerializedValue* vegetation = FindSerializedObjectField(*settings, "vegetation"))
			outPlan.vegetation = VansSceneEnvironmentNodeConfigReader::ReadVegetation(*vegetation, projectRoot);
		if (const VansSerializedValue* water = FindSerializedObjectField(*settings, "water"))
			outPlan.water = VansSceneEnvironmentNodeConfigReader::ReadWater(*water);
	}

	outPlan.valid = true;
	return true;
}

}
