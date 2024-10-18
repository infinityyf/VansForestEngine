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

void VansGraphics::VansMaterial::UpdateMaterialDescriporSet(VansMaterialManager& materialManager)
{
	switch (m_MaterialType)
	{
	case VansGraphics::VAN_PBR:
		UpdatePBRMaterialData(materialManager);
		break;
	case VansGraphics::VAN_COAT:
		break;
	case VansGraphics::VAN_TRANSPARENT:
		break;
	case VansGraphics::VAN_POST_PROCESS:
		break;
	default:
		break;
	}
}

void VansGraphics::VansMaterial::UpdatePBRMaterialData(VansMaterialManager& materialManager)
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
