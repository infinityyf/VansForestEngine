#pragma once

#include "../VansRenderRuntimeConfig.h"
#include "../VulkanCore/VansVKBuffer.h"
#include "../VulkanCore/VansVKImage.h"
#include "../../SceneCore/VansSceneLocalVolumetricFogComponentConfig.h"
#include "../../ParticleCore/VansParticleInstanceData.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <unordered_set>
#include <vector>

class VansScriptLocalVolumetricFogComponent;
class VansScriptObject;

namespace VansGraphics
{
	class VansComputeShader;
	class VansScene;
	class VansTexture;
	class VansVKCommandBuffer;
	class VansVKDevice;
	struct VansRenderViewSnapshot;

	inline constexpr std::uint32_t MaxLocalFogUserFieldDescriptors = 192;
	inline constexpr std::uint32_t MaxLocalFogFieldTextureDescriptors = 193;
	inline constexpr std::uint32_t MaxLocalFogFieldSampleHandles = 194;

	struct alignas(16) VansLocalFogFieldSampleMetadataGPU final
	{
		// x: descriptor，y/z: channel，w: 0 scalar / 1 vector2。
		glm::uvec4 descriptorChannelsAndKind{ 0u };
		// xy: texture resolution，z: mip count，w: signed decode dead-zone。
		glm::vec4 resolutionMipAndZeroThreshold{ 1.0f };
		// x: decode scale，y: decode bias。
		glm::vec4 decodeScaleBiasPadding{ 0.0f };
	};
	static_assert(sizeof(VansLocalFogFieldSampleMetadataGPU) == 48);

	// Local Fog 私有的 Texture2D field 资源表，不占用 PBR material texture slot。
	class VansLocalFogFieldResourceTable final
	{
	public:
		bool Initialize(VansVKDevice& device, VansScene& scene);
		void BeginBuild();
		std::uint32_t RegisterScalar(
			const Vans::VansLocalFogScalarTextureSourceConfig& source,
			const Vans::VansLocalFogTextureMapping2DConfig& mapping);
		std::uint32_t RegisterVector2(
			const Vans::VansLocalFogVector2TextureSourceConfig& source,
			const Vans::VansLocalFogTextureMapping2DConfig& mapping);
		bool EndBuild();
		void Shutdown();

		const std::vector<VkDescriptorImageInfo>& GetDescriptorInfos() const
		{
			return m_DescriptorInfos;
		}
		VkDescriptorBufferInfo GetMetadataDescriptor() const;

	private:
		struct DescriptorEntry final
		{
			std::string assetGuid;
			Vans::VansLocalFogTextureAddressMode addressMode =
				Vans::VansLocalFogTextureAddressMode::Repeat;
			VansTexture* texture = nullptr;
		};

		struct SampleEntry final
		{
			std::string assetGuid;
			Vans::VansLocalFogTextureAddressMode addressMode =
				Vans::VansLocalFogTextureAddressMode::Repeat;
			Vans::VansLocalFogTextureChannel channel0 =
				Vans::VansLocalFogTextureChannel::R;
			Vans::VansLocalFogTextureChannel channel1 =
				Vans::VansLocalFogTextureChannel::R;
			bool vector2 = false;
			std::uint32_t handle = 0u;
		};

		std::uint32_t RegisterSample(
			const std::string& assetGuid,
			Vans::VansLocalFogTextureAddressMode addressMode,
			Vans::VansLocalFogTextureChannel channel0,
			Vans::VansLocalFogTextureChannel channel1,
			bool vector2);
		VansTexture* ResolveTexture(const std::string& assetGuid);
		VkSampler SamplerFor(Vans::VansLocalFogTextureAddressMode addressMode) const;
		void ReportInvalidSourceOnce(const std::string& key, const std::string& message);

		VansVKDevice* m_Device = nullptr;
		VansScene* m_Scene = nullptr;
		VansTexture* m_NeutralTexture = nullptr;
		VansVKBuffer m_MetadataBuffer;
		bool m_MetadataBufferCreated = false;
		VkSampler m_RepeatSampler = VK_NULL_HANDLE;
		VkSampler m_ClampToEdgeSampler = VK_NULL_HANDLE;
		VkSampler m_ClampToBorderZeroSampler = VK_NULL_HANDLE;
		std::vector<DescriptorEntry> m_BuildDescriptors;
		std::vector<SampleEntry> m_BuildSamples;
		std::vector<VansLocalFogFieldSampleMetadataGPU> m_BuildMetadata;
		std::vector<VkDescriptorImageInfo> m_DescriptorInfos;
		std::unordered_set<std::string> m_ReportedInvalidSources;
		std::uint64_t m_Signature = 0u;
		bool m_HasSignature = false;
	};

	struct alignas(16) VansNearMediaParamsGPU final
	{
		glm::vec4 depthRangeAndGrid{ 0.0f };
		glm::vec4 sliceHistoryAndVolumeCount{ 0.0f };
		// xy: 局部雾候选表网格，z: 每格容量，w: 启用时间超采样。
		glm::vec4 localFogTileGridAndLimits{ 0.0f };
		// x: 光源方向透射采样数，y: 最大追踪距离（米）。
		glm::vec4 lightTransmittance{ 0.0f };
	};
	static_assert(sizeof(VansNearMediaParamsGPU) == 64);

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
		glm::uvec4 fieldHandlesAndFlags{ 0u };
		glm::vec4 shapeTilingOffset{ 0.0f };
		glm::vec4 shapeRemapInfluenceLod{ 0.0f };
		glm::vec4 detailTilingOffset{ 0.0f };
		glm::vec4 detailRemapInfluenceLod{ 0.0f };
		glm::vec4 flowTilingOffset{ 0.0f };
		glm::vec4 flowSpeedDistancePhaseLod{ 0.0f };
		glm::vec4 flowFallbackDirectionPadding{ 0.0f };
	};
	static_assert(sizeof(VansLocalFogVolumeGPU) == 272);

	struct alignas(16) VansVolumetricParticleParamsGPU final
	{
		// x: 有效粒子数，y: 每 Tile 候选上限，z/w: Froxel XY 尺寸。
		glm::uvec4 particleCountCandidateLimitAndGrid{ 0u };
	};
	static_assert(sizeof(VansVolumetricParticleParamsGPU) == 16);

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
		void PrepareVolumetricParticles(
			const VansRenderViewSnapshot& view,
			bool featureRequested,
			const std::vector<VansVolumetricParticleInstanceData>& instances);
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
		static constexpr std::uint32_t MaxVolumetricParticles = 4096;
		static constexpr std::uint32_t MaxVolumetricParticleCandidatesPerTile = 32;

		struct LocalFogRegistryEntry final
		{
			VansScriptObject* object = nullptr;
			VansScriptLocalVolumetricFogComponent* component = nullptr;
		};

		bool CreateResources();
		bool CreateDescriptors();
		bool CreateVolumetricParticleResources();
		bool CreateVolumetricParticleDescriptors();
		bool ValidateLocalFogFieldDescriptorSupport() const;
		void UpdateLocalFogFieldDescriptors();
		void DestroyResources();
		void DestroyDescriptors();
		void DestroyVolumetricParticleResources();
		void DestroyVolumetricParticleDescriptors();
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
		VansVKBuffer m_VolumetricParticlesBuffer;
		VansVKBuffer m_VolumetricParticleTileHeadersBuffer;
		VansVKBuffer m_VolumetricParticleTileIndicesBuffer;
		VansVKBuffer m_VolumetricParticleParamsBuffer;
		VansLocalFogFieldResourceTable m_LocalFogFieldResources;
		bool m_ParamsBufferCreated = false;
		bool m_LocalFogVolumesBufferCreated = false;
		bool m_LocalFogTileHeadersBufferCreated = false;
		bool m_LocalFogTileIndicesBufferCreated = false;
		bool m_VolumetricParticlesBufferCreated = false;
		bool m_VolumetricParticleTileHeadersBufferCreated = false;
		bool m_VolumetricParticleTileIndicesBufferCreated = false;
		bool m_VolumetricParticleParamsBufferCreated = false;
		bool m_LocalFogFieldDescriptorsDirty = false;
		std::uint64_t m_LocalFogRegistryGeneration = UINT64_MAX;
		std::vector<LocalFogRegistryEntry> m_LocalFogRegistry;
		// 每帧重写内容但复用容量，避免候选表造成瞬时分配。
		std::vector<VansLocalFogVolumeGPU> m_LocalFogVolumeScratch;
		std::vector<glm::uvec2> m_LocalFogTileHeaderScratch;
		std::vector<std::uint32_t> m_LocalFogTileIndexScratch;
		std::vector<VansLocalFogVolumeGPU> m_PreviousVolumes;
		std::vector<VansVolumetricParticleInstanceData> m_VolumetricParticleScratch;
		std::vector<glm::uvec2> m_VolumetricParticleTileHeaderScratch;
		std::vector<std::uint32_t> m_VolumetricParticleTileIndexScratch;
		VansVKImage m_RawInjection;
		VansVKImage m_MaterialScatteringExtinction;
		VansVKImage m_MaterialLightingPhaseCloud;
		VansVKImage m_MaterialEmissiveWeight;
		VansVKImage m_VolumetricParticleActivity[2];
		VansVKImage m_Injection[2];
		VansVKImage m_Scattering;
		VansVKImage m_OpticalDepth;
		VkDescriptorSetLayout m_PassLayout = VK_NULL_HANDLE;
		VkDescriptorSet m_PassSets[2]{ VK_NULL_HANDLE, VK_NULL_HANDLE };
		VkDescriptorSetLayout m_VolumetricParticlePassLayout = VK_NULL_HANDLE;
		VkDescriptorSet m_VolumetricParticlePassSets[2]{ VK_NULL_HANDLE, VK_NULL_HANDLE };
		VansComputeShader* m_InjectionShader = nullptr;
		VansComputeShader* m_LightingShader = nullptr;
		VansComputeShader* m_TemporalResolveShader = nullptr;
		VansComputeShader* m_IntegrationShader = nullptr;
		VansComputeShader* m_NearMediaUnifiedInjectionShader = nullptr;
		VansComputeShader* m_VolumetricParticleTemporalResolveShader = nullptr;
		std::uint32_t m_FrameParity = 0;
		bool m_RawInjectionInitialized = false;
		bool m_MaterialVolumesInitialized = false;
		bool m_VolumetricParticleActivityInitialized[2]{ false, false };
		bool m_InjectionInitialized[2]{ false, false };
		bool m_HasResolvedInjection = false;
		bool m_HistoryValid = false;
		bool m_OutputInitialized = false;
		bool m_VolumetricParticleResourcesCreated = false;
		bool m_VolumetricParticleFeatureRequested = false;
		bool m_PreviousVolumetricParticleFeatureRequested = false;
		bool m_VolumetricParticlePreparationFailed = false;
		bool m_VolumetricParticleFirstNonEmptyInputLogged = false;
		bool m_VolumetricParticleFirstDispatchLogged = false;
		bool m_VolumetricParticleOverflowFallbackLogged = false;
		bool m_Initialized = false;
	};
}
