#include "VansMaterialLiveEditService.h"

#include "../../AssetCore/VansAssetDatabase.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../RenderCore/VansMaterial.h"
#include "../../RenderCore/VansRenderNode.h"
#include "../../RenderCore/VansScene.h"
#include "../../RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../../Util/VansLog.h"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <variant>

namespace VansGraphics
{
namespace
{
std::filesystem::path NormalizeAssetPath(const std::filesystem::path& path)
{
	std::error_code error;
	std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
	if (!error)
		return normalized;
	normalized = std::filesystem::absolute(path, error);
	return error ? path.lexically_normal() : normalized.lexically_normal();
}

VansMaterialParameterValue ToMaterialParameterValue(const Vans::EditorAPI::PropertyValue& value)
{
	return std::visit([](const auto& typedValue) -> VansMaterialParameterValue
		{
			using T = std::decay_t<decltype(typedValue)>;
			if constexpr (std::is_same_v<T, std::monostate>)
				return std::monostate{};
			else if constexpr (std::is_same_v<T, Vans::EditorAPI::Vec2>)
				return glm::vec2(typedValue.x, typedValue.y);
			else if constexpr (std::is_same_v<T, Vans::EditorAPI::Vec3>)
				return glm::vec3(typedValue.x, typedValue.y, typedValue.z);
			else if constexpr (std::is_same_v<T, Vans::EditorAPI::Vec4>)
				return glm::vec4(typedValue.x, typedValue.y, typedValue.z, typedValue.w);
			else
				return typedValue;
		}, value);
}

std::string NormalizeSlotName(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	const std::string suffix = "_texture";
	if (value.size() > suffix.size() &&
		value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0)
		value.resize(value.size() - suffix.size());
	return value;
}

bool IsSRGBTextureSlot(const std::string& slot)
{
	return slot == "basecolor" ||
		slot == "base_color" ||
		slot == "albedo" ||
		slot == "diffuse" ||
		slot == "color" ||
		slot == "emissive";
}

const char* DefaultTextureForSlot(const std::string& slot)
{
	if (slot == "normal")
		return "defaultNormal";
	if (slot == "metal" || slot == "metallic")
		return "defaultMetal";
	if (slot == "roughness")
		return "defaultRoughness";
	if (slot == "ao" || slot == "occlusion" ||
		slot == "cavity" || slot == "specular" ||
		slot == "sss_mask" || slot == "sssmask" ||
		slot == "subsurface_mask" || slot == "scatter_mask" ||
		slot == "thickness" || slot == "transmission" || slot == "thinness")
		return "defaultAo";
	return "defaultAlbedo";
}

VansTexture* ResolveTextureGuid(VansScene& scene, const std::string& textureGuid, const std::string& slot)
{
	if (textureGuid.empty())
		return scene.ResolveTextureAssetOrDefault(nullptr, DefaultTextureForSlot(slot));

	Vans::VansAssetGuid guid;
	if (!Vans::VansAssetGuid::TryParse(textureGuid, guid))
		return scene.ResolveTextureAssetOrDefault(nullptr, DefaultTextureForSlot(slot));

	auto* database = Vans::VansProjectManager::Get().GetAssetDatabase();
	const auto record = database ? database->Find(guid) : std::nullopt;
	if (!record || record->type != Vans::VansAssetType::Texture ||
		record->state == Vans::VansAssetState::Missing)
	{
		return scene.ResolveTextureAssetOrDefault(nullptr, DefaultTextureForSlot(slot));
	}

	VansTexture* texture = scene.FindOrLoadTexture(record->sourcePath.string(), IsSRGBTextureSlot(slot));
	return scene.ResolveTextureAssetOrDefault(texture, DefaultTextureForSlot(slot));
}

VansMaterial* FindRuntimeMaterial(VansScene& scene, const std::string& materialName)
{
	if (materialName.empty())
		return nullptr;
	return static_cast<VansMaterial*>(scene.FindMaterialAsset(materialName));
}

std::unordered_map<std::string, int>::iterator FindCustomTextureSlot(
	VansMaterial& material,
	const std::string& normalizedSlot)
{
	for (auto it = material.m_CustomTextureSlots.begin();
		it != material.m_CustomTextureSlots.end();
		++it)
	{
		if (NormalizeSlotName(it->first) == normalizedSlot)
			return it;
	}
	return material.m_CustomTextureSlots.end();
}

int MaterialGlobalTextureBaseIndex(VansMaterial& material)
{
	if (auto* pbr = dynamic_cast<VansPBRMaterial*>(&material))
		return pbr->m_MaterialIndex * 5;
	if (auto* emissive = dynamic_cast<VansEmissiveMaterial*>(&material))
		return emissive->m_MaterialIndex * 5;
	if (auto* decal = dynamic_cast<VansDecalMaterial*>(&material))
		return decal->m_MaterialIndex * 5;
	if (auto* sss = dynamic_cast<VansSubsurfaceMaterial*>(&material))
		return sss->m_MaterialIndex * 5;
	if (auto* cloth = dynamic_cast<VansClothMaterial*>(&material))
		return cloth->m_MaterialIndex * 5;
	if (auto* skin = dynamic_cast<VansSkinMaterial*>(&material))
		return skin->m_MaterialIndex * 5;
	return -1;
}

int StandardBindlessSlotIndexForMaterial(VansMaterial& material, const std::string& slot)
{
	if (material.m_MaterialType == VansMaterialType::VAN_SKIN)
	{
		if (slot == "basecolor" || slot == "base_color" || slot == "albedo" ||
			slot == "diffuse" || slot == "color")
			return 0;
		if (slot == "normal")
			return 1;
		if (slot == "cavity" || slot == "specular" || slot == "ao" || slot == "occlusion")
			return 2;
		if (slot == "roughness")
			return 3;
		if (slot == "sss_mask" || slot == "sssmask" || slot == "subsurface_mask" || slot == "scatter_mask")
			return 4;
		return -1;
	}
	if (slot == "basecolor" || slot == "base_color" || slot == "albedo" ||
		slot == "diffuse" || slot == "color")
		return 0;
	if (slot == "normal")
		return 1;
	if (slot == "metal" || slot == "metallic" || slot == "thickness")
		return 2;
	if (slot == "roughness")
		return 3;
	if (slot == "ao" || slot == "occlusion" || slot == "emissive" || slot == "mask")
		return 4;
	if (material.m_MaterialType == VansMaterialType::VAN_CLOTH && slot == "translucency")
		return 2;
	return -1;
}

bool SetMaterialTexturePointer(VansMaterial& material, const std::string& slot, VansTexture* texture)
{
	if (!texture)
		return false;

	if (auto* pbr = dynamic_cast<VansPBRMaterial*>(&material))
	{
		if (slot == "basecolor" || slot == "base_color" || slot == "albedo" || slot == "diffuse" || slot == "color")
			pbr->m_BaseColorTexture = texture;
		else if (slot == "normal")
			pbr->m_NormalTexture = texture;
		else if (slot == "metal" || slot == "metallic")
			pbr->m_MetalTexture = texture;
		else if (slot == "roughness")
			pbr->m_RoughnessTexture = texture;
		else if (slot == "ao" || slot == "occlusion")
			pbr->m_AoTexture = texture;
		else
			return false;
		return true;
	}
	if (auto* emissive = dynamic_cast<VansEmissiveMaterial*>(&material))
	{
		if (material.m_MaterialType == VansMaterialType::VAN_EMISSIVE)
		{
			if (slot == "emissive" || slot == "basecolor" || slot == "albedo" || slot == "diffuse" || slot == "color")
				emissive->m_EmissiveTexture = texture;
			else
				return false;
		}
		else if (slot == "basecolor" || slot == "albedo" || slot == "diffuse" || slot == "color")
			emissive->m_BaseColorTexture = texture;
		else if (slot == "normal")
			emissive->m_NormalTexture = texture;
		else if (slot == "metal" || slot == "metallic")
			emissive->m_MetalTexture = texture;
		else if (slot == "roughness")
			emissive->m_RoughnessTexture = texture;
		else if (slot == "emissive" || slot == "mask")
			emissive->m_EmissiveTexture = texture;
		else
			return false;
		return true;
	}
	if (auto* decal = dynamic_cast<VansDecalMaterial*>(&material))
	{
		if (slot == "basecolor" || slot == "albedo" || slot == "diffuse")
			decal->m_BaseColorTexture = texture;
		else if (slot == "normal")
			decal->m_NormalTexture = texture;
		else if (slot == "metal" || slot == "metallic")
			decal->m_MetalTexture = texture;
		else if (slot == "roughness")
			decal->m_RoughnessTexture = texture;
		else if (slot == "ao" || slot == "occlusion")
			decal->m_AoTexture = texture;
		else
			return false;
		return true;
	}
	if (auto* skin = dynamic_cast<VansSkinMaterial*>(&material))
	{
		if (slot == "basecolor" || slot == "albedo" || slot == "diffuse")
			skin->m_BaseColorTexture = texture;
		else if (slot == "normal")
			skin->m_NormalTexture = texture;
		else if (slot == "roughness")
			skin->m_RoughnessTexture = texture;
		else if (slot == "cavity" || slot == "specular" || slot == "ao" || slot == "occlusion")
			skin->m_CavityTexture = texture;
		else if (slot == "scatter_mask" || slot == "scattermask" || slot == "sss_mask" || slot == "sssmask" || slot == "subsurface_mask")
			skin->m_ScatterMaskTexture = texture;
		else if (slot == "thickness" || slot == "transmission" || slot == "thinness")
			skin->m_ThicknessTexture = texture;
		else
			return false;
		return true;
	}
	if (auto* cloth = dynamic_cast<VansClothMaterial*>(&material))
	{
		if (slot == "basecolor" || slot == "albedo" || slot == "diffuse")
			cloth->m_BaseColorTexture = texture;
		else if (slot == "normal")
			cloth->m_NormalTexture = texture;
		else if (slot == "roughness")
			cloth->m_RoughnessTexture = texture;
		else if (slot == "ao" || slot == "occlusion")
			cloth->m_AoTexture = texture;
		else
			return false;
		return true;
	}
	if (auto* hair = dynamic_cast<VansHairMaterial*>(&material))
	{
		if (slot == "albedo" || slot == "basecolor" || slot == "diffuse")
			hair->m_AlbedoTexture = texture;
		else if (slot == "alpha" || slot == "opacity")
			hair->m_AlphaTexture = texture;
		else if (slot == "normal")
			hair->m_NormalTexture = texture;
		else if (slot == "roughness")
			hair->m_RoughnessTexture = texture;
		else if (slot == "ao" || slot == "occlusion")
			hair->m_AOTexture = texture;
		else if (slot == "shift")
			hair->m_ShiftTexture = texture;
		else if (slot == "flow")
			hair->m_FlowTexture = texture;
		else if (slot == "id")
			hair->m_IDTexture = texture;
		else
			return false;
		return true;
	}
	if (auto* sss = dynamic_cast<VansSubsurfaceMaterial*>(&material))
	{
		if (slot == "basecolor" || slot == "albedo" || slot == "diffuse")
			sss->m_BaseColorTexture = texture;
		else if (slot == "normal")
			sss->m_NormalTexture = texture;
		else if (slot == "thickness")
			sss->m_ThicknessTexture = texture;
		else if (slot == "roughness")
			sss->m_RoughnessTexture = texture;
		else
			return false;
		return true;
	}
	if (auto* grass = dynamic_cast<VansGrassMaterial*>(&material))
	{
		if (slot == "basecolor" || slot == "albedo" || slot == "diffuse")
			grass->m_AlbedoTexture = texture;
		else if (slot == "normal")
			grass->m_NormalTexture = texture;
		else if (slot == "roughness")
			grass->m_RoughnessTexture = texture;
		else if (slot == "translucency")
			grass->m_TranslucencyTexture = texture;
		else if (slot == "ao" || slot == "occlusion")
			grass->m_AOTexture = texture;
		else
			return false;
		return true;
	}
	if (auto* glass = dynamic_cast<VansTransmissionMaterial*>(&material))
	{
		if (slot == "basecolor" || slot == "albedo" || slot == "diffuse")
		{
			glass->m_BaseColorTexture = texture;
			glass->m_CustomTextures["baseColor"] = texture;
		}
		else if (slot == "normal")
		{
			glass->m_NormalTexture = texture;
			glass->m_CustomTextures["normal"] = texture;
		}
		else if (slot == "roughness")
		{
			glass->m_RoughnessTexture = texture;
			glass->m_CustomTextures["roughness"] = texture;
		}
		else if (slot == "thickness")
		{
			glass->m_ThicknessTexture = texture;
			glass->m_CustomTextures["thickness"] = texture;
		}
		else if (slot == "reflection")
		{
			glass->m_ReflectionTexture = texture;
			glass->m_CustomTextures["reflection"] = texture;
		}
		else
			return false;
		return true;
	}
	if (auto* trans = dynamic_cast<VansTransparentMaterial*>(&material))
	{
		for (std::size_t i = 0; i < trans->m_TransparentTextureMap.size(); ++i)
		{
			if (NormalizeSlotName(trans->m_TransparentTextureMap[i].first) == slot)
			{
				if (i >= trans->m_TransparentTextures.size())
					trans->m_TransparentTextures.resize(i + 1, nullptr);
				trans->m_TransparentTextures[i] = texture;
				trans->m_TransparentTextureMap[i].second = texture->m_AssetName;
				return true;
			}
		}
		trans->m_TransparentTextureMap.push_back({ slot, texture->m_AssetName });
		trans->m_TransparentTextures.push_back(texture);
		return true;
	}

	auto slotIt = FindCustomTextureSlot(material, slot);
	if (slotIt != material.m_CustomTextureSlots.end())
	{
		material.m_CustomTextures[slotIt->first] = texture;
		return true;
	}
	return false;
}

int CustomMaterialTextureGlobalIndex(VansMaterial& material, const std::string& slot)
{
	auto slotIt = FindCustomTextureSlot(material, slot);
	if (slotIt == material.m_CustomTextureSlots.end())
		return -1;
	const int textureSlot = slotIt->second;
	if (textureSlot < 0 || textureSlot >= VANS_CUSTOM_MATERIAL_TEXTURE_COUNT)
		return -1;
	if (textureSlot < 4)
		return material.m_CustomMaterialPayload.textureIndices[textureSlot];
	return static_cast<int>(material.m_CustomMaterialPayload.values[5][textureSlot - 4]);
}

bool UsesOwnedMaterialDescriptors(const VansMaterial& material)
{
	return material.m_MaterialType == VAN_TRANSPARENT ||
		material.m_MaterialType == VAN_SKIN ||
		material.m_MaterialType == VAN_CLOTH ||
		material.m_MaterialType == VAN_HAIR ||
		material.m_MaterialType == VAN_SUBSURFACE ||
		material.m_MaterialType == VAN_GRASS;
}

void DestroyMaterialOwnedTextureDescriptors(VansMaterial& material)
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	if (auto* trans = dynamic_cast<VansTransparentMaterial*>(&material))
	{
		descMgr->DestroyDescriptorSet(trans->m_TransparentOwnedDescSets);
		descMgr->DestroyDescriptorSetLayout(trans->m_TransparentOwnedLayout);
		trans->m_TransparentOwnedLayout = VK_NULL_HANDLE;
		trans->m_TransparentOwnedDescSets.clear();
	}
	else if (auto* skin = dynamic_cast<VansSkinMaterial*>(&material))
	{
		descMgr->DestroyDescriptorSet(skin->m_SkinOwnedDescSets);
		descMgr->DestroyDescriptorSetLayout(skin->m_SkinOwnedLayout);
		skin->m_SkinOwnedLayout = VK_NULL_HANDLE;
		skin->m_SkinOwnedDescSets.clear();
	}
	else if (auto* cloth = dynamic_cast<VansClothMaterial*>(&material))
	{
		descMgr->DestroyDescriptorSet(cloth->m_ClothOwnedDescSets);
		descMgr->DestroyDescriptorSetLayout(cloth->m_ClothOwnedLayout);
		cloth->m_ClothOwnedLayout = VK_NULL_HANDLE;
		cloth->m_ClothOwnedDescSets.clear();
	}
	else if (auto* hair = dynamic_cast<VansHairMaterial*>(&material))
	{
		descMgr->DestroyDescriptorSet(hair->m_HairOwnedDescSets);
		descMgr->DestroyDescriptorSetLayout(hair->m_HairOwnedLayout);
		hair->m_HairOwnedLayout = VK_NULL_HANDLE;
		hair->m_HairOwnedDescSets.clear();
	}
	else if (auto* sss = dynamic_cast<VansSubsurfaceMaterial*>(&material))
	{
		descMgr->DestroyDescriptorSet(sss->m_SubsurfaceOwnedDescSets);
		descMgr->DestroyDescriptorSetLayout(sss->m_SubsurfaceOwnedLayout);
		sss->m_SubsurfaceOwnedLayout = VK_NULL_HANDLE;
		sss->m_SubsurfaceOwnedDescSets.clear();
	}
	else if (auto* grass = dynamic_cast<VansGrassMaterial*>(&material))
	{
		descMgr->DestroyDescriptorSet(grass->m_GrassOwnedDescSets);
		descMgr->DestroyDescriptorSetLayout(grass->m_GrassOwnedLayout);
		grass->m_GrassOwnedLayout = VK_NULL_HANDLE;
		grass->m_GrassOwnedDescSets.clear();
	}
}

void RecreateNodesUsingMaterial(VansScene& scene, VansMaterial& material)
{
	for (VansRenderNode* node : scene.CollectSSBOManagedRenderNodes())
	{
		if (node && node->m_Material == &material)
		{
			node->RecreateDescriptorSets(
				scene.GetCamera(),
				*scene.GetLightManager(),
				*scene.GetMaterialManager());
		}
	}
}

RenderNodeType NodeTypeForMaterial(const VansMaterial& material, RenderNodeType fallback)
{
	if (material.m_MaterialType == VAN_HAIR)
		return HAIR_NODE;
	if (material.m_MaterialType == VAN_TRANSPARENT ||
		material.m_MaterialType == VAN_PBR_TRANSMISSION)
		return TRANSPARENT_NODE;
	if (material.m_MaterialType == VAN_CUSTOM_SHADER)
	{
		return material.HasPass(VansPass::GBUFFER)
			? OPAQUE_NODE
			: (material.m_CustomShaderDepthWrite
				? FORWARD_OPAQUE_AFTER_DEFERRED_NODE
				: TRANSPARENT_NODE);
	}
	if (fallback == DECAL_NODE || material.m_MaterialType == VAN_DECAL)
		return DECAL_NODE;
	return OPAQUE_NODE;
}

bool CompatibleNodeClass(const VansRenderNode& node, RenderNodeType targetType)
{
	if ((node.GetNodeType() == DECAL_NODE) != (targetType == DECAL_NODE))
		return false;
	return (node.GetNodeType() == TRANSPARENT_NODE) == (targetType == TRANSPARENT_NODE);
}

}

bool VansMaterialLiveEditService::ApplyMaterialPreviewChange(
	VansScene* scene,
	const std::filesystem::path& assetPath,
	const std::vector<Vans::EditorAPI::RuntimeMaterialParameterEdit>& parameterEdits,
	const std::vector<Vans::EditorAPI::RuntimeMaterialTextureEdit>& textureEdits)
{
	if (!scene || (parameterEdits.empty() && textureEdits.empty())) return false;
	auto* database = Vans::VansProjectManager::Get().GetAssetDatabase();
	if (!database) return false;

	std::string materialName;
	const std::filesystem::path selectedPath = NormalizeAssetPath(assetPath);
	for (const Vans::VansAssetRecord& record : database->All())
	{
		if (NormalizeAssetPath(record.sourcePath) == selectedPath)
		{
			materialName = record.guid.ToString();
			break;
		}
	}
	if (materialName.empty()) return false;

	bool changed = false;
	for (const Vans::EditorAPI::RuntimeMaterialParameterEdit& edit : parameterEdits)
		changed |= ApplyMaterialParameter(scene, materialName, edit.parameterPath, edit.value);
	for (const Vans::EditorAPI::RuntimeMaterialTextureEdit& edit : textureEdits)
		changed |= ApplyMaterialTexture(scene, materialName, edit.slot, edit.textureGuid);
	return changed;
}

bool VansMaterialLiveEditService::ApplyMaterialParameter(
	VansScene* scene,
	const std::string& materialName,
	const std::string& parameterPath,
	const Vans::EditorAPI::PropertyValue& value)
{
	if (!scene || materialName.empty()) return false;

	VansMaterial* material = nullptr;
	for (VansAsset* asset : scene->GetMaterialAssets())
	{
		if (asset && asset->m_AssetName == materialName)
		{
			material = dynamic_cast<VansMaterial*>(asset);
			break;
		}
	}
	if (!material) return false;
	VansMaterialManager* materialManager = scene->GetMaterialManager();
	return materialManager && materialManager->ApplyMaterialParameter(
		*material,
		parameterPath,
		ToMaterialParameterValue(value));
}

bool VansMaterialLiveEditService::ApplyMaterialTexture(
	VansScene* scene,
	const std::string& materialName,
	const std::string& slot,
	const std::string& textureGuid)
{
	if (!scene || materialName.empty() || slot.empty())
		return false;

	VansMaterial* material = FindRuntimeMaterial(*scene, materialName);
	if (!material)
		return false;

	const std::string normalizedSlot = NormalizeSlotName(slot);
	VansTexture* texture = ResolveTextureGuid(*scene, textureGuid, normalizedSlot);
	if (!texture || !SetMaterialTexturePointer(*material, normalizedSlot, texture))
		return false;

	VansMaterialManager* materialManager = scene->GetMaterialManager();
	if (!materialManager)
		return false;

	const int standardSlot = StandardBindlessSlotIndexForMaterial(*material, normalizedSlot);
	const int baseIndex = MaterialGlobalTextureBaseIndex(*material);
	if (standardSlot >= 0 && baseIndex >= 0)
	{
		materialManager->ReplaceGlobalBindlessTexture(
			static_cast<std::size_t>(baseIndex + standardSlot),
			texture,
			scene->GetGlobalDescriptorSet());
	}
	else if (material->m_MaterialType == VAN_CUSTOM_SHADER ||
		material->m_MaterialType == VAN_PBR_TRANSMISSION)
	{
		const int globalIndex = CustomMaterialTextureGlobalIndex(*material, normalizedSlot);
		if (globalIndex >= 0)
		{
			materialManager->ReplaceGlobalBindlessTexture(
				static_cast<std::size_t>(globalIndex),
				texture,
				scene->GetGlobalDescriptorSet());
		}
	}

	if (UsesOwnedMaterialDescriptors(*material))
	{
		DestroyMaterialOwnedTextureDescriptors(*material);
		RecreateNodesUsingMaterial(*scene, *material);
	}

	return true;
}

bool VansMaterialLiveEditService::ApplyRendererMaterialOverride(
	VansScene* scene,
	const Vans::EditorAPI::RuntimeRendererMaterialOverrideEdit& edit)
{
	if (!scene || edit.entityGuid.empty() || edit.materialGuid.empty())
		return false;

	VansMaterial* material = FindRuntimeMaterial(*scene, edit.materialGuid);
	if (!material)
		return false;

	bool changed = false;
	const std::string slot = NormalizeSlotName(edit.slot);
	for (VansRenderNode* node : scene->CollectSSBOManagedRenderNodes())
	{
		if (!node)
			continue;
		if (node->m_EntityGuid != edit.entityGuid &&
			node->m_ParentEntityGuid != edit.entityGuid)
			continue;
		if (!slot.empty() && slot != "default" && slot != "0")
		{
			if (node->m_SubmeshIndex == UINT32_MAX ||
				slot != std::to_string(node->m_SubmeshIndex))
				continue;
		}

		const RenderNodeType targetType = NodeTypeForMaterial(*material, node->GetNodeType());
		if (!CompatibleNodeClass(*node, targetType))
		{
			VANS_LOG_WARN("[MaterialLiveEdit] Renderer material switch requires node rebuild: entity="
				<< edit.entityGuid << " slot=" << edit.slot
				<< " material=" << edit.materialGuid);
			continue;
		}

		if (node->GetNodeType() != targetType)
		{
			scene->RemoveRenderNodeFromVector(node);
			node->SetNodeType(targetType);
			scene->RegistRenderNode(node, targetType);
		}
		node->m_Material = material;
		node->m_RayTracingEnabled =
			material->m_MaterialType != VAN_TRANSPARENT &&
			material->m_MaterialType != VAN_PBR_TRANSMISSION;
		node->RecreateDescriptorSets(
			scene->GetCamera(),
			*scene->GetLightManager(),
			*scene->GetMaterialManager());
		changed = true;
	}
	return changed;
}
}

