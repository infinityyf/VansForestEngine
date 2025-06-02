#include "VansMaterial.h"
using namespace VansVulkan;

void VansGraphics::VansMaterialManager::InitMaterialDataDescriptors()
{
	VkDescriptorSetLayoutBinding basePBRDataBinding =
	{
		VansVKDescriptorManager::m_MaterialBufferSetBinding,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		1,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ basePBRDataBinding }, m_MaterialPBRBaseDataLayout);
	VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_MaterialPBRBaseDataLayout }, m_MaterialPBRBaseDataDescriptorSets);
}

VansGraphics::VansMaterialManager::VansMaterialManager()
{
	InitMaterialDataDescriptors();
}

void VansGraphics::VansMaterial::CreatePBRMaterialDataBuffer(VkDevice& logic_device)
{
	VkDeviceSize bufferSize = sizeof(m_BasePBRParam);
	m_BasePBRDataBuffer.CreatVulkanBuffer(
		logic_device, bufferSize, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
	);
}

void VansGraphics::VansMaterial::CreateAtmosphereMaterialDataBuffer(VkDevice& logic_device)
{
	VkDeviceSize bufferSize = sizeof(m_AtmospherePBRParam);
	m_AtmospherePBRDataBuffer.CreatVulkanBuffer(
		logic_device, bufferSize, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
	);
}

void VansGraphics::VansMaterial::UpdateMaterialData(VansMaterialManager& materialManager, VansLightManager& lightManager)
{
	switch (m_MaterialType)
	{
	case VansGraphics::VAN_DEFERRED:
		UpdatePBRLutData(materialManager);
		break;
	case VansGraphics::VAN_PBR:
		UpdatePBRUniformData(materialManager);
		UpdatePBRLutData(materialManager);
		break;
	case VansGraphics::VAN_COAT:
		break;
	case VansGraphics::VAN_TRANSPARENT:
		break;
	case VansGraphics::VAN_POST_PROCESS:
		break;
	case VansGraphics::VAN_SKY_BOX:
		UpdateAtmosphereMaterialData(materialManager, lightManager);
		break;
	default:
		break;
	}
}

void VansGraphics::VansMaterial::UpdatePBRLutData(VansMaterialManager& materialManager)
{
	//update descriptor
	VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.clear();
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.clear();

	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			materialManager.m_BRDFInterationTextDescriptorSets[0],
			VansVKDescriptorManager::m_SampleTexture0SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					materialManager.m_BRDFIntegralLUT->GetImage().GetSampler(),
					materialManager.m_BRDFIntegralLUT->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			materialManager.m_BRDFInterationTextDescriptorSets[0],
			VansVKDescriptorManager::m_SampleTexture1SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					materialManager.m_PreConvDiffuse->GetImage().GetSampler(),
					materialManager.m_PreConvDiffuse->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_GENERAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			materialManager.m_BRDFInterationTextDescriptorSets[0],
			VansVKDescriptorManager::m_SampleTexture2SetBinding,
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					materialManager.m_PreConvSpecular->GetImage().GetSampler(),
					materialManager.m_PreConvSpecular->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_GENERAL
				}
			}
		}
	);

	VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}

void VansGraphics::VansMaterial::UpdatePBRUniformData(VansMaterialManager& materialManager)
{
	uint32_t offset = 0;
	uint32_t size = sizeof(VansBasePBRParam);
	m_BasePBRDataBuffer.SetBufferData(&m_BasePBRParam, offset, size);

	//update descriptor
	VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.clear();
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.clear();
	VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
		{
			materialManager.m_MaterialPBRBaseDataDescriptorSets[0],
			VansVKDescriptorManager::m_MaterialBufferSetBinding,
			0,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{
				{
					m_BasePBRDataBuffer.GetMativeBuffer(),
					0,
					m_BasePBRDataBuffer.GetBufferSize()
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}

void VansGraphics::VansMaterial::UpdateAtmosphereMaterialData(VansMaterialManager& materialManager, VansLightManager& lightManager)
{
	uint32_t offset = 0;
	uint32_t size = sizeof(VansAtmospherePBRParam);
	m_AtmospherePBRParam.m_SunDirection = lightManager.GetDirectionLights()[0].m_Direction;
	m_AtmospherePBRParam.m_SunDirection = glm::normalize(m_AtmospherePBRParam.m_SunDirection);
	m_AtmospherePBRDataBuffer.SetBufferData(&m_AtmospherePBRParam, offset, size);

	//update descriptor
	VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.clear();
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.clear();
	VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
		{
			materialManager.m_MaterialAtmosphereDataDescriptorSets[0],
			VansVKDescriptorManager::m_AtmosphereBufferSetBinding,
			0,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{
				{
					m_AtmospherePBRDataBuffer.GetMativeBuffer(),
					0,
					m_AtmospherePBRDataBuffer.GetBufferSize()
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}
