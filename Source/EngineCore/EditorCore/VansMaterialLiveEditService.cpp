#include "VansMaterialLiveEditService.h"

#include "../AssetCore/VansAssetDatabase.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../RenderCore/VansMaterial.h"

#include <algorithm>
#include <system_error>

namespace VansGraphics
{
namespace
{
bool ReadVec3(const nlohmann::ordered_json& value, glm::vec3& out)
{
	const nlohmann::ordered_json* scalarValue = &value;
	if (value.is_object())
	{
		if (value.contains("value")) scalarValue = &value["value"];
		else if (value.contains("default")) scalarValue = &value["default"];
	}
	if (!scalarValue->is_array() || scalarValue->size() < 3) return false;
	out = glm::vec3((*scalarValue)[0].get<float>(), (*scalarValue)[1].get<float>(), (*scalarValue)[2].get<float>());
	return true;
}

bool ReadFloat(const nlohmann::ordered_json& value, float& out)
{
	const nlohmann::ordered_json* scalarValue = &value;
	if (value.is_object())
	{
		if (value.contains("value")) scalarValue = &value["value"];
		else if (value.contains("default")) scalarValue = &value["default"];
	}
	if (!scalarValue->is_number()) return false;
	out = scalarValue->get<float>();
	return true;
}

const nlohmann::ordered_json* FindMaterialParameters(const nlohmann::ordered_json& root)
{
	if (root.contains("parameters") && root["parameters"].is_object())
		return &root["parameters"];
	return root.is_object() ? &root : nullptr;
}

std::filesystem::path NormalizeAssetPath(const std::filesystem::path& path)
{
	std::error_code error;
	std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
	if (!error)
		return normalized;
	normalized = std::filesystem::absolute(path, error);
	return error ? path.lexically_normal() : normalized.lexically_normal();
}

std::string UnescapePointerToken(std::string token)
{
	std::string result;
	result.reserve(token.size());
	for (std::size_t i = 0; i < token.size(); ++i)
	{
		if (token[i] == '~' && i + 1 < token.size())
		{
			if (token[i + 1] == '0') { result.push_back('~'); ++i; continue; }
			if (token[i + 1] == '1') { result.push_back('/'); ++i; continue; }
		}
		result.push_back(token[i]);
	}
	return result;
}

std::vector<std::string> SplitJsonPointer(const std::string& pointer)
{
	std::vector<std::string> tokens;
	if (pointer.empty() || pointer[0] != '/')
		return tokens;

	std::size_t start = 1;
	while (start <= pointer.size())
	{
		const std::size_t slash = pointer.find('/', start);
		const std::size_t end = slash == std::string::npos ? pointer.size() : slash;
		tokens.push_back(UnescapePointerToken(pointer.substr(start, end - start)));
		if (slash == std::string::npos)
			break;
		start = slash + 1;
	}
	return tokens;
}

glm::vec4 ReadVec4Value(const nlohmann::ordered_json& value)
{
	if (value.is_number())
		return glm::vec4(value.get<float>(), 0.0f, 0.0f, 0.0f);
	if (value.is_array())
	{
		glm::vec4 result(0.0f);
		const std::size_t count = std::min<std::size_t>(value.size(), 4);
		for (std::size_t i = 0; i < count; ++i)
			if (value[i].is_number())
				result[static_cast<int>(i)] = value[i].get<float>();
		return result;
	}
	if (value.is_object())
	{
		if (value.contains("value"))
			return ReadVec4Value(value["value"]);
		if (value.contains("default"))
			return ReadVec4Value(value["default"]);
	}
	return glm::vec4(0.0f);
}
}

int VansMaterialLiveEditService::GetGlobalMaterialIndex(VansMaterial* material)
{
	if (auto* pbr = dynamic_cast<VansPBRMaterial*>(material))
		return pbr->m_MaterialIndex;
	if (auto* emissive = dynamic_cast<VansEmissiveMaterial*>(material))
		return emissive->m_MaterialIndex;
	if (auto* decal = dynamic_cast<VansDecalMaterial*>(material))
		return decal->m_MaterialIndex;
	if (auto* sss = dynamic_cast<VansSubsurfaceMaterial*>(material))
		return sss->m_MaterialIndex;
	if (auto* cloth = dynamic_cast<VansClothMaterial*>(material))
		return cloth->m_MaterialIndex;
	if (material->m_MaterialType == VansMaterialType::VAN_CUSTOM_SHADER)
		return material->m_MaterialIndex;
	return -1;
}

void VansMaterialLiveEditService::UploadPBRPayload(VansMaterial* material, VansMaterialManager* manager)
{
	if (!material || !manager || manager->m_GlobalPBRDataBuffer.GetNativeBuffer() == VK_NULL_HANDLE)
		return;
	const int index = GetGlobalMaterialIndex(material);
	if (index < 0)
		return;
	const VkDeviceSize offset = sizeof(VansBasePBRParam) * static_cast<VkDeviceSize>(index);
	if (auto* pbr = dynamic_cast<VansPBRMaterial*>(material))
	{
		if (index < static_cast<int>(manager->m_GlobalPBRParamData.size()))
			manager->m_GlobalPBRParamData[index] = pbr->m_BasePBRParam;
		manager->m_GlobalPBRDataBuffer.SetBufferData(&pbr->m_BasePBRParam, offset, sizeof(VansBasePBRParam));
	}
	else if (auto* emissive = dynamic_cast<VansEmissiveMaterial*>(material))
	{
		if (index < static_cast<int>(manager->m_GlobalPBRParamData.size()))
			manager->m_GlobalPBRParamData[index] = emissive->m_BasePBRParam;
		manager->m_GlobalPBRDataBuffer.SetBufferData(&emissive->m_BasePBRParam, offset, sizeof(VansBasePBRParam));
	}
	else if (auto* decal = dynamic_cast<VansDecalMaterial*>(material))
	{
		if (index < static_cast<int>(manager->m_GlobalPBRParamData.size()))
			manager->m_GlobalPBRParamData[index] = decal->m_BasePBRParam;
		manager->m_GlobalPBRDataBuffer.SetBufferData(&decal->m_BasePBRParam, offset, sizeof(VansBasePBRParam));
	}
	else if (auto* sss = dynamic_cast<VansSubsurfaceMaterial*>(material))
	{
		if (index < static_cast<int>(manager->m_GlobalPBRParamData.size()))
			manager->m_GlobalPBRParamData[index] = sss->m_BasePBRParam;
		manager->m_GlobalPBRDataBuffer.SetBufferData(&sss->m_BasePBRParam, offset, sizeof(VansBasePBRParam));
	}
	else if (auto* cloth = dynamic_cast<VansClothMaterial*>(material))
	{
		if (index < static_cast<int>(manager->m_GlobalPBRParamData.size()))
			manager->m_GlobalPBRParamData[index] = cloth->m_BasePBRParam;
		manager->m_GlobalPBRDataBuffer.SetBufferData(&cloth->m_BasePBRParam, offset, sizeof(VansBasePBRParam));
	}
}

void VansMaterialLiveEditService::UploadCustomPayload(VansMaterial* material, VansMaterialManager* manager)
{
	if (!material || !manager || manager->m_GlobalCustomMaterialDataBuffer.GetNativeBuffer() == VK_NULL_HANDLE)
		return;
	const int index = GetGlobalMaterialIndex(material);
	if (index < 0)
		return;
	if (index < static_cast<int>(manager->m_GlobalCustomMaterialParamData.size()))
		manager->m_GlobalCustomMaterialParamData[index] = material->m_CustomMaterialPayload;
	const VkDeviceSize offset = sizeof(VansCustomMaterialPayload) * static_cast<VkDeviceSize>(index);
	manager->m_GlobalCustomMaterialDataBuffer.SetBufferData(
		&material->m_CustomMaterialPayload, offset, sizeof(VansCustomMaterialPayload));
}

bool VansMaterialLiveEditService::ApplyMaterialAssetPatch(
	const std::filesystem::path& assetPath,
	const Json& assetRoot,
	const std::string& changedPointer)
{
	if (!m_Scene) return false;
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

	const std::vector<std::string> pointerTokens = SplitJsonPointer(changedPointer);
	if (pointerTokens.size() >= 3 && pointerTokens[0] == "asset")
	{
		if (pointerTokens[1] == "parameters" &&
			assetRoot.contains("parameters") &&
			assetRoot["parameters"].is_object())
		{
			if (pointerTokens.size() == 3 && assetRoot["parameters"].contains(pointerTokens[2]))
				return ApplyMaterialParameter(materialName, pointerTokens[2], assetRoot["parameters"][pointerTokens[2]]);
		}
		else if (pointerTokens[1] == "customParameters" &&
			assetRoot.contains("customParameters") &&
			assetRoot["customParameters"].is_object())
		{
			if (assetRoot["customParameters"].contains(pointerTokens[2]))
				return ApplyMaterialParameter(
					materialName,
					"customParameters/" + pointerTokens[2],
					assetRoot["customParameters"][pointerTokens[2]]);
		}
	}

	const Json* params = FindMaterialParameters(assetRoot);

	bool changed = false;
	if (params)
		for (auto it = params->begin(); it != params->end(); ++it)
			changed |= ApplyMaterialParameter(materialName, it.key(), it.value());
	if (assetRoot.contains("customParameters") && assetRoot["customParameters"].is_object())
		for (auto it = assetRoot["customParameters"].begin(); it != assetRoot["customParameters"].end(); ++it)
			changed |= ApplyMaterialParameter(materialName, "customParameters/" + it.key(), it.value());
	return changed;
}

bool VansMaterialLiveEditService::ApplyMaterialParameter(
	const std::string& materialName, const std::string& parameterPath, const Json& value)
{
	if (!m_Scene || materialName.empty()) return false;

	VansMaterial* material = nullptr;
	for (VansAsset* asset : m_Scene->GetMaterialAssets())
	{
		if (asset && asset->m_AssetName == materialName)
		{
			material = dynamic_cast<VansMaterial*>(asset);
			break;
		}
	}
	if (!material) return false;

	const std::string key = parameterPath;
	if (key.rfind("customParameters/", 0) == 0)
	{
		const std::string customName = key.substr(std::string("customParameters/").size());
		auto slot = material->m_CustomParameterSlots.find(customName);
		if (slot != material->m_CustomParameterSlots.end() &&
			slot->second >= 0 && slot->second < VANS_CUSTOM_MATERIAL_VEC4_COUNT)
		{
			material->m_CustomMaterialPayload.values[slot->second] = ReadVec4Value(value);
			UploadCustomPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
	}
	if (material->m_MaterialType == VansMaterialType::VAN_CUSTOM_SHADER)
	{
		auto slot = material->m_CustomParameterSlots.find(key);
		if (slot != material->m_CustomParameterSlots.end() &&
			slot->second >= 0 && slot->second < VANS_CUSTOM_MATERIAL_VEC4_COUNT)
		{
			material->m_CustomMaterialPayload.values[slot->second] = ReadVec4Value(value);
			UploadCustomPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
	}
	if (auto* pbr = dynamic_cast<VansPBRMaterial*>(material))
	{
		glm::vec3 color;
		if ((key == "albedo" || key == "baseColor" || key == "basecolor") && ReadVec3(value, color))
		{
			pbr->m_BasePBRParam.m_albedo = color;
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
		float scalar = 0.0f;
		if (key == "roughness" && ReadFloat(value, scalar))
		{
			pbr->m_BasePBRParam.m_roughness = scalar;
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
		if (key == "metallic" && ReadFloat(value, scalar))
		{
			pbr->m_BasePBRParam.m_metallic = scalar;
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
		if (key == "ao" && ReadFloat(value, scalar))
		{
			pbr->m_BasePBRParam.m_ao = scalar;
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
	}
	else if (auto* sss = dynamic_cast<VansSubsurfaceMaterial*>(material))
	{
		glm::vec3 color;
		if ((key == "subsurfaceColor" || key == "color" || key == "albedo" || key == "baseColor" || key == "basecolor") &&
			ReadVec3(value, color))
		{
			sss->m_SubsurfaceColor = color;
			sss->m_BasePBRParam.m_albedo = color;
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
		float scalar = 0.0f;
		if (key == "subsurfacePower" && ReadFloat(value, scalar))
		{
			sss->m_SubsurfacePower = scalar;
			sss->m_BasePBRParam.m_roughness = sss->m_SubsurfacePower;
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
		if (key == "thickness" && ReadFloat(value, scalar))
		{
			sss->m_Thickness = scalar;
			sss->m_BasePBRParam.m_metallic = sss->m_Thickness;
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
		if (key == "subsurfaceAmount" && ReadFloat(value, scalar))
		{
			sss->m_SubsurfaceAmount = scalar;
			sss->m_BasePBRParam.m_ao = sss->m_SubsurfaceAmount;
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
		if (key == "curvatureInfluence" && ReadFloat(value, scalar))
		{
			sss->m_CurvatureInfluence = scalar;
			sss->m_BasePBRParam.padding = sss->m_CurvatureInfluence;
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
	}
	else if (auto* decal = dynamic_cast<VansDecalMaterial*>(material))
	{
		glm::vec3 color;
		if ((key == "albedo" || key == "baseColor" || key == "basecolor" || key == "color") && ReadVec3(value, color))
		{
			decal->m_BasePBRParam.m_albedo = color;
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
		float scalar = 0.0f;
		if (key == "roughness" && ReadFloat(value, scalar))
		{
			decal->m_BasePBRParam.m_roughness = scalar;
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
		if (key == "metallic" && ReadFloat(value, scalar))
		{
			decal->m_BasePBRParam.m_metallic = scalar;
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
		if (key == "ao" && ReadFloat(value, scalar))
		{
			decal->m_BasePBRParam.m_ao = scalar;
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
	}
	else if (auto* emissive = dynamic_cast<VansEmissiveMaterial*>(material))
	{
		glm::vec3 color;
		if ((key == "albedo" || key == "emissive" || key == "emissive_color" || key == "color") && ReadVec3(value, color))
		{
			emissive->m_BasePBRParam.m_albedo = color;
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
		float scalar = 0.0f;
		if ((key == "intensity" || key == "emissiveIntensity" || key == "emissive_intensity" || key == "roughness") &&
			ReadFloat(value, scalar))
		{
			emissive->m_BasePBRParam.m_roughness = scalar;
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
	}
	else if (auto* cloth = dynamic_cast<VansClothMaterial*>(material))
	{
		glm::vec3 color;
		if ((key == "albedo" || key == "baseColor" || key == "basecolor" || key == "color") && ReadVec3(value, color))
		{
			cloth->m_BasePBRParam.m_albedo = color;
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
		float scalar = 0.0f;
		if ((key == "sheenRoughness" || key == "roughness") && ReadFloat(value, scalar))
		{
			cloth->m_SheenRoughness = std::clamp(scalar, 0.045f, 1.0f);
			cloth->m_BasePBRParam.m_roughness = cloth->m_SheenRoughness;
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
		if (key == "sheenStrength" && ReadFloat(value, scalar))
		{
			cloth->m_BasePBRParam.padding = std::clamp(scalar, 0.0f, 1.0f);
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
		if (key == "translucency" && ReadFloat(value, scalar))
		{
			cloth->m_BasePBRParam.m_metallic = std::clamp(scalar, 0.0f, 1.0f);
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
		if (key == "ao" && ReadFloat(value, scalar))
		{
			cloth->m_BasePBRParam.m_ao = std::clamp(scalar, 0.0f, 1.0f);
			UploadPBRPayload(material, m_Scene->GetMaterialManager());
			return true;
		}
	}

	return false;
}
}
