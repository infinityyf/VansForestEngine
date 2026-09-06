#include "VansSceneAssetDependencyBuilder.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../AssetCore/VansAssetMeta.h"
#include "../AssetCore/VansAssetObjectRepository.h"
#include "../AssetCore/VansBuiltInAssetCatalog.h"
#include "../AssetCore/VansMaterialAuthoringAsset.h"
#include "../AssetCore/VansShaderAuthoringAsset.h"
#include "../AudioCore/VansAudioBus.h"
#include "../AICore/VansAIBehaviorAsset.h"
#include "../AnimationCore/VansAnimatorIO.h"
#include "../AnimationCore/Procedural/VansAnimationRig.h"
#include "../AnimationCore/Retargeting/VansRetargetProfile.h"
#include "../AnimationCore/VansBoneMask.h"
#include "../GameplayActionSchema/VansGameplayAssetSchema.h"
#include "../NavigationCore/VansNavigationMesh.h"
#include "../PhysicsCore/VansRagdollTypes.h"
#include "../RuntimeUI/Serialization/VansUIAssetDocument.h"
#include "../SceneCore/VansSceneSchema.h"
#include "../SceneCore/VansSceneLocalVolumetricFogComponentConfig.h"
#include "../SceneCore/Serialization/VansVegetationConfigCodec.h"
#include "../TimelineCore/VansTimelineDependencyBuilder.h"
#include "../TimelineCore/VansTimelineValidator.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Vans
{
namespace
{
	constexpr int SceneTexture2D = 0;
	constexpr int SceneTextureCube = 2;

	void ApplyTextureImportSettings(
		VansSceneTextureResourceRequest& request,
		const VansAssetMeta& meta)
	{
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
			case VansAssetType::SkinProfile: result.requiredSkinProfiles.insert(found->first); break;
			case VansAssetType::NavigationMesh:
			case VansAssetType::AIBehavior:
			case VansAssetType::VegetationConfig:
				result.requiredAssets.insert(found->first); break;
			default:
				result.requiredAssets.insert(found->first);
				break;
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

	void AppendDependencyError(
		VansSceneAssetDependencyBuildResult& result,
		const std::string& message);

	template <typename Asset>
	std::shared_ptr<const Asset> ResolveMemoryAsset(
		const VansAssetObjectRepository& repository,
		const VansAssetRecord& record)
	{
		VansAssetObjectSnapshotInfo info;
		if (!repository.FindInfo(record.guid, info) || info.assetType != record.type)
			return {};
		return repository.ResolveLatest<Asset>(record.guid);
	}

	void ExpandUIAssetDependencies(
		const std::unordered_map<std::string, VansAssetRecord>& recordsByGuid,
		const std::unordered_map<std::string, VansAssetType>& assetTypesByGuid,
		const VansAssetObjectRepository& objectRepository,
		VansSceneAssetDependencyBuildResult& result)
	{
		std::deque<std::string> pending(
			result.requiredAssets.begin(), result.requiredAssets.end());
		std::unordered_set<std::string> expanded;
		while (!pending.empty())
		{
			const std::string guid = std::move(pending.front());
			pending.pop_front();
			if (!expanded.insert(guid).second) continue;
			const auto found = recordsByGuid.find(guid);
			if (found == recordsByGuid.end()) continue;
			const VansAssetType type = found->second.type;
			if (type != VansAssetType::UIScreen && type != VansAssetType::UIComponent &&
				type != VansAssetType::UIThemeTokens && type != VansAssetType::UILocalization)
				continue;

			const auto memoryDocument = ResolveMemoryAsset<VansRuntime::VansUIAssetDocument>(
				objectRepository, found->second);
			if (!memoryDocument)
			{
				AppendDependencyError(result,
					"UI asset '" + guid + "' has no memory object");
				continue;
			}
			const std::unordered_set<std::string> before = result.requiredAssets;
			CollectSerializedAssetReferences(memoryDocument->root, assetTypesByGuid, result);
			for (const std::string& dependency : result.requiredAssets)
				if (before.find(dependency) == before.end()) pending.push_back(dependency);
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

	struct TypedAssetDependency
	{
		std::string guid;
		VansAssetType expectedType = VansAssetType::Unknown;
		std::string chain;
	};

	void AppendDependencyError(
		VansSceneAssetDependencyBuildResult& result,
		const std::string& message)
	{
		result.errors.push_back(message);
		VANS_LOG_ERROR("[AssetDatabase] " << message);
	}

	void CollectStrictAssetReference(
		const VansSerializedValue* reference,
		VansAssetType expectedType,
		const std::string& chain,
		std::vector<TypedAssetDependency>& dependencies,
		VansSceneAssetDependencyBuildResult& result)
	{
		if (reference == nullptr)
			return;
		const std::string guidText = ReadAssetGuidReference(*reference);
		if (guidText.empty() && reference->kind == VansSerializedValue::Kind::String)
			return;
		VansAssetGuid guid;
		if (!VansAssetGuid::TryParse(guidText, guid))
		{
			AppendDependencyError(result, chain
				+ " must reference a current-project or engine asset by GUID; paths and cross-project references are not allowed");
			return;
		}
		dependencies.push_back({ guidText, expectedType, chain + " -> " + guidText });
	}

	void CollectSceneLocalFogDependencies(
		const VansSerializedValue& sceneDocument,
		std::vector<TypedAssetDependency>& dependencies,
		std::unordered_map<std::string, int>& fieldTextureRequiredChannels,
		VansSceneAssetDependencyBuildResult& result)
	{
		const VansSerializedValue* entities = ReadSerializedArrayField(sceneDocument, "entities");
		if (!entities)
			return;
		for (const VansSerializedValue& entity : entities->arrayItems)
		{
			const std::string entityName = ReadSerializedStringField(entity, "name", "<unnamed>");
			const VansSerializedValue* components = ReadSerializedArrayField(entity, "components");
			if (!components)
				continue;
			for (const VansSerializedValue& component : components->arrayItems)
			{
				if (ReadSerializedStringField(component, "type") != "LocalVolumetricFog")
					continue;
				const VansSerializedValue* data = ReadSerializedObjectField(component, "data");
				if (!data)
					continue;
				for (const char* fieldName : { "shapeMask", "detailNoise", "flow" })
				{
					const VansSerializedValue* field = ReadSerializedObjectField(*data, fieldName);
					if (!field)
						continue;
					const std::string chain = "scene entity '" + entityName +
						"' LocalVolumetricFog." + fieldName;
					const VansSerializedValue* source = ReadSerializedObjectField(*field, "source");
					const VansSerializedValue* asset = source
						? FindObjectField(*source, "asset") : nullptr;
					const std::string guid = asset ? ReadAssetGuidReference(*asset) : std::string{};
					if (source)
					{
						const std::string channels = ReadSerializedStringField(
							*source, "channels", fieldName == std::string("flow") ? "rg" : "r");
						VansLocalFogTextureChannel channel0{};
						VansLocalFogTextureChannel channel1{};
						const bool validChannel = fieldName == std::string("flow")
							? TryParseLocalFogVector2TextureChannels(
								channels, channel0, channel1)
							: TryParseLocalFogScalarTextureChannels(channels, channel0);
						if (!validChannel)
							AppendDependencyError(result, chain + ".source.channels is invalid");
						else if (!guid.empty())
						{
							const int requiredChannels =
								RequiredLocalFogFieldChannelCount(channels);
							int& accumulated = fieldTextureRequiredChannels[guid];
							accumulated = (std::max)(accumulated, requiredChannels);
						}
					}
					if (!guid.empty())
						CollectStrictAssetReference(asset, VansAssetType::Texture,
							chain + ".source.asset", dependencies, result);
					if (const VansSerializedValue* mapping = ReadSerializedObjectField(*field, "mapping"))
					{
						const std::string projection = ReadSerializedStringField(*mapping, "projection", "localXZ");
						const std::string addressMode = ReadSerializedStringField(*mapping, "addressMode");
						if (projection != "localXZ")
							AppendDependencyError(result, chain + ".mapping.projection must be localXZ");
						if (!addressMode.empty() && addressMode != "repeat" &&
							addressMode != "clampToEdge" && addressMode != "clampToBorderZero")
							AppendDependencyError(result, chain + ".mapping.addressMode is invalid");
					}
				}

				const VansSerializedValue* detail = ReadSerializedObjectField(*data, "detailNoise");
				const VansSerializedValue* flow = ReadSerializedObjectField(*data, "flow");
				const VansSerializedValue* flowSpeed = flow
					? FindObjectField(*flow, "speedMetersPerSecond") : nullptr;
				if (detail && flow && ReadSerializedBoolField(*detail, "enabled", false) &&
					ReadSerializedBoolField(*flow, "enabled", false) && flowSpeed &&
					ReadSerializedNumber(*flowSpeed, 0.0) > 0.0)
				{
					const VansSerializedValue* mapping = ReadSerializedObjectField(*detail, "mapping");
					if (mapping && ReadSerializedStringField(*mapping, "addressMode", "repeat") != "repeat")
						AppendDependencyError(result, "scene entity '" + entityName +
							"' LocalVolumetricFog flowed detailNoise must use repeat address mode");
				}
			}
		}
	}

	void CollectSceneAnimationDependencies(
		const VansSerializedValue& sceneDocument,
		std::vector<TypedAssetDependency>& dependencies,
		VansSceneAssetDependencyBuildResult& result)
	{
		const VansSerializedValue* entities = ReadSerializedArrayField(sceneDocument, "entities");
		if (entities == nullptr)
			return;
		for (const VansSerializedValue& entity : entities->arrayItems)
		{
			const std::string entityName = ReadSerializedStringField(entity, "name", "<unnamed>");
			const VansSerializedValue* components = ReadSerializedArrayField(entity, "components");
			if (components == nullptr)
				continue;
			for (const VansSerializedValue& component : components->arrayItems)
			{
				if (ReadSerializedStringField(component, "type") != "Animation")
					continue;
				const VansSerializedValue* data = ReadSerializedObjectField(component, "data");
				if (data == nullptr)
					continue;
				const std::string owner = "scene entity '" + entityName + "' Animation";
				CollectStrictAssetReference(FindObjectField(*data, "animator"),
					VansAssetType::AnimatorController, owner + ".animator", dependencies, result);
				CollectStrictAssetReference(FindObjectField(*data, "rig"),
					VansAssetType::AnimationRig, owner + ".rig", dependencies, result);

				if (const VansSerializedValue* retarget = ReadSerializedObjectField(*data, "retarget"))
				{
					CollectStrictAssetReference(FindObjectField(*retarget, "profile"),
						VansAssetType::RetargetProfile,
						owner + ".retarget.profile", dependencies, result);
					CollectStrictAssetReference(FindObjectField(*retarget, "source_model"),
						VansAssetType::Model,
						owner + ".retarget.source_model", dependencies, result);
					CollectStrictAssetReference(FindObjectField(*retarget, "source_animator"),
						VansAssetType::AnimatorController,
						owner + ".retarget.source_animator", dependencies, result);
				}
				if (const VansSerializedValue* ragdoll = ReadSerializedObjectField(*data, "ragdoll"))
				{
					CollectStrictAssetReference(FindObjectField(*ragdoll, "profile"),
						VansAssetType::RagdollProfile,
						owner + ".ragdoll.profile", dependencies, result);
				}
			}
		}
	}

	void CollectSceneTimelineDependencies(
		const VansSerializedValue& sceneDocument,
		std::vector<TypedAssetDependency>& dependencies,
		VansSceneAssetDependencyBuildResult& result)
	{
		const VansSerializedValue* entities = ReadSerializedArrayField(sceneDocument, "entities");
		if (entities == nullptr) return;
		for (const VansSerializedValue& entity : entities->arrayItems)
		{
			const std::string entityName = ReadSerializedStringField(entity, "name", "<unnamed>");
			const VansSerializedValue* components = ReadSerializedArrayField(entity, "components");
			if (components == nullptr) continue;
			for (const VansSerializedValue& component : components->arrayItems)
			{
				if (ReadSerializedStringField(component, "type") != "Timeline") continue;
				const VansSerializedValue* data = ReadSerializedObjectField(component, "data");
				const VansSerializedValue* timeline = data ? ReadSerializedObjectField(*data, "timeline") : nullptr;
				CollectStrictAssetReference(timeline, VansAssetType::Timeline,
					"scene entity '" + entityName + "' Timeline.timeline", dependencies, result);
			}
		}
	}

	void CollectSceneAIDependencies(
		const VansSerializedValue& sceneDocument,
		std::vector<TypedAssetDependency>& dependencies,
		VansSceneAssetDependencyBuildResult& result)
	{
		const VansSerializedValue* entities = ReadSerializedArrayField(sceneDocument, "entities");
		if (entities == nullptr) return;
		for (const VansSerializedValue& entity : entities->arrayItems)
		{
			const std::string entityName = ReadSerializedStringField(entity, "name", "<unnamed>");
			const VansSerializedValue* components = ReadSerializedArrayField(entity, "components");
			if (components == nullptr) continue;
			for (const VansSerializedValue& component : components->arrayItems)
			{
				const std::string type = ReadSerializedStringField(component, "type");
				const VansSerializedValue* data = ReadSerializedObjectField(component, "data");
				if (!data) continue;
				if (type == "NavigationAgent")
				{
					CollectStrictAssetReference(FindObjectField(*data, "navigationMesh"),
						VansAssetType::NavigationMesh,
						"scene entity '" + entityName + "' NavigationAgent.navigationMesh",
						dependencies, result);
				}
				else if (type == "AIAgent")
				{
					CollectStrictAssetReference(FindObjectField(*data, "behavior"),
						VansAssetType::AIBehavior,
						"scene entity '" + entityName + "' AIAgent.behavior",
						dependencies, result);
				}
			}
		}
	}

	VansAssetType TimelineReferenceType(
		const VansTimelineDependency& dependency,
		const std::unordered_map<std::string, VansAssetRecord>& records)
	{
		if (dependency.stableType == "Timeline") return VansAssetType::Timeline;
		const auto found = records.find(dependency.guid);
		return found == records.end() ? VansAssetType::Unknown : found->second.type;
	}
}

VansSceneAssetDependencyBuildResult VansSceneAssetDependencyBuilder::BuildResourcePlan(
	VansAssetDatabase& database,
	const VansSerializedValue& sceneDocument,
	const std::filesystem::path& sceneSourcePath,
	const std::unordered_map<std::string, std::string>& runtimeAssetBindings,
	const VansAssetObjectRepository& objectRepository,
	VansAssetDatabase* builtInAssetDatabase)
{
	VansSceneAssetDependencyBuildResult result;

	const std::filesystem::path projectRoot = database.AssetsRoot().parent_path();
	for (const auto& [alias, guid] : runtimeAssetBindings)
	{
		if (!VansBuiltInAssetCatalog::IsReservedRuntimeAlias(alias))
			result.requiredModels.insert(guid);
	}

	if (sceneDocument.kind != VansSerializedValue::Kind::Object ||
		ReadSerializedIntField(sceneDocument, "schemaVersion", 0) != VansSceneSchemaVersion)
	{
		VANS_LOG_ERROR("[AssetDatabase] Cannot collect Scene dependencies from " << sceneSourcePath.string());
		return result;
	}

	VANS_LOG("[AssetDatabase] Collecting dependencies from " << sceneSourcePath.string());
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
	std::unordered_map<std::string, VansAssetRecord> assetRecordsByGuid;
	for (const VansAssetRecord& record : allRecords)
	{
		assetTypesByGuid.emplace(record.guid.ToString(), record.type);
		assetRecordsByGuid.emplace(record.guid.ToString(), record);
	}
	if (builtInAssetDatabase != nullptr)
	{
		for (const VansAssetRecord& record : builtInAssetDatabase->All())
		{
			assetTypesByGuid.emplace(record.guid.ToString(), record.type);
			assetRecordsByGuid.emplace(record.guid.ToString(), record);
		}
	}
	CollectSerializedAssetReferences(sceneDocument, assetTypesByGuid, result);
	ExpandUIAssetDependencies(assetRecordsByGuid, assetTypesByGuid, objectRepository, result);
	std::vector<TypedAssetDependency> animationDependencies;
	CollectSceneAnimationDependencies(sceneDocument, animationDependencies, result);
	std::vector<TypedAssetDependency> timelineDependencies;
	CollectSceneTimelineDependencies(sceneDocument, timelineDependencies, result);
	std::vector<TypedAssetDependency> aiDependencies;
	CollectSceneAIDependencies(sceneDocument, aiDependencies, result);
	std::vector<TypedAssetDependency> localFogDependencies;
	std::unordered_map<std::string, int> localFogFieldTextureRequiredChannels;
	CollectSceneLocalFogDependencies(
		sceneDocument, localFogDependencies, localFogFieldTextureRequiredChannels, result);
	if (const VansSerializedValue* vegetationConfig = ReadVegetationField(sceneDocument))
	{
		const std::string vegetationGuidText =
			VansVegetationConfigCodec::ReadReferenceGuid(*vegetationConfig);
		VansVegetationConfigAsset vegetationAsset;
		VansSceneVegetationNodeConfig effectiveVegetation;
		bool vegetationResolved = false;
		std::string vegetationError;
		VansAssetGuid vegetationGuid;
		if (!VansAssetGuid::TryParse(vegetationGuidText, vegetationGuid))
		{
			result.errors.push_back(
				"Vegetation reference must contain a valid asset GUID");
		}
		else
		{
			const std::optional<VansAssetRecord> record = database.Find(vegetationGuid);
			if (!record || record->type != VansAssetType::VegetationConfig)
			{
				result.errors.push_back(
					"Vegetation configuration GUID is not registered as a vegetation asset: " +
					vegetationGuidText);
			}
			else
			{
				const auto published = ResolveMemoryAsset<VansVegetationConfigAsset>(
					objectRepository, *record);
				if (published)
				{
					vegetationAsset = *published;
				}
				else
				{
					vegetationError = "Vegetation configuration has no memory object";
					result.errors.push_back(vegetationError);
				}
				if (vegetationError.empty())
				{
					vegetationResolved = VansVegetationConfigCodec::ResolveReference(
						*vegetationConfig, vegetationAsset, effectiveVegetation, vegetationError);
					if (!vegetationResolved)
						result.errors.push_back(std::move(vegetationError));
				}
			}
		}
		if (vegetationResolved && effectiveVegetation.valid)
		{
			VansSerializedValue effectiveRoot;
			if (!VansVegetationConfigCodec::Encode(
				effectiveVegetation, effectiveRoot, vegetationError))
				result.errors.push_back(std::move(vegetationError));
			else
			{
				CollectSerializedAssetReferences(effectiveRoot, assetTypesByGuid, result);
				VANS_LOG("[AssetDatabase] Collected vegetation dependencies: "
					<< result.requiredModels.size() << " models, "
					<< result.requiredMaterials.size() << " materials, "
					<< result.requiredTextures.size() << " textures");
			}
		}
	}

	result.requiredAssets.insert(result.requiredModels.begin(), result.requiredModels.end());
	result.requiredAssets.insert(result.requiredMaterials.begin(), result.requiredMaterials.end());
	result.requiredAssets.insert(result.requiredTextures.begin(), result.requiredTextures.end());
	result.requiredAssets.insert(result.requiredShaders.begin(), result.requiredShaders.end());
	result.requiredAssets.insert(result.requiredSkinProfiles.begin(), result.requiredSkinProfiles.end());

	std::deque<TypedAssetDependency> pendingDependencies(
		animationDependencies.begin(), animationDependencies.end());
	pendingDependencies.insert(pendingDependencies.end(),
		timelineDependencies.begin(), timelineDependencies.end());
	pendingDependencies.insert(pendingDependencies.end(),
		aiDependencies.begin(), aiDependencies.end());
	pendingDependencies.insert(pendingDependencies.end(),
		localFogDependencies.begin(), localFogDependencies.end());
	std::unordered_set<std::string> expandedAnimators;
	std::unordered_set<std::string> expandedTimelines;
	while (!pendingDependencies.empty())
	{
		TypedAssetDependency dependency = std::move(pendingDependencies.front());
		pendingDependencies.pop_front();
		result.requiredAssets.insert(dependency.guid);
		switch (dependency.expectedType)
		{
		case VansAssetType::Model: result.requiredModels.insert(dependency.guid); break;
		case VansAssetType::Material: result.requiredMaterials.insert(dependency.guid); break;
		case VansAssetType::Texture: result.requiredTextures.insert(dependency.guid); break;
		case VansAssetType::Shader: result.requiredShaders.insert(dependency.guid); break;
		default: break;
		}

		const auto found = assetRecordsByGuid.find(dependency.guid);
		if (found == assetRecordsByGuid.end())
		{
			AppendDependencyError(result, dependency.chain + " is missing from the asset database");
			continue;
		}
		const VansAssetRecord& record = found->second;
		if (record.state == VansAssetState::Missing)
		{
			AppendDependencyError(result, dependency.chain + " resolves to a missing asset");
			continue;
		}
		if (record.type != dependency.expectedType)
		{
			AppendDependencyError(result, dependency.chain + " resolves to the wrong asset type");
			continue;
		}
		if (record.type == VansAssetType::AIBehavior)
		{
			if (!ResolveMemoryAsset<VansAIBehaviorAsset>(objectRepository, record))
			{
				AppendDependencyError(result,
					dependency.chain + " has no AI Behavior memory object");
				continue;
			}
		}
		else if (record.type == VansAssetType::NavigationMesh)
		{
			if (!ResolveMemoryAsset<VansNavigationMesh>(objectRepository, record))
			{
				AppendDependencyError(result,
					dependency.chain + " has no Navigation Mesh memory object");
				continue;
			}
		}
		else if (record.type == VansAssetType::RetargetProfile)
		{
			if (!ResolveMemoryAsset<VansGraphics::VansRetargetProfileAsset>(
					objectRepository, record))
			{
				AppendDependencyError(result,
					dependency.chain + " has no Retarget Profile memory object");
				continue;
			}
		}
		else if (record.type == VansAssetType::RagdollProfile)
		{
			if (!ResolveMemoryAsset<VansEngine::RagdollProfile>(objectRepository, record))
			{
				AppendDependencyError(result,
					dependency.chain + " has no Ragdoll Profile memory object");
				continue;
			}
		}
		else if (record.type == VansAssetType::AnimationRig)
		{
			if (!ResolveMemoryAsset<VansGraphics::VansAnimationRigAsset>(objectRepository, record))
			{
				AppendDependencyError(result,
					dependency.chain + " has no Animation Rig memory object");
				continue;
			}
		}
		else if (record.type == VansAssetType::BoneMask)
		{
			if (!ResolveMemoryAsset<VansGraphics::VansBoneMaskAsset>(objectRepository, record))
			{
				AppendDependencyError(result,
					dependency.chain + " has no Bone Mask memory object");
				continue;
			}
		}
		if (record.type == VansAssetType::Timeline)
		{
			if (!expandedTimelines.insert(dependency.guid).second) continue;
			VansTimelineAsset timeline;
			const auto memoryTimeline = ResolveMemoryAsset<VansTimelineAsset>(
				objectRepository, record);
			if (memoryTimeline)
				timeline = *memoryTimeline;
			else
			{
				AppendDependencyError(result,
					dependency.chain + " has no Timeline memory object");
				continue;
			}
			VansTimelineDependencyClosure closure;
			VansTimelineDiagnostics diagnostics;
			const bool closureBuilt = VansTimelineDependencyBuilder::BuildClosure(
				timeline,
				VansTimelineTrackExtensionRegistry::BuiltIns(),
				[&](const VansTimelineDependency& reference, VansTimelineAsset& nested,
					std::string& identity, std::string& error)
				{
					if (reference.guid.empty())
					{
						error = "SubTimeline requires an indexed asset GUID";
						return false;
					}
					const auto child = assetRecordsByGuid.find(reference.guid);
					if (child == assetRecordsByGuid.end() || child->second.type != VansAssetType::Timeline ||
						child->second.state == VansAssetState::Missing)
					{
						error = "SubTimeline GUID is missing or has the wrong asset type";
						return false;
					}
					identity = reference.guid;
					if (const auto published = objectRepository.ResolveLatest<VansTimelineAsset>(
						child->second.guid))
					{
						nested = *published;
						return true;
					}
					error = "SubTimeline has no memory object";
					return false;
				}, closure, diagnostics);
			if (!closureBuilt)
			{
				for (const VansTimelineDiagnostic& diagnostic : diagnostics)
					if (diagnostic.severity == VansTimelineDiagnosticSeverity::Error)
						AppendDependencyError(result, dependency.chain + " -> " + diagnostic.objectId +
							"." + diagnostic.propertyPath + ": " + diagnostic.message);
				continue;
			}
			std::vector<VansTimelineDependency> timelineReferences = closure.direct;
			timelineReferences.insert(timelineReferences.end(),
				closure.transitive.begin(), closure.transitive.end());
			for (const VansTimelineDependency& reference : timelineReferences)
			{
				if (reference.kind != VansTimelineDependencyKind::Asset) continue;
				if (reference.guid.empty())
				{
					AppendDependencyError(result, dependency.chain + " -> Timeline object '" +
						reference.sourceObjectId + "' requires an indexed dependency GUID");
					continue;
				}
				const VansAssetType expected = TimelineReferenceType(reference, assetRecordsByGuid);
				if (expected == VansAssetType::Unknown)
				{
					AppendDependencyError(result, dependency.chain + " -> Timeline dependency '" +
						reference.sourceObjectId + "' has no executable asset type");
					continue;
				}
				pendingDependencies.push_back({ reference.guid, expected,
					dependency.chain + " -> Timeline object '" + reference.sourceObjectId + "' -> " + reference.guid });
			}
			continue;
		}
		if (record.type != VansAssetType::AnimatorController ||
			!expandedAnimators.insert(dependency.guid).second) continue;

		const auto memoryAnimator = ResolveMemoryAsset<VansGraphics::AnimatorAssetData>(
			objectRepository, record);
		if (!memoryAnimator)
		{
			AppendDependencyError(result,
				dependency.chain + " has no Animator memory object");
			continue;
		}
		const VansGraphics::AnimatorAssetData& animator = *memoryAnimator;
		pendingDependencies.push_back({ animator.animationRigGuid, VansAssetType::AnimationRig,
			dependency.chain + " -> Animation Rig -> " + animator.animationRigGuid });
		for (const VansGraphics::AnimatorClipRef& clip : animator.clipRefs)
		{
			pendingDependencies.push_back({ clip.assetGuid, VansAssetType::AnimationClip,
				dependency.chain + " -> Clip '" + clip.name + "' -> " + clip.assetGuid });
		}
		for (const VansGraphics::VansAnimationLayerDefinition& layer : animator.layers)
		{
			if (layer.kind != VansGraphics::VansAnimationLayerKind::Overlay)
				continue;
			pendingDependencies.push_back({ layer.maskGuid, VansAssetType::BoneMask,
				dependency.chain + " -> Layer '" + layer.name + "' Bone Mask -> " + layer.maskGuid });
		}
	}
	if (!result.errors.empty())
		return result;

	for (const VansAssetRecord& record : database.All())
	{
		if (record.type != VansAssetType::Material || record.state == VansAssetState::Missing)
			continue;

		VansMaterialAuthoringAsset material;
		const auto memoryMaterial = ResolveMemoryAsset<VansMaterialAuthoringAsset>(
			objectRepository, record);
		if (memoryMaterial)
			material = *memoryMaterial;
		else
		{
			AppendDependencyError(result,
				"Material '" + record.guid.ToString() + "' has no memory object");
			continue;
		}

		if (result.requiredMaterials.find(record.guid.ToString()) != result.requiredMaterials.end())
		{
			CollectSerializedAssetReferences(
				WriteMaterialAuthoringAssetRoot(material), assetTypesByGuid, result);
			CollectMaterialTextureDependencies(database, material, result.requiredTextures);
		}
	}
	if (!result.errors.empty())
		return result;

	for (const VansAssetRecord& record : database.All())
	{
		if (record.state == VansAssetState::Missing)
			continue;

		VansAssetMeta meta;
		const auto memoryMeta = ResolveMemoryAsset<VansAssetMeta>(objectRepository, record);
		if (memoryMeta)
			meta = *memoryMeta;
		else
		{
			AppendDependencyError(result,
				"Asset metadata '" + record.guid.ToString() + "' has no memory object");
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
			request.skeletalImport = ReadSkeletalMeshImportSettings(meta);
			result.resourcePlan.meshes.push_back(std::move(request));
		}
		else if (record.type == VansAssetType::Texture)
		{
			if (result.requiredTextures.find(record.guid.ToString()) == result.requiredTextures.end())
				continue;
			const auto localFogField = localFogFieldTextureRequiredChannels.find(
				record.guid.ToString());
			if (localFogField != localFogFieldTextureRequiredChannels.end())
			{
				const std::string colorSpace = LowerAsciiCopy(
					meta.ReadStringSetting("colorSpace"));
				const bool isSrgb = colorSpace.empty()
					? meta.ReadBoolSetting("sRGB", true) : colorSpace != "linear";
				const VansLocalFogFieldTextureImportSettings fieldImport{
					!isSrgb,
					meta.ReadBoolSetting("useCompress", "compress", true),
					meta.ReadBoolSetting("needMip", "generateMip", true),
					meta.ReadIntSetting("importChannel", 4),
					meta.ReadStringSetting("precision", "low8")
				};
				if (!ValidateLocalFogFieldTextureImportSettings(
					fieldImport, localFogField->second))
				{
					AppendDependencyError(result,
						"LocalVolumetricFog field texture " + record.guid.ToString() +
						" must be linear, uncompressed, mipmapped, and expose all selected channels "
						"through a low8 importChannel 1, 2, or 4 artifact");
					continue;
				}
			}
			const bool isCubemap = record.sourcePath.extension() == ".cubemap";
			const std::string texturePath = isCubemap
				? meta.ReadStringSetting("sourcePath") : relativePath;
			VansSceneTextureResourceRequest request;
			request.name = record.guid.ToString();
			request.assetGuid = record.guid.ToString();
			request.path = texturePath;
			request.artifactPath = record.artifactPath.string();
			request.textureType = isCubemap ? SceneTextureCube : SceneTexture2D;
			ApplyTextureImportSettings(request, meta);
			result.resourcePlan.textures.push_back(std::move(request));
		}
		else if (record.type == VansAssetType::Shader)
		{
			if (result.requiredShaders.find(record.guid.ToString()) == result.requiredShaders.end())
				continue;

			const auto shader = ResolveMemoryAsset<VansShaderAuthoringAsset>(
				objectRepository, record);
			if (!shader)
			{
				AppendDependencyError(result,
					"Shader '" + record.guid.ToString() + "' has no memory object");
				continue;
			}
			result.resourcePlan.shaders.push_back(BuildShaderResourceRequest(
				*shader, record, projectRoot));
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
			const std::string attenuationMode = meta.ReadStringSetting("attenuationMode");
			request.attenuationMode = attenuationMode.empty() ? "linear" : attenuationMode;
			request.reverbSend = meta.ReadFloatSetting("reverbSend", 0.0f);
			request.bus = VansEngine::NormalizeAudioBusName(meta.ReadStringSetting("bus"));
			request.lowpassHighFrequencyGain =
				std::clamp(meta.ReadFloatSetting("lowpassHighFrequencyGain", 1.0f), 0.0f, 1.0f);
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
			const auto memoryMeta = ResolveMemoryAsset<VansAssetMeta>(
				objectRepository, *record);
			if (memoryMeta)
				meta = *memoryMeta;
			else
			{
				VANS_LOG_ERROR("[AssetDatabase] Built-in asset metadata has no memory object: "
					<< record->guid.ToString());
				return result;
			}
			switch (entry.type)
			{
			case VansAssetType::Model:
			{
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
				break;
			}
			case VansAssetType::Texture:
			{
				VansSceneTextureResourceRequest request;
				request.name = entry.runtimeAlias;
				request.assetGuid = record->guid.ToString();
				// 内建纹理由 GUID/asset index 解析，不能误按项目相对路径读取。
				request.path.clear();
				request.artifactPath = record->artifactPath.string();
				request.textureType = SceneTexture2D;
				ApplyTextureImportSettings(request, meta);
				result.resourcePlan.textures.push_back(std::move(request));
				break;
			}
			case VansAssetType::UIXaml:
				// 内置 XAML 是内存配置资产，不产生 GPU 资源请求。
				result.requiredAssets.insert(record->guid.ToString());
				break;
			default:
				VANS_LOG_ERROR("[AssetDatabase] Unsupported built-in runtime asset type for '"
					<< entry.sourcePath << "'");
				return result;
			}
		}
	}

	result.requiredAssets.insert(result.requiredModels.begin(), result.requiredModels.end());
	result.requiredAssets.insert(result.requiredMaterials.begin(), result.requiredMaterials.end());
	result.requiredAssets.insert(result.requiredTextures.begin(), result.requiredTextures.end());
	result.requiredAssets.insert(result.requiredShaders.begin(), result.requiredShaders.end());
	result.requiredAssets.insert(result.requiredSkinProfiles.begin(), result.requiredSkinProfiles.end());
	if (!result.errors.empty())
		return result;
	result.success = true;
	return result;
}
}
