#pragma once

#include "VansVolumetricCloudTypes.h"
#include "../VansRenderRuntimeConfig.h"
#include "../VulkanCore/VansVKBuffer.h"
#include "../VulkanCore/VansVKImage.h"

#include <array>
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
		void DiscardRecordedFrame();
		void BindGlobalDescriptors(VkDescriptorSet globalSet, std::uint32_t slot);
		bool SelectFrameSceneDepth(std::uint32_t frameSlot,
			std::uint32_t gbufferTargetSlot);
		void Shutdown();

		VkDescriptorImageInfo GetShadowDescriptor();
		VkImage GetShadowImage() { return m_ShadowInitialized ? m_Shadow.GetImage() : VK_NULL_HANDLE; }
		VkDescriptorImageInfo GetResultDescriptor();
		VkDescriptorImageInfo GetDepthDescriptor();
		VkDescriptorImageInfo GetOpticalDepthDescriptor();
		struct ViewResourceImages
		{
			VkImage radiance = VK_NULL_HANDLE;
			VkImage depth = VK_NULL_HANDLE;
			VkImage opticalDepth = VK_NULL_HANDLE;
		};
		ViewResourceImages GetViewResourceImages()
		{
			if (!m_Initialized)
				return {};
			return { m_Result.GetImage(), m_Depth.GetImage(), m_OpticalDepth.GetImage() };
		}

	private:
		bool CreatePersistentResources();
		bool CreateViewResources();
		bool LoadNoiseResources();
		bool CreateDescriptors();
		void UploadParameters(std::uint32_t slot);
		void TransitionForWrite(VansVKCommandBuffer&, VansVKImage&, bool, VkPipelineStageFlags readers);
		void BarrierForSampling(VansVKCommandBuffer&, VansVKImage&, VkPipelineStageFlags readers);
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
		std::array<VansVKBuffer, VansCommandRecordingConfig::MaximumFramesInFlight> m_ParamsBuffers;
		std::array<bool, VansCommandRecordingConfig::MaximumFramesInFlight> m_ParamsBufferCreated{};
		VansVKImage m_Result;
		VansVKImage m_Depth;
		VansVKImage m_OpticalDepth;
		VansVKImage m_Shadow;
		VansTexture* m_MainNoise = nullptr;
		VansTexture* m_DetailNoise = nullptr;
		VkDescriptorSetLayout m_PassLayout = VK_NULL_HANDLE;
		std::array<VkDescriptorSet, VansCommandRecordingConfig::MaximumFramesInFlight> m_PassSets{};
		std::array<VkDescriptorSet, VansCommandRecordingConfig::MaximumFramesInFlight> m_ShadowPassSets{};
		std::array<std::uint32_t, VansCommandRecordingConfig::MaximumFramesInFlight> m_SceneDepthTargetSlots{};
		std::array<std::uint64_t, VansCommandRecordingConfig::MaximumFramesInFlight> m_SceneDepthTargetGenerations{};
		VansComputeShader* m_RayMarchShader = nullptr;
		VansComputeShader* m_ShadowShader = nullptr;
		bool m_ResultInitialized = false;
		bool m_ShadowInitialized = false;
		bool m_Initialized = false;
	};
}
