#pragma once

#include "VansAtmosphereTypes.h"
#include "../VulkanCore/VansVKBuffer.h"
#include "../VulkanCore/VansVKImage.h"

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
			const VkDescriptorImageInfo& cloudShadow,
			const VkDescriptorImageInfo& cloudResult,
			const VkDescriptorImageInfo& localScattering,
			const VkDescriptorImageInfo& localOpticalDepth);

		void RecordStaticLutUpdates(
			VansVKCommandBuffer& commandBuffer,
			std::uint64_t frameIndex);
		void RecordViewLutUpdates(VansVKCommandBuffer& commandBuffer);
		void RecordComposite(VansVKCommandBuffer& commandBuffer);
		void InvalidateStaticLuts() { m_StaticLutsDirty = true; }

		bool IsInitialized() const { return m_Initialized; }
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

		VansVKBuffer m_StaticParamsBuffer;
		VansVKBuffer m_FrameParamsBuffer;
		bool m_StaticParamsBufferCreated = false;
		bool m_FrameParamsBufferCreated = false;

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
		VkDescriptorSet m_PassSet = VK_NULL_HANDLE;
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
