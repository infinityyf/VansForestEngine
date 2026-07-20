#include "VansMaterial.h"
#include <algorithm>
#include <cmath>
using namespace VansGraphics;

namespace
{
bool ReadMaterialVec3(const nlohmann::ordered_json& value, glm::vec3& out)
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

bool ReadMaterialFloat(const nlohmann::ordered_json& value, float& out)
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

glm::vec4 ReadMaterialVec4Value(const nlohmann::ordered_json& value)
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
			return ReadMaterialVec4Value(value["value"]);
		if (value.contains("default"))
			return ReadMaterialVec4Value(value["default"]);
	}
	return glm::vec4(0.0f);
}
}

// ============================================================
// VansMaterial �?pass shader accessors
// ============================================================
VansGraphicsShader* VansGraphics::VansMaterial::GetPassShader(const std::string& passName) const
{
	auto it = m_PassShaders.find(passName);
	return (it != m_PassShaders.end()) ? it->second : nullptr;
}

bool VansGraphics::VansMaterial::HasPass(const std::string& passName) const
{
	return m_PassShaders.count(passName) > 0;
}

// ============================================================
// Material subclass destructors �?release owned Vulkan resources
// ============================================================

VansGraphics::VansTransparentMaterial::~VansTransparentMaterial()
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_TransparentOwnedDescSets);
	descMgr->DestroyDescriptorSetLayout(m_TransparentOwnedLayout);
}

VansGraphics::VansSkinMaterial::~VansSkinMaterial()
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_SkinOwnedDescSets);
	descMgr->DestroyDescriptorSetLayout(m_SkinOwnedLayout);
}

VansGraphics::VansClothMaterial::~VansClothMaterial()
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_ClothOwnedDescSets);
	descMgr->DestroyDescriptorSetLayout(m_ClothOwnedLayout);
}

VansGraphics::VansHairMaterial::~VansHairMaterial()
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_HairOwnedDescSets);
	descMgr->DestroyDescriptorSetLayout(m_HairOwnedLayout);
	if (m_ParamsDevice != VK_NULL_HANDLE && m_ParamsBuffer.GetNativeBuffer() != VK_NULL_HANDLE)
	{
		m_ParamsBuffer.DestroyVulkanBuffer(m_ParamsDevice);
	}
}

VansGraphics::VansSubsurfaceMaterial::~VansSubsurfaceMaterial()
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_SubsurfaceOwnedDescSets);
	descMgr->DestroyDescriptorSetLayout(m_SubsurfaceOwnedLayout);
}

VansGraphics::VansGrassMaterial::~VansGrassMaterial()
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_GrassOwnedDescSets);
	descMgr->DestroyDescriptorSetLayout(m_GrassOwnedLayout);
}

VansGraphics::VansMaterialManager::VansMaterialManager()
{
}

void VansGraphics::VansMaterialManager::UploadCloudParamsToGPU()
{
	if (m_CloudParamsCBBuffer.GetNativeBuffer() == VK_NULL_HANDLE)
	{
		return;
	}

	m_CloudParamsCBBuffer.SetBufferData(&m_CloudParams, 0, sizeof(VansCloudParamsGPU));
}

VansGraphics::VansMaterialManager::~VansMaterialManager()
{
	ClearRuntimeRenderTextures();
}

bool VansGraphics::VansMaterialManager::RegisterRuntimeRenderTexture(const std::string& name, VansTexture* texture, bool replaceExisting)
{
	return m_RuntimeRenderTextureManager.Add(name, texture, replaceExisting);
}

VansTexture* VansGraphics::VansMaterialManager::GetRuntimeRenderTexture(const std::string& name) const
{
	return m_RuntimeRenderTextureManager.Get(name);
}

bool VansGraphics::VansMaterialManager::HasRuntimeRenderTexture(const std::string& name) const
{
	return m_RuntimeRenderTextureManager.Has(name);
}

bool VansGraphics::VansMaterialManager::RemoveRuntimeRenderTexture(const std::string& name)
{
	return m_RuntimeRenderTextureManager.Remove(name);
}

bool VansGraphics::VansMaterialManager::UnregisterRuntimeRenderTexture(const std::string& name)
{
	return m_RuntimeRenderTextureManager.Unregister(name);
}

void VansGraphics::VansMaterialManager::ClearRuntimeRenderTextures()
{
	m_RuntimeRenderTextureManager.Clear();
	m_SSGITemporalFrame = 0;
}

void VansGraphics::VansMaterialManager::ClearScenePBRData(VkDevice device)
{
	// 清空 CPU �?PBR 数组（指针不拥有所有权，material �?VansScene 管理�?
	m_GlobalPBRMaterial.clear();
	m_GlobalPBRParamData.clear();
	m_GlobalCustomMaterialParamData.clear();
	m_GlobalPBRTextures.clear();

	// 销�?GPU buffer
	m_GlobalPBRDataBuffer.DestroyVulkanBuffer(device);
	m_GlobalCustomMaterialDataBuffer.DestroyVulkanBuffer(device);

	// 释放 descriptor set �?layout
	auto descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_GlobalPBRDataDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_GlobalPBRDataSetLayout);
	descMgr->DestroyDescriptorSet(m_GlobalPBRTexDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_GlobalPBRTexSetLayout);
}

bool VansGraphics::VansMaterialManager::FlushMaterialPayload(VansMaterial& material)
{
	auto getGlobalMaterialIndex = [](VansMaterial& source) -> int
	{
		if (auto* pbr = dynamic_cast<VansPBRMaterial*>(&source))
			return pbr->m_MaterialIndex;
		if (auto* emissive = dynamic_cast<VansEmissiveMaterial*>(&source))
			return emissive->m_MaterialIndex;
		if (auto* decal = dynamic_cast<VansDecalMaterial*>(&source))
			return decal->m_MaterialIndex;
		if (auto* sss = dynamic_cast<VansSubsurfaceMaterial*>(&source))
			return sss->m_MaterialIndex;
		if (auto* cloth = dynamic_cast<VansClothMaterial*>(&source))
			return cloth->m_MaterialIndex;
		if (source.m_MaterialType == VansMaterialType::VAN_CUSTOM_SHADER ||
			source.m_MaterialType == VansMaterialType::VAN_PBR_TRANSMISSION)
		{
			return source.m_MaterialIndex;
		}
		return -1;
	};

	const int index = getGlobalMaterialIndex(material);
	if (index < 0)
		return false;

	auto flushPbrPayload = [&](const VansBasePBRParam& payload) -> bool
	{
		if (m_GlobalPBRDataBuffer.GetNativeBuffer() == VK_NULL_HANDLE)
			return false;
		if (index < static_cast<int>(m_GlobalPBRParamData.size()))
			m_GlobalPBRParamData[index] = payload;
		const VkDeviceSize offset = sizeof(VansBasePBRParam) * static_cast<VkDeviceSize>(index);
		m_GlobalPBRDataBuffer.SetBufferData(&payload, offset, sizeof(VansBasePBRParam));
		return true;
	};

	if (auto* pbr = dynamic_cast<VansPBRMaterial*>(&material))
		return flushPbrPayload(pbr->m_BasePBRParam);
	if (auto* emissive = dynamic_cast<VansEmissiveMaterial*>(&material))
		return flushPbrPayload(emissive->m_BasePBRParam);
	if (auto* decal = dynamic_cast<VansDecalMaterial*>(&material))
		return flushPbrPayload(decal->m_BasePBRParam);
	if (auto* sss = dynamic_cast<VansSubsurfaceMaterial*>(&material))
		return flushPbrPayload(sss->m_BasePBRParam);
	if (auto* cloth = dynamic_cast<VansClothMaterial*>(&material))
		return flushPbrPayload(cloth->m_BasePBRParam);

	if (m_GlobalCustomMaterialDataBuffer.GetNativeBuffer() == VK_NULL_HANDLE)
		return false;
	if (index < static_cast<int>(m_GlobalCustomMaterialParamData.size()))
		m_GlobalCustomMaterialParamData[index] = material.m_CustomMaterialPayload;
	const VkDeviceSize offset = sizeof(VansCustomMaterialPayload) * static_cast<VkDeviceSize>(index);
	m_GlobalCustomMaterialDataBuffer.SetBufferData(
		&material.m_CustomMaterialPayload,
		offset,
		sizeof(VansCustomMaterialPayload));
	return true;
}

bool VansGraphics::VansMaterialManager::ApplyMaterialParameter(
	VansMaterial& material,
	const std::string& parameterPath,
	const nlohmann::ordered_json& value)
{
	const std::string key = parameterPath;
	if (key.rfind("customParameters/", 0) == 0)
	{
		const std::string customName = key.substr(std::string("customParameters/").size());
		auto slot = material.m_CustomParameterSlots.find(customName);
		if (slot != material.m_CustomParameterSlots.end() &&
			slot->second >= 0 && slot->second < VANS_CUSTOM_MATERIAL_VEC4_COUNT)
		{
			material.m_CustomMaterialPayload.values[slot->second] = ReadMaterialVec4Value(value);
			FlushMaterialPayload(material);
			return true;
		}
	}
	if (material.m_MaterialType == VansMaterialType::VAN_CUSTOM_SHADER)
	{
		auto slot = material.m_CustomParameterSlots.find(key);
		if (slot != material.m_CustomParameterSlots.end() &&
			slot->second >= 0 && slot->second < VANS_CUSTOM_MATERIAL_VEC4_COUNT)
		{
			material.m_CustomMaterialPayload.values[slot->second] = ReadMaterialVec4Value(value);
			FlushMaterialPayload(material);
			return true;
		}
	}
	if (material.m_MaterialType == VansMaterialType::VAN_PBR_TRANSMISSION)
	{
		glm::vec3 color;
		float scalar = 0.0f;
		bool changed = false;

		if ((key == "color" || key == "albedo" || key == "baseColor" || key == "basecolor") && ReadMaterialVec3(value, color))
		{
			material.m_CustomMaterialPayload.values[0].x = color.x;
			material.m_CustomMaterialPayload.values[0].y = color.y;
			material.m_CustomMaterialPayload.values[0].z = color.z;
			changed = true;
		}
		else if (key == "alphaCoverage" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[0].w = std::clamp(scalar, 0.0f, 1.0f);
			changed = true;
		}
		else if (key == "roughness" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[1].x = std::clamp(scalar, 0.0f, 1.0f);
			changed = true;
		}
		else if (key == "transmission" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[1].y = std::clamp(scalar, 0.0f, 1.0f);
			changed = true;
		}
		else if (key == "ior" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[1].z = std::max(scalar, 1.0001f);
			changed = true;
		}
		else if (key == "thickness" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[1].w = std::max(scalar, 0.0f);
			changed = true;
		}
		else if (key == "attenuationColor" && ReadMaterialVec3(value, color))
		{
			material.m_CustomMaterialPayload.values[2].x = color.x;
			material.m_CustomMaterialPayload.values[2].y = color.y;
			material.m_CustomMaterialPayload.values[2].z = color.z;
			changed = true;
		}
		else if (key == "attenuationDistance" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[2].w = std::max(scalar, 0.0f);
			changed = true;
		}
		else if (key == "normalScale" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[3].x = scalar;
			changed = true;
		}
		else if (key == "refractionStrength" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[3].y = std::clamp(scalar, 0.0f, 1.0f);
			changed = true;
		}
		else if (key == "reflectionStrength" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[3].z = std::clamp(scalar, 0.0f, 1.0f);
			changed = true;
		}
		else if (key == "refractionMode" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[3].w = std::clamp(scalar, 0.0f, 2.0f);
			changed = true;
		}
		else if (key == "scatteringColor" && ReadMaterialVec3(value, color))
		{
			material.m_CustomMaterialPayload.values[4].x = std::max(color.x, 0.0f);
			material.m_CustomMaterialPayload.values[4].y = std::max(color.y, 0.0f);
			material.m_CustomMaterialPayload.values[4].z = std::max(color.z, 0.0f);
			changed = true;
		}
		else if (key == "scatteringStrength" && ReadMaterialFloat(value, scalar))
		{
			material.m_CustomMaterialPayload.values[4].w = std::max(scalar, 0.0f);
			changed = true;
		}

		if (changed)
		{
			FlushMaterialPayload(material);
			return true;
		}
	}
	if (auto* pbr = dynamic_cast<VansPBRMaterial*>(&material))
	{
		glm::vec3 color;
		if ((key == "albedo" || key == "baseColor" || key == "basecolor") && ReadMaterialVec3(value, color))
		{
			pbr->m_BasePBRParam.m_albedo = color;
			FlushMaterialPayload(material);
			return true;
		}
		float scalar = 0.0f;
		if (key == "roughness" && ReadMaterialFloat(value, scalar))
		{
			pbr->m_BasePBRParam.m_roughness = scalar;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "metallic" && ReadMaterialFloat(value, scalar))
		{
			pbr->m_BasePBRParam.m_metallic = scalar;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "ao" && ReadMaterialFloat(value, scalar))
		{
			pbr->m_BasePBRParam.m_ao = scalar;
			FlushMaterialPayload(material);
			return true;
		}
	}
	else if (auto* sss = dynamic_cast<VansSubsurfaceMaterial*>(&material))
	{
		glm::vec3 color;
		if ((key == "subsurfaceColor" || key == "color" || key == "albedo" || key == "baseColor" || key == "basecolor") &&
			ReadMaterialVec3(value, color))
		{
			sss->m_SubsurfaceColor = color;
			sss->m_BasePBRParam.m_albedo = color;
			FlushMaterialPayload(material);
			return true;
		}
		float scalar = 0.0f;
		if (key == "subsurfacePower" && ReadMaterialFloat(value, scalar))
		{
			sss->m_SubsurfacePower = scalar;
			sss->m_BasePBRParam.m_roughness = sss->m_SubsurfacePower;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "thickness" && ReadMaterialFloat(value, scalar))
		{
			sss->m_Thickness = scalar;
			sss->m_BasePBRParam.m_metallic = sss->m_Thickness;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "subsurfaceAmount" && ReadMaterialFloat(value, scalar))
		{
			sss->m_SubsurfaceAmount = scalar;
			sss->m_BasePBRParam.m_ao = sss->m_SubsurfaceAmount;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "curvatureInfluence" && ReadMaterialFloat(value, scalar))
		{
			sss->m_CurvatureInfluence = scalar;
			sss->m_BasePBRParam.padding = sss->m_CurvatureInfluence;
			FlushMaterialPayload(material);
			return true;
		}
	}
	else if (auto* decal = dynamic_cast<VansDecalMaterial*>(&material))
	{
		glm::vec3 color;
		if ((key == "albedo" || key == "baseColor" || key == "basecolor" || key == "color") && ReadMaterialVec3(value, color))
		{
			decal->m_BasePBRParam.m_albedo = color;
			FlushMaterialPayload(material);
			return true;
		}
		float scalar = 0.0f;
		if (key == "roughness" && ReadMaterialFloat(value, scalar))
		{
			decal->m_BasePBRParam.m_roughness = scalar;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "metallic" && ReadMaterialFloat(value, scalar))
		{
			decal->m_BasePBRParam.m_metallic = scalar;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "ao" && ReadMaterialFloat(value, scalar))
		{
			decal->m_BasePBRParam.m_ao = scalar;
			FlushMaterialPayload(material);
			return true;
		}
	}
	else if (auto* emissive = dynamic_cast<VansEmissiveMaterial*>(&material))
	{
		glm::vec3 color;
		if ((key == "albedo" || key == "emissive" || key == "emissive_color" || key == "color") && ReadMaterialVec3(value, color))
		{
			emissive->m_BasePBRParam.m_albedo = color;
			FlushMaterialPayload(material);
			return true;
		}
		float scalar = 0.0f;
		if ((key == "intensity" || key == "emissiveIntensity" || key == "emissive_intensity" || key == "roughness") &&
			ReadMaterialFloat(value, scalar))
		{
			emissive->m_BasePBRParam.m_roughness = scalar;
			FlushMaterialPayload(material);
			return true;
		}
	}
	else if (auto* cloth = dynamic_cast<VansClothMaterial*>(&material))
	{
		glm::vec3 color;
		if ((key == "albedo" || key == "baseColor" || key == "basecolor" || key == "color") && ReadMaterialVec3(value, color))
		{
			cloth->m_BasePBRParam.m_albedo = color;
			FlushMaterialPayload(material);
			return true;
		}
		float scalar = 0.0f;
		if ((key == "sheenRoughness" || key == "roughness") && ReadMaterialFloat(value, scalar))
		{
			cloth->m_SheenRoughness = std::clamp(scalar, 0.045f, 1.0f);
			cloth->m_BasePBRParam.m_roughness = cloth->m_SheenRoughness;
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "sheenStrength" && ReadMaterialFloat(value, scalar))
		{
			cloth->m_BasePBRParam.padding = std::clamp(scalar, 0.0f, 1.0f);
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "translucency" && ReadMaterialFloat(value, scalar))
		{
			cloth->m_BasePBRParam.m_metallic = std::clamp(scalar, 0.0f, 1.0f);
			FlushMaterialPayload(material);
			return true;
		}
		if (key == "ao" && ReadMaterialFloat(value, scalar))
		{
			cloth->m_BasePBRParam.m_ao = std::clamp(scalar, 0.0f, 1.0f);
			FlushMaterialPayload(material);
			return true;
		}
	}

	return false;
}

void VansGraphics::VansMaterialManager::ApplyFogSettings(const VansFogSettings& settings)
{
	m_FogSettings = settings;
	if (m_FogParamsCBBuffer.GetNativeBuffer() == VK_NULL_HANDLE)
		return;

	m_FogParamsCBBuffer.SetBufferData(
		&m_FogSettings,
		0,
		sizeof(VansFogSettings));
}

void VansGraphics::VansMaterialManager::ApplyFogVolumeSettings(const VansFogVolumeSettings& settings)
{
	m_FogVolumeSettings = settings;
	if (m_FogVolumeParamsCBBuffer.GetNativeBuffer() == VK_NULL_HANDLE)
		return;

	m_FogVolumeParamsCBBuffer.SetBufferData(
		&m_FogVolumeSettings,
		0,
		sizeof(VansFogVolumeSettings));
}

void VansGraphics::VansMaterialManager::UpdatePBRLutDescriptorSets()
{
	//update descriptor
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->BeginDescriptorUpdate();
	descMgr->WriteBufferDescriptor(
		m_BRDFInterationTextDescriptorSets[0],
		PassBinding::BUFFER_3,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{ { m_SkySHResultBuffer.GetNativeBuffer(), 0, m_SkySHResultBuffer.GetBufferSize() } });

	descMgr->WriteImageDescriptor(
		m_BRDFInterationTextDescriptorSets[0],
		PassBinding::TEXTURE_0,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { m_BRDFIntegralLUT->GetImage().GetSampler(), m_BRDFIntegralLUT->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(
		m_BRDFInterationTextDescriptorSets[0],
		PassBinding::TEXTURE_1,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { m_PreConvDiffuse->GetImage().GetSampler(), m_PreConvDiffuse->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(
		m_BRDFInterationTextDescriptorSets[0],
		PassBinding::TEXTURE_2,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { m_PreConvSpecular->GetImage().GetSampler(), m_PreConvSpecular->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(
		m_BRDFInterationTextDescriptorSets[0],
		PassBinding::TEXTURE_4,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { m_SkinBSDFLUT->GetImage().GetSampler(), m_SkinBSDFLUT->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });

	// binding 8 �?Cloth BRDF LUT (split-sum .rg + sheen tint .b)
	if (m_ClothBRDFLUT)
	{
		descMgr->WriteImageDescriptor(
			m_BRDFInterationTextDescriptorSets[0],
			PassBinding::TEXTURE_5,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{ { m_ClothBRDFLUT->GetImage().GetSampler(), m_ClothBRDFLUT->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	}

	descMgr->CommitDescriptorUpdates();
}

void VansGraphics::VansClothMaterial::BuildClothTextureDescriptors()
{
	VansDescriptorSetLayoutFactory::CreateAndAllocate_ClothTexture(m_ClothOwnedLayout, m_ClothOwnedDescSets);

	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->BeginDescriptorUpdate();

	if (m_BaseColorTexture)
	{
		descManager->WriteImageDescriptor(
			m_ClothOwnedDescSets[0],
			CLOTH_TEXTURE_BINDING_ALBEDO,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_BaseColorTexture->GetImage().GetSampler(),
				m_BaseColorTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_NormalTexture)
	{
		descManager->WriteImageDescriptor(
			m_ClothOwnedDescSets[0],
			CLOTH_TEXTURE_BINDING_NORMAL,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_NormalTexture->GetImage().GetSampler(),
				m_NormalTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_RoughnessTexture)
	{
		descManager->WriteImageDescriptor(
			m_ClothOwnedDescSets[0],
			CLOTH_TEXTURE_BINDING_ROUGHNESS,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_RoughnessTexture->GetImage().GetSampler(),
				m_RoughnessTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_AoTexture)
	{
		descManager->WriteImageDescriptor(
			m_ClothOwnedDescSets[0],
			CLOTH_TEXTURE_BINDING_AO,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_AoTexture->GetImage().GetSampler(),
				m_AoTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	descManager->CommitDescriptorUpdates();
}

void VansGraphics::VansMaterialManager::UpdateAtmosphereDescriptorSets()
{
	//update descriptor
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->BeginDescriptorUpdate();
	descMgr->WriteBufferDescriptor(
		m_MaterialAtmosphereDataDescriptorSets[0],
		SKYBOX_BINDING_ATMOSPHERE_UBO,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		{ { m_AtmospherePBRDataBuffer.GetNativeBuffer(), 0, m_AtmospherePBRDataBuffer.GetBufferSize() } });

	VansTexture* volumetricFogResult = GetRuntimeRenderTexture(RT_VOLUMETRIC_FOG_RESULT);
	if (volumetricFogResult != nullptr)
	{
		descMgr->WriteImageDescriptor(
			m_MaterialAtmosphereDataDescriptorSets[0],
			SKYBOX_BINDING_FOG,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{ { volumetricFogResult->GetImage().GetSampler(), volumetricFogResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	}

	// 绑定 1/4 分辨率体积云结果�?SkyBox set �?binding=2（SKYBOX_BINDING_CLOUD�?
	VansTexture* cloudBuffer = GetRuntimeRenderTexture(RT_CLOUD_BUFFER);
	if (cloudBuffer != nullptr)
	{
		descMgr->WriteImageDescriptor(
			m_MaterialAtmosphereDataDescriptorSets[0],
			SKYBOX_BINDING_CLOUD,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{ { cloudBuffer->GetImage().GetSampler(), cloudBuffer->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	}
	if (m_MoonAlbedoTexture != nullptr)
	{
		descMgr->WriteImageDescriptor(
			m_MaterialAtmosphereDataDescriptorSets[0],
			SKYBOX_BINDING_MOON_ALBEDO,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{ { m_MoonAlbedoTexture->GetImage().GetSampler(), m_MoonAlbedoTexture->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	}
	descMgr->CommitDescriptorUpdates();
}

//void VansGraphics::VansMaterial::CreatePBRMaterialDataBuffer(VkDevice& logic_device)
//{
//	VkDeviceSize bufferSize = sizeof(m_BasePBRParam);
//	m_BasePBRDataBuffer.CreatVulkanBuffer(
//		logic_device, bufferSize, VK_FORMAT_R32_SFLOAT,
//		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
//		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
//	);
//}
//
//void VansGraphics::VansMaterial::UpdatePBRUniformData()
//{
//	uint32_t offset = 0;
//	uint32_t size = sizeof(VansBasePBRParam);
//	m_BasePBRDataBuffer.SetBufferData(&m_BasePBRParam, offset, size);
//}

void VansGraphics::VansSkyBoxMaterial::UpdateAtmosphereMaterialData(VansMaterialManager& materialManager, VansLightManager& lightManager)
{
	// 场景没有方向光时，保�?m_SunDirection 不变，避免空向量访问越界
	if (lightManager.GetDirectionLights().empty())
		return;

	uint32_t offset = 0;
	uint32_t size = sizeof(VansAtmospherePBRParam);
	const auto& dirLight = lightManager.GetDirectionLights()[0];
	glm::vec3 sunDirection = glm::normalize(dirLight.m_Direction);
	m_AtmospherePBRParam.m_SunDirection = sunDirection;
	// CPU 预计算大气衰减后的太阳颜色，写入 AtmosphereUBO
	// 供无法直接读 LightsData.glsl �?shader（如 VolumeCloud.frag）使�?
	m_AtmospherePBRParam.m_EffectiveSunColor = VansLightManager::ComputeAtmosphereSunColor(
		dirLight.m_Direction, dirLight.m_Color);
	const glm::vec3 moonDirection = -sunDirection;
	const float moonPhase = std::clamp(0.5f * (1.0f - glm::dot(sunDirection, moonDirection)), 0.0f, 1.0f);
	const glm::vec3 sunRadiance = glm::max(
		dirLight.m_Color * dirLight.m_Intensity * m_AtmospherePBRParam.m_SunLuminance,
		glm::vec3(0.0f));
	const glm::vec3 moonRadiance = sunRadiance * m_MoonDiskRadianceScale * glm::vec3(0.82f, 0.86f, 1.0f);

	m_AtmospherePBRParam.m_SunDiskDirectionAngularRadius = glm::vec4(sunDirection, m_SunDiskAngularRadius);
	m_AtmospherePBRParam.m_SunDiskRadianceEnabled = glm::vec4(sunRadiance * m_SunDiskRadianceScale, m_SunDiskEnabled ? 1.0f : 0.0f);
	m_AtmospherePBRParam.m_SunDiskParams = glm::vec4(m_SunDiskFeather, 1.0f, m_SunDiskOcclusionStrength, 0.0f);
	m_AtmospherePBRParam.m_MoonDiskDirectionAngularRadius = glm::vec4(moonDirection, m_MoonDiskAngularRadius);
	m_AtmospherePBRParam.m_MoonDiskRadianceEnabled = glm::vec4(moonRadiance, m_MoonDiskEnabled ? 1.0f : 0.0f);
	m_AtmospherePBRParam.m_MoonDiskParams = glm::vec4(m_MoonDiskFeather, moonPhase, m_MoonDiskOcclusionStrength, 0.0f);
	m_AtmospherePBRParam.m_MainCelestialLightInfo = glm::vec4(sunDirection, 0.0f);
	materialManager.m_AtmospherePBRDataBuffer.SetBufferData(&m_AtmospherePBRParam, offset, size);
}

void VansGraphics::VansTransparentMaterial::CreateTransparentDescriptorLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings)
{
	VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom(
		bindings,
		m_TransparentOwnedLayout,
		m_TransparentOwnedDescSets);
}

void VansGraphics::VansTransparentMaterial::BuildTransparentTextureDescriptors()
{
	// Build one COMBINED_IMAGE_SAMPLER binding per texture slot (in JSON order).
	const uint32_t slotCount = static_cast<uint32_t>(m_TransparentTextures.size());
	if (slotCount == 0)
	{
		// No textures �?create an empty layout so the pipeline still has Set 1.
		CreateTransparentDescriptorLayout();
		return;
	}

	// 1. Build layout bindings
	std::vector<VkDescriptorSetLayoutBinding> bindings(slotCount);
	for (uint32_t i = 0; i < slotCount; ++i)
	{
		bindings[i] = {};
		bindings[i].binding            = i;  // binding index == slot order in JSON
		bindings[i].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[i].descriptorCount    = 1;
		bindings[i].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
		bindings[i].pImmutableSamplers = nullptr;
	}

	// 2. Create layout & allocate descriptor set
	CreateTransparentDescriptorLayout(bindings);

	// 3. Write texture descriptors
	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->BeginDescriptorUpdate();
	for (uint32_t i = 0; i < slotCount; ++i)
	{
		VansTexture* tex = m_TransparentTextures[i];
		if (tex == nullptr)
			continue; // skip unresolved slots

		descManager->WriteImageDescriptor(
			m_TransparentOwnedDescSets[0],
			i,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{ { tex->GetImage().GetSampler(), tex->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	}
	descManager->CommitDescriptorUpdates();
}

void VansGraphics::VansSkinMaterial::BuildSkinTextureDescriptors()
{
	// Allocate the skin texture descriptor set (Set 4: albedo + normal).
	VansDescriptorSetLayoutFactory::CreateAndAllocate_SkinTexture(m_SkinOwnedLayout, m_SkinOwnedDescSets);

	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->BeginDescriptorUpdate();

	if (m_BaseColorTexture)
	{
		descManager->WriteImageDescriptor(
			m_SkinOwnedDescSets[0],
			SKIN_TEXTURE_BINDING_ALBEDO,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_BaseColorTexture->GetImage().GetSampler(),
				m_BaseColorTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_NormalTexture)
	{
		descManager->WriteImageDescriptor(
			m_SkinOwnedDescSets[0],
			SKIN_TEXTURE_BINDING_NORMAL,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_NormalTexture->GetImage().GetSampler(),
				m_NormalTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	descManager->CommitDescriptorUpdates();
}

void VansGraphics::VansHairMaterial::BuildHairDescriptors(VkDevice& device)
{
	VansDescriptorSetLayoutFactory::CreateAndAllocate_HairTexture(m_HairOwnedLayout, m_HairOwnedDescSets);
	m_ParamsDevice = device;
	if (m_ParamsBuffer.GetNativeBuffer() == VK_NULL_HANDLE)
	{
		m_ParamsBuffer.CreatVulkanBuffer(
			device,
			sizeof(VansHairParamsGPU),
			VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}
	m_ParamsBuffer.SetBufferData(&m_Params, 0, sizeof(VansHairParamsGPU));

	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->BeginDescriptorUpdate();

	if (m_AlbedoTexture)
	{
		descManager->WriteImageDescriptor(
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_ALBEDO,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_AlbedoTexture->GetImage().GetSampler(),
				m_AlbedoTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_AlphaTexture)
	{
		descManager->WriteImageDescriptor(
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_ALPHA,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_AlphaTexture->GetImage().GetSampler(),
				m_AlphaTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_NormalTexture)
	{
		descManager->WriteImageDescriptor(
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_NORMAL,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_NormalTexture->GetImage().GetSampler(),
				m_NormalTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_RoughnessTexture)
	{
		descManager->WriteImageDescriptor(
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_ROUGHNESS,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_RoughnessTexture->GetImage().GetSampler(),
				m_RoughnessTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_AOTexture)
	{
		descManager->WriteImageDescriptor(
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_AO,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_AOTexture->GetImage().GetSampler(),
				m_AOTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_ShiftTexture)
	{
		descManager->WriteImageDescriptor(
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_SHIFT,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_ShiftTexture->GetImage().GetSampler(),
				m_ShiftTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_FlowTexture)
	{
		descManager->WriteImageDescriptor(
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_FLOW,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_FlowTexture->GetImage().GetSampler(),
				m_FlowTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_IDTexture)
	{
		descManager->WriteImageDescriptor(
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_ID,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_IDTexture->GetImage().GetSampler(),
				m_IDTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	descManager->WriteBufferDescriptor(
		m_HairOwnedDescSets[0],
		HAIR_TEXTURE_BINDING_PARAMS,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		{{
			m_ParamsBuffer.GetNativeBuffer(),
			0,
			m_ParamsBuffer.GetBufferSize()
		}});

	descManager->CommitDescriptorUpdates();
}

void VansGraphics::VansSubsurfaceMaterial::BuildSubsurfaceTextureDescriptors()
{
	VansDescriptorSetLayoutFactory::CreateAndAllocate_SubsurfaceTexture(m_SubsurfaceOwnedLayout, m_SubsurfaceOwnedDescSets);

	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->BeginDescriptorUpdate();

	if (m_BaseColorTexture)
	{
		descManager->WriteImageDescriptor(
			m_SubsurfaceOwnedDescSets[0],
			SUBSURFACE_TEXTURE_BINDING_ALBEDO,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_BaseColorTexture->GetImage().GetSampler(),
				m_BaseColorTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_NormalTexture)
	{
		descManager->WriteImageDescriptor(
			m_SubsurfaceOwnedDescSets[0],
			SUBSURFACE_TEXTURE_BINDING_NORMAL,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_NormalTexture->GetImage().GetSampler(),
				m_NormalTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_ThicknessTexture)
	{
		descManager->WriteImageDescriptor(
			m_SubsurfaceOwnedDescSets[0],
			SUBSURFACE_TEXTURE_BINDING_THICKNESS,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_ThicknessTexture->GetImage().GetSampler(),
				m_ThicknessTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_RoughnessTexture)
	{
		descManager->WriteImageDescriptor(
			m_SubsurfaceOwnedDescSets[0],
			SUBSURFACE_TEXTURE_BINDING_ROUGHNESS,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_RoughnessTexture->GetImage().GetSampler(),
				m_RoughnessTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	descManager->CommitDescriptorUpdates();
}

void VansGraphics::VansGrassMaterial::BuildGrassTextureDescriptors()
{
	VansDescriptorSetLayoutFactory::CreateAndAllocate_GrassTexture(m_GrassOwnedLayout, m_GrassOwnedDescSets);

	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->BeginDescriptorUpdate();

	if (m_AlbedoTexture)
	{
		descManager->WriteImageDescriptor(
			m_GrassOwnedDescSets[0],
			GRASS_TEXTURE_BINDING_ALBEDO,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_AlbedoTexture->GetImage().GetSampler(),
				m_AlbedoTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_NormalTexture)
	{
		descManager->WriteImageDescriptor(
			m_GrassOwnedDescSets[0],
			GRASS_TEXTURE_BINDING_NORMAL,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_NormalTexture->GetImage().GetSampler(),
				m_NormalTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_RoughnessTexture)
	{
		descManager->WriteImageDescriptor(
			m_GrassOwnedDescSets[0],
			GRASS_TEXTURE_BINDING_ROUGHNESS,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_RoughnessTexture->GetImage().GetSampler(),
				m_RoughnessTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_TranslucencyTexture)
	{
		descManager->WriteImageDescriptor(
			m_GrassOwnedDescSets[0],
			GRASS_TEXTURE_BINDING_TRANSLUCENCY,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_TranslucencyTexture->GetImage().GetSampler(),
				m_TranslucencyTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	if (m_AOTexture)
	{
		descManager->WriteImageDescriptor(
			m_GrassOwnedDescSets[0],
			GRASS_TEXTURE_BINDING_AO,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_AOTexture->GetImage().GetSampler(),
				m_AOTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
	}

	descManager->CommitDescriptorUpdates();
}
