#include "VansSceneAssetDependencyBuilder.h"

#include "../AssetCore/VansAssetMeta.h"
#include "../SceneCore/VansSceneSchema.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Vans
{
namespace
{
	constexpr int SceneTexture2D = 0;
	constexpr int SceneTextureCube = 2;

	std::string ReadStringField(const nlohmann::json& object, const char* key)
	{
		if (!object.is_object())
			return {};
		const auto found = object.find(key);
		return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
	}

	bool ReadBoolField(const nlohmann::json& object, const char* key, bool fallback)
	{
		if (!object.is_object())
			return fallback;
		const auto found = object.find(key);
		return found != object.end() && found->is_boolean() ? found->get<bool>() : fallback;
	}

	float ReadFloatField(const nlohmann::json& object, const char* key, float fallback)
	{
		if (!object.is_object())
			return fallback;
		const auto found = object.find(key);
		return found != object.end() && found->is_number() ? found->get<float>() : fallback;
	}

	const nlohmann::json* ReadObjectField(const nlohmann::json& object, const char* key)
	{
		if (!object.is_object())
			return nullptr;
		const auto found = object.find(key);
		return found != object.end() && found->is_object() ? &(*found) : nullptr;
	}

	const nlohmann::json* ReadArrayField(const nlohmann::json& object, const char* key)
	{
		if (!object.is_object())
			return nullptr;
		const auto found = object.find(key);
		return found != object.end() && found->is_array() ? &(*found) : nullptr;
	}

	std::vector<std::string> ReadShaderMaterialPasses(const nlohmann::json& shader)
	{
		std::vector<std::string> passes;
		const nlohmann::json* passValue = nullptr;
		if (shader.contains("passes"))
			passValue = &shader["passes"];
		else if (shader.contains("materialPasses"))
			passValue = &shader["materialPasses"];
		else if (shader.contains("pass"))
			passValue = &shader["pass"];

		if (passValue == nullptr)
			return passes;

		if (passValue->is_string())
		{
			passes.push_back(passValue->get<std::string>());
		}
		else if (passValue->is_array())
		{
			for (const auto& passName : *passValue)
			{
				if (passName.is_string())
					passes.push_back(passName.get<std::string>());
			}
		}
		else if (passValue->is_object())
		{
			for (const auto& [passName, enabled] : passValue->items())
			{
				if (!enabled.is_boolean() || enabled.get<bool>())
					passes.push_back(passName);
			}
		}

		return passes;
	}

	VansSceneShaderResourceRequest BuildShaderResourceRequest(
		const nlohmann::json& shader,
		const VansAssetRecord& record,
		const std::filesystem::path& projectRoot)
	{
		VansSceneShaderResourceRequest request;
		request.name = shader.value("name", record.guid.ToString());
		request.source = shader.value("source", shader.value("path", std::string{}));
		if (request.source.empty())
		{
			std::error_code relErr;
			request.source = std::filesystem::relative(record.sourcePath.parent_path(), projectRoot, relErr).generic_string();
			if (relErr)
				request.source = record.sourcePath.parent_path().string();
		}

		request.kind = shader.value("kind", shader.value("type", std::string("graphics")));
		if (shader.contains("pushConstantSize") && shader["pushConstantSize"].is_number_integer())
			request.pushConstantSize = shader["pushConstantSize"].get<int>();
		request.depthTest = shader.value("depthTest", true);
		request.depthWrite = shader.value("depthWrite", true);
		request.depthCompare = shader.value("depthCompare", std::string("lessOrEqual"));
		request.cull = shader.value("cull", std::string("back"));
		request.alphaBlend = shader.value("alphaBlend", false);
		request.decalBlend = shader.value("decalBlend", false);
		request.additiveBlend = shader.value("additiveBlend", false);
		request.additiveBlendAttachmentMask = shader.value("additiveBlendAttachmentMask", 0u);
		request.premultipliedAlphaBlend = shader.value("premultipliedAlphaBlend", false);
		request.colorAttachmentCount = shader.value("colorAttachmentCount", -1);
		request.polygonMode = shader.value("polygonMode", std::string("fill"));
		request.frontFace = shader.value("frontFace", std::string("counterClockwise"));
		request.primitiveTopology = shader.value("primitiveTopology", std::string("triangleList"));
		request.patchControlPoints = shader.value("patchControlPoints", 1u);
		request.renderPath = shader.value("renderPath", std::string{});
		request.materialPasses = ReadShaderMaterialPasses(shader);

		if (const nlohmann::json* stages = ReadObjectField(shader, "stages"))
		{
			for (const auto& [stageName, stageFile] : stages->items())
			{
				if (stageFile.is_string())
					request.stages[stageName] = stageFile.get<std::string>();
			}
		}

		auto readStageFile = [&](const char* key)
		{
			const std::string file = ReadStringField(shader, key);
			if (!file.empty())
				request.stages[key] = file;
		};
		readStageFile("vertex");
		readStageFile("fragment");
		readStageFile("compute");
		readStageFile("geometry");
		readStageFile("tessControl");
		readStageFile("tessEval");

		return request;
	}

	const nlohmann::json* ReadVegetationField(const nlohmann::json& sceneDocument)
	{
		if (!sceneDocument.is_object())
			return nullptr;
		auto found = sceneDocument.find("vegetation");
		if (found != sceneDocument.end())
			return &(*found);
		const nlohmann::json* settings = ReadObjectField(sceneDocument, "settings");
		if (settings == nullptr)
			return nullptr;
		found = settings->find("vegetation");
		return found != settings->end() ? &(*found) : nullptr;
	}

	std::string ReadVegetationConfigPath(const nlohmann::json& vegetationData)
	{
		if (vegetationData.is_string())
			return vegetationData.get<std::string>();
		if (!vegetationData.is_object())
			return {};
		std::string path = ReadStringField(vegetationData, "config");
		if (path.empty()) path = ReadStringField(vegetationData, "configPath");
		if (path.empty()) path = ReadStringField(vegetationData, "path");
		return path;
	}

	nlohmann::json LoadVegetationConfigFromReference(const nlohmann::json& vegetationData, const std::string& projectRoot)
	{
		const std::string configPath = ReadVegetationConfigPath(vegetationData);
		if (configPath.empty())
			return vegetationData;

		std::filesystem::path resolved = std::filesystem::path(configPath);
		if (resolved.is_relative())
			resolved = std::filesystem::path(projectRoot) / resolved;

		std::ifstream input(resolved);
		if (!input)
		{
			VANS_LOG_ERROR("[VegetationConfig] Cannot open config file: " << resolved.string());
			return nlohmann::json::object();
		}

		nlohmann::json loaded = nlohmann::json::parse(input, nullptr, false);
		if (loaded.is_discarded() || !loaded.is_object())
		{
			VANS_LOG_ERROR("[VegetationConfig] Invalid JSON config file: " << resolved.string());
			return nlohmann::json::object();
		}
		VANS_LOG("[VegetationConfig] Loaded config file: " << resolved.string());

		if (vegetationData.is_object())
		{
			for (const auto& [key, value] : vegetationData.items())
			{
				if (key == "config" || key == "configPath" || key == "path")
					continue;
				loaded[key] = value;
			}
		}
		return loaded;
	}

	std::string LowerAsciiCopy(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	}

	std::string ResolveTextureGuidFromAlias(
		VansAssetDatabase& database,
		const std::string& textureName,
		const std::string& preferredRoot)
	{
		if (textureName.empty())
			return {};
		VansAssetGuid parsed;
		if (VansAssetGuid::TryParse(textureName, parsed))
			return textureName;

		const std::string wantedStem = LowerAsciiCopy(std::filesystem::path(textureName).stem().string());
		const std::string wantedFile = LowerAsciiCopy(textureName);
		const std::string preferredToken = LowerAsciiCopy(preferredRoot);
		std::string fallbackGuid;
		for (const VansAssetRecord& candidate : database.All())
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

	void CollectMaterialTextureDependencies(
		VansAssetDatabase& database,
		const nlohmann::json& material,
		std::unordered_set<std::string>& requiredTextures)
	{
		const std::string preferredRoot = material.value("importSource", nlohmann::json::object()).value("model", "");
		if (const nlohmann::json* texturesObject = ReadObjectField(material, "textures"))
		{
			for (const auto& [slot, texture] : texturesObject->items())
			{
				std::string textureGuid;
				if (texture.is_object())
					textureGuid = ReadStringField(texture, "guid");
				else if (texture.is_string())
					textureGuid = ResolveTextureGuidFromAlias(database, texture.get<std::string>(), preferredRoot);
				if (!textureGuid.empty())
				{
					if (preferredRoot == "BMW_M4" || material.value("importSource", nlohmann::json::object()).value("generatedFor", "") == "runtimeMultiMeshExpansion")
					{
						VANS_LOG("[MaterialDeps] material=" << material.value("guid", std::string{})
							<< " slot=" << slot
							<< " textureGuid=" << textureGuid
							<< " preferredRoot=" << preferredRoot);
					}
					requiredTextures.insert(textureGuid);
				}
			}
			return;
		}
		if (material.contains("textures") && material["textures"].is_array())
		{
			for (const auto& entry : material["textures"])
			{
				if (!entry.is_object() || !entry.contains("texture"))
					continue;
				const nlohmann::json& texture = entry["texture"];
				std::string textureGuid;
				if (texture.is_object())
					textureGuid = ReadStringField(texture, "guid");
				else if (texture.is_string())
					textureGuid = ResolveTextureGuidFromAlias(database, texture.get<std::string>(), preferredRoot);
				if (!textureGuid.empty())
				{
					if (preferredRoot == "BMW_M4" || material.value("importSource", nlohmann::json::object()).value("generatedFor", "") == "runtimeMultiMeshExpansion")
					{
						VANS_LOG("[MaterialDeps] material=" << material.value("guid", std::string{})
							<< " slot=" << entry.value("slot", std::string{})
							<< " textureGuid=" << textureGuid
							<< " preferredRoot=" << preferredRoot);
					}
					requiredTextures.insert(textureGuid);
				}
			}
		}
	}

	void CollectSceneModelRendererDependencies(
		const nlohmann::json& sceneDocument,
		std::unordered_set<std::string>& requiredModels,
		std::unordered_set<std::string>& requiredMaterials)
	{
		const nlohmann::json* entities = ReadArrayField(sceneDocument, "entities");
		if (entities == nullptr)
			return;

		for (const nlohmann::json& entity : *entities)
		{
			const nlohmann::json* components = ReadArrayField(entity, "components");
			if (components == nullptr)
				continue;
			for (const nlohmann::json& component : *components)
			{
				const std::string componentType = ReadStringField(component, "type");
				if (componentType != "ModelRenderer" && componentType != "MultiMeshRoot")
					continue;
				const nlohmann::json* data = ReadObjectField(component, "data");
				if (data == nullptr)
					continue;
				const nlohmann::json* modelReference = ReadObjectField(*data, "model");
				const std::string model = modelReference ? ReadStringField(*modelReference, "guid") : std::string{};
				if (!model.empty()) requiredModels.insert(model);
				if (componentType != "ModelRenderer")
					continue;
				const nlohmann::json* overrides = ReadObjectField(*data, "materialOverrides");
				if (overrides == nullptr)
					continue;
				for (const auto& [slot, material] : overrides->items())
				{
					const std::string materialGuid = ReadStringField(material, "guid");
					if (!materialGuid.empty()) requiredMaterials.insert(materialGuid);
				}
			}
		}
	}

	void CollectScenePhysicsMeshColliderDependencies(
		const nlohmann::json& sceneDocument,
		std::unordered_set<std::string>& meshColliderModels)
	{
		const nlohmann::json* entities = ReadArrayField(sceneDocument, "entities");
		if (entities == nullptr)
			return;

		for (const nlohmann::json& entity : *entities)
		{
			const nlohmann::json* components = ReadArrayField(entity, "components");
			if (components == nullptr)
				continue;
			for (const nlohmann::json& component : *components)
			{
				if (ReadStringField(component, "type") != "Physics")
					continue;
				const nlohmann::json* data = ReadObjectField(component, "data");
				if (data == nullptr || !ReadBoolField(*data, "useMeshCollider", false))
					continue;

				const std::string colliderType = ReadStringField(*data, "colliderType");
				if (colliderType != "mesh" && colliderType != "convex")
					continue;

				const std::string meshGuid = ReadStringField(*data, "mesh");
				if (!meshGuid.empty())
					meshColliderModels.insert(meshGuid);
			}
		}
	}
}

VansSceneAssetDependencyBuildResult VansSceneAssetDependencyBuilder::BuildResourcePlan(
	VansAssetDatabase& database,
	const std::filesystem::path& scenePath,
	const std::unordered_map<std::string, std::string>& runtimeAssetBindings)
{
	VansSceneAssetDependencyBuildResult result;

	const std::filesystem::path projectRoot = database.AssetsRoot().parent_path();
	for (const auto& [alias, guid] : runtimeAssetBindings)
	{
		if (alias != "fullScreenQuad" && alias != "plane")
			result.requiredModels.insert(guid);
	}

	std::ifstream sceneInput(scenePath);
	nlohmann::json sceneDocument = sceneInput ? nlohmann::json::parse(sceneInput, nullptr, false) : nlohmann::json();
	if (!sceneDocument.is_object() || sceneDocument.value("schemaVersion", 0u) != VansSceneSchemaVersion)
	{
		VANS_LOG_ERROR("[AssetDatabase] Cannot collect Scene dependencies from " << scenePath.string());
		return result;
	}

	VANS_LOG("[AssetDatabase] Collecting dependencies from " << scenePath.string());
	const nlohmann::json* entities = ReadArrayField(sceneDocument, "entities");
	if (entities == nullptr)
	{
		VANS_LOG_ERROR("[AssetDatabase] Scene entities must be an array");
		return result;
	}

	CollectSceneModelRendererDependencies(sceneDocument, result.requiredModels, result.requiredMaterials);
	std::unordered_set<std::string> meshColliderModels;
	CollectScenePhysicsMeshColliderDependencies(sceneDocument, meshColliderModels);
	result.requiredModels.insert(meshColliderModels.begin(), meshColliderModels.end());

	const std::vector<VansAssetRecord> allRecords = database.All();
	std::unordered_map<std::string, VansAssetType> assetTypesByGuid;
	for (const VansAssetRecord& record : allRecords)
		assetTypesByGuid.emplace(record.guid.ToString(), record.type);

	std::function<void(const nlohmann::json&)> collectAssetReferences = [&](const nlohmann::json& value)
	{
		if (value.is_string())
		{
			const auto found = assetTypesByGuid.find(value.get<std::string>());
			if (found == assetTypesByGuid.end()) return;
			switch (found->second)
			{
			case VansAssetType::Model: result.requiredModels.insert(found->first); break;
			case VansAssetType::Texture: result.requiredTextures.insert(found->first); break;
			case VansAssetType::Material: result.requiredMaterials.insert(found->first); break;
			case VansAssetType::Shader: result.requiredShaders.insert(found->first); break;
			default: break;
			}
			return;
		}
		if (value.is_array())
			for (const nlohmann::json& item : value) collectAssetReferences(item);
		else if (value.is_object())
			for (const auto& [key, item] : value.items()) collectAssetReferences(item);
	};

	collectAssetReferences(sceneDocument);
	if (const nlohmann::json* vegetationConfig = ReadVegetationField(sceneDocument))
	{
		nlohmann::json externalVegetationConfig = LoadVegetationConfigFromReference(*vegetationConfig, projectRoot.string());
		if (externalVegetationConfig.is_object())
		{
			collectAssetReferences(externalVegetationConfig);
			VANS_LOG("[AssetDatabase] Collected vegetation dependencies: "
				<< result.requiredModels.size() << " models, "
				<< result.requiredMaterials.size() << " materials, "
				<< result.requiredTextures.size() << " textures");
		}
	}

	for (const VansAssetRecord& record : database.All())
	{
		if (record.type != VansAssetType::Material ||
			result.requiredMaterials.find(record.guid.ToString()) == result.requiredMaterials.end())
			continue;
		std::ifstream materialInput(record.sourcePath);
		const nlohmann::json material = materialInput ? nlohmann::json::parse(materialInput, nullptr, false) : nlohmann::json();
		if (!material.is_object())
		{
			VANS_LOG_ERROR("[AssetDatabase] Cannot read material dependency data: " << record.sourcePath.string());
			continue;
		}
		collectAssetReferences(material);
		CollectMaterialTextureDependencies(database, material, result.requiredTextures);
	}

	for (const VansAssetRecord& record : database.All())
	{
		if (record.state == VansAssetState::Missing)
			continue;

		VansAssetMeta meta;
		std::string metaError;
		if (!VansAssetMeta::Load(record.metaPath, meta, metaError))
		{
			VANS_LOG_ERROR("[AssetDatabase] " << metaError);
			continue;
		}
		if (!meta.settings.is_object())
		{
			VANS_LOG_ERROR("[AssetDatabase] Asset settings must be an object: " << record.metaPath.string());
			continue;
		}

		std::error_code relativeError;
		const std::string relativePath = std::filesystem::relative(
			record.sourcePath, projectRoot, relativeError).generic_string();
		if (relativeError)
		{
			VANS_LOG_ERROR("[AssetDatabase] Cannot make project-relative path: " << record.sourcePath.string());
			continue;
		}

		if (record.type == VansAssetType::Model)
		{
			if (result.requiredModels.find(record.guid.ToString()) == result.requiredModels.end())
				continue;
			const bool isFbx = record.sourcePath.extension() == ".fbx" || record.sourcePath.extension() == ".FBX";
			VansSceneMeshResourceRequest request;
			request.name = record.guid.ToString();
			request.path = relativePath;
			request.needTangent = ReadBoolField(meta.settings, "generateTangents", true);
			request.supportRayTracing = ReadBoolField(meta.settings, "buildRayTracingData", true);
			request.needCpuData = ReadBoolField(meta.settings, "keepCpuMeshData", false) ||
				meshColliderModels.find(record.guid.ToString()) != meshColliderModels.end();
			request.scaleFactor = ReadFloatField(meta.settings, "scaleFactor",
				ReadFloatField(meta.settings, "scale", 1.0f));
			request.loadMultiMesh = ReadBoolField(meta.settings, "loadMultiMesh", isFbx);
			request.rebuildIdentityBoneOffsetsFromHierarchy = ReadBoolField(
				meta.settings, "rebuildIdentityBoneOffsetsFromHierarchy", false);
			request.remapWeaponAttachmentBonesToHands = ReadBoolField(
				meta.settings, "remapWeaponAttachmentBonesToHands", false);
			result.resourcePlan.meshes.push_back(std::move(request));
		}
		else if (record.type == VansAssetType::Texture)
		{
			if (result.requiredTextures.find(record.guid.ToString()) == result.requiredTextures.end())
				continue;
			const bool isCubemap = record.sourcePath.extension() == ".cubemap";
			const std::string texturePath = isCubemap
				? ReadStringField(meta.settings, "sourcePath") : relativePath;
			VansSceneTextureResourceRequest request;
			request.name = record.guid.ToString();
			request.path = texturePath;
			request.artifactPath = record.artifactPath.string();
			request.textureType = isCubemap ? SceneTextureCube : SceneTexture2D;
			const std::string colorSpace = ReadStringField(meta.settings, "colorSpace");
			request.srgb = colorSpace.empty()
				? ReadBoolField(meta.settings, "sRGB", true)
				: colorSpace != "linear";
			request.useCompress = ReadBoolField(meta.settings, "useCompress",
				ReadBoolField(meta.settings, "compress", true));
			request.needMip = ReadBoolField(meta.settings, "needMip",
				ReadBoolField(meta.settings, "generateMip", true));
			const std::string precision = ReadStringField(meta.settings, "precision");
			if (!precision.empty())
				request.precision = precision;
			if (meta.settings.contains("importChannel") && meta.settings["importChannel"].is_number_integer())
				request.importChannel = meta.settings["importChannel"].get<int>();
			const std::string addressMode = ReadStringField(meta.settings, "addressMode");
			if (!addressMode.empty())
				request.addressMode = addressMode;
			result.resourcePlan.textures.push_back(std::move(request));
		}
		else if (record.type == VansAssetType::Shader)
		{
			if (result.requiredShaders.find(record.guid.ToString()) == result.requiredShaders.end())
				continue;
			std::ifstream shaderInput(record.sourcePath);
			nlohmann::json shader = shaderInput ? nlohmann::json::parse(shaderInput, nullptr, false) : nlohmann::json();
			if (!shader.is_object())
			{
				VANS_LOG_ERROR("[AssetDatabase] Cannot read shader asset: " << record.sourcePath.string());
				continue;
			}
			result.resourcePlan.shaders.push_back(BuildShaderResourceRequest(shader, record, projectRoot));
		}
		else if (record.type == VansAssetType::Audio)
		{
			const std::string runtimeName = ReadStringField(meta.settings, "runtimeName");
			VansSceneAudioResourceRequest request;
			request.name = runtimeName.empty() ? record.guid.ToString() : runtimeName;
			request.path = relativePath;
			request.playMode = ReadStringField(meta.settings, "playMode").empty() ? "static" : ReadStringField(meta.settings, "playMode");
			request.loop = ReadBoolField(meta.settings, "loop", false);
			request.autoPlay = ReadBoolField(meta.settings, "autoPlay", false);
			request.volume = meta.settings.value("volume", 1.0f);
			request.pitch = meta.settings.value("pitch", 1.0f);
			request.spatial = ReadBoolField(meta.settings, "spatial", false);
			request.referenceDistance = meta.settings.value("referenceDistance", 1.0f);
			request.maxDistance = meta.settings.value("maxDistance", 100.0f);
			request.rolloff = meta.settings.value("rolloff", 1.0f);
			result.resourcePlan.audios.push_back(std::move(request));
		}
		else if (record.type == VansAssetType::Video)
		{
			const std::string runtimeName = ReadStringField(meta.settings, "runtimeName");
			VansSceneVideoResourceRequest request;
			request.name = runtimeName.empty() ? record.guid.ToString() : runtimeName;
			request.path = relativePath;
			request.loop = ReadBoolField(meta.settings, "loop", true);
			request.autoplay = ReadBoolField(meta.settings, "autoPlay", false);
			request.srgb = ReadBoolField(meta.settings, "sRGB", true);
			result.resourcePlan.videos.push_back(std::move(request));
		}
	}

	result.success = true;
	return result;
}
}
