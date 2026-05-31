#include "VansMaterial.h"
using namespace VansGraphics;

// ============================================================
// VansMaterial — pass shader accessors
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
// Material subclass destructors — release owned Vulkan resources
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
	// 清空 CPU 侧 PBR 数组（指针不拥有所有权，material 由 VansScene 管理）
	m_GlobalPBRMaterial.clear();
	m_GlobalPBRParamData.clear();
	m_GlobalPBRTextures.clear();

	// 销毁 GPU buffer
	m_GlobalPBRDataBuffer.DestroyVulkanBuffer(device);

	// 释放 descriptor set 和 layout
	auto descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_GlobalPBRDataDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_GlobalPBRDataSetLayout);
	descMgr->DestroyDescriptorSet(m_GlobalPBRTexDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_GlobalPBRTexSetLayout);
}

void VansGraphics::VansMaterialManager::UpdatePBRLutDescriptorSets()
{
	//update descriptor
	VansVKDescriptorManager::GetInstance()->ResetState();
	VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
		{
			m_BRDFInterationTextDescriptorSets[0],
			PassBinding::BUFFER_3,
			0,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{
				{
					m_SkySHResultBuffer.GetNativeBuffer(),
					0,
					m_SkySHResultBuffer.GetBufferSize()
				}
			}
		}
	);

	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			m_BRDFInterationTextDescriptorSets[0],
			PassBinding::TEXTURE_0,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					m_BRDFIntegralLUT->GetImage().GetSampler(),
					m_BRDFIntegralLUT->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			m_BRDFInterationTextDescriptorSets[0],
			PassBinding::TEXTURE_1,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					m_PreConvDiffuse->GetImage().GetSampler(),
					m_PreConvDiffuse->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			m_BRDFInterationTextDescriptorSets[0],
			PassBinding::TEXTURE_2,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					m_PreConvSpecular->GetImage().GetSampler(),
					m_PreConvSpecular->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);

	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			m_BRDFInterationTextDescriptorSets[0],
			PassBinding::TEXTURE_4,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					m_SkinBSDFLUT->GetImage().GetSampler(),
					m_SkinBSDFLUT->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);

	// binding 8 — Cloth BRDF LUT (split-sum .rg + sheen tint .b)
	if (m_ClothBRDFLUT)
	{
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				m_BRDFInterationTextDescriptorSets[0],
				PassBinding::TEXTURE_5,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						m_ClothBRDFLUT->GetImage().GetSampler(),
						m_ClothBRDFLUT->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					}
				}
			}
		);
	}

	VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}

void VansGraphics::VansClothMaterial::BuildClothTextureDescriptors()
{
	VansDescriptorSetLayoutFactory::CreateAndAllocate_ClothTexture(m_ClothOwnedLayout, m_ClothOwnedDescSets);

	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->ResetState();

	if (m_BaseColorTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_ClothOwnedDescSets[0],
			CLOTH_TEXTURE_BINDING_ALBEDO, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_BaseColorTexture->GetImage().GetSampler(),
				m_BaseColorTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	if (m_NormalTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_ClothOwnedDescSets[0],
			CLOTH_TEXTURE_BINDING_NORMAL, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_NormalTexture->GetImage().GetSampler(),
				m_NormalTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	if (m_RoughnessTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_ClothOwnedDescSets[0],
			CLOTH_TEXTURE_BINDING_ROUGHNESS, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_RoughnessTexture->GetImage().GetSampler(),
				m_RoughnessTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	if (m_AoTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_ClothOwnedDescSets[0],
			CLOTH_TEXTURE_BINDING_AO, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_AoTexture->GetImage().GetSampler(),
				m_AoTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	descManager->UpdateDescriptorSets();
}

void VansGraphics::VansMaterialManager::UpdateAtmosphereDescriptorSets()
{
	//update descriptor
	VansVKDescriptorManager::GetInstance()->ResetState();
	VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
		{
			m_MaterialAtmosphereDataDescriptorSets[0],
			SKYBOX_BINDING_ATMOSPHERE_UBO,
			0,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{
				{
					m_AtmospherePBRDataBuffer.GetNativeBuffer(),
					0,
					m_AtmospherePBRDataBuffer.GetBufferSize()
				}
			}
		}
	);

	VansTexture* volumetricFogResult = GetRuntimeRenderTexture(RT_VOLUMETRIC_FOG_RESULT);
	if (volumetricFogResult != nullptr)
	{
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				m_MaterialAtmosphereDataDescriptorSets[0],
				SKYBOX_BINDING_FOG,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						volumetricFogResult->GetImage().GetSampler(),
						volumetricFogResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					}
				}
			}
		);
	}

	// 绑定 1/4 分辨率体积云结果到 SkyBox set 的 binding=2（SKYBOX_BINDING_CLOUD）
	VansTexture* cloudBuffer = GetRuntimeRenderTexture(RT_CLOUD_BUFFER);
	if (cloudBuffer != nullptr)
	{
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				m_MaterialAtmosphereDataDescriptorSets[0],
				SKYBOX_BINDING_CLOUD,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						cloudBuffer->GetImage().GetSampler(),
						cloudBuffer->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL  // cloud buffer 由 compute shader 以 GENERAL 布局写入，采样时保持一致
					}
				}
			}
		);
	}
	VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
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
	// 场景没有方向光时，保持 m_SunDirection 不变，避免空向量访问越界
	if (lightManager.GetDirectionLights().empty())
		return;

	uint32_t offset = 0;
	uint32_t size = sizeof(VansAtmospherePBRParam);
	m_AtmospherePBRParam.m_SunDirection = lightManager.GetDirectionLights()[0].m_Direction;
	m_AtmospherePBRParam.m_SunDirection = glm::normalize(m_AtmospherePBRParam.m_SunDirection);
	materialManager.m_AtmospherePBRDataBuffer.SetBufferData(&m_AtmospherePBRParam, offset, size);
}

void VansGraphics::VansTransparentMaterial::CreateTransparentDescriptorLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings)
{
	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->CreateDesciptorSetLayout(bindings, m_TransparentOwnedLayout);
	std::vector<VkDescriptorSetLayout> layouts(1, m_TransparentOwnedLayout);
	descManager->AllocateDescriptorSet(layouts, m_TransparentOwnedDescSets);
}

void VansGraphics::VansTransparentMaterial::BuildTransparentTextureDescriptors()
{
	// Build one COMBINED_IMAGE_SAMPLER binding per texture slot (in JSON order).
	const uint32_t slotCount = static_cast<uint32_t>(m_TransparentTextures.size());
	if (slotCount == 0)
	{
		// No textures — create an empty layout so the pipeline still has Set 1.
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
	descManager->ResetState();
	for (uint32_t i = 0; i < slotCount; ++i)
	{
		VansTexture* tex = m_TransparentTextures[i];
		if (tex == nullptr)
			continue; // skip unresolved slots

		descManager->m_ImageDescInfos.push_back(
			{
				m_TransparentOwnedDescSets[0],
				i,  // binding
				0,  // array element
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						tex->GetImage().GetSampler(),
						tex->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					}
				}
			}
		);
	}
	descManager->UpdateDescriptorSets();
}

void VansGraphics::VansSkinMaterial::BuildSkinTextureDescriptors()
{
	// Allocate the skin texture descriptor set (Set 4: albedo + normal).
	VansDescriptorSetLayoutFactory::CreateAndAllocate_SkinTexture(m_SkinOwnedLayout, m_SkinOwnedDescSets);

	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->ResetState();

	if (m_BaseColorTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_SkinOwnedDescSets[0],
			SKIN_TEXTURE_BINDING_ALBEDO, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_BaseColorTexture->GetImage().GetSampler(),
				m_BaseColorTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	if (m_NormalTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_SkinOwnedDescSets[0],
			SKIN_TEXTURE_BINDING_NORMAL, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_NormalTexture->GetImage().GetSampler(),
				m_NormalTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	descManager->UpdateDescriptorSets();
}

void VansGraphics::VansHairMaterial::BuildHairTextureDescriptors()
{
	// Allocate the hair texture descriptor set (Set 4: albedo+alpha, normal, roughness, ao, shift)
	VansDescriptorSetLayoutFactory::CreateAndAllocate_HairTexture(m_HairOwnedLayout, m_HairOwnedDescSets);

	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->ResetState();

	if (m_AlbedoAlphaTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_ALBEDO_ALPHA, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_AlbedoAlphaTexture->GetImage().GetSampler(),
				m_AlbedoAlphaTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	if (m_NormalTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_NORMAL, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_NormalTexture->GetImage().GetSampler(),
				m_NormalTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	if (m_RoughnessTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_ROUGHNESS, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_RoughnessTexture->GetImage().GetSampler(),
				m_RoughnessTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	if (m_AoTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_AO, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_AoTexture->GetImage().GetSampler(),
				m_AoTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	if (m_ShiftTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_SHIFT, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_ShiftTexture->GetImage().GetSampler(),
				m_ShiftTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	if (m_AlphaTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_ALPHA, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_AlphaTexture->GetImage().GetSampler(),
				m_AlphaTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	if (m_FlowTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_HairOwnedDescSets[0],
			HAIR_TEXTURE_BINDING_FLOW, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_FlowTexture->GetImage().GetSampler(),
				m_FlowTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	descManager->UpdateDescriptorSets();
}

void VansGraphics::VansSubsurfaceMaterial::BuildSubsurfaceTextureDescriptors()
{
	VansDescriptorSetLayoutFactory::CreateAndAllocate_SubsurfaceTexture(m_SubsurfaceOwnedLayout, m_SubsurfaceOwnedDescSets);

	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->ResetState();

	if (m_BaseColorTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_SubsurfaceOwnedDescSets[0],
			SUBSURFACE_TEXTURE_BINDING_ALBEDO, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_BaseColorTexture->GetImage().GetSampler(),
				m_BaseColorTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	if (m_NormalTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_SubsurfaceOwnedDescSets[0],
			SUBSURFACE_TEXTURE_BINDING_NORMAL, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_NormalTexture->GetImage().GetSampler(),
				m_NormalTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	if (m_ThicknessTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_SubsurfaceOwnedDescSets[0],
			SUBSURFACE_TEXTURE_BINDING_THICKNESS, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_ThicknessTexture->GetImage().GetSampler(),
				m_ThicknessTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	if (m_RoughnessTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_SubsurfaceOwnedDescSets[0],
			SUBSURFACE_TEXTURE_BINDING_ROUGHNESS, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_RoughnessTexture->GetImage().GetSampler(),
				m_RoughnessTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	descManager->UpdateDescriptorSets();
}

void VansGraphics::VansGrassMaterial::BuildGrassTextureDescriptors()
{
	VansDescriptorSetLayoutFactory::CreateAndAllocate_GrassTexture(m_GrassOwnedLayout, m_GrassOwnedDescSets);

	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->ResetState();

	if (m_AlbedoTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_GrassOwnedDescSets[0],
			GRASS_TEXTURE_BINDING_ALBEDO, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_AlbedoTexture->GetImage().GetSampler(),
				m_AlbedoTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	if (m_NormalTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_GrassOwnedDescSets[0],
			GRASS_TEXTURE_BINDING_NORMAL, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_NormalTexture->GetImage().GetSampler(),
				m_NormalTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	if (m_RoughnessTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_GrassOwnedDescSets[0],
			GRASS_TEXTURE_BINDING_ROUGHNESS, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_RoughnessTexture->GetImage().GetSampler(),
				m_RoughnessTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	if (m_TranslucencyTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_GrassOwnedDescSets[0],
			GRASS_TEXTURE_BINDING_TRANSLUCENCY, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_TranslucencyTexture->GetImage().GetSampler(),
				m_TranslucencyTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	if (m_AOTexture)
	{
		descManager->m_ImageDescInfos.push_back({
			m_GrassOwnedDescSets[0],
			GRASS_TEXTURE_BINDING_AO, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				m_AOTexture->GetImage().GetSampler(),
				m_AOTexture->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}}
		});
	}

	descManager->UpdateDescriptorSets();
}