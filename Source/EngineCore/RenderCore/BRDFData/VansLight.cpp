#include "VansLight.h"
#include "../../../EngineCore/RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../../../EngineCore/Configration/VansConfigration.h"
#include "../../../EngineCore/VansTimer.h"
#include "../VansCamera.h"
#include <iostream>
#include <algorithm>
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

void VansGraphics::VansLightManager::UpdateLightShadowMatrixData(const glm::vec3& cameraPosition)
{
	auto vansConfig = VansConfigration::GetInstance();
	int cascadeCount = vansConfig->GetCascadeCount();
	const float* cascadeSplits = vansConfig->GetCascadeSplits();
	int cascadeMapSize = vansConfig->GetCascadeShadowMapSize();

	int directionLightCount = m_DirectionalLights.size();
	for (int dirLightIndex = 0; dirLightIndex < directionLightCount; dirLightIndex++)
	{
		auto lightDir = glm::normalize(m_DirectionalLights[dirLightIndex].m_Direction);

		// Store cascade split distances for shader usage
		m_DirectionalLights[dirLightIndex].m_CascadeSplits = glm::vec4(
			cascadeSplits[0], cascadeSplits[1], cascadeSplits[2], cascadeSplits[3]);

		// Coverage margin: each cascade covers 1.5x its split distance
		const float coverageMargin = 1.5f;

		for (int cascade = 0; cascade < cascadeCount; ++cascade)
		{
			float halfExtent = cascadeSplits[cascade] * coverageMargin;
			float texelSize = 2.0f * halfExtent / (float)cascadeMapSize;

			// Build light view matrix centered on the camera position.
			// The light "eye" is placed far behind the camera along the light direction
			// so the ortho frustum covers geometry around where the camera is looking.
			glm::vec3 lightEye = cameraPosition + lightDir * halfExtent * 2.0f;
			glm::vec3 up = (std::abs(glm::dot(lightDir, glm::vec3(0, 1, 0))) > 0.99f)
				? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
			glm::mat4x4 viewMatrix = glm::lookAt(lightEye, cameraPosition, up);

			// Snap the camera's light-space XY to texel grid to reduce shadow swimming
			glm::vec4 camLS = viewMatrix * glm::vec4(cameraPosition, 1.0f);
			float snapOffsetX = std::fmod(camLS.x, texelSize);
			float snapOffsetY = std::fmod(camLS.y, texelSize);
			glm::mat4x4 snapMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(-snapOffsetX, -snapOffsetY, 0.0f));

			// Generous Z range to capture shadow casters behind the camera (from the light's perspective)
			float zRange = halfExtent * 4.0f;
			glm::mat4x4 projectionMatrix = glm::ortho<float>(
				-halfExtent, halfExtent,
				-halfExtent, halfExtent,
				-zRange, zRange);

			m_DirectionalLights[dirLightIndex].m_ShadowMatrix[cascade] = projectionMatrix * snapMatrix * viewMatrix;
		}
	}

	int pointLightCount = m_PointLights.size();
	for (int pointLightIndex = 0; pointLightIndex < pointLightCount; pointLightIndex++)
	{
		glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.001f, m_PointLights[pointLightIndex].m_Radius);
		glm::vec3 lightPos = m_PointLights[pointLightIndex].m_Position;

		m_PointLights[pointLightIndex].m_PointShadowMatrix[0] = shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(1, 0, 0), glm::vec3(0, -1, 0)); // +X

		m_PointLights[pointLightIndex].m_PointShadowMatrix[1] = shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(-1, 0, 0), glm::vec3(0, -1, 0)); // -X

		m_PointLights[pointLightIndex].m_PointShadowMatrix[2] = shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)); // +Y

		m_PointLights[pointLightIndex].m_PointShadowMatrix[3] = shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0, -1, 0), glm::vec3(0, 0, -1)); // -Y

		m_PointLights[pointLightIndex].m_PointShadowMatrix[4] = shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0, 0, 1), glm::vec3(0, -1, 0)); // +Z
		
		m_PointLights[pointLightIndex].m_PointShadowMatrix[5] = shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0, 0, -1), glm::vec3(0, -1, 0)); // -Z
		
		m_PointLights[pointLightIndex].m_ShadowIndex = pointLightIndex;
	}
	int spotLightCount = m_SpotLights.size();
	for (int spotLightIndex = 0; spotLightIndex < spotLightCount; spotLightIndex++)
	{

		float spotAngle = m_SpotLights[spotLightIndex].m_OuterCutOff;
		glm::mat4 shadowProj = glm::perspective(spotAngle * 2, 1.0f, 0.001f, m_SpotLights[spotLightIndex].m_Radius);

		glm::vec3 lightPos = m_SpotLights[spotLightIndex].m_Position;
		glm::vec3 lightDir = -m_SpotLights[spotLightIndex].m_Direction;
		glm::mat4 shadowView = glm::lookAt(lightPos, lightPos + lightDir, glm::vec3(0, 1, 0));
		m_SpotLights[spotLightIndex].m_SpotShadowMatrix = shadowProj * shadowView;
		m_SpotLights[spotLightIndex].m_ShadowIndex = spotLightIndex;
	}
}

void VansGraphics::VansLightManager::UpdateLightCPUData()
{
	int spotLightCount = m_SpotLights.size();
	//for (int spotLightIndex = 0; spotLightIndex < spotLightCount; spotLightIndex++)
	//{

	//	m_SpotLights[spotLightIndex].m_Position.x = std::sin(VansTimer::GetFrameTime() * 0.5f) * 6;
	//}

	auto vansConfigration = VansConfigration::GetInstance();
	float punctualShadowSize = vansConfigration->GetPunctualShadowMapWidth();
	float patchShadowSize = punctualShadowSize / 8;

	uint32_t offset = 0;
	uint32_t size = sizeof(uint32_t) * 4;
	m_LightCounts[0] = m_PointLights.size();
	m_LightCounts[1] = m_SpotLights.size();
	m_LightCounts[2] = patchShadowSize;
	m_LightCounts[3] = 8;
	m_LightBuffer.SetBufferData(m_LightCounts, offset, size);
	offset += size;
	size = sizeof(float) * 4;
	m_SoftShadowParams[0] = m_SoftShadowParams[0] + 1;
	m_SoftShadowParams[1] = 0;
	m_SoftShadowParams[2] = 0;
	m_SoftShadowParams[3] = 0;
	m_LightBuffer.SetBufferData(m_SoftShadowParams, offset, size);
	offset += size;
	size = sizeof(VansDirectionalLight) * m_MaxDirectionLightCount;
	m_LightBuffer.SetBufferData(m_DirectionalLights.data(), offset, size);
	offset += size;
	size = sizeof(VansPointLight) * m_MaxPointLightCount;
	m_LightBuffer.SetBufferData(m_PointLights.data(), offset, size);
	offset += size;
	size = sizeof(VansSpotLight) * m_MaxSpotLightCount;
	m_LightBuffer.SetBufferData(m_SpotLights.data(), offset, size);

}

void VansGraphics::VansLightManager::CreateLightUniformData(VkDevice& logic_device)
{
	uint32_t bufferSize = sizeof(uint32_t) * 4 + sizeof(VansDirectionalLight) * m_MaxDirectionLightCount +
		sizeof(VansPointLight) * m_MaxPointLightCount +
		sizeof(VansSpotLight) * m_MaxSpotLightCount + sizeof(float) * 4;
	m_LightBuffer.CreatVulkanBuffer(
		logic_device, bufferSize, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
	);

	//创建资源
	VkDescriptorSetLayoutBinding lightBufferBinding =
	{
		PassBinding::CBUFFER_0,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		1,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
		nullptr
	};
	VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ lightBufferBinding }, m_LightDataDescriptorSetLayout);
	VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_LightDataDescriptorSetLayout }, m_LightDataDescriptorSets);

	//update descriptor
	VansVKDescriptorManager::GetInstance()->ResetState();
	VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
		{
			m_LightDataDescriptorSets[0],
			PassBinding::CBUFFER_0,
			0,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{
				{
					m_LightBuffer.GetNativeBuffer(),
					0,
					m_LightBuffer.GetBufferSize()
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}

VansGraphics::VansLightManager::~VansLightManager()
{
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(m_LightDataDescriptorSets);
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSetLayout(m_LightDataDescriptorSetLayout);
}
