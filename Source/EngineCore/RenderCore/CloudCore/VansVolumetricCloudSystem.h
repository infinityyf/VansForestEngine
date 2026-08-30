#pragma once

#include "VansVolumetricCloudTypes.h"
#include "../VansRenderRuntimeConfig.h"
#include "../VulkanCore/VansVKBuffer.h"
#include "../VulkanCore/VansVKImage.h"

#include <cstdint>
#include <glm/glm.hpp>

namespace VansGraphics
{
	class VansComputeShader;
	class VansScene;
	class VansTexture;
	class VansVKCommandBuffer;
	class VansVKDevice;

	class VansVolumetricCloudSystem final
	{
	public:
		bool Initialize(VansVKDevice& device, VansScene& scene,
			const VansCloudShadowQualityConfig& quality,
			std::uint32_t renderWidth, std::uint32_t renderHeight);
		bool Reinitialize(const VansCloudShadowQualityConfig& quality,
			std::uint32_t renderWidth, std::uint32_t renderHeight);
		void RecordShadow(VansVKCommandBuffer& commandBuffer);
		void RecordRayMarch(VansVKCommandBuffer& commandBuffer);
		void BindGlobalDescriptors(VkDescriptorSet globalSet);
		void Shutdown();

		VkDescriptorImageInfo GetShadowDescriptor();
		VkDescriptorImageInfo GetResultDescriptor();
		VkDescriptorImageInfo GetDepthDescriptor();
		VkDescriptorImageInfo GetOpticalDepthDescriptor();

	private:
		bool CreatePersistentResources();
		bool CreateViewResources();
		bool LoadNoiseResources();
		bool CreateDescriptors();
		void UploadParameters();
		void TransitionForWrite(VansVKCommandBuffer&, VansVKImage&, bool);
		void BarrierForSampling(VansVKCommandBuffer&, VansVKImage&);
		void DestroyDescriptors();
		void DestroyViewResources();
		void DestroyPersistentResources();

		VansVKDevice* m_Device = nullptr;
		VansScene* m_Scene = nullptr;
		VansCloudShadowQualityConfig m_Quality{};
		std::uint32_t m_RenderWidth = 0;
		std::uint32_t m_RenderHeight = 0;
		std::uint32_t m_CloudWidth = 0;
		std::uint32_t m_CloudHeight = 0;
		VansVKBuffer m_ParamsBuffer;
		bool m_ParamsBufferCreated = false;
		VansVKImage m_Result;
		VansVKImage m_Depth;
		VansVKImage m_OpticalDepth;
		VansVKImage m_Shadow;
		VansTexture* m_MainNoise = nullptr;
		VansTexture* m_DetailNoise = nullptr;
		VkDescriptorSetLayout m_PassLayout = VK_NULL_HANDLE;
		VkDescriptorSet m_PassSet = VK_NULL_HANDLE;
		VansComputeShader* m_RayMarchShader = nullptr;
		VansComputeShader* m_ShadowShader = nullptr;
		bool m_ResultInitialized = false;
		bool m_ShadowInitialized = false;
		bool m_Initialized = false;
	};
}
