#include "VansSceneRuntimeProjection.h"
#include "VansSceneContentBuildPlan.h"
#include "Prefab/VansPrefabAsset.h"
#include "VansSceneAssetDependencyBuilder.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../AssetCore/Serialization/VansSerializedObjectReference.h"
#include "../AssetCore/VansAssetMeta.h"
#include "../AssetCore/VansAssetObjectRepository.h"
#include "../AssetCore/VansAssetReference.h"
#include "../AssetCore/VansBuiltInAssetCatalog.h"
#include "../AssetCore/VansDerivedArtifactLayout.h"
#include "../AssetCore/VansMaterialAuthoringAsset.h"
#include "../AssetCore/VansShaderAuthoringAsset.h"
#include "../AudioCore/VansAudioBus.h"
#include "../AudioCore/VansAudioReverbPresetAsset.h"
#include "../AICore/VansAIBehaviorAsset.h"
#include "../AnimationCore/VansAnimatorIO.h"
#include "../AnimationCore/Procedural/VansAnimationRig.h"
#include "../AnimationCore/Retargeting/VansRetargetProfile.h"
#include "../AnimationCore/VansBoneMask.h"
#include "../GameplayActionSchema/VansGameplayAssetSchema.h"
#include "../NavigationCore/VansNavigationMesh.h"
#include "../NavigationCore/VansNavigationSource.h"
#include "../NavigationCore/VansSceneNavigationGeometry.h"
#include "../PhysicsCore/VansRagdollTypes.h"
#include "../ParticleCore/VansParticleAsset.h"
#include "../RuntimeUI/Serialization/VansUIAssetDocument.h"
#include "../ScriptCore/VansScriptComponentReader.h"
#include "../SceneCore/VansComponentTypeCatalog.h"
#include "../SceneCore/VansSceneSchema.h"
#include "../SceneCore/VansSceneLocalVolumetricFogComponentConfig.h"
#include "../SceneCore/Serialization/VansVegetationConfigCodec.h"
#include "../TimelineCore/VansTimelineDependencyBuilder.h"
#include "../TimelineCore/VansTimelineValidator.h"
#include "../Timeline/VansEngineTimelineRegistry.h"
#include "../TerrainCore/VansTerrainAsset.h"
#include "../PcgCore/VansPcgResourcePlan.h"
#include "../Util/VansLog.h"
#include "../Util/VansFileFingerprint.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
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
		request.useCompress = meta.ReadBoolSetting("useCompress", true);
		request.needMip = meta.ReadBoolSetting("needMip", true);
		const std::string precision = meta.ReadStringSetting("precision");
		if (!precision.empty())
			request.precision = precision;
		request.importChannel = meta.ReadIntSetting("importChannel", request.importChannel);
		const std::string addressMode = meta.ReadStringSetting("addressMode");
		if (!addressMode.empty())
			request.addressMode = addressMode;
	}

	bool SupportsRectLightEmissive(const VansSceneTextureResourceRequest& request)
	{
		return request.textureType == SceneTexture2D &&
			!request.useCompress &&
			request.precision == "low8" &&
			request.importChannel == 4;
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
		const std::filesystem::path& projectRoot,
		const std::filesystem::path& artifactRoot)
	{
		const VansSerializedValue& root = shader.root;

		VansSceneShaderResourceRequest request;
		request.name = shader.name.empty() ? record.guid.ToString() : shader.name;
		request.assetGuid = record.guid.ToString();
		request.artifactRoot = VansDerivedArtifactLayout::ProjectShaderCache(artifactRoot).path;
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

	void AppendDependencyError(
		VansSceneAssetDependencyBuildResult& result,
		const std::string& message);

	void InsertRequiredAsset(
		const std::string& guid,
		VansAssetType type,
		VansSceneAssetDependencyBuildResult& result)
	{
		if (guid.empty()) return;
		result.requiredAssets.insert(guid);
		switch (type)
		{
		case VansAssetType::Model: result.requiredModels.insert(guid); break;
		case VansAssetType::Texture: result.requiredTextures.insert(guid); break;
		case VansAssetType::Material: result.requiredMaterials.insert(guid); break;
		case VansAssetType::Shader: result.requiredShaders.insert(guid); break;
		case VansAssetType::SkinProfile: result.requiredSkinProfiles.insert(guid); break;
		default: break;
		}
	}

	void CollectUIReference(
		const VansSerializedValue& reference,
		VansAssetType expectedType,
		const std::string& chain,
		const std::unordered_map<std::string, VansAssetRecord>& recordsByGuid,
		VansSceneAssetDependencyBuildResult& result)
	{
		VansAssetGuid guid;
		if (!TryReadAssetGuidReference(reference, guid))
		{
			AppendDependencyError(result, chain + " must contain exactly one valid guid");
			return;
		}
		const std::string guidText = guid.ToString();
		const auto record = recordsByGuid.find(guidText);
		if (record == recordsByGuid.end() || record->second.state == VansAssetState::Missing ||
			record->second.type != expectedType)
		{
			AppendDependencyError(result, chain + " resolves to a missing or wrong-type asset: " + guidText);
			return;
		}
		InsertRequiredAsset(guidText, expectedType, result);
	}

	void CollectUIReferenceArray(
		const VansSerializedValue& root,
		const char* fieldName,
		VansAssetType expectedType,
		const std::unordered_map<std::string, VansAssetRecord>& recordsByGuid,
		VansSceneAssetDependencyBuildResult& result)
	{
		const VansSerializedValue* references = FindObjectField(root, fieldName);
		if (references == nullptr) return;
		if (references->kind != VansSerializedValue::Kind::Array)
		{
			AppendDependencyError(result, std::string("UI.") + fieldName + " must be an array");
			return;
		}
		for (std::size_t index = 0; index < references->arrayItems.size(); ++index)
			CollectUIReference(references->arrayItems[index], expectedType,
				std::string("UI.") + fieldName + "[" + std::to_string(index) + "]",
				recordsByGuid, result);
	}

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
			const VansSerializedValue* xaml = FindObjectField(memoryDocument->root, "xaml");
			if (xaml != nullptr)
				CollectUIReference(*xaml, VansAssetType::UIXaml,
					"UI.xaml", recordsByGuid, result);
			if (type == VansAssetType::UIScreen)
			{
				CollectUIReferenceArray(memoryDocument->root, "themes",
					VansAssetType::UIThemeTokens, recordsByGuid, result);
				CollectUIReferenceArray(memoryDocument->root, "tokens",
					VansAssetType::UIThemeTokens, recordsByGuid, result);
				CollectUIReferenceArray(memoryDocument->root, "localization",
					VansAssetType::UILocalization, recordsByGuid, result);
				if (const VansSerializedValue* dependencies =
					FindObjectField(memoryDocument->root, "dependencies"))
				{
					if (dependencies->kind != VansSerializedValue::Kind::Array)
						AppendDependencyError(result, "UI.dependencies must be an array");
					else for (const VansSerializedValue& dependency : dependencies->arrayItems)
					{
						if (dependency.kind != VansSerializedValue::Kind::String) continue;
						const auto dependencyRecord = recordsByGuid.find(dependency.stringValue);
						// UI dependencies also carry relative XAML resource paths. Only an exact
						// indexed GUID is an AssetDatabase dependency; paths stay in the UI owner.
						if (dependencyRecord == recordsByGuid.end() ||
							dependencyRecord->second.state == VansAssetState::Missing) continue;
						InsertRequiredAsset(dependency.stringValue,
							dependencyRecord->second.type, result);
					}
				}
			}
			for (const std::string& dependency : result.requiredAssets)
				if (before.find(dependency) == before.end()) pending.push_back(dependency);
		}
	}

	void ExpandTerrainDependencies(
		const std::unordered_map<std::string, VansAssetRecord>& recordsByGuid,
		const VansAssetObjectRepository& objectRepository,
		VansSceneAssetDependencyBuildResult& result,
		std::unordered_set<std::string>& heightTextures,
		std::unordered_set<std::string>& splatTextures)
	{
		const std::vector<std::string> referencedAssets(
			result.requiredAssets.begin(), result.requiredAssets.end());
		for (const std::string& guid : referencedAssets)
		{
			const auto found = recordsByGuid.find(guid);
			if (found == recordsByGuid.end() || found->second.type != VansAssetType::Terrain)
				continue;
			const auto terrain = ResolveMemoryAsset<VansTerrainAsset>(
				objectRepository, found->second);
			if (!terrain)
			{
				AppendDependencyError(result,
					"Terrain asset '" + guid + "' has no memory object");
				continue;
			}
			heightTextures.insert(terrain->heightmap.ToString());
			for (const VansAssetGuid splat : terrain->splatmaps)
				splatTextures.insert(splat.ToString());
			for (const VansAssetGuid dependency : terrain->Dependencies())
			{
				const std::string textureGuid = dependency.ToString();
				const auto texture = recordsByGuid.find(textureGuid);
				if (texture == recordsByGuid.end() ||
					texture->second.type != VansAssetType::Texture ||
					texture->second.state == VansAssetState::Missing)
				{
					AppendDependencyError(result, "Terrain '" + guid +
						"' requires an indexed texture: " + textureGuid);
					continue;
				}
				result.requiredAssets.insert(textureGuid);
				if (dependency != terrain->heightmap &&
					dependency != terrain->splatmaps[0] &&
					dependency != terrain->splatmaps[1])
					result.requiredTextures.insert(textureGuid);
			}
		}
	}

    void ExpandParticleDependencies(
        const std::unordered_map<std::string,VansAssetRecord>& records,
        const VansAssetObjectRepository& repository, VansSceneAssetDependencyBuildResult& result)
    {
        std::deque<std::string> pending(result.requiredAssets.begin(),result.requiredAssets.end());
        std::unordered_set<std::string> expanded;
        while (!pending.empty())
        {
            auto guid=std::move(pending.front()); pending.pop_front();
            if (!expanded.insert(guid).second) continue;
            const auto found=records.find(guid);
            if (found==records.end()) continue;
            const auto& record=found->second;
            if (record.type==VansAssetType::Particle)
            {
                const auto asset=repository.ResolveLatest<VansGraphics::VansParticleAsset>(record.guid);
                if (!asset) { AppendDependencyError(result,"Particle asset has no memory object: "+guid); continue; }
                for (const auto& texture : asset->TextureDependencies())
                {
                    const auto textureRecord=records.find(texture.ToString());
                    if (textureRecord==records.end() || textureRecord->second.type!=VansAssetType::Texture || textureRecord->second.state==VansAssetState::Missing)
                        AppendDependencyError(result,"Particle "+guid+" requires a registered texture: "+texture.ToString());
                    else { result.requiredTextures.insert(texture.ToString()); result.requiredAssets.insert(texture.ToString()); }
                }
            }
            else if (VansGameplayAssetSchemaRegistry::IsGameplayAssetType(record.type))
            {
                VansAssetObjectSnapshotInfo info;
                if (!repository.FindInfo(record.guid,info)) { AppendDependencyError(result,"Gameplay asset has no memory dependency snapshot: "+guid); continue; }
                for (const auto& dependency : info.dependencies)
                {
                    const auto child=records.find(dependency.ToString());
                    if (child!=records.end() && (child->second.type==VansAssetType::Particle || VansGameplayAssetSchemaRegistry::IsGameplayAssetType(child->second.type)))
                    { result.requiredAssets.insert(child->first); pending.push_back(child->first); }
                }
            }
        }
    }

	void CollectMaterialReference(
		const VansSerializedValue& reference,
		VansAssetType expectedType,
		const std::string& chain,
		const std::unordered_map<std::string, VansAssetRecord>& recordsByGuid,
		VansSceneAssetDependencyBuildResult& result)
	{
		VansAssetGuid guid;
		if (!TryReadAssetGuidReference(reference, guid))
		{
			AppendDependencyError(result, chain + " must contain exactly one valid guid");
			return;
		}
		const std::string guidText = guid.ToString();
		const auto record = recordsByGuid.find(guidText);
		if (record == recordsByGuid.end() || record->second.state == VansAssetState::Missing ||
			record->second.type != expectedType)
		{
			AppendDependencyError(result, chain + " resolves to a missing or wrong-type asset: " + guidText);
			return;
		}
		InsertRequiredAsset(guidText, expectedType, result);
	}

	void CollectMaterialDependencies(
		const VansMaterialAuthoringAsset& material,
		const std::unordered_map<std::string, VansAssetRecord>& recordsByGuid,
		VansSceneAssetDependencyBuildResult& result)
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
				VansAssetGuid textureGuid;
				if (TryReadAssetGuidReference(texture, textureGuid))
				{
					const std::string guidText = textureGuid.ToString();
					logTexture(slot, guidText);
					CollectMaterialReference(texture, VansAssetType::Texture,
						"Material '" + material.guid + "'.textures['" + slot + "']",
						recordsByGuid, result);
				}
			}
		}
		else if (material.textures.kind == VansSerializedValue::Kind::Array)
		{
			for (const VansSerializedValue& entry : material.textures.arrayItems)
			{
				if (entry.kind != VansSerializedValue::Kind::Object)
					continue;
				const VansSerializedValue* texture = FindObjectField(entry, "texture");
				if (texture == nullptr)
					continue;

				VansAssetGuid textureGuid;
				if (TryReadAssetGuidReference(*texture, textureGuid))
				{
					const std::string guidText = textureGuid.ToString();
					logTexture(ReadSerializedStringField(entry, "slot"), guidText);
					CollectMaterialReference(*texture, VansAssetType::Texture,
						"Material '" + material.guid + "'.textures['" +
						ReadSerializedStringField(entry, "slot") + "']",
						recordsByGuid, result);
				}
			}
		}

		auto collectObjectValues = [&](const VansSerializedValue& values,
			VansAssetType expectedType, const char* fieldName)
		{
			if (values.kind != VansSerializedValue::Kind::Object) return;
			for (const auto& [name, reference] : values.objectFields)
				CollectMaterialReference(reference, expectedType,
					"Material '" + material.guid + "'." + fieldName + "['" + name + "']",
					recordsByGuid, result);
		};
		if (material.shader.kind != VansSerializedValue::Kind::Null)
			CollectMaterialReference(material.shader, VansAssetType::Shader,
				"Material '" + material.guid + "'.shader", recordsByGuid, result);
		collectObjectValues(material.shaderPasses, VansAssetType::Shader, "shaderPasses");
		collectObjectValues(material.customTextures, VansAssetType::Texture, "customTextures");
		if (const VansSerializedValue* skinProfile = FindObjectField(material.parameters, "skinProfile"))
			CollectMaterialReference(*skinProfile, VansAssetType::SkinProfile,
				"Material '" + material.guid + "'.parameters.skinProfile", recordsByGuid, result);
	}

	void CollectPhysicsMeshColliderDependencies(
		const VansSerializedValue& entities,
		std::unordered_set<std::string>& meshColliderModels)
	{
		if (entities.kind != VansSerializedValue::Kind::Array)
			return;

		for (const VansSerializedValue& entity : entities.arrayItems)
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

	void ValidateNavigationSources(
		const VansSerializedValue& entities,
		const std::filesystem::path& projectRoot,
		const std::filesystem::path& sceneSourcePath,
		const std::unordered_map<std::string, VansAssetRecord>& records,
		const VansAssetObjectRepository& objectRepository,
		VansSceneAssetDependencyBuildResult& result)
	{
		bool hasNavigationMesh = false;
		for (const std::string& guid : result.requiredAssets)
		{
			const auto record = records.find(guid);
			if (record != records.end() && record->second.type == VansAssetType::NavigationMesh)
			{
				hasNavigationMesh = true;
				break;
			}
		}
		if (!hasNavigationMesh) return;

		VansSceneContentBuildPlan contentPlan;
		std::string error;
		if (!VansSceneRuntimeProjection::BuildRuntimeSceneEntityPlan(
			entities, projectRoot.generic_string(), contentPlan, error))
		{
			AppendDependencyError(result,
				"Could not project Scene for navigation source validation: " + error);
			return;
		}
		std::vector<std::string> colliderGuids;
		if (!VansSceneNavigationGeometry::CollectEnvironmentMeshAssets(
			contentPlan.objects, colliderGuids, error))
		{
			AppendDependencyError(result,
				"Could not collect navigation collider sources: " + error);
			return;
		}
		std::vector<VansNavigationColliderSource> colliderSources;
		colliderSources.reserve(colliderGuids.size());
		for (const std::string& guid : colliderGuids)
		{
			const auto record = records.find(guid);
			if (record == records.end() || record->second.type != VansAssetType::Model ||
				record->second.state == VansAssetState::Missing)
			{
				AppendDependencyError(result,
					"Navigation collider Model is missing: " + guid);
				return;
			}
			colliderSources.push_back({ guid, record->second.sourceHash,
				record->second.metaHash });
		}

		VansFileFingerprint sceneFingerprint;
		if (!ComputeFileFingerprint(sceneSourcePath, sceneFingerprint, &error))
		{
			AppendDependencyError(result,
				"Could not fingerprint navigation source Scene: " + error);
			return;
		}
		std::error_code relativeError;
		const std::string scene = std::filesystem::relative(
			sceneSourcePath, projectRoot, relativeError).generic_string();
		if (relativeError || scene.empty())
		{
			AppendDependencyError(result,
				"Could not resolve the navigation source Scene path");
			return;
		}
		const std::uint64_t colliderHash =
			HashNavigationColliders(std::move(colliderSources));
		for (const std::string& guid : result.requiredAssets)
		{
			const auto record = records.find(guid);
			if (record == records.end() ||
				record->second.type != VansAssetType::NavigationMesh)
			{
				continue;
			}
			VansAssetGuid assetGuid;
			VansAssetGuid::TryParse(guid, assetGuid);
			const auto navigation =
				objectRepository.ResolveLatest<VansNavigationMesh>(assetGuid);
			if (!navigation)
			{
				AppendDependencyError(result,
					"Navigation Mesh " + guid +
					" is unavailable for source validation; run ForestAssetTool bake-navigation");
				continue;
			}
			const VansNavigationSource& source = navigation->GetSource();
			if (!source.IsValid() || source.scene != scene ||
				source.sceneHash != sceneFingerprint.contentHash ||
				source.colliderHash != colliderHash)
			{
				AppendDependencyError(result,
					"Navigation Mesh " + guid + " is stale for Scene '" + scene +
					"'; run ForestAssetTool bake-navigation");
			}
		}
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

	void CollectGuidObjectAssetReference(
		const VansSerializedValue* reference,
		VansAssetType expectedType,
		const std::string& chain,
		std::vector<TypedAssetDependency>& dependencies,
		VansSceneAssetDependencyBuildResult& result)
	{
		if (reference == nullptr)
			return;
		std::optional<VansAssetGuid> guid;
		if (!TryReadOptionalAssetGuidReference(*reference, guid))
		{
			AppendDependencyError(result,
				chain + " must be an object containing exactly one guid");
			return;
		}
		if (!guid)
			return;
		const std::string guidText = guid->ToString();
		dependencies.push_back({ guidText, expectedType, chain + " -> " + guidText });
	}

	VansAssetType ParseDeclaredAssetType(std::string_view declaredType)
	{
		const std::string normalized = LowerAsciiCopy(std::string(declaredType));
		for (int value = static_cast<int>(VansAssetType::Model);
			value <= static_cast<int>(VansAssetType::Prefab); ++value)
		{
			const VansAssetType type = static_cast<VansAssetType>(value);
			if (LowerAsciiCopy(std::string(VansAssetDatabase::SerializedTypeName(type))) == normalized)
				return type;
		}
		return VansAssetType::Unknown;
	}

	void CollectCatalogReferenceValue(
		std::string_view componentType,
		const VansSerializedValue& value,
		const std::string& parentKey,
		const std::string& fieldKey,
		const std::string& chain,
		std::vector<TypedAssetDependency>& dependencies,
		VansSceneAssetDependencyBuildResult& result)
	{
		if (const VansComponentAssetReferenceRule* rule =
			VansComponentTypeCatalog::FindAssetReferenceRule(componentType, parentKey, fieldKey))
		{
			const VansAssetType expectedType = ParseDeclaredAssetType(rule->assetType);
			if (expectedType == VansAssetType::Unknown)
			{
				AppendDependencyError(result, chain + " declares an unknown asset type");
				return;
			}
			if (rule->storage == VansComponentAssetReferenceStorage::GuidObject)
			{
				const std::string domain = ReadSerializedStringField(value, "domain");
				if (!domain.empty())
				{
					SerializedObjectReferenceValue reference;
					const std::string expectedSerializedType(
						VansAssetDatabase::SerializedTypeName(expectedType));
					if (!TryReadSerializedObjectReference(value, reference) ||
						reference.domain != "ProjectAsset" ||
						LowerAsciiCopy(reference.assetType) != LowerAsciiCopy(expectedSerializedType))
					{
						AppendDependencyError(result, chain +
							" must be a ProjectAsset " + expectedSerializedType + " reference");
						return;
					}
					if (reference.guid.empty()) return;
					CollectStrictAssetReference(FindObjectField(value, "guid"), expectedType,
						chain, dependencies, result);
				}
				else
					CollectGuidObjectAssetReference(&value, expectedType, chain, dependencies, result);
			}
			else
				CollectStrictAssetReference(&value, expectedType, chain, dependencies, result);
			return;
		}

		if (value.kind == VansSerializedValue::Kind::Object)
		{
			for (const auto& [name, child] : value.objectFields)
				CollectCatalogReferenceValue(componentType, child,
					LowerAsciiCopy(fieldKey), LowerAsciiCopy(name), chain + "." + name,
					dependencies, result);
		}
		else if (value.kind == VansSerializedValue::Kind::Array)
		{
			for (std::size_t index = 0; index < value.arrayItems.size(); ++index)
				CollectCatalogReferenceValue(componentType, value.arrayItems[index],
					LowerAsciiCopy(fieldKey), {}, chain + "[" + std::to_string(index) + "]",
					dependencies, result);
		}
	}

	bool UsesCatalogDependencyCollection(std::string_view componentType)
	{
		return componentType == "DirectionalLight" || componentType == "PointLight" ||
			componentType == "SpotLight" || componentType == "RectLight" ||
			componentType == "Particle" || componentType == "Cloth" ||
			componentType == "ActionHost" || componentType == "UIController";
	}

	void CollectCatalogComponentDependencies(
		const VansSerializedValue& entities,
		std::vector<TypedAssetDependency>& dependencies,
		VansSceneAssetDependencyBuildResult& result)
	{
		if (entities.kind != VansSerializedValue::Kind::Array) return;
		for (const VansSerializedValue& entity : entities.arrayItems)
		{
			const std::string entityName = ReadSerializedStringField(entity, "name", "<unnamed>");
			const VansSerializedValue* components = ReadSerializedArrayField(entity, "components");
			if (components == nullptr) continue;
			for (const VansSerializedValue& component : components->arrayItems)
			{
				const std::string componentType = ReadSerializedStringField(component, "type");
				if (!UsesCatalogDependencyCollection(componentType)) continue;
				const VansSerializedValue* data = FindObjectField(component, "data");
				if (data == nullptr) continue;
				CollectCatalogReferenceValue(componentType, *data, {}, "data",
					"scene entity '" + entityName + "' " + componentType + ".data",
					dependencies, result);
			}
		}
	}

	void CollectScriptFieldAssetDependencies(
		const VansSerializedValue& entities,
		std::vector<TypedAssetDependency>& dependencies,
		VansSceneAssetDependencyBuildResult& result)
	{
		if (entities.kind != VansSerializedValue::Kind::Array) return;
		for (const VansSerializedValue& entity : entities.arrayItems)
		{
			const std::string entityName = ReadSerializedStringField(entity, "name", "<unnamed>");
			const VansSerializedValue* components = ReadSerializedArrayField(entity, "components");
			if (components == nullptr) continue;
			for (const VansSerializedValue& component : components->arrayItems)
			{
				if (ReadSerializedStringField(component, "type") != "Script") continue;
				const VansSerializedValue* data = ReadSerializedObjectField(component, "data");
				if (data == nullptr) continue;
				std::vector<VansScriptSerializedObjectReference> references;
				std::string error;
				if (!VansScriptComponentReader::CollectProjectAssetReferences(
						*data, references, error))
				{
					AppendDependencyError(result, "scene entity '" + entityName +
						"' Script.data.fields: " + error);
					continue;
				}
				for (const VansScriptSerializedObjectReference& reference : references)
				{
					const VansAssetType expectedType = ParseDeclaredAssetType(reference.assetType);
					if (expectedType == VansAssetType::Unknown)
					{
						AppendDependencyError(result, "scene entity '" + entityName +
							"' Script.data.fields declares unknown ProjectAsset type '" +
							reference.assetType + "'");
						continue;
					}
					const VansSerializedValue guid = VansSerializedValue::String(reference.guid);
					CollectStrictAssetReference(&guid, expectedType,
						"scene entity '" + entityName + "' Script.data.fields ProjectAsset",
						dependencies, result);
				}
			}
		}
	}

	void CollectSceneSettingDependencies(
		const VansSerializedValue& sceneDocument,
		std::vector<TypedAssetDependency>& dependencies,
		VansSceneAssetDependencyBuildResult& result)
	{
		const VansSerializedValue* settings = ReadSerializedObjectField(sceneDocument, "settings");
		if (settings == nullptr) return;
		if (const VansSerializedValue* terrain = ReadSerializedObjectField(*settings, "terrain"))
		{
			const VansSerializedValue* asset = FindObjectField(*terrain, "asset");
			SerializedObjectReferenceValue reference;
			if (asset == nullptr || !TryReadSerializedObjectReference(*asset, reference) ||
				reference.domain != "ProjectAsset" || reference.assetType != "terrain")
				AppendDependencyError(result,
					"Scene settings.terrain.asset must be a ProjectAsset terrain reference");
			else
				CollectStrictAssetReference(FindObjectField(*asset, "guid"), VansAssetType::Terrain,
					"Scene settings.terrain.asset", dependencies, result);
		}
		if (const VansSerializedValue* vegetation = ReadVegetationField(sceneDocument))
		{
			const VansSerializedValue* asset = FindObjectField(*vegetation, "asset");
			CollectGuidObjectAssetReference(asset, VansAssetType::VegetationConfig,
				"Scene settings.vegetation.asset", dependencies, result);
		}
		if (const VansSerializedValue* splines = FindObjectField(*settings, "pcgSplines"))
		{
			SerializedObjectReferenceValue reference;
			if (!TryReadSerializedObjectReference(*splines, reference) ||
				reference.domain != "ProjectAsset" || reference.assetType != "pcgSpline")
				AppendDependencyError(result,
					"Scene settings.pcgSplines must be a ProjectAsset pcgSpline reference");
			else
				CollectStrictAssetReference(FindObjectField(*splines, "guid"), VansAssetType::PcgSpline,
					"Scene settings.pcgSplines", dependencies, result);
		}
	}

	void CollectModelRendererDependencies(
		const VansSerializedValue& entities,
		std::vector<TypedAssetDependency>& dependencies,
		VansSceneAssetDependencyBuildResult& result)
	{
		if (entities.kind != VansSerializedValue::Kind::Array)
			return;

		for (const VansSerializedValue& entity : entities.arrayItems)
		{
			const std::string entityName = ReadSerializedStringField(entity, "name", "<unnamed>");
			const VansSerializedValue* components = ReadSerializedArrayField(entity, "components");
			if (!components)
				continue;
			for (const VansSerializedValue& component : components->arrayItems)
			{
				if (ReadSerializedStringField(component, "type") != "ModelRenderer")
					continue;
				const VansSerializedValue* data = ReadSerializedObjectField(component, "data");
				const std::string owner = "scene entity '" + entityName + "' ModelRenderer";
				CollectStrictAssetReference(
					data ? FindObjectField(*data, "model") : nullptr,
					VansAssetType::Model,
					owner + ".model",
					dependencies,
					result);
				if (!data)
					continue;

				for (const char* fieldName : { "materialOverrides", "submeshMaterialOverrides" })
				{
					const VansSerializedValue* overrides = FindObjectField(*data, fieldName);
					if (!overrides)
						continue;
					if (overrides->kind != VansSerializedValue::Kind::Object)
					{
						AppendDependencyError(result, owner + "." + fieldName + " must be an object");
						continue;
					}
					for (const auto& [slot, material] : overrides->objectFields)
					{
						const std::string materialGuid = ReadAssetGuidReference(material);
						if (materialGuid.empty())
						{
							const VansSerializedValue* guid = material.kind == VansSerializedValue::Kind::Object
								? FindObjectField(material, "guid") : nullptr;
							const bool emptyReference =
								(material.kind == VansSerializedValue::Kind::String && material.stringValue.empty()) ||
								(material.kind == VansSerializedValue::Kind::Object &&
									(material.objectFields.empty() ||
										(guid && guid->kind == VansSerializedValue::Kind::String &&
											guid->stringValue.empty())));
							if (!emptyReference)
								AppendDependencyError(result, owner + "." + fieldName + "['" + slot +
									"'] must be an empty binding or a Material GUID reference");
							continue;
						}
						CollectStrictAssetReference(
							&material,
							VansAssetType::Material,
							owner + "." + fieldName + "['" + slot + "']",
							dependencies,
							result);
					}
				}
			}
		}
	}

	void CollectLodGroupDependencies(
		const VansSerializedValue& entities,
		std::vector<TypedAssetDependency>& dependencies,
		VansSceneAssetDependencyBuildResult& result)
	{
		if (entities.kind != VansSerializedValue::Kind::Array)
			return;
		for (const VansSerializedValue& entity : entities.arrayItems)
		{
			const std::string entityName = ReadSerializedStringField(entity, "name", "<unnamed>");
			const VansSerializedValue* components = ReadSerializedArrayField(entity, "components");
			if (!components)
				continue;
			for (const VansSerializedValue& component : components->arrayItems)
			{
				if (ReadSerializedStringField(component, "type") != "LODGroup")
					continue;
				const VansSerializedValue* data = ReadSerializedObjectField(component, "data");
				const VansSerializedValue* levels = data
					? ReadSerializedArrayField(*data, "levels") : nullptr;
				if (!levels)
					continue;
				for (std::size_t levelIndex = 0; levelIndex < levels->arrayItems.size(); ++levelIndex)
				{
					const VansSerializedValue* meshes = ReadSerializedArrayField(
						levels->arrayItems[levelIndex], "meshes");
					if (!meshes)
						continue;
					for (std::size_t meshIndex = 0; meshIndex < meshes->arrayItems.size(); ++meshIndex)
					{
						const VansSerializedValue& mesh = meshes->arrayItems[meshIndex];
						if (mesh.kind == VansSerializedValue::Kind::String && mesh.stringValue.empty())
							continue;
						CollectStrictAssetReference(
							&mesh,
							VansAssetType::Model,
							"scene entity '" + entityName + "' LODGroup.levels[" +
								std::to_string(levelIndex) + "].meshes[" +
								std::to_string(meshIndex) + "]",
							dependencies,
							result);
					}
				}
			}
		}
	}

	void CollectMultiMeshRootDependencies(
		const VansSerializedValue& entities,
		std::vector<TypedAssetDependency>& dependencies,
		VansSceneAssetDependencyBuildResult& result)
	{
		if (entities.kind != VansSerializedValue::Kind::Array)
			return;
		for (const VansSerializedValue& entity : entities.arrayItems)
		{
			const std::string entityName = ReadSerializedStringField(entity, "name", "<unnamed>");
			const VansSerializedValue* components = ReadSerializedArrayField(entity, "components");
			if (!components)
				continue;
			for (const VansSerializedValue& component : components->arrayItems)
			{
				if (ReadSerializedStringField(component, "type") != "MultiMeshRoot")
					continue;
				const VansSerializedValue* data = ReadSerializedObjectField(component, "data");
				CollectStrictAssetReference(
					data ? FindObjectField(*data, "model") : nullptr,
					VansAssetType::Model,
					"scene entity '" + entityName + "' MultiMeshRoot.model",
					dependencies,
					result);
			}
		}
	}

	void CollectLightDependencies(
		const VansSerializedValue& entities,
		std::vector<TypedAssetDependency>& dependencies,
		std::unordered_set<std::string>& rectLightEmissiveTextures,
		VansSceneAssetDependencyBuildResult& result)
	{
		if (entities.kind != VansSerializedValue::Kind::Array)
			return;
		for (const VansSerializedValue& entity : entities.arrayItems)
		{
			const std::string entityName = ReadSerializedStringField(entity, "name", "<unnamed>");
			const VansSerializedValue* components = ReadSerializedArrayField(entity, "components");
			if (!components)
				continue;
			for (const VansSerializedValue& component : components->arrayItems)
			{
				const std::string type = ReadSerializedStringField(component, "type");
				const VansSerializedValue* data = ReadSerializedObjectField(component, "data");
				if (!data)
					continue;
				if (type == "PointLight" || type == "SpotLight")
				{
					CollectStrictAssetReference(
						FindObjectField(*data, "ies_profile_guid"),
						VansAssetType::IESProfile,
						"scene entity '" + entityName + "' " + type + ".ies_profile_guid",
						dependencies,
						result);
					continue;
				}
				if (type != "RectLight")
					continue;

				const VansSerializedValue* reference =
					FindObjectField(*data, "emissive_texture_guid");
				CollectStrictAssetReference(
					reference,
					VansAssetType::Texture,
					"scene entity '" + entityName + "' RectLight.emissive_texture_guid",
					dependencies,
					result);
				if (reference != nullptr)
				{
					const std::string guidText = ReadAssetGuidReference(*reference);
					VansAssetGuid guid;
					if (VansAssetGuid::TryParse(guidText, guid))
						rectLightEmissiveTextures.insert(guidText);
				}
			}
		}
	}

	void CollectAudioReverbZoneDependencies(
		const VansSerializedValue& entities,
		std::vector<TypedAssetDependency>& dependencies,
		VansSceneAssetDependencyBuildResult& result)
	{
		if (entities.kind != VansSerializedValue::Kind::Array)
			return;
		for (const VansSerializedValue& entity : entities.arrayItems)
		{
			const std::string entityName = ReadSerializedStringField(entity, "name", "<unnamed>");
			const VansSerializedValue* components = ReadSerializedArrayField(entity, "components");
			if (!components)
				continue;
			for (const VansSerializedValue& component : components->arrayItems)
			{
				const std::string type = ReadSerializedStringField(component, "type");
				if (type != "AudioReverbZone" && type != "AudioVolume")
					continue;
				const VansSerializedValue* data = ReadSerializedObjectField(component, "data");
				const VansSerializedValue* reference = data
					? FindObjectField(*data, "presetAsset") : nullptr;
				CollectGuidObjectAssetReference(
					reference,
					VansAssetType::AudioReverbPreset,
					"scene entity '" + entityName + "' " + type + ".presetAsset",
					dependencies,
					result);
			}
		}
	}

	void CollectCameraMediaDependencies(
		const VansSerializedValue& entities,
		std::vector<TypedAssetDependency>& dependencies,
		VansSceneAssetDependencyBuildResult& result)
	{
		if (entities.kind != VansSerializedValue::Kind::Array)
			return;
		for (const VansSerializedValue& entity : entities.arrayItems)
		{
			const std::string entityName = ReadSerializedStringField(entity, "name", "<unnamed>");
			const VansSerializedValue* components = ReadSerializedArrayField(entity, "components");
			if (!components)
				continue;
			for (const VansSerializedValue& component : components->arrayItems)
			{
				const std::string type = ReadSerializedStringField(component, "type");
				if (type != "Audio" && type != "Video")
					continue;
				const VansSerializedValue* data = ReadSerializedObjectField(component, "data");
				CollectGuidObjectAssetReference(
					data ? FindObjectField(*data, "source") : nullptr,
					type == "Audio" ? VansAssetType::Audio : VansAssetType::Video,
					"scene entity '" + entityName + "' " + type + ".source",
					dependencies,
					result);
			}
		}
	}

	void CollectLocalFogDependencies(
		const VansSerializedValue& entities,
		std::vector<TypedAssetDependency>& dependencies,
		std::unordered_map<std::string, int>& fieldTextureRequiredChannels,
		VansSceneAssetDependencyBuildResult& result)
	{
		if (entities.kind != VansSerializedValue::Kind::Array)
			return;
		for (const VansSerializedValue& entity : entities.arrayItems)
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

	void CollectAnimationDependencies(
		const VansSerializedValue& entities,
		std::vector<TypedAssetDependency>& dependencies,
		VansSceneAssetDependencyBuildResult& result)
	{
		if (entities.kind != VansSerializedValue::Kind::Array)
			return;
		for (const VansSerializedValue& entity : entities.arrayItems)
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

	void CollectTimelineDependencies(
		const VansSerializedValue& entities,
		std::vector<TypedAssetDependency>& dependencies,
		VansSceneAssetDependencyBuildResult& result)
	{
		if (entities.kind != VansSerializedValue::Kind::Array) return;
		for (const VansSerializedValue& entity : entities.arrayItems)
		{
			const std::string entityName = ReadSerializedStringField(entity, "name", "<unnamed>");
			const VansSerializedValue* components = ReadSerializedArrayField(entity, "components");
			if (components == nullptr) continue;
			for (const VansSerializedValue& component : components->arrayItems)
			{
				if (ReadSerializedStringField(component, "type") != "Timeline") continue;
				const VansSerializedValue* data = ReadSerializedObjectField(component, "data");
				const VansSerializedValue* timeline = data ? ReadSerializedObjectField(*data, "timeline") : nullptr;
				CollectGuidObjectAssetReference(timeline, VansAssetType::Timeline,
					"scene entity '" + entityName + "' Timeline.timeline", dependencies, result);
			}
		}
	}

	void CollectAIDependencies(
		const VansSerializedValue& entities,
		std::vector<TypedAssetDependency>& dependencies,
		VansSceneAssetDependencyBuildResult& result)
	{
		if (entities.kind != VansSerializedValue::Kind::Array) return;
		for (const VansSerializedValue& entity : entities.arrayItems)
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
	const VansSerializedValue& inputDocument,
	const std::filesystem::path& sceneSourcePath,
	const std::unordered_map<std::string, std::string>& runtimeAssetBindings,
	const VansAssetObjectRepository& objectRepository,
	VansAssetDatabase* builtInAssetDatabase)
{
	VansSceneAssetDependencyBuildResult result;
	const VansSerializedValue& sceneDocument = inputDocument;

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
    if (const auto* declared = FindObjectField(sceneDocument, "prefabAssets"))
    {
        if (declared->kind != VansSerializedValue::Kind::Array)
        { result.errors.push_back("/prefabAssets must be an array of Prefab GUIDs"); return result; }
        for (const auto& reference : declared->arrayItems)
        {
            const auto found = assetTypesByGuid.find(reference.stringValue);
            if (reference.kind != VansSerializedValue::Kind::String || found == assetTypesByGuid.end() || found->second != VansAssetType::Prefab)
            { result.errors.push_back("Missing or invalid dynamic Prefab asset: " + reference.stringValue); return result; }
            result.requiredAssets.insert(reference.stringValue);
        }
    }
	// 动态模板保留源 identity，只遍历模板数据；依赖收集不生成运行时实例或展开副本。
	std::vector<std::shared_ptr<const VansPrefabAsset>> prefabDependencyAssets;
    std::unordered_set<std::string> expandedPrefabs;
    for (;;)
    {
        std::string pending;
        for (const auto& guid : result.requiredAssets)
            if (assetTypesByGuid[guid] == VansAssetType::Prefab && !expandedPrefabs.count(guid))
            { pending = guid; break; }
        if (pending.empty()) break;
        expandedPrefabs.insert(pending);
        VansAssetGuid guid; VansAssetGuid::TryParse(pending, guid);
        const auto asset = objectRepository.ResolveLatest<VansPrefabAsset>(guid);
        if (!asset) { result.errors.push_back("Prefab memory asset missing: " + pending); return result; }
		std::string error;
		if (!VansPrefabCodec::Validate(*asset, error))
        { result.errors.push_back("Prefab " + pending + ": " + error); return result; }
		prefabDependencyAssets.push_back(asset);
    }

	std::unordered_set<std::string> meshColliderModels;
	std::unordered_set<std::string> terrainHeightTextures;
	std::unordered_set<std::string> terrainSplatTextures;
	std::unordered_set<std::string> pcgPixelTextures;
	std::unordered_set<std::string> pcgGrassModels;
	std::vector<TypedAssetDependency> animationDependencies;
	std::vector<TypedAssetDependency> timelineDependencies;
	std::vector<TypedAssetDependency> aiDependencies;
	std::vector<TypedAssetDependency> cameraMediaDependencies;
	std::vector<TypedAssetDependency> audioReverbZoneDependencies;
	std::vector<TypedAssetDependency> lodGroupDependencies;
	std::vector<TypedAssetDependency> multiMeshRootDependencies;
	std::vector<TypedAssetDependency> localFogDependencies;
	std::vector<TypedAssetDependency> lightDependencies;
	std::vector<TypedAssetDependency> catalogDependencies;
	std::vector<TypedAssetDependency> sceneSettingDependencies;
	std::unordered_set<std::string> rectLightEmissiveTextures;
	std::unordered_map<std::string, int> localFogFieldTextureRequiredChannels;
	std::vector<TypedAssetDependency> modelRendererDependencies;
	const auto collectEntityDependencies = [&](const VansSerializedValue& entityArray)
	{
		CollectCatalogComponentDependencies(entityArray, catalogDependencies, result);
		CollectScriptFieldAssetDependencies(entityArray, catalogDependencies, result);
		CollectModelRendererDependencies(
			entityArray, modelRendererDependencies, result);
		CollectPhysicsMeshColliderDependencies(entityArray, meshColliderModels);
		CollectAnimationDependencies(entityArray, animationDependencies, result);
		CollectTimelineDependencies(entityArray, timelineDependencies, result);
		CollectAIDependencies(entityArray, aiDependencies, result);
		CollectCameraMediaDependencies(entityArray, cameraMediaDependencies, result);
		CollectAudioReverbZoneDependencies(
			entityArray, audioReverbZoneDependencies, result);
		CollectLodGroupDependencies(entityArray, lodGroupDependencies, result);
		CollectMultiMeshRootDependencies(
			entityArray, multiMeshRootDependencies, result);
		CollectLightDependencies(
			entityArray, lightDependencies, rectLightEmissiveTextures, result);
		CollectLocalFogDependencies(
			entityArray, localFogDependencies, localFogFieldTextureRequiredChannels, result);
	};
	collectEntityDependencies(*entities);
	for (const auto& prefab : prefabDependencyAssets)
		collectEntityDependencies(prefab->entities);
	CollectSceneSettingDependencies(sceneDocument, sceneSettingDependencies, result);
	result.requiredModels.insert(meshColliderModels.begin(), meshColliderModels.end());
	if (const VansSerializedValue* vegetationConfig = ReadVegetationField(sceneDocument))
	{
		const std::string vegetationGuidText =
			VansVegetationConfigCodec::ReadReferenceGuid(*vegetationConfig);
		VansVegetationConfigAsset vegetationAsset;
		VansPcgRecipeAsset effectiveVegetation;
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
		if (vegetationResolved)
		{
			result.requiredAssets.insert(vegetationGuidText);
			const auto pcg = BuildPcgResourcePlan(effectiveVegetation, objectRepository,
				[&](VansAssetGuid guid) { return database.Find(guid); });
			if (!pcg) result.errors.push_back(pcg.error);
			else for (const auto& resource : pcg.resources)
			{
				const std::string guid = resource.guid.ToString();
				result.requiredAssets.insert(guid);
				if (resource.meshCpuData) pcgGrassModels.insert(guid);
				if (resource.maskPixels) pcgPixelTextures.insert(guid);
				else if (resource.type == VansAssetType::Model) result.requiredModels.insert(guid);
				else if (resource.type == VansAssetType::Material) result.requiredMaterials.insert(guid);
			}
		}
	}
	result.requiredAssets.insert(result.requiredModels.begin(), result.requiredModels.end());
	result.requiredAssets.insert(result.requiredMaterials.begin(), result.requiredMaterials.end());
	result.requiredAssets.insert(result.requiredTextures.begin(), result.requiredTextures.end());
	result.requiredAssets.insert(result.requiredShaders.begin(), result.requiredShaders.end());
	result.requiredAssets.insert(result.requiredSkinProfiles.begin(), result.requiredSkinProfiles.end());

	std::deque<TypedAssetDependency> pendingDependencies(
		modelRendererDependencies.begin(), modelRendererDependencies.end());
	pendingDependencies.insert(pendingDependencies.end(),
		animationDependencies.begin(), animationDependencies.end());
	pendingDependencies.insert(pendingDependencies.end(),
		timelineDependencies.begin(), timelineDependencies.end());
	pendingDependencies.insert(pendingDependencies.end(),
		aiDependencies.begin(), aiDependencies.end());
	pendingDependencies.insert(pendingDependencies.end(),
		cameraMediaDependencies.begin(), cameraMediaDependencies.end());
	pendingDependencies.insert(pendingDependencies.end(),
		audioReverbZoneDependencies.begin(), audioReverbZoneDependencies.end());
	pendingDependencies.insert(pendingDependencies.end(),
		lodGroupDependencies.begin(), lodGroupDependencies.end());
	pendingDependencies.insert(pendingDependencies.end(),
		multiMeshRootDependencies.begin(), multiMeshRootDependencies.end());
	pendingDependencies.insert(pendingDependencies.end(),
		localFogDependencies.begin(), localFogDependencies.end());
	pendingDependencies.insert(pendingDependencies.end(),
		lightDependencies.begin(), lightDependencies.end());
	pendingDependencies.insert(pendingDependencies.end(),
		catalogDependencies.begin(), catalogDependencies.end());
	pendingDependencies.insert(pendingDependencies.end(),
		sceneSettingDependencies.begin(), sceneSettingDependencies.end());
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
		else if (record.type == VansAssetType::AudioReverbPreset)
		{
			if (!ResolveMemoryAsset<VansAudioReverbPresetAsset>(objectRepository, record))
			{
				AppendDependencyError(result,
					dependency.chain + " has no Audio Reverb Preset memory object");
				continue;
			}
		}
		if (record.type == VansAssetType::Timeline)
		{
			const VansEngineTimelineCatalog catalog = VansGetEngineTimelineCatalog();
			if (!catalog)
			{
				AppendDependencyError(result, dependency.chain + ": " + std::string(catalog.error));
				continue;
			}
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
				*catalog.trackExtensions,
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
			// Empty mask means a generic full-body overlay. It has no asset
			// dependency; the runtime expands it to a full-body mask.
			if (layer.maskGuid.empty() && layer.maskPathHint.empty())
				continue;
			pendingDependencies.push_back({ layer.maskGuid, VansAssetType::BoneMask,
				dependency.chain + " -> Layer '" + layer.name + "' Bone Mask -> " + layer.maskGuid });
		}
	}
	if (!result.errors.empty())
		return result;

	ExpandUIAssetDependencies(assetRecordsByGuid, objectRepository, result);
	// PCG 可以显式引用一个高度场，依赖处理不依赖它在场景中的先后位置。
	ExpandTerrainDependencies(assetRecordsByGuid, objectRepository, result,
		terrainHeightTextures, terrainSplatTextures);
	if (!result.errors.empty())
		return result;

    ExpandParticleDependencies(assetRecordsByGuid, objectRepository, result);
    if (!result.errors.empty()) return result;

	// Runtime scene projection publishes every project Material into the scene material
	// manager so name-based model material resolution keeps working even without an
	// explicit scene GUID override.  The GPU resource closure must mirror that policy;
	// otherwise a packaged build can contain the Material authoring snapshot while
	// omitting its texture artifacts.
	for (const VansAssetRecord& record : database.All())
	{
		if (record.type != VansAssetType::Material || record.state == VansAssetState::Missing)
			continue;
		result.requiredMaterials.insert(record.guid.ToString());
		result.requiredAssets.insert(record.guid.ToString());
	}

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
			CollectMaterialDependencies(material, assetRecordsByGuid, result);
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
			const VansDerivedArtifactLocation artifact =
				VansDerivedArtifactLayout::ImportedRuntimeCache(
					database.ArtifactRoot(), record.type, record.guid);
			if (artifact)
				request.artifactPath = artifact.path.string();
			request.needTangent = meta.ReadBoolSetting("generateTangents", true);
			request.supportRayTracing = meta.ReadBoolSetting("buildRayTracingData", true);
			request.needCpuData = meta.ReadBoolSetting("keepCpuMeshData", false) ||
				pcgGrassModels.find(record.guid.ToString()) != pcgGrassModels.end() ||
				meshColliderModels.find(record.guid.ToString()) != meshColliderModels.end();
			request.scaleFactor = meta.ReadFloatSetting("scaleFactor", 1.0f);
			request.loadMultiMesh = meta.ReadBoolSetting("loadMultiMesh", isFbx);
			request.skeletalImport = ReadSkeletalMeshImportSettings(meta);
			result.resourcePlan.meshes.push_back(std::move(request));
		}
		else if (record.type == VansAssetType::Texture)
		{
			const bool terrainHeight = terrainHeightTextures.find(record.guid.ToString()) !=
				terrainHeightTextures.end() || pcgPixelTextures.count(record.guid.ToString()) != 0;
			const bool terrainSplat = terrainSplatTextures.find(record.guid.ToString()) !=
				terrainSplatTextures.end();
			if (terrainHeight || terrainSplat)
			{
				const std::string colorSpace = LowerAsciiCopy(meta.ReadStringSetting("colorSpace"));
				const bool linear = colorSpace == "linear" ||
					(colorSpace.empty() && !meta.ReadBoolSetting("sRGB", true));
				const bool uncompressed = !meta.ReadBoolSetting("useCompress", true);
				const bool noMip = !meta.ReadBoolSetting("needMip", true);
				const int channels = meta.ReadIntSetting("importChannel", terrainHeight ? 1 : 4);
				const std::string precision = LowerAsciiCopy(meta.ReadStringSetting("precision"));
				const std::string addressMode = LowerAsciiCopy(meta.ReadStringSetting("addressMode"));
				const bool valid = linear && uncompressed && noMip && addressMode == "clamp" &&
					(terrainHeight ? channels == 1 && precision == "mid16"
						: channels == 4 && precision == "low8");
				if (!valid)
				{
					AppendDependencyError(result,
						std::string(terrainHeight ? "Height/PCG data texture " : "Terrain splat texture ") +
						record.guid.ToString() +
						(terrainHeight
							? " must be linear, uncompressed, non-mipmapped, clamp, one-channel mid16"
							: " must be linear, uncompressed, non-mipmapped, clamp, four-channel low8"));
					continue;
				}
			}
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
					meta.ReadBoolSetting("useCompress", true),
					meta.ReadBoolSetting("needMip", true),
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
			request.retainRgba8Pixels =
				rectLightEmissiveTextures.count(record.guid.ToString()) != 0;
			if (request.retainRgba8Pixels && !SupportsRectLightEmissive(request))
			{
				AppendDependencyError(result,
					"RectLight emissive texture " + record.guid.ToString() +
					" must be a 2D, uncompressed, four-channel low8 texture");
				continue;
			}
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
				*shader, record, projectRoot, database.ArtifactRoot()));
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
				const VansDerivedArtifactLocation artifact =
					VansDerivedArtifactLayout::ImportedRuntimeCache(
						builtInAssetDatabase->ArtifactRoot(), record->type, record->guid);
				if (artifact)
					request.artifactPath = artifact.path.string();
				request.needTangent = meta.ReadBoolSetting("generateTangents", true);
				request.supportRayTracing = meta.ReadBoolSetting("buildRayTracingData", true);
				request.needCpuData = meta.ReadBoolSetting("keepCpuMeshData", false);
				request.scaleFactor = meta.ReadFloatSetting("scaleFactor", 1.0f);
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
				request.retainRgba8Pixels =
					rectLightEmissiveTextures.count(record->guid.ToString()) != 0;
				if (request.retainRgba8Pixels && !SupportsRectLightEmissive(request))
				{
					AppendDependencyError(result,
						"RectLight emissive texture " + record->guid.ToString() +
						" must be a 2D, uncompressed, four-channel low8 texture");
					break;
				}
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
	// 根集合中的模型、材质等也必须验证；只遍历已有记录会静默漏掉缺失资源。
	for (const auto& guid : result.requiredAssets)
	{
		const auto found = assetRecordsByGuid.find(guid);
		if (found == assetRecordsByGuid.end() || found->second.state == VansAssetState::Missing)
		{
			AppendDependencyError(result, "Required asset is missing: " + guid);
			continue;
		}
		const auto type = found->second.type;
		if ((result.requiredModels.count(guid) && type != VansAssetType::Model) ||
			(result.requiredMaterials.count(guid) && type != VansAssetType::Material) ||
			(result.requiredTextures.count(guid) && type != VansAssetType::Texture) ||
			(result.requiredShaders.count(guid) && type != VansAssetType::Shader) ||
			(result.requiredSkinProfiles.count(guid) && type != VansAssetType::SkinProfile))
			AppendDependencyError(result, "Required asset has the wrong type: " + guid);
	}
	if (!result.errors.empty()) return result;
	ValidateNavigationSources(*entities, projectRoot, sceneSourcePath,
		assetRecordsByGuid, objectRepository, result);
	if (!result.errors.empty()) return result;
	result.success = true;
	return result;
}
}
