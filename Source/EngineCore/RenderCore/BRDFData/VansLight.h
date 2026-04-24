#pragma once
#include "../../ScriptCore/VansCommonUtils.h"
#include "../VulkanCore/VansVKBuffer.h"
#include <vector>
using namespace VansGraphics;

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
		glm::mat4x4				m_ShadowMatrix[4];      // one per cascade
		glm::vec4				m_CascadeSplits;        // view-space far distances per cascade
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
		float m_SoftShadowParams[4];

		VansVKBuffer m_LightBuffer;

		const uint32_t m_MaxDirectionLightCount = 1;
		const uint32_t m_MaxPointLightCount = 64;
		const uint32_t m_MaxSpotLightCount = 64;

	public:

		//Descriptor set layout
		VkDescriptorSetLayout m_LightDataDescriptorSetLayout;
		std::vector<VkDescriptorSet> m_LightDataDescriptorSets;

	public:
		VansVKBuffer& GetLightBuffer() { return m_LightBuffer; }

		void AddDirectionalLight(const VansDirectionalLight& light);

		void AddPointLight(const VansPointLight& light);

		void AddSpotLight(const VansSpotLight& light);

		void UpdateLightShadowMatrixData(const glm::vec3& cameraPosition);

		void UpdateLightCPUData();

		void SyncLightGPUData(const glm::vec3& cameraPosition);

		//Create light gpu data
		void CreateLightUniformData(VkDevice& logic_device);

		std::vector<VansDirectionalLight>& GetDirectionLights() { return m_DirectionalLights; }

		std::vector<VansPointLight>& GetPointLights() { return m_PointLights; }

		std::vector<VansSpotLight>& GetSpotLight() { return m_SpotLights; }

		uint32_t GetMaxPointLightCount() const { return m_MaxPointLightCount; }

		uint32_t GetMaxSpotLightCount() const { return m_MaxSpotLightCount; }

		// ── 场景切换时清空灯光数据 ────────────────────────────────────
		// 仅清空 CPU 侧灯光列表和计数器，保留 GPU buffer 和 descriptor
		// 以便下一次 CreateLightUniformData 时复用。
		void ClearLights();

		// 销毁 GPU buffer 和 descriptor（用于完全卸载场景）
		void DestroyGPUResources(VkDevice device);

		~VansLightManager();
	};
}