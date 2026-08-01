#include "VansSceneAssetDependencyBuilder.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../AssetCore/Serialization/VansSerializedValueLegacyJsonAdapter.h"
#include "../AssetCore/Storage/VansMaterialAuthoringAssetStorage.h"
#include "../AssetCore/VansAssetMeta.h"
#include "../AssetCore/VansBuiltInAssetCatalog.h"
#include "../AssetCore/VansMaterialAuthoringAsset.h"
#include "../AssetCore/VansShaderAuthoringAsset.h"
#include "../AssetCore/Storage/VansAssetMetaStorage.h"
#include "../AssetCore/Storage/VansJsonFileStorage.h"
#include "../AssetCore/Storage/VansShaderAuthoringAssetStorage.h"
#include "../SceneCore/VansSceneDocumentLoader.h"
#include "../SceneCore/VansSceneSchema.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <vector>

namespace Vans
{
namespace
{
	constexpr int SceneTexture2D = 0;
	constexpr int SceneTextureCube = 2;

	VansSerializedValue EmptyObject()
	{
		return VansSerializedValue::Object({});
	}

	VansSerializedValue LoadSerializedJsonFileOrEmpty(const std::filesystem::path& path, const char* context)
	{
		nlohmann::json root;
		std::string error;
		if (!VansJsonFileStorage::Read(path, root, error))
		{
			VANS_LOG_ERROR("[" << context << "] Cannot read JSON file: " << path.string() << " (" << error << ")");
			return EmptyObject();
		}
		if (!root.is_object())
			return EmptyObject();
		return DecodeSerializedValueLegacyJson(root);
	}

	VansSerializedValue LoadSceneDocumentOrEmpty(const std::filesystem::path& path)
	{
		SceneDocumentLoadResult loadResult = VansSceneDocumentLoader::Load(path);
		if (!loadResult)
		{
			VANS_LOG_ERROR("[AssetDatabase] Cannot read Scene document: " << path.string());
			for (const SceneDiagnostic& diagnostic : loadResult.diagnostics)
			{
				if (!diagnostic.message.empty())
					VANS_LOG_ERROR("[AssetDatabase] " << diagnostic.message);
			}
			return EmptyObject();
		}
		return loadResult.document->SerializedRootSnapshot();
	}

	const VansSerializedValue* ReadSerializedObjectField(const VansSerializedValue& object, const char* key)
	{
		const VansSerializedValue* field = FindObjectField(object, key);
		return field != nullptr && field->kind == VansSerializedValue::Kind::Object ? field : nullptr;
	}

	const VansSerializedValue* ReadSerializedArrayField(const VansSerializedValue& object, const char* key)
	{
		const VansSerializedValue* field = FindObjectField(object, key);
		return field != nullptr && field->kind == VansSerializedValue::Kind::Array ? field : nullptr;
	}

	std::string ReadAssetGuidReference(const VansSerializedValue& value)
	{
		if (value.kind == VansSerializedValue::Kind::String)
			return value.stringValue;
		if (value.kind == VansSerializedValue::Kind::Object)
			return ReadSerializedStringField(value, "guid");
		return {};
	}

	std::vector<std::string> ReadShaderMaterialPasses(const VansSerializedValue& shader)
	{
		std::vector<std::string> passes;
		const VansSerializedValue* passValue = FindObjectField(shader, "passes");
		if (passValue == nullptr)
			passValue = FindObjectField(shader, "materialPasses");
		if (passValue == nullptr)
			passValue = FindObjectField(shader, "pass");

		if (passValue == nullptr)
			return passes;

		if (passValue->kind == VansSerializedValue::Kind::String)
		{
			passes.push_back(passValue->stringValue);
		}
		else if (passValue->kind == VansSerializedValue::Kind::Array)
		{
			for (const VansSerializedValue& passName : passValue->arrayItems)
			{
				if (passName.kind == VansSerializedValue::Kind::String)
					passes.push_back(passName.stringValue);
			}
		}
		else if (passValue->kind == VansSerializedValue::Kind::Object)
		{
			for (const auto& [passName, enabled] : passValue->objectFields)
			{
				if (enabled.kind != VansSerializedValue::Kind::Bool || enabled.boolValue)
					passes.push_back(passName);
			}
		}

		return passes;
	}

	VansSceneShaderResourceRequest BuildShaderResourceRequest(
		const VansShaderAuthoringAsset& shader,
		const VansAssetRecord& record,
		const std::filesystem::path& projectRoot)
	{
		const VansSerializedValue& root = shader.root;

		VansSceneShaderResourceRequest request;
		request.name = shader.name.empty() ? record.guid.ToString() : shader.name;
		request.assetGuid = record.guid.ToString();
		request.source = ReadSerializedStringField(root, "source");
		if (request.source.empty())
			request.source = ReadSerializedStringField(root, "path");
		if (request.source.empty())
		{
			std::error_code relErr;
			request.source = std::filesystem::relative(record.sourcePath.parent_path(), projectRoot, relErr).generic_string();
			if (relErr)
				request.source = record.sourcePath.parent_path().string();
		}

		request.kind = ReadSerializedStringField(root, "kind", ReadSerializedStringField(root, "type", "graphics"));
		if (const VansSerializedValue* pushConstantSize = FindObjectField(root, "pushConstantSize"))
			request.pushConstantSize = static_cast<int>(ReadSerializedInt(*pushConstantSize, request.pushConstantSize));
		request.depthTest = ReadSerializedBoolField(root, "depthTest", true);
		request.depthWrite = ReadSerializedBoolField(root, "depthWrite", true);
		request.depthCompare = ReadSerializedStringField(root, "depthCompare", "lessOrEqual");
		request.cull = ReadSerializedStringField(root, "cull", "back");
		request.alphaBlend = ReadSerializedBoolField(root, "alphaBlend", false);
		request.decalBlend = ReadSerializedBoolField(root, "decalBlend", false);
		request.additiveBlend = ReadSerializedBoolField(root, "additiveBlend", false);
		request.additiveBlendAttachmentMask = static_cast<unsigned int>(
			ReadSerializedIntField(root, "additiveBlendAttachmentMask", 0));
		request.premultipliedAlphaBlend = ReadSerializedBoolField(root, "premultipliedAlphaBlend", false);
		request.colorAttachmentCount = static_cast<int>(ReadSerializedIntField(root, "colorAttachmentCount", -1));
		request.polygonMode = ReadSerializedStringField(root, "polygonMode", "fill");
		request.frontFace = ReadSerializedStringField(root, "frontFace", "counterClockwise");
		request.primitiveTopology = ReadSerializedStringField(root, "primitiveTopology", "triangleList");
		request.patchControlPoints = static_cast<unsigned int>(ReadSerializedIntField(root, "patchControlPoints", 1));
		request.renderPath = ReadSerializedStringField(root, "renderPath");
		request.materialPasses = ReadShaderMaterialPasses(root);

		if (const VansSerializedValue* stages = FindObjectField(root, "stages");
			stages != nullptr && stages->kind == VansSerializedValue::Kind::Object)
		{
			for (const auto& [stageName, stageFile] : stages->objectFields)
			{
				if (stageFile.kind == VansSerializedValue::Kind::String)
					request.stages[stageName] = stageFile.stringValue;
			}
		}

		auto readStageFile = [&](const char* key)
		{
			const std::string file = ReadSerializedStringField(root, key);
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

	const VansSerializedValue* ReadVegetationField(const VansSerializedValue& sceneDocument)
	{
		if (sceneDocument.kind != VansSerializedValue::Kind::Object)
			return nullptr;
		if (const VansSerializedValue* vegetation = FindObjectField(sceneDocument, "vegetation"))
			return vegetation;
		const VansSerializedValue* settings = ReadSerializedObjectField(sceneDocument, "settings");
		if (settings == nullptr)
			return nullptr;
		return FindObjectField(*settings, "vegetation");
	}

	std::string ReadVegetationConfigPath(const VansSerializedValue& vegetationData)
	{
		if (vegetationData.kind == VansSerializedValue::Kind::String)
			return vegetationData.stringValue;
		if (vegetationData.kind != VansSerializedValue::Kind::Object)
			return {};
		std::string path = ReadSerializedStringField(vegetationData, "config");
		if (path.empty()) path = ReadSerializedStringField(vegetationData, "configPath");
		if (path.empty()) path = ReadSerializedStringField(vegetationData, "path");
		return path;
	}

	VansSerializedValue LoadVegetationConfigFromReference(const VansSerializedValue& vegetationData, const std::string& projectRoot)
	{
		const std::string configPath = ReadVegetationConfigPath(vegetationData);
		if (configPath.empty())
			return vegetationData;

		std::filesystem::path resolved = std::filesystem::path(configPath);
		if (resolved.is_relative())
			resolved = std::filesystem::path(projectRoot) / resolved;

		VansSerializedValue loaded = LoadSerializedJsonFileOrEmpty(resolved, "VegetationConfig");
		if (loaded.kind != VansSerializedValue::Kind::Object)
		{
			VANS_LOG_ERROR("[VegetationConfig] Invalid JSON config file: " << resolved.string());
			return EmptyObject();
		}
		VANS_LOG("[VegetationConfig] Loaded config file: " << resolved.string());

		if (vegetationData.kind == VansSerializedValue::Kind::Object)
		{
			for (const auto& [key, value] : vegetationData.objectFields)
			{
				if (key == "config" || key == "configPath" || key == "path")
					continue;
				SetSerializedObjectField(loaded, key, value);
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

	void CollectSerializedAssetReferences(
		const VansSerializedValue& value,
		const std::unordered_map<std::string, VansAssetType>& assetTypesByGuid,
		VansSceneAssetDependencyBuildResult& result)
	{
		if (value.kind == VansSerializedValue::Kind::String)
		{
			const auto found = assetTypesByGuid.find(value.stringValue);
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

		if (value.kind == VansSerializedValue::Kind::Array)
		{
			for (const VansSerializedValue& item : value.arrayItems)
				CollectSerializedAssetReferences(item, assetTypesByGuid, result);
			return;
		}

		if (value.kind == VansSerializedValue::Kind::Object)
		{
			for (const auto& [key, item] : value.objectFields)
				CollectSerializedAssetReferences(item, assetTypesByGuid, result);
		}
	}

	void CollectMaterialTextureDependencies(
		VansAssetDatabase& database,
		const VansMaterialAuthoringAsset& material,
		std::unordered_set<std::string>& requiredTextures)
	{
		const std::string preferredRoot = material.preferredImportModel;
		const std::string generatedFor = ReadSerializedStringField(material.importSource, "generatedFor");
		auto logTexture = [&](const std::string& slot, const std::string& textureGuid)
		{
			if (preferredRoot == "BMW_M4" || generatedFor == "runtimeMultiMeshExpansion")
			{
				VANS_LOG("[MaterialDeps] material=" << material.guid
					<< " slot=" << slot
					<< " textureGuid=" << textureGuid
					<< " preferredRoot=" << preferredRoot);
			}
		};

		if (material.textures.kind == VansSerializedValue::Kind::Object)
		{
			for (const auto& [slot, texture] : material.textures.objectFields)
			{
				std::string textureGuid;
				if (texture.kind == VansSerializedValue::Kind::Object)
					textureGuid = ReadSerializedStringField(texture, "guid");
				else if (texture.kind == VansSerializedValue::Kind::String)
					textureGuid = ResolveTextureGuidFromAlias(database, texture.stringValue, preferredRoot);
				if (!textureGuid.empty())
				{
					logTexture(slot, textureGuid);
					requiredTextures.insert(textureGuid);
				}
			}
			return;
		}

		if (material.textures.kind == VansSerializedValue::Kind::Array)
		{
			for (const VansSerializedValue& entry : material.textures.arrayItems)
			{
				if (entry.kind != VansSerializedValue::Kind::Object)
					continue;
				const VansSerializedValue* texture = FindObjectField(entry, "texture");
				if (texture == nullptr)
					continue;

				std::string textureGuid;
				if (texture->kind == VansSerializedValue::Kind::Object)
					textureGuid = ReadSerializedStringField(*texture, "guid");
				else if (texture->kind == VansSerializedValue::Kind::String)
					textureGuid = ResolveTextureGuidFromAlias(database, texture->stringValue, preferredRoot);
				if (!textureGuid.empty())
				{
					logTexture(ReadSerializedStringField(entry, "slot"), textureGuid);
					requiredTextures.insert(textureGuid);
				}
			}
		}
	}

	void CollectSceneModelRendererDependencies(
		const VansSerializedValue& sceneDocument,
		std::unordered_set<std::string>& requiredModels,
		std::unordered_set<std::string>& requiredMaterials)
	{
		const VansSerializedValue* entities = ReadSerializedArrayField(sceneDocument, "entities");
		if (entities == nullptr)
			return;

		for (const VansSerializedValue& entity : entities->arrayItems)
		{
			const VansSerializedValue* components = ReadSerializedArrayField(entity, "components");
			if (components == nullptr)
				continue;
			for (const VansSerializedValue& component : components->arrayItems)
			{
				const std::string componentType = ReadSerializedStringField(component, "type");
				if (componentType != "ModelRenderer" && componentType != "MultiMeshRoot")
					continue;
				const VansSerializedValue* data = ReadSerializedObjectField(component, "data");
				if (data == nullptr)
					continue;
				const VansSerializedValue* modelReference = FindObjectField(*data, "model");
				const std::string model = modelReference ? ReadAssetGuidReference(*modelReference) : std::string{};
				if (!model.empty()) requiredModels.insert(model);
				if (componentType != "ModelRenderer")
					continue;
				const VansSerializedValue* overrides = ReadSerializedObjectField(*data, "materialOverrides");
				if (overrides == nullptr)
					continue;
				for (const auto& [slot, material] : overrides->objectFields)
				{
					const std::string materialGuid = ReadAssetGuidReference(material);
					if (!materialGuid.empty()) requiredMaterials.insert(materialGuid);
				}
			}
		}
	}

	void CollectScenePhysicsMeshColliderDependencies(
		const VansSerializedValue& sceneDocument,
		std::unordered_set<std::string>& meshColliderModels)
	{
		const VansSerializedValue* entities = ReadSerializedArrayField(sceneDocument, "entities");
		if (entities == nullptr)
			return;

		for (const VansSerializedValue& entity : entities->arrayItems)
		{
			const VansSerializedValue* components = ReadSerializedArrayField(entity, "components");
			if (components == nullptr)
				continue;
			for (const VansSerializedValue& component : components->arrayItems)
			{
				if (ReadSerializedStringField(component, "type") != "Physics")
					continue;
				const VansSerializedValue* data = ReadSerializedObjectField(component, "data");
				if (data == nullptr || !ReadSerializedBoolField(*data, "useMeshCollider", false))
					continue;

				const std::string colliderType = ReadSerializedStringField(*data, "colliderType");
				if (colliderType != "mesh" && colliderType != "convex")
					continue;

				const std::string meshGuid = ReadSerializedStringField(*data, "mesh");
				if (!meshGuid.empty())
					meshColliderModels.insert(meshGuid);
			}
		}
	}
}

VansSceneAssetDependencyBuildResult VansSceneAssetDependencyBuilder::BuildResourcePlan(
	VansAssetDatabase& database,
	const std::filesystem::path& scenePath,
	const std::unordered_map<std::string, std::string>& runtimeAssetBindings,
	VansAssetDatabase* builtInAssetDatabase)
{
	VansSceneAssetDependencyBuildResult result;

	const std::filesystem::path projectRoot = database.AssetsRoot().parent_path();
	for (const auto& [alias, guid] : runtimeAssetBindings)
	{
		if (!VansBuiltInAssetCatalog::IsReservedRuntimeAlias(alias))
			result.requiredModels.insert(guid);
	}

	VansSerializedValue sceneDocument = LoadSceneDocumentOrEmpty(scenePath);
	if (sceneDocument.kind != VansSerializedValue::Kind::Object ||
		ReadSerializedIntField(sceneDocument, "schemaVersion", 0) != VansSceneSchemaVersion)
	{
		VANS_LOG_ERROR("[AssetDatabase] Cannot collect Scene dependencies from " << scenePath.string());
		return result;
	}

	VANS_LOG("[AssetDatabase] Collecting dependencies from " << scenePath.string());
	const VansSerializedValue* entities = ReadSerializedArrayField(sceneDocument, "entities");
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

	CollectSerializedAssetReferences(sceneDocument, assetTypesByGuid, result);
	if (const VansSerializedValue* vegetationConfig = ReadVegetationField(sceneDocument))
	{
		VansSerializedValue externalVegetationConfig = LoadVegetationConfigFromReference(*vegetationConfig, projectRoot.string());
		if (externalVegetationConfig.kind == VansSerializedValue::Kind::Object)
		{
			CollectSerializedAssetReferences(externalVegetationConfig, assetTypesByGuid, result);
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

		VansMaterialAuthoringAsset material;
		std::string materialError;
		if (!VansMaterialAuthoringAssetStorage::Load(record.sourcePath, material, materialError))
		{
			VANS_LOG_ERROR("[AssetDatabase] Cannot read material dependency data: "
				<< record.sourcePath.string() << " (" << materialError << ")");
			continue;
		}

		CollectSerializedAssetReferences(WriteMaterialAuthoringAssetRoot(material), assetTypesByGuid, result);
		CollectMaterialTextureDependencies(database, material, result.requiredTextures);
	}

	for (const VansAssetRecord& record : database.All())
	{
		if (record.state == VansAssetState::Missing)
			continue;

		VansAssetMeta meta;
		std::string metaError;
		if (!VansAssetMetaStorage::Load(record.metaPath, meta, metaError))
		{
			VANS_LOG_ERROR("[AssetDatabase] " << metaError);
			continue;
		}
		if (!meta.HasObjectSettings())
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
			request.assetGuid = record.guid.ToString();
			request.path = relativePath;
			if (!database.ArtifactRoot().empty())
				request.artifactPath = (database.ArtifactRoot() / "Meshes" / (record.guid.ToString() + ".vmesh")).string();
			request.needTangent = meta.ReadBoolSetting("generateTangents", true);
			request.supportRayTracing = meta.ReadBoolSetting("buildRayTracingData", true);
			request.needCpuData = meta.ReadBoolSetting("keepCpuMeshData", false) ||
				meshColliderModels.find(record.guid.ToString()) != meshColliderModels.end();
			request.scaleFactor = meta.ReadFloatSetting("scaleFactor", "scale", 1.0f);
			request.loadMultiMesh = meta.ReadBoolSetting("loadMultiMesh", isFbx);
			request.rebuildIdentityBoneOffsetsFromHierarchy = meta.ReadBoolSetting(
				"rebuildIdentityBoneOffsetsFromHierarchy", false);
			request.remapWeaponAttachmentBonesToHands = meta.ReadBoolSetting(
				"remapWeaponAttachmentBonesToHands", false);
			result.resourcePlan.meshes.push_back(std::move(request));
		}
		else if (record.type == VansAssetType::Texture)
		{
			if (result.requiredTextures.find(record.guid.ToString()) == result.requiredTextures.end())
				continue;
			const bool isCubemap = record.sourcePath.extension() == ".cubemap";
			const std::string texturePath = isCubemap
				? meta.ReadStringSetting("sourcePath") : relativePath;
			VansSceneTextureResourceRequest request;
			request.name = record.guid.ToString();
			request.assetGuid = record.guid.ToString();
			request.path = texturePath;
			request.artifactPath = record.artifactPath.string();
			request.textureType = isCubemap ? SceneTextureCube : SceneTexture2D;
			const std::string colorSpace = meta.ReadStringSetting("colorSpace");
			request.srgb = colorSpace.empty()
				? meta.ReadBoolSetting("sRGB", true)
				: colorSpace != "linear";
			request.useCompress = meta.ReadBoolSetting("useCompress", "compress", true);
			request.needMip = meta.ReadBoolSetting("needMip", "generateMip", true);
			const std::string precision = meta.ReadStringSetting("precision");
			if (!precision.empty())
				request.precision = precision;
			request.importChannel = meta.ReadIntSetting("importChannel", request.importChannel);
			const std::string addressMode = meta.ReadStringSetting("addressMode");
			if (!addressMode.empty())
				request.addressMode = addressMode;
			result.resourcePlan.textures.push_back(std::move(request));
		}
		else if (record.type == VansAssetType::Shader)
		{
			if (result.requiredShaders.find(record.guid.ToString()) == result.requiredShaders.end())
				continue;

			VansShaderAuthoringAsset shader;
			std::string shaderError;
			if (!VansShaderAuthoringAssetStorage::Load(record.sourcePath, shader, shaderError))
			{
				VANS_LOG_ERROR("[AssetDatabase] Cannot read shader asset: "
					<< record.sourcePath.string() << " (" << shaderError << ")");
				continue;
			}
			result.resourcePlan.shaders.push_back(BuildShaderResourceRequest(shader, record, projectRoot));
		}
		else if (record.type == VansAssetType::Audio)
		{
			const std::string runtimeName = meta.ReadStringSetting("runtimeName");
			VansSceneAudioResourceRequest request;
			request.name = runtimeName.empty() ? record.guid.ToString() : runtimeName;
			request.assetGuid = record.guid.ToString();
			request.path = relativePath;
			const std::string playMode = meta.ReadStringSetting("playMode");
			request.playMode = playMode.empty() ? "static" : playMode;
			request.loop = meta.ReadBoolSetting("loop", false);
			request.autoPlay = meta.ReadBoolSetting("autoPlay", false);
			request.volume = meta.ReadFloatSetting("volume", 1.0f);
			request.pitch = meta.ReadFloatSetting("pitch", 1.0f);
			request.spatial = meta.ReadBoolSetting("spatial", false);
			request.referenceDistance = meta.ReadFloatSetting("referenceDistance", 1.0f);
			request.maxDistance = meta.ReadFloatSetting("maxDistance", 100.0f);
			request.rolloff = meta.ReadFloatSetting("rolloff", 1.0f);
			result.resourcePlan.audios.push_back(std::move(request));
		}
		else if (record.type == VansAssetType::Video)
		{
			const std::string runtimeName = meta.ReadStringSetting("runtimeName");
			VansSceneVideoResourceRequest request;
			request.name = runtimeName.empty() ? record.guid.ToString() : runtimeName;
			request.assetGuid = record.guid.ToString();
			request.path = relativePath;
			request.loop = meta.ReadBoolSetting("loop", true);
			request.autoplay = meta.ReadBoolSetting("autoPlay", false);
			request.srgb = meta.ReadBoolSetting("sRGB", true);
			result.resourcePlan.videos.push_back(std::move(request));
		}
	}

	if (builtInAssetDatabase != nullptr)
	{
		for (const VansBuiltInAssetEntry& entry : VansBuiltInAssetCatalog::Entries())
		{
			if (entry.type != VansAssetType::Model)
				continue;

			VansAssetGuid guid;
			if (!VansAssetGuid::TryParse(entry.guid, guid))
			{
				VANS_LOG_ERROR("[AssetDatabase] Invalid built-in asset guid: " << entry.guid);
				return result;
			}

			const std::optional<VansAssetRecord> record = builtInAssetDatabase->Find(guid);
			if (!record || record->state == VansAssetState::Missing)
			{
				VANS_LOG_ERROR("[AssetDatabase] Required built-in asset is missing: " << entry.sourcePath);
				return result;
			}

			VansAssetMeta meta;
			std::string metaError;
			if (!VansAssetMetaStorage::Load(record->metaPath, meta, metaError))
			{
				VANS_LOG_ERROR("[AssetDatabase] " << metaError);
				return result;
			}

			VansSceneMeshResourceRequest request;
			request.name = record->guid.ToString();
			request.assetGuid = record->guid.ToString();
			request.path = record->sourcePath.string();
			request.artifactPath = (
				builtInAssetDatabase->ArtifactRoot() / "Meshes" / (request.assetGuid + ".vmesh")).string();
			request.needTangent = meta.ReadBoolSetting("generateTangents", true);
			request.supportRayTracing = meta.ReadBoolSetting("buildRayTracingData", true);
			request.needCpuData = meta.ReadBoolSetting("keepCpuMeshData", false);
			request.scaleFactor = meta.ReadFloatSetting("scaleFactor", "scale", 1.0f);
			request.loadMultiMesh = meta.ReadBoolSetting("loadMultiMesh", false);
			result.resourcePlan.meshes.push_back(std::move(request));
		}
	}

	result.success = true;
	return result;
}
}
