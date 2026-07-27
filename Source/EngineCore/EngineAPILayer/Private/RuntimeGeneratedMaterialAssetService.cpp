#include "RuntimeGeneratedMaterialAssetService.h"

#include "../../AssetCore/VansAssetDatabase.h"
#include "../../AssetCore/VansAssetGuid.h"
#include "../../AssetCore/VansAssetMeta.h"
#include "../../AssetCore/VansMaterialAuthoringAsset.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../AssetCore/Storage/VansAssetMetaStorage.h"
#include "../../AssetCore/Storage/VansMaterialAuthoringAssetStorage.h"
#include "../../AssetCore/Storage/VansStagedFileTransaction.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../RenderCore/VansMaterial.h"
#include "../../RenderCore/VansRenderNode.h"
#include "../../RenderCore/VulkanCore/VansMesh.h"
#include "../../RenderCore/VulkanCore/VansTexture.h"
#include "../../Util/VansLog.h"

#include <../../GLM/glm.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace Vans::EditorAPI
{
namespace
{
std::string SafeRuntimeAssetName(std::string value)
{
	if (value.empty())
		value = "Unnamed";
	for (char& c : value)
	{
		const unsigned char uc = static_cast<unsigned char>(c);
		if (!std::isalnum(uc) && c != '_' && c != '-')
			c = '_';
	}
	while (!value.empty() && value.front() == '_') value.erase(value.begin());
	while (!value.empty() && value.back() == '_') value.pop_back();
	if (value.empty())
		value = "Unnamed";
	if (value.size() > 96)
		value.resize(96);
	return value;
}

Vans::VansSerializedValue Vec3Value(const glm::vec3& value)
{
	return Vans::VansSerializedValue::Array({
		Vans::VansSerializedValue::Float(value.x),
		Vans::VansSerializedValue::Float(value.y),
		Vans::VansSerializedValue::Float(value.z)
	});
}

Vans::VansSerializedValue TextureReferenceValue(const std::string& guid)
{
	return Vans::VansSerializedValue::Object({
		{ "guid", Vans::VansSerializedValue::String(guid) }
	});
}

void AppendTransparentTexture(
	Vans::VansSerializedValue& textures,
	std::string slot,
	std::string textureGuid)
{
	if (textureGuid.empty())
		return;
	if (textures.kind != Vans::VansSerializedValue::Kind::Array)
		textures = Vans::VansSerializedValue::Array({});
	textures.arrayItems.push_back(Vans::VansSerializedValue::Object({
		{ "slot", Vans::VansSerializedValue::String(std::move(slot)) },
		{ "texture", TextureReferenceValue(textureGuid) }
	}));
}

Vans::VansAssetGuid ReadOrCreateMetaGuid(const std::filesystem::path& metaPath)
{
	Vans::VansAssetMeta meta;
	std::string error;
	if (Vans::VansAssetMetaStorage::Load(metaPath, meta, error) && meta.guid.IsValid())
		return meta.guid;
	return Vans::VansAssetGuid::New();
}

bool IsGuidString(const std::string& value)
{
	Vans::VansAssetGuid parsed;
	return Vans::VansAssetGuid::TryParse(value, parsed);
}

std::string LowerAscii(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

std::string ResolveRuntimeTextureGuid(
	const std::string& textureName,
	Vans::VansAssetDatabase* database,
	const std::string& rootName)
{
	if (textureName.empty())
		return {};
	if (IsGuidString(textureName))
		return textureName;
	if (database == nullptr)
		return {};

	const std::string wanted = LowerAscii(std::filesystem::path(textureName).stem().string());
	const std::string rootToken = LowerAscii(SafeRuntimeAssetName(rootName));
	std::string fallbackGuid;
	for (const Vans::VansAssetRecord& record : database->All())
	{
		if (record.type != Vans::VansAssetType::Texture || record.state == Vans::VansAssetState::Missing)
			continue;
		const std::string recordStem = LowerAscii(record.sourcePath.stem().string());
		const std::string recordFile = LowerAscii(record.sourcePath.filename().string());
		if (recordStem != wanted && recordFile != LowerAscii(textureName))
			continue;

		if (fallbackGuid.empty())
			fallbackGuid = record.guid.ToString();
		const std::string recordPath = LowerAscii(record.sourcePath.generic_string());
		if (!rootToken.empty() && recordPath.find(rootToken) != std::string::npos)
			return record.guid.ToString();
	}
	return fallbackGuid;
}

std::string ResolveRuntimeTextureGuid(
	VansGraphics::VansTexture* texture,
	Vans::VansAssetDatabase* database,
	const std::string& rootName)
{
	return texture ? ResolveRuntimeTextureGuid(texture->m_AssetName, database, rootName) : std::string{};
}

bool IsDefaultRuntimeTextureName(const std::string& textureName)
{
	const std::string lowered = LowerAscii(std::filesystem::path(textureName).stem().string());
	return lowered == "defaultalbedo" ||
		   lowered == "defaultnormal" ||
		   lowered == "defaultmetal" ||
		   lowered == "defaultroughness" ||
		   lowered == "defaultao";
}

void AddTextureRefIfResolvable(
	Vans::VansSerializedValue& textures,
	const char* slot,
	VansGraphics::VansTexture* texture,
	Vans::VansAssetDatabase* database,
	const std::string& rootName)
{
	const std::string textureGuid = ResolveRuntimeTextureGuid(texture, database, rootName);
	if (!textureGuid.empty())
		Vans::SetSerializedObjectField(textures, slot, TextureReferenceValue(textureGuid));
}

void AddTextureRefFromPathIfResolvable(
	Vans::VansSerializedValue& textures,
	const char* slot,
	const std::string& texturePath,
	Vans::VansAssetDatabase* database,
	const std::string& rootName)
{
	const std::string textureGuid = ResolveRuntimeTextureGuid(texturePath, database, rootName);
	if (!textureGuid.empty())
		Vans::SetSerializedObjectField(textures, slot, TextureReferenceValue(textureGuid));
}

Vans::VansMaterialAuthoringAsset BuildFbxMaterialAsset(
	const VansGraphics::FBXSubmeshMaterialInfo& fbxInfo,
	Vans::VansAssetDatabase* database,
	const std::string& rootName)
{
	Vans::VansMaterialAuthoringAsset asset;

	if (fbxInfo.IsTransparent())
	{
		asset.materialType = "transparent";
		asset.parameters = Vans::VansSerializedValue::Object({});
		asset.textures = Vans::VansSerializedValue::Array({});

		auto addTransparentTexture = [&](const char* slot, const std::string& texturePath)
		{
			const std::string textureGuid = ResolveRuntimeTextureGuid(texturePath, database, rootName);
			AppendTransparentTexture(asset.textures, slot, textureGuid);
		};

		addTransparentTexture("diffuse", fbxInfo.diffuseTexPath);
		addTransparentTexture("opacity", fbxInfo.opacityTexPath);
		return asset;
	}

	asset.materialType = Vans::DefaultMaterialAuthoringType();
	asset.parameters = Vans::CreatePbrMaterialAuthoringParameters(
		fbxInfo.diffuseColor[0],
		fbxInfo.diffuseColor[1],
		fbxInfo.diffuseColor[2],
		fbxInfo.metallic,
		fbxInfo.roughness,
		1.0f);

	asset.textures = Vans::VansSerializedValue::Object({});
	AddTextureRefFromPathIfResolvable(asset.textures, "basecolor", fbxInfo.diffuseTexPath, database, rootName);
	AddTextureRefFromPathIfResolvable(asset.textures, "normal", fbxInfo.normalTexPath, database, rootName);
	AddTextureRefFromPathIfResolvable(asset.textures, "metal", fbxInfo.metallicTexPath, database, rootName);
	AddTextureRefFromPathIfResolvable(asset.textures, "roughness", fbxInfo.roughnessTexPath, database, rootName);
	AddTextureRefFromPathIfResolvable(asset.textures, "ao", fbxInfo.aoTexPath, database, rootName);
	return asset;
}

Vans::VansMaterialAuthoringAsset BuildRuntimeMaterialAsset(
	VansGraphics::VansMaterial* material,
	Vans::VansAssetDatabase* database,
	const std::string& rootName)
{
	Vans::VansMaterialAuthoringAsset asset;

	if (auto* pbr = dynamic_cast<VansGraphics::VansPBRMaterial*>(material))
	{
		asset.materialType = Vans::DefaultMaterialAuthoringType();
		asset.parameters = Vans::CreatePbrMaterialAuthoringParameters(
			pbr->m_BasePBRParam.m_albedo.x,
			pbr->m_BasePBRParam.m_albedo.y,
			pbr->m_BasePBRParam.m_albedo.z,
			pbr->m_BasePBRParam.m_metallic,
			pbr->m_BasePBRParam.m_roughness,
			pbr->m_BasePBRParam.m_ao);
		asset.textures = Vans::VansSerializedValue::Object({});
		AddTextureRefIfResolvable(asset.textures, "basecolor", pbr->m_BaseColorTexture, database, rootName);
		AddTextureRefIfResolvable(asset.textures, "normal", pbr->m_NormalTexture, database, rootName);
		AddTextureRefIfResolvable(asset.textures, "metal", pbr->m_MetalTexture, database, rootName);
		AddTextureRefIfResolvable(asset.textures, "roughness", pbr->m_RoughnessTexture, database, rootName);
		AddTextureRefIfResolvable(asset.textures, "ao", pbr->m_AoTexture, database, rootName);
		return asset;
	}

	if (auto* cloth = dynamic_cast<VansGraphics::VansClothMaterial*>(material))
	{
		const char* clothModel = "fuzz";
		if (cloth->m_ClothModel == VansGraphics::VansClothModel::Silk) clothModel = "silk";
		else if (cloth->m_ClothModel == VansGraphics::VansClothModel::Thin) clothModel = "thin";

		asset.materialType = "cloth";
		asset.parameters = Vans::VansSerializedValue::Object({
			{ "albedo", Vec3Value(cloth->m_BasePBRParam.m_albedo) },
			{ "clothModel", Vans::VansSerializedValue::String(clothModel) },
			{ "sheenRoughness", Vans::VansSerializedValue::Float(cloth->m_SheenRoughness) },
			{ "sheenStrength", Vans::VansSerializedValue::Float(cloth->m_SheenStrength) },
			{ "anisotropy", Vans::VansSerializedValue::Float(cloth->m_Anisotropy) },
			{ "transmissionColor", Vec3Value(cloth->m_TransmissionColor) },
			{ "translucency", Vans::VansSerializedValue::Float(cloth->m_Translucency) },
			{ "thickness", Vans::VansSerializedValue::Float(cloth->m_Thickness) },
			{ "ao", Vans::VansSerializedValue::Float(cloth->m_BasePBRParam.m_ao) }
		});
		if ((cloth->m_ClothFlags & VansGraphics::VANS_CLOTH_FLAG_ALBEDO_SHEEN_TINT) == 0u)
			Vans::SetSerializedObjectField(asset.parameters, "sheenColor", Vec3Value(cloth->m_SheenColor));
		asset.textures = Vans::VansSerializedValue::Object({});
		AddTextureRefIfResolvable(asset.textures, "basecolor", cloth->m_BaseColorTexture, database, rootName);
		AddTextureRefIfResolvable(asset.textures, "normal", cloth->m_NormalTexture, database, rootName);
		AddTextureRefIfResolvable(asset.textures, "roughness", cloth->m_RoughnessTexture, database, rootName);
		AddTextureRefIfResolvable(asset.textures, "ao", cloth->m_AoTexture, database, rootName);
		return asset;
	}

	if (auto* transparent = dynamic_cast<VansGraphics::VansTransparentMaterial*>(material))
	{
		asset.materialType = "transparent";
		asset.parameters = Vans::VansSerializedValue::Object({});
		asset.textures = Vans::VansSerializedValue::Array({});
		const size_t textureCount = std::max(
			transparent->m_TransparentTextures.size(),
			transparent->m_TransparentTextureMap.size());
		for (size_t index = 0; index < textureCount; ++index)
		{
			const std::string slot = index < transparent->m_TransparentTextureMap.size()
				? transparent->m_TransparentTextureMap[index].first
				: "texture_" + std::to_string(index);
			std::string textureName = index < transparent->m_TransparentTextureMap.size()
				? transparent->m_TransparentTextureMap[index].second
				: std::string{};
			if (textureName.empty() && index < transparent->m_TransparentTextures.size()
				&& transparent->m_TransparentTextures[index] != nullptr)
			{
				textureName = transparent->m_TransparentTextures[index]->m_AssetName;
			}
			if (textureName.empty())
				continue;

			const std::string textureGuid = ResolveRuntimeTextureGuid(textureName, database, rootName);
			AppendTransparentTexture(asset.textures, slot, textureGuid);
		}
		return asset;
	}

	asset.materialType = Vans::DefaultMaterialAuthoringType();
	asset.parameters = Vans::CreatePbrMaterialAuthoringParameters(1.0f, 1.0f, 1.0f, 0.0f, 0.5f, 1.0f);
	asset.textures = Vans::VansSerializedValue::Object({});
	return asset;
}

bool HasTextureSlot(const Vans::VansSerializedValue& textures, const std::string& slot)
{
	if (textures.kind == Vans::VansSerializedValue::Kind::Object)
		return Vans::FindObjectField(textures, slot) != nullptr;

	if (textures.kind == Vans::VansSerializedValue::Kind::Array)
	{
		for (const Vans::VansSerializedValue& entry : textures.arrayItems)
		{
			if (entry.kind == Vans::VansSerializedValue::Kind::Object &&
				Vans::ReadSerializedStringField(entry, "slot") == slot)
				return true;
		}
	}

	return false;
}

void AddRuntimeBaseColorFallback(
	Vans::VansMaterialAuthoringAsset& materialAsset,
	VansGraphics::VansMaterial* material,
	Vans::VansAssetDatabase* database,
	const std::string& rootName)
{
	if (material == nullptr || database == nullptr)
		return;

	if (Vans::IsTransparentMaterialAuthoringType(materialAsset.materialType))
	{
		if (HasTextureSlot(materialAsset.textures, "diffuse"))
			return;

		auto* transparent = dynamic_cast<VansGraphics::VansTransparentMaterial*>(material);
		if (transparent == nullptr)
			return;

		std::string textureName;
		for (const auto& [slot, name] : transparent->m_TransparentTextureMap)
		{
			if (slot == "diffuse" || slot == "basecolor" || slot == "baseColor")
			{
				textureName = name;
				break;
			}
		}
		if (textureName.empty() && !transparent->m_TransparentTextures.empty()
			&& transparent->m_TransparentTextures[0] != nullptr)
		{
			textureName = transparent->m_TransparentTextures[0]->m_AssetName;
		}
		if (IsDefaultRuntimeTextureName(textureName))
		{
			return;
		}

		const std::string textureGuid = ResolveRuntimeTextureGuid(textureName, database, rootName);
		if (!textureGuid.empty())
			AppendTransparentTexture(materialAsset.textures, "diffuse", textureGuid);
		return;
	}

	if (HasTextureSlot(materialAsset.textures, "basecolor"))
		return;

	auto* pbr = dynamic_cast<VansGraphics::VansPBRMaterial*>(material);
	if (pbr == nullptr || pbr->m_BaseColorTexture == nullptr)
		return;

	const std::string runtimeTextureName = pbr->m_BaseColorTexture->m_AssetName;
	if (IsDefaultRuntimeTextureName(runtimeTextureName))
	{
		return;
	}

	const std::string textureGuid = ResolveRuntimeTextureGuid(pbr->m_BaseColorTexture, database, rootName);
	if (!textureGuid.empty())
	{
		if (materialAsset.textures.kind != Vans::VansSerializedValue::Kind::Object)
			materialAsset.textures = Vans::VansSerializedValue::Object({});
		Vans::SetSerializedObjectField(materialAsset.textures, "basecolor", TextureReferenceValue(textureGuid));
	}
	else
	{
		VANS_LOG_WARN("[MultiMeshMaterialGen] Runtime basecolor fallback unresolved for "
			<< rootName << " material=" << material->m_AssetName
			<< " texture=" << runtimeTextureName);
	}
}
}

std::string SanitizeRuntimeGeneratedMaterialText(std::string value)
{
	for (char& c : value)
	{
		const unsigned char uc = static_cast<unsigned char>(c);
		if (uc < 0x20 || uc >= 0x7f)
			c = '_';
	}
	return value;
}

std::string EnsureRuntimeGeneratedMaterialAsset(
	const std::string& rootName,
	VansGraphics::VansRenderNode* node,
	const std::filesystem::path& assetsRoot)
{
	if (node == nullptr || node->m_Material == nullptr)
		return {};

	const std::string materialName = SafeRuntimeAssetName(
		rootName + "_" + node->m_Material->m_AssetName + "_" + std::to_string(node->m_SubmeshIndex));
	const std::filesystem::path materialDir = assetsRoot / "Generated" / "MultiMeshMaterials" / SafeRuntimeAssetName(rootName);
	const std::filesystem::path materialPath = materialDir / (materialName + ".mat");
	const std::filesystem::path metaPath = materialPath.string() + ".meta";
	const Vans::VansAssetGuid guid = ReadOrCreateMetaGuid(metaPath);

	Vans::VansAssetDatabase* database = Vans::VansProjectManager::Get().GetAssetDatabase();
	Vans::VansMaterialAuthoringAsset materialAsset;
	if (node->m_SourceMesh != nullptr &&
		node->m_SubmeshIndex != UINT32_MAX &&
		!node->m_SourceMesh->m_SubmeshMaterialInfos.empty())
	{
		const auto& materialInfos = node->m_SourceMesh->m_SubmeshMaterialInfos;
		const VansGraphics::FBXSubmeshMaterialInfo& fbxInfo =
			node->m_SubmeshIndex < materialInfos.size() ? materialInfos[node->m_SubmeshIndex] : materialInfos[0];
		materialAsset = BuildFbxMaterialAsset(fbxInfo, database, rootName);
		AddRuntimeBaseColorFallback(materialAsset, node->m_Material, database, rootName);
	}
	else
	{
		materialAsset = BuildRuntimeMaterialAsset(node->m_Material, database, rootName);
	}
	materialAsset.guid = guid.ToString();
	materialAsset.importSource = Vans::VansSerializedValue::Object({
		{ "model", Vans::VansSerializedValue::String(rootName) },
		{ "sourceNode", Vans::VansSerializedValue::String(SanitizeRuntimeGeneratedMaterialText(
			node->m_Mesh ? node->m_Mesh->m_SourceNodeName : std::string{})) },
		{ "sourceMaterial", Vans::VansSerializedValue::String(SanitizeRuntimeGeneratedMaterialText(
			node->m_Material->m_AssetName)) },
		{ "submeshIndex", Vans::VansSerializedValue::Int(static_cast<std::int64_t>(node->m_SubmeshIndex)) },
		{ "generatedFor", Vans::VansSerializedValue::String("runtimeMultiMeshExpansion") }
	});

	Vans::VansStagedFileTransaction transaction;
	Vans::VansStagedFile materialStage;
	std::string writeError;
	if (!Vans::VansMaterialAuthoringAssetStorage::StageWrite(materialPath, materialAsset, materialStage, writeError))
	{
		VANS_LOG_ERROR("[MultiMeshMaterialGen] Failed staging generated material '"
			<< materialPath.string() << "': " << writeError);
		return {};
	}
	transaction.Add(std::move(materialStage));

	Vans::VansAssetMeta meta;
	meta.guid = guid;
	meta.importer = "MaterialImporter";
	meta.version = 1u;
	meta.SetStringSetting("generatedFrom", rootName);
	meta.SetStringSetting("generatedFor", "runtimeMultiMeshExpansion");
	Vans::VansStagedFile metaStage;
	if (!Vans::VansAssetMetaStorage::StageSave(metaPath, meta, metaStage, writeError))
	{
		VANS_LOG_ERROR("[MultiMeshMaterialGen] Failed staging generated material meta '"
			<< metaPath.string() << "': " << writeError);
		return {};
	}
	transaction.Add(std::move(metaStage));

	if (!transaction.Publish(writeError))
	{
		VANS_LOG_ERROR("[MultiMeshMaterialGen] Failed publishing generated material pair '"
			<< materialPath.string() << "': " << writeError);
		return {};
	}

	return guid.ToString();
}
}
