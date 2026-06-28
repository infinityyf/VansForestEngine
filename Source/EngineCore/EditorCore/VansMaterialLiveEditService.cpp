#include "VansMaterialLiveEditService.h"

#include "../AssetCore/VansAssetDatabase.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../RenderCore/VansMaterial.h"

namespace VansGraphics
{
namespace
{
bool ReadVec3(const nlohmann::ordered_json& value, glm::vec3& out)
{
	if (!value.is_array() || value.size() < 3) return false;
	out = glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
	return true;
}

const nlohmann::ordered_json* FindMaterialParameters(const nlohmann::ordered_json& root)
{
	if (root.contains("parameters") && root["parameters"].is_object())
		return &root["parameters"];
	return root.is_object() ? &root : nullptr;
}
}

bool VansMaterialLiveEditService::ApplyMaterialAssetPatch(
	const std::filesystem::path& assetPath, const Json& assetRoot)
{
	if (!m_Scene) return false;
	auto* database = Vans::VansProjectManager::Get().GetAssetDatabase();
	if (!database) return false;

	std::string materialName;
	for (const Vans::VansAssetRecord& record : database->All())
	{
		if (record.sourcePath == assetPath)
		{
			materialName = record.guid.ToString();
			break;
		}
	}
	if (materialName.empty()) return false;

	const Json* params = FindMaterialParameters(assetRoot);
	if (!params) return false;

	bool changed = false;
	for (auto it = params->begin(); it != params->end(); ++it)
		changed |= ApplyMaterialParameter(materialName, it.key(), it.value());
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
	if (auto* pbr = dynamic_cast<VansPBRMaterial*>(material))
	{
		glm::vec3 color;
		if ((key == "albedo" || key == "baseColor" || key == "basecolor") && ReadVec3(value, color))
		{
			pbr->m_BasePBRParam.m_albedo = color;
			return true;
		}
		if (key == "roughness" && value.is_number())
		{
			pbr->m_BasePBRParam.m_roughness = value.get<float>();
			return true;
		}
		if (key == "metallic" && value.is_number())
		{
			pbr->m_BasePBRParam.m_metallic = value.get<float>();
			return true;
		}
		if (key == "ao" && value.is_number())
		{
			pbr->m_BasePBRParam.m_ao = value.get<float>();
			return true;
		}
	}
	else if (auto* emissive = dynamic_cast<VansEmissiveMaterial*>(material))
	{
		glm::vec3 color;
		if ((key == "albedo" || key == "emissive" || key == "color") && ReadVec3(value, color))
		{
			emissive->m_BasePBRParam.m_albedo = color;
			return true;
		}
		if ((key == "intensity" || key == "emissiveIntensity" || key == "roughness") && value.is_number())
		{
			emissive->m_BasePBRParam.m_roughness = value.get<float>();
			return true;
		}
	}

	return false;
}
}
