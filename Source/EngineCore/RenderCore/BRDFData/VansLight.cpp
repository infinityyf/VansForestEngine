#include "VansLight.h"
#include "../../../EngineCore/RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include <iostream>
void VansGraphics::VansLightManager::AddDirectionalLight(const VansDirectionalLight& light)
{
	m_DirectionalLights.push_back(light);
}

void VansGraphics::VansLightManager::AddPointLight(const VansPointLight& light)
{
	m_PointLights.push_back(light);
}

void VansGraphics::VansLightManager::AddSpotLight(const VansSpotLight& light)
{
	m_SpotLights.push_back(light);
}

void VansGraphics::VansLightManager::UpdateLightShadowMatrixData()
{
	int directionLightCount = m_DirectionalLights.size();
	for (int dirLightIndex = 0; dirLightIndex < directionLightCount; dirLightIndex++)
	{
		auto lightDirection = m_DirectionalLights[dirLightIndex].m_Direction;
		glm::mat4x4 projectionMatrix = glm::ortho<float>(-10,10,-10,10,-10,10);
		glm::mat4x4 viewMatrix = glm::lookAt(lightDirection, glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
		m_DirectionalLights[dirLightIndex].m_ShadowMatrix = projectionMatrix * viewMatrix;
	}
}

void VansGraphics::VansLightManager::UpdateLightCPUData()
{
	uint32_t offset = 0;
	uint32_t size = sizeof(VansDirectionalLight) * m_MaxDirectionLightCount;
	m_LightBuffer.SetBufferData(m_DirectionalLights.data(), offset, size);
	offset += size;
	size = sizeof(VansPointLight) * m_MaxPointLightCount;
	m_LightBuffer.SetBufferData(m_PointLights.data(), offset, size);
	offset += size;
	size = sizeof(VansSpotLight) * m_MaxSpotLightCount;
	m_LightBuffer.SetBufferData(m_SpotLights.data(), offset, size);

	//update descriptor
	VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.clear();
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.clear();
	VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
		{
			m_LightDataDescriptorSets[0],
			VansVKDescriptorManager::m_LightsBufferSetBinding,
			0,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{
				{
					m_LightBuffer.GetMativeBuffer(),
					0,
					m_LightBuffer.GetBufferSize()
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}

void VansGraphics::VansLightManager::CreateLightUniformData(VkDevice& logic_device)
{
	uint32_t bufferSize = sizeof(VansDirectionalLight) * m_MaxDirectionLightCount +
		sizeof(VansPointLight) * m_MaxPointLightCount +
		sizeof(VansSpotLight) * m_MaxSpotLightCount;
	m_LightBuffer.CreatVulkanBuffer(
		logic_device, bufferSize, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
	);

	//创建资源
	VkDescriptorSetLayoutBinding lightBufferBinding =
	{
		VansVKDescriptorManager::m_LightsBufferSetBinding,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		1,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		nullptr
	};
	VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ lightBufferBinding }, m_LightDataDescriptorSetLayout);
	VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_LightDataDescriptorSetLayout }, m_LightDataDescriptorSets);
}

VansGraphics::VansLightManager::~VansLightManager()
{
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(m_LightDataDescriptorSets);
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSetLayout(m_LightDataDescriptorSetLayout);
}
