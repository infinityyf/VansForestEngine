#include "VansMaterial.h"
using namespace VansGraphics;

VansGraphics::VansMaterialManager::VansMaterialManager()
{
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

void VansGraphics::VansMaterialManager::ClearRuntimeRenderTextures()
{
	m_RuntimeRenderTextureManager.Clear();
	m_SSGITemporalFrame = 0;
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

	VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
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

void VansGraphics::VansMaterial::UpdateAtmosphereMaterialData(VansMaterialManager& materialManager, VansLightManager& lightManager)
{
	uint32_t offset = 0;
	uint32_t size = sizeof(VansAtmospherePBRParam);
	m_AtmospherePBRParam.m_SunDirection = lightManager.GetDirectionLights()[0].m_Direction;
	m_AtmospherePBRParam.m_SunDirection = glm::normalize(m_AtmospherePBRParam.m_SunDirection);
	materialManager.m_AtmospherePBRDataBuffer.SetBufferData(&m_AtmospherePBRParam, offset, size);
}
