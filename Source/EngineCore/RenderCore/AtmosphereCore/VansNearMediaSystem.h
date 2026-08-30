#pragma once

#include "../VansRenderRuntimeConfig.h"
#include "../VulkanCore/VansVKBuffer.h"
#include "../VulkanCore/VansVKImage.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

class VansScriptLocalVolumetricFogComponent;
class VansScriptObject;

namespace VansGraphics
{
	class VansComputeShader;
	class VansScene;
	class VansVKCommandBuffer;
	class VansVKDevice;

	struct alignas(16) VansNearMediaParamsGPU final
	{
		glm::vec4 depthRangeAndGrid{ 0.0f };
		glm::vec4 sliceHistoryAndVolumeCount{ 0.0f };
		// xy: 局部雾候选表网格，z: 每格容量，w: 保留。
		glm::vec4 localFogTileGridAndLimits{ 0.0f };
	};
	static_assert(sizeof(VansNearMediaParamsGPU) == 48);

	struct alignas(16) VansLocalFogVolumeGPU final
	{
		glm::mat4 worldToLocal{ 1.0f };
		// x: extinction，y: anisotropy，z: receive cloud shadow。
		glm::vec4 extinctionAnisotropyCloudPadding{ 0.0f };
		// xyz: OBB 完整世界尺寸，w: 米制边缘淡化距离。
		glm::vec4 dimensionsAndEdgeFade{ 0.0f };
		glm::vec4 scatteringAlbedo{ 0.0f };
		glm::vec4 emissivePerMeter{ 0.0f };
		glm::vec4 lightingAndDistanceFade{ 0.0f };
	};
	static_assert(sizeof(VansLocalFogVolumeGPU) == 144);

	// NearMedia 联合积分物理大气、近地高度雾与所有 LocalVolumetricFog 组件，
	// 只输出累计散射和 optical depth，不直接写 SceneColor。
	class VansNearMediaSystem final
	{
	public:
		bool Initialize(VansVKDevice& device, VansScene& scene,
			const VansNearMediaQualityConfig& quality,
			std::uint32_t renderWidth, std::uint32_t renderHeight);
		bool Reinitialize(const VansNearMediaQualityConfig& quality,
			std::uint32_t renderWidth, std::uint32_t renderHeight);
		void Record(VansVKCommandBuffer& commandBuffer);
		void InvalidateHistory() { m_HistoryValid = false; }
		void BindGlobalDescriptor(VkDescriptorSet globalSet);
		void Shutdown();

		VkDescriptorImageInfo GetScatteringDescriptor();
		VkDescriptorImageInfo GetOpticalDepthDescriptor();
		VkDescriptorImageInfo GetCurrentInjectionDescriptor();
		const VansNearMediaQualityConfig& GetQuality() const { return m_Quality; }

	private:
		static constexpr std::uint32_t MaxLocalFogVolumes = 64;
		static constexpr std::uint32_t MaxLocalFogCandidatesPerTile = 16;

		struct LocalFogRegistryEntry final
		{
			VansScriptObject* object = nullptr;
			VansScriptLocalVolumetricFogComponent* component = nullptr;
		};

		bool CreateResources();
		bool CreateDescriptors();
		void DestroyResources();
		void DestroyDescriptors();
		void UploadParameters();
		void RefreshLocalFogRegistry();
		void CollectLocalFogVolumes(
			std::vector<VansLocalFogVolumeGPU>& outVolumes,
			std::vector<glm::uvec2>& outTileHeaders,
			std::vector<std::uint32_t>& outTileIndices);
		void TransitionForWrite(VansVKCommandBuffer& commandBuffer,
			VansVKImage& image, bool initialized);
		void BarrierForSampling(VansVKCommandBuffer& commandBuffer, VansVKImage& image);

		VansVKDevice* m_Device = nullptr;
		VansScene* m_Scene = nullptr;
		VansNearMediaQualityConfig m_Quality{};
		std::uint32_t m_RenderWidth = 0;
		std::uint32_t m_RenderHeight = 0;
		std::uint32_t m_GridWidth = 0;
		std::uint32_t m_GridHeight = 0;
		float m_PreviousEffectiveFarDistanceMeters = 0.0f;
		VansVKBuffer m_ParamsBuffer;
		VansVKBuffer m_LocalFogVolumesBuffer;
		VansVKBuffer m_LocalFogTileHeadersBuffer;
		VansVKBuffer m_LocalFogTileIndicesBuffer;
		bool m_ParamsBufferCreated = false;
		bool m_LocalFogVolumesBufferCreated = false;
		bool m_LocalFogTileHeadersBufferCreated = false;
		bool m_LocalFogTileIndicesBufferCreated = false;
		std::uint64_t m_LocalFogRegistryGeneration = UINT64_MAX;
		std::vector<LocalFogRegistryEntry> m_LocalFogRegistry;
		// 每帧重写内容但复用容量，避免候选表造成瞬时分配。
		std::vector<VansLocalFogVolumeGPU> m_LocalFogVolumeScratch;
		std::vector<glm::uvec2> m_LocalFogTileHeaderScratch;
		std::vector<std::uint32_t> m_LocalFogTileIndexScratch;
		std::vector<VansLocalFogVolumeGPU> m_PreviousVolumes;
		VansVKImage m_Injection[2];
		VansVKImage m_Scattering;
		VansVKImage m_OpticalDepth;
		VkDescriptorSetLayout m_PassLayout = VK_NULL_HANDLE;
		VkDescriptorSet m_PassSets[2]{ VK_NULL_HANDLE, VK_NULL_HANDLE };
		VansComputeShader* m_InjectionShader = nullptr;
		VansComputeShader* m_IntegrationShader = nullptr;
		std::uint32_t m_FrameParity = 0;
		bool m_HistoryValid = false;
		bool m_OutputInitialized = false;
		bool m_Initialized = false;
	};
}
