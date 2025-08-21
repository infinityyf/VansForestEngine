#pragma once
#include "../VansCommonUtils.h"
#include "../VulkanCore/VansVKBuffer.h"
#include <vector>
using namespace VansVulkan;

namespace VansGraphics
{
	enum class VansLightType
	{
		DIRECTIONAL = 0,
		POINT = 1,
		SPOT = 2
	};

	//保证16字节对齐
	//CPU和GPU上保持一致
	struct alignas(16) VansDirectionalLight
	{
		glm::vec3				m_Direction;
		alignas(16) glm::vec3	m_Color;
		alignas(16) float		m_Intensity;
		float					padding[3];
		glm::mat4x4				m_ShadowMatrix;
		
	};

	struct alignas(16) VansPointLight
	{
		glm::vec3				m_Position;
		alignas(16) glm::vec3	m_Color;
		alignas(16) float		m_Intensity;
		float					m_Radius;
		float					m_ShadowIndex;
		float					padding;
		glm::mat4				m_PointShadowMatrix[6];
	};

	struct alignas(16) VansSpotLight
	{
		glm::vec3				m_Position;
		alignas(16) glm::vec3	m_Direction;
		alignas(16) glm::vec3	m_Color;
		alignas(16) float		m_Intensity;
		float					m_Radius;
		float					m_InnerCutOff;
		float					m_OuterCutOff;
		glm::mat4				m_SpotShadowMatrix;
		float					m_ShadowIndex;
	};

	class VansLightManager
	{
		friend class VansRenderNode;
	private:
		std::vector<VansDirectionalLight> m_DirectionalLights;
		std::vector<VansPointLight> m_PointLights;
		std::vector<VansSpotLight> m_SpotLights;

		uint32_t m_LightCounts[4];

		VansVKBuffer m_LightBuffer;

		const uint32_t m_MaxDirectionLightCount = 1;
		const uint32_t m_MaxPointLightCount = 10;
		const uint32_t m_MaxSpotLightCount = 10;

	public:

		//Descriptor set layout
		VkDescriptorSetLayout m_LightDataDescriptorSetLayout;
		std::vector<VkDescriptorSet> m_LightDataDescriptorSets;

	public:
		void AddDirectionalLight(const VansDirectionalLight& light);

		void AddPointLight(const VansPointLight& light);

		void AddSpotLight(const VansSpotLight& light);

		void UpdateLightShadowMatrixData();

		void UpdateLightCPUData();

		//Create light gpu data
		void CreateLightUniformData(VkDevice& logic_device);

		std::vector<VansDirectionalLight>& GetDirectionLights() { return m_DirectionalLights; }

		std::vector<VansPointLight>& GetPointLights() { return m_PointLights; }

		std::vector<VansSpotLight>& GetSpotLight() { return m_SpotLights; }

		~VansLightManager();
	};
}