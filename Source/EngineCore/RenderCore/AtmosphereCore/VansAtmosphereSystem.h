#pragma once

#include "VansAtmosphereTypes.h"
#include "../VansRenderRuntimeConfig.h"
#include "../VulkanCore/VansVKBuffer.h"
#include "../VulkanCore/VansVKImage.h"

#include <array>
#include <cstdint>
#include <vulkan/vulkan.h>

namespace VansGraphics
{
	class VansComputeShader;
	class VansScene;
	class VansTexture;
	class VansVKCommandBuffer;
	class VansVKDevice;

	// 物理天空和相机大气透视的唯一 GPU 资源所有者。
	// 云与局部雾只通过全局只读输出契约参与最终合成。
	class VansAtmosphereSystem final
	{
	public:
		VansAtmosphereSystem() = default;
		~VansAtmosphereSystem() = default;

		bool Initialize(
			VansVKDevice& device,
			VansScene& scene,
			const VansAtmosphereQualityConfig& quality,
			std::uint32_t renderWidth,
			std::uint32_t renderHeight);
		void Shutdown();
		bool ReinitializeViewResources(
			const VansAtmosphereQualityConfig& quality,
			std::uint32_t renderWidth,
			std::uint32_t renderHeight);

		void BindGlobalDescriptors(
			VkDescriptorSet globalSet,
			std::uint32_t slot,
			const VkDescriptorImageInfo& cloudShadow,
			const VkDescriptorImageInfo& cloudResult,
			const VkDescriptorImageInfo& localScattering,
			const VkDescriptorImageInfo& localOpticalDepth);
		bool SelectFrameGlobalDescriptorSet(VkDescriptorSet globalSet,
			std::uint32_t frameSlot, std::uint32_t gbufferTargetSlot);

		bool RecordStaticLutUpdates(
			VansVKCommandBuffer& commandBuffer,
			std::uint64_t frameIndex);
		void CommitStaticLutUpdate() { m_StaticLutsDirty = false; }
		void DiscardRecordedFrame();
		void RecordViewLutUpdates(VansVKCommandBuffer& commandBuffer);
		void RecordComposite(VansVKCommandBuffer& commandBuffer);
		void RefreshRawOpaqueSceneColorDescriptors();

		bool IsInitialized() const { return m_Initialized; }
		struct ResourceImages
		{
			VkImage transmittance = VK_NULL_HANDLE;
			VkImage multiScattering = VK_NULL_HANDLE;
			VkImage skyView = VK_NULL_HANDLE;
			VkImage aerialScattering = VK_NULL_HANDLE;
			VkImage aerialClearScattering = VK_NULL_HANDLE;
			VkImage aerialOpticalDepth = VK_NULL_HANDLE;
		};
		ResourceImages GetResourceImages()
		{
			if (!m_Initialized)
				return {};
			return {
				m_TransmittanceLut.GetImage(), m_MultiScatteringLut.GetImage(),
				m_SkyViewLut.GetImage(), m_AerialScattering.GetImage(),
				m_AerialClearScattering.GetImage(), m_AerialOpticalDepth.GetImage()
			};
		}
		VkDescriptorImageInfo GetNeutralCloudShadowDescriptor();
		VkDescriptorImageInfo GetNeutralCloudResultDescriptor();
		VkDescriptorImageInfo GetNeutralLocalMediaDescriptor();

	private:
		bool CreateStaticResources();
		bool CreateViewResources();
		bool CreateDescriptorResources();
		void DestroyViewResources();
		void DestroyDescriptorResources();
		void UploadEnvironmentParameters(std::uint64_t frameIndex);
		void TransitionForWrite(
			VansVKCommandBuffer& commandBuffer,
			VansVKImage& image,
			bool wasWritten,
			std::uint32_t mipCount = 1,
			std::uint32_t layerCount = 1);
		void BarrierForSampling(
			VansVKCommandBuffer& commandBuffer,
			VansVKImage& image,
			std::uint32_t mipCount = 1,
			std::uint32_t layerCount = 1);

		VansVKDevice* m_Device = nullptr;
		VansScene* m_Scene = nullptr;
		VansAtmosphereQualityConfig m_Quality;
		std::uint32_t m_RenderWidth = 0;
		std::uint32_t m_RenderHeight = 0;
		std::uint32_t m_AerialWidth = 0;
		std::uint32_t m_AerialHeight = 0;

		std::array<VansVKBuffer, VansCommandRecordingConfig::MaximumFramesInFlight> m_StaticParamsBuffers;
		std::array<VansVKBuffer, VansCommandRecordingConfig::MaximumFramesInFlight> m_FrameParamsBuffers;
		std::array<bool, VansCommandRecordingConfig::MaximumFramesInFlight> m_StaticParamsBufferCreated{};
		std::array<bool, VansCommandRecordingConfig::MaximumFramesInFlight> m_FrameParamsBufferCreated{};
		std::array<bool, VansCommandRecordingConfig::MaximumFramesInFlight> m_StaticParamsSlotUploaded{};
		std::array<std::uint64_t, VansCommandRecordingConfig::MaximumFramesInFlight> m_StaticParamsSlotGeneration{};
		std::uint32_t m_FrameResourceSlot = 0;

		VansVKImage m_TransmittanceLut;
		VansVKImage m_MultiScatteringLut;
		VansVKImage m_SkyViewLut;
		VansVKImage m_AerialScattering;
		VansVKImage m_AerialClearScattering;
		VansVKImage m_AerialOpticalDepth;
		VansVKImage m_NeutralCloudShadow;
		VansVKImage m_NeutralCloudResult;
		VansVKImage m_NeutralLocalMedia;

		VkDescriptorSetLayout m_PassLayout = VK_NULL_HANDLE;
		std::array<VkDescriptorSet, VansCommandRecordingConfig::MaximumFramesInFlight> m_PassSets{};
		std::array<std::uint32_t, VansCommandRecordingConfig::MaximumFramesInFlight> m_SceneDepthTargetSlots{};
		std::array<std::uint64_t, VansCommandRecordingConfig::MaximumFramesInFlight> m_SceneDepthTargetGenerations{};
		VkDescriptorSet m_GlobalSet = VK_NULL_HANDLE;
		VansComputeShader* m_TransmittanceShader = nullptr;
		VansComputeShader* m_MultiScatteringShader = nullptr;
		VansComputeShader* m_SkyViewShader = nullptr;
		VansComputeShader* m_AerialPerspectiveShader = nullptr;
		VansComputeShader* m_CompositeShader = nullptr;

		std::uint64_t m_AppliedEnvironmentGeneration = 0;
		VansAtmosphereStaticParamsGPU m_LastUploadedStaticParams{};
		bool m_HasUploadedStaticParams = false;
		bool m_StaticLutsDirty = true;
		bool m_TransmittanceReady = false;
		bool m_MultiScatteringReady = false;
		bool m_SkyViewReady = false;
		bool m_AerialReady = false;
		bool m_FinalColorReady = false;
		bool m_Initialized = false;
	};
}
