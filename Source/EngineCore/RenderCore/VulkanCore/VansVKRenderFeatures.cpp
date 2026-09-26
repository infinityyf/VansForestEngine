#include "VansVKDevice.h"

#include "VansVKDescriptorManager.h"

#include "VansRenderPass.h"

#include "../VansScene.h"
#include "../VansCamera.h"
#include "../VansTemporalProjection.h"
#include "../AtmosphereCore/VansAtmosphereSystem.h"

#include "../VansPostProcessProfile.h"
#include "../VansRenderBootstrapSettings.h"
#include "../../Util/VansProfiler.h"


#include <algorithm>

#include <cstddef>

#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include "../VansShaderManager.h"
#include "../../Util/VansLog.h"

namespace VansGraphics

{

	SSGIParamsGPU BuildSSGIParamsFromGISettings(
			const VansGISettings& gi,
			uint32_t renderWidth,
			uint32_t renderHeight)
		{
			const float width = static_cast<float>(std::max(renderWidth, 1u));
			const float height = static_cast<float>(std::max(renderHeight, 1u));
			SSGIParamsGPU data{};
			data.screenSize = glm::vec4(width, height, 1.0f / width, 1.0f / height);
			uint32_t regionCount = 0u;
			float maxTraceDistance = 1.0f;
			float fadeStartRatio = 0.75f;
			for (const GIProbeRegionDesc* desc : BuildActiveGIRegionOrder(gi))
			{
				if (regionCount >= VANS_SSGI_MAX_GI_REGIONS)
					continue;
				const GIResolvedRegion region = ResolveGIRegion(*desc);
				const uint64_t probeCount64 =
					static_cast<uint64_t>(region.gridDimensions.x) *
					static_cast<uint64_t>(region.gridDimensions.y) *
					static_cast<uint64_t>(region.gridDimensions.z);
				const int totalProbeCount = static_cast<int>(
					std::max<uint64_t>(probeCount64, 1u));
				// Keep this identical to DDGI atlas allocation.  These values are
				// immutable for the region and let the screen-query cache avoid
				// repeating textureSize and atlas-row division for every cache texel.
				const int probesPerAtlasRow = std::max(1,
					static_cast<int>(std::ceil(std::sqrt(
						static_cast<float>(totalProbeCount)))));
				const int probesPerAtlasColumn =
					(totalProbeCount + probesPerAtlasRow - 1) / probesPerAtlasRow;
				SSGIRegionParamsGPU& destination = data.regions[regionCount++];
				// volumeMin.w and traceParams.w were reserved.  Retaining the
				// existing std140 layout also keeps Deferred and SSGI consumers ABI
				// compatible while publishing the fixed atlas tile dimensions.
				destination.volumeMin = glm::vec4(
					region.volumeMin, static_cast<float>(probesPerAtlasRow));
				destination.volumeSizeAndBias = glm::vec4(region.volumeSize, region.normalBias);
				destination.traceParams = glm::vec4(
					region.maxRayDistance, 0.75f, region.volumeFadeDistance,
					static_cast<float>(probesPerAtlasColumn));
				destination.gridDimensionsAndPriority = glm::vec4(glm::vec3(region.gridDimensions), region.priority);
				maxTraceDistance = std::max(maxTraceDistance, region.maxRayDistance);
				fadeStartRatio = std::min(fadeStartRatio, destination.traceParams.y);
			}
			data.regionInfo = glm::vec4(
				static_cast<float>(regionCount), maxTraceDistance, fadeStartRatio, 0.0f);
			data.deferredProbeDebug = glm::vec4(
				gi.probeOnlyDeferredOutput ? 1.0f : 0.0f,
				gi.probeOnlyDeferredExposure,
				0.0f,
				0.0f);
			return data;
		}

	namespace
	{
		void RecordShaderWriteToReadMemoryDependency(
			VansVKCommandBuffer& commandBuffer,
			VkPipelineStageFlags srcStageMask,
			VkPipelineStageFlags dstStageMask)
		{
			VkMemoryBarrier memoryBarrier = {};
			memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			commandBuffer.PipelineBarrier(srcStageMask, dstStageMask, { memoryBarrier });
		}
	}

	void VansVKDevice::UploadSSGIParams(const VansGISettings& settings)
	{
		if (m_Scene == nullptr)
			return;

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager == nullptr || manager->m_SSGICBBuffer.GetNativeBuffer() == VK_NULL_HANDLE)
			return;

		SSGIParamsGPU data = BuildSSGIParamsFromGISettings(
			settings, m_RenderWidth, m_RenderHeight);
        if (rayTracingContext.IsReady())
        {
            data.regionInfo.x = float(rayTracingContext.GetGIRegionCount());
            data.regionInfo.w = rayTracingContext.UsesSparseGI() ? 1.0f : 0.0f;
            for (uint32_t index = 0; index < rayTracingContext.GetGIRegionCount(); ++index)
            {
                const uint32_t count = std::max(1u, rayTracingContext.GetGIRegionPhysicalProbeCount(index));
                const uint32_t columns = uint32_t(std::ceil(std::sqrt(float(count))));
                data.regions[index].volumeMin.w = float(columns);
                data.regions[index].traceParams.w = float((count + columns - 1u) / columns);
            }
        }
		manager->m_SSGICBBuffer.SetBufferData(&data, 0, sizeof(data));
	}

	void VansVKDevice::UpdateSSGI(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)

	{

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager == nullptr || manager->m_SSGIDescriptorSets.empty())
			return;

		computeCmd.EnsureComputeShader(*manager->m_SSGIShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_SSGITexSetLayout });

		computeCmd.DispatchCompute(*manager->m_SSGIShader, (m_RenderWidth + 7u) / 8u, (m_RenderHeight + 7u) / 8u, 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_SSGIDescriptorSets[0] });

	}

	void VansVKDevice::UpdateSSGIProbeCache(
		VansRenderPassManager* renderPassManager,
		VansVKCommandBuffer& computeCmd)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager == nullptr || manager->m_SSGIProbeCacheShader == nullptr ||
			manager->m_SSGIProbeCacheDescriptorSets.size() < 2u)
		{
			return;
		}

		VansTexture* output = manager->GetRuntimeRenderTexture(
			VansMaterialManager::RT_SSGI_PROBE_CACHE_RADIANCE);
		if (output == nullptr)
			return;

		computeCmd.EnsureComputeShader(
			*manager->m_SSGIProbeCacheShader,
			{ m_Scene->GetGlobalDescriptorSetLayout(), manager->m_SSGIProbeCacheSetLayout });
		computeCmd.DispatchCompute(
			*manager->m_SSGIProbeCacheShader,
			(output->GetWidth() + 7u) / 8u,
			(output->GetHeight() + 7u) / 8u,
			1,
			{ m_Scene->GetGlobalDescriptorSet(), manager->m_SSGIProbeCacheDescriptorSets[manager->m_SSGITemporalFrame % 2u] });

	}


	void VansVKDevice::TemporalFilterSSGI(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)

	{

		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		uint32_t writeIdx = manager->m_SSGITemporalFrame % 2;





		computeCmd.EnsureComputeShader(*manager->m_SSGITemporalShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_SSGITemporalSetLayout });

		computeCmd.DispatchCompute(*manager->m_SSGITemporalShader, (m_RenderWidth + 7) / 8, (m_RenderHeight + 7) / 8, 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_SSGITemporalDescriptorSets[writeIdx] });

	}



	void VansVKDevice::BilateralFilterSSAO(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)

	{
		// SSAO filter 是独立 pass。同步与异步路径都只更新自身 descriptor，
		// 不能在 GraphicsScreen 记录期间重写 SSGI/DDGI 的 atlas/state 绑定。
		UpdateSSAOFilterDescriptorSet(renderPassManager);
		if (!IsFeatureDescriptorCurrent(m_SSAOFilterDescSetGeneration))
			return;

		uint32_t halfResWidth = m_RenderWidth / 2;

		uint32_t halfResHeight = m_RenderHeight / 2;



		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager == nullptr || manager->m_BilateralFilterShader == nullptr ||
			manager->m_BilateralFilterDescriptorSets.empty())
		{
			return;
		}

		manager->m_BilateralFilterPushConstant.sigmaSpace = 3.0f;

		manager->m_BilateralFilterPushConstant.sigmaDepth = 0.08f;

		manager->m_BilateralFilterPushConstant.radius = 4;

		manager->m_BilateralFilterPushConstant.depthThreshold = 0.18f;

		manager->m_BilateralFilterPushConstant.depthMode = 2;

		manager->m_BilateralFilterShader->SetPushConstantData(&(manager->m_BilateralFilterPushConstant));

		computeCmd.EnsureComputeShader(*manager->m_BilateralFilterShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_BilateralFilterSetLayout });

		computeCmd.DispatchCompute(*manager->m_BilateralFilterShader, (halfResWidth + 7) / 8, (halfResHeight + 7) / 8, 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_BilateralFilterDescriptorSets[0] });

	}


	void VansVKDevice::UpdateSSAOFilterDescriptorSet(VansRenderPassManager* renderPassManager)
	{
		if (IsFeatureDescriptorCurrent(m_SSAOFilterDescSetGeneration))
			return;

		VansMaterialManager* manager = m_Scene != nullptr ? m_Scene->GetMaterialManager() : nullptr;
		if (manager == nullptr || manager->m_BilateralFilterDescriptorSets.empty())
			return;

		VansTexture* ssaoResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_SSAO_RESULT);
		VansTexture* ssaoFilterResult = manager->GetRuntimeRenderTexture(
			VansMaterialManager::RT_SSAO_FILTER_RESULT);
		if (ssaoResult == nullptr || ssaoFilterResult == nullptr)
			return;

		auto& positionGbuffer = renderPassManager->GetGbuffer2();
		auto* descriptorManager = VansVKDescriptorManager::GetInstance();
		descriptorManager->BeginDescriptorUpdate();
		descriptorManager->WriteImageDescriptor(
			manager->m_BilateralFilterDescriptorSets[0],
			PassBinding::TEXTURE_0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ ssaoResult->GetImage().GetSampler(), ssaoResult->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_GENERAL }}, 0);
		descriptorManager->WriteImageDescriptor(
			manager->m_BilateralFilterDescriptorSets[0],
			PassBinding::TEXTURE_1,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ positionGbuffer.GetSampler(), positionGbuffer.GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }}, 0);
		descriptorManager->WriteImageDescriptor(
			manager->m_BilateralFilterDescriptorSets[0],
			PassBinding::UAV_IMAGE_1,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ ssaoFilterResult->GetImage().GetSampler(),
				ssaoFilterResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }}, 0);
		descriptorManager->CommitDescriptorUpdates();
		MarkFeatureDescriptorCurrent(m_SSAOFilterDescSetGeneration);
	}



	void VansVKDevice::UpdateGIDataDescriptorSets(VansRenderPassManager* renderPassManager)

	{
		// SSGI、temporal、filter 与 Probe Cache 的纹理和 buffer 都随渲染资源
		// generation 重建；帧间只更新 UBO 内容，不会更换 descriptor 指向。
		// 避免每帧重复提交几十次固定 descriptor 写入。
		if (IsFeatureDescriptorCurrent(m_GIDataDescSetGeneration))
			return;

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager == nullptr || manager->m_SkyLighting.DiffuseIrradiance() == nullptr)
			return;
		VansVKImage& skyDiffuse = manager->m_SkyLighting.DiffuseIrradiance()->GetImage();



		auto getRuntimeTexture = [manager](const char* key)

			{

				return manager->GetRuntimeRenderTexture(key);

		};


		VansTexture* ssgiResult = getRuntimeTexture(VansMaterialManager::RT_SSGI_RESULT);

		std::vector<VansTexture*> giIrradianceAtlases;
		std::vector<VansTexture*> giVisibilityAtlases;
		std::vector<const VansVKBuffer*> giProbeStateBuffers;
		const uint32_t availableGIRegions = std::min(rayTracingContext.GetGIRegionCount(), VANS_SSGI_MAX_GI_REGIONS);
		for (uint32_t regionIndex = 0u; regionIndex < availableGIRegions; ++regionIndex)
		{
			VansTexture* irradiance = rayTracingContext.GetGIRegionIrradianceAtlas(regionIndex);
			VansTexture* visibility = rayTracingContext.GetGIRegionVisibilityAtlas(regionIndex);
			const VansVKBuffer* probeState = rayTracingContext.GetGIRegionProbeStateBuffer(regionIndex);
			if (irradiance != nullptr && visibility != nullptr &&
				probeState != nullptr && probeState->GetNativeBuffer() != VK_NULL_HANDLE)
			{
				giIrradianceAtlases.push_back(irradiance);
				giVisibilityAtlases.push_back(visibility);
				giProbeStateBuffers.push_back(probeState);
			}
		}
		VansTexture* hzbResult = getRuntimeTexture(VansMaterialManager::RT_HZB_RESULT);

		VansTexture* ssgiTemporalA = getRuntimeTexture(VansMaterialManager::RT_SSGI_TEMPORAL_A);

		VansTexture* ssgiTemporalB = getRuntimeTexture(VansMaterialManager::RT_SSGI_TEMPORAL_B);

		VansTexture* ssgiMomentsA = getRuntimeTexture(VansMaterialManager::RT_SSGI_MOMENTS_A);

		VansTexture* ssgiMomentsB = getRuntimeTexture(VansMaterialManager::RT_SSGI_MOMENTS_B);

		VansTexture* ssgiSurfaceHistoryA = getRuntimeTexture(VansMaterialManager::RT_SSGI_SURFACE_HISTORY_A);

		VansTexture* ssgiSurfaceHistoryB = getRuntimeTexture(VansMaterialManager::RT_SSGI_SURFACE_HISTORY_B);

		VansTexture* ssgiFilterResult = getRuntimeTexture(VansMaterialManager::RT_SSGI_FILTER_RESULT);
		VansTexture* ssgiProbeCacheRadiance = getRuntimeTexture(
			VansMaterialManager::RT_SSGI_PROBE_CACHE_RADIANCE);
		VansTexture* ssgiProbeCacheSurface = getRuntimeTexture(
			VansMaterialManager::RT_SSGI_PROBE_CACHE_SURFACE);



		if (manager->m_SSGIProbeCacheDescriptorSets.size() < 2u ||
            manager->m_SSGITemporalDescriptorSets.size() < 2u ||
            manager->m_SSGIDescriptorSets.empty() ||
            ssgiResult == nullptr || giIrradianceAtlases.empty() ||
			giVisibilityAtlases.empty() ||
			giProbeStateBuffers.empty() || hzbResult == nullptr || ssgiTemporalA == nullptr || ssgiTemporalB == nullptr ||

			ssgiFilterResult == nullptr ||
			ssgiProbeCacheRadiance == nullptr || ssgiProbeCacheSurface == nullptr ||
			ssgiMomentsA == nullptr || ssgiMomentsB == nullptr ||
			ssgiSurfaceHistoryA == nullptr || ssgiSurfaceHistoryB == nullptr)

		{

			return;

		}



		VansVKDescriptorManager::GetInstance()->BeginDescriptorUpdate();

		VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(manager->m_SSGIDescriptorSets[0], PassBinding::CBUFFER_6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {

					{

						manager->m_SSGICBBuffer.GetNativeBuffer(),

						0,

						manager->m_SSGICBBuffer.GetBufferSize()

					}

				}, 0);



		auto& normal = renderPassManager->GetNormal();

		auto& depth = renderPassManager->GetDepth();

		auto& diffuseExitantRadianceHistory = renderPassManager->GetDiffuseExitantRadianceHistory();

		auto& positionGbuffer = renderPassManager->GetGbuffer2();

		auto& materialGbuffer = renderPassManager->GetGbuffer1();

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGIDescriptorSets[0], PassBinding::TEXTURE_0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						normal.GetSampler(),

						normal.GetImageView(),

						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGIDescriptorSets[0], PassBinding::TEXTURE_1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						depth.GetSampler(),

						depth.GetImageView(),

						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGIDescriptorSets[0], PassBinding::TEXTURE_2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						diffuseExitantRadianceHistory.GetSampler(),

						diffuseExitantRadianceHistory.GetImageView(),

						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGIDescriptorSets[0], PassBinding::TEXTURE_3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						positionGbuffer.GetSampler(),

						positionGbuffer.GetImageView(),

						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGIDescriptorSets[0], PassBinding::TEXTURE_4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						skyDiffuse.GetSampler(),

						skyDiffuse.GetImageView(),

						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

					}

				}, 0);



		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGIDescriptorSets[0], PassBinding::UAV_IMAGE_4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {

					{

						ssgiResult->GetImage().GetSampler(),

						ssgiResult->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);



		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGIDescriptorSets[0], PassBinding::TEXTURE_10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						hzbResult->GetImage().GetSampler(),

						hzbResult->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGIDescriptorSets[0], PassBinding::TEXTURE_11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						materialGbuffer.GetSampler(),

						materialGbuffer.GetImageView(),

						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

					}

				}, 0);

		std::vector<VkDescriptorImageInfo> irradianceInfos;
		irradianceInfos.reserve(VANS_SSGI_MAX_GI_REGIONS);
		std::vector<VkDescriptorImageInfo> visibilityInfos;
		visibilityInfos.reserve(VANS_SSGI_MAX_GI_REGIONS);
		std::vector<VkDescriptorBufferInfo> probeStateInfos;
		probeStateInfos.reserve(VANS_SSGI_MAX_GI_REGIONS);
		for (size_t regionIndex = 0; regionIndex < giIrradianceAtlases.size(); ++regionIndex)
		{
			irradianceInfos.push_back({ giIrradianceAtlases[regionIndex]->GetImage().GetSampler(), giIrradianceAtlases[regionIndex]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL });
			visibilityInfos.push_back({ giVisibilityAtlases[regionIndex]->GetImage().GetSampler(), giVisibilityAtlases[regionIndex]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL });
			probeStateInfos.push_back({ giProbeStateBuffers[regionIndex]->GetNativeBuffer(), 0, giProbeStateBuffers[regionIndex]->GetBufferSize() });
		}
		while (irradianceInfos.size() < VANS_SSGI_MAX_GI_REGIONS)
		{
			irradianceInfos.push_back(irradianceInfos.front());
			visibilityInfos.push_back(visibilityInfos.front());
			probeStateInfos.push_back(probeStateInfos.front());
		}
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
			manager->m_SSGIDescriptorSets[0],
			SSGI_BINDING_GI_IRRADIANCE,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			irradianceInfos,
			0);
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
			manager->m_SSGIDescriptorSets[0],
			SSGI_BINDING_GI_VISIBILITY,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			visibilityInfos,
			0);
		VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(
			manager->m_SSGIDescriptorSets[0],
			SSGI_BINDING_GI_PROBE_STATE,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			probeStateInfos,
			0);
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
			manager->m_SSGIDescriptorSets[0],
			SSGI_BINDING_PROBE_CACHE_RADIANCE,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ ssgiProbeCacheRadiance->GetImage().GetSampler(), ssgiProbeCacheRadiance->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }},
			0);
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
			manager->m_SSGIDescriptorSets[0],
			SSGI_BINDING_PROBE_CACHE_SURFACE,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ ssgiProbeCacheSurface->GetImage().GetSampler(), ssgiProbeCacheSurface->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }},
			0);

        const auto& layoutBuffer = rayTracingContext.GetGIProbeLayoutBuffer();
        auto& receiver = manager->m_GIReceiverVisibility;
        auto* descriptorManager = VansVKDescriptorManager::GetInstance();
        const VkDescriptorBufferInfo receiverInfo{receiver.current.GetNativeBuffer(), 0, receiver.current.GetBufferSize()};
        descriptorManager->WriteBufferDescriptor(manager->m_SSGIDescriptorSets[0], 26u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {receiverInfo});
        for (auto set : manager->m_SSGIProbeCacheDescriptorSets)
            descriptorManager->WriteBufferDescriptor(set, 24u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {receiverInfo});
        const VkDescriptorBufferInfo biasInfo{receiver.bias.GetNativeBuffer(), 0, receiver.bias.GetBufferSize()};
        descriptorManager->WriteBufferDescriptor(manager->m_SSGIDescriptorSets[0], 27u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {biasInfo});
        for (auto set : manager->m_SSGIProbeCacheDescriptorSets)
            descriptorManager->WriteBufferDescriptor(set, 30u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {biasInfo});
        const VkDescriptorSet receiverSet = receiver.sets.at(0);
        descriptorManager->WriteBufferDescriptor(receiverSet, 15u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {biasInfo});
        VansVKImage* receiverImages[] = {&positionGbuffer, &normal, &materialGbuffer, &renderPassManager->GetMotionVector()};
        for (uint32_t i = 0; i < 4; ++i)
            descriptorManager->WriteImageDescriptor(receiverSet, i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {{receiverImages[i]->GetSampler(), receiverImages[i]->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
        descriptorManager->WriteBufferDescriptor(receiverSet, 4u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            {{manager->m_SSGICBBuffer.GetNativeBuffer(), 0, manager->m_SSGICBBuffer.GetBufferSize()}});
        descriptorManager->WriteBufferDescriptor(receiverSet, 6u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {{layoutBuffer.GetNativeBuffer(), 0, layoutBuffer.GetBufferSize()}});
        descriptorManager->WriteBufferDescriptor(receiverSet, 7u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, probeStateInfos);
        std::vector<VkDescriptorBufferInfo> previousStateInfos;
        for (uint32_t i = 0; i < VANS_SSGI_MAX_GI_REGIONS; ++i)
        {
            auto* previous = rayTracingContext.GetGIRegionPreviousProbeStateBuffer(i < availableGIRegions ? i : 0u);
            previousStateInfos.push_back({previous->GetNativeBuffer(), 0, previous->GetBufferSize()});
        }
        descriptorManager->WriteBufferDescriptor(receiverSet, 8u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, previousStateInfos);
        descriptorManager->WriteBufferDescriptor(receiverSet, 9u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {{receiver.history.GetNativeBuffer(), 0, receiver.history.GetBufferSize()}});
        descriptorManager->WriteBufferDescriptor(receiverSet, 10u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {receiverInfo});
        descriptorManager->WriteBufferDescriptor(receiverSet, 11u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {{receiver.work.GetNativeBuffer(), 0, receiver.work.GetBufferSize()}});
        descriptorManager->WriteBufferDescriptor(receiverSet, 12u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {{receiver.anchors.GetNativeBuffer(), 0, receiver.anchors.GetBufferSize()}});
        descriptorManager->WriteBufferDescriptor(receiverSet, 13u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {{receiver.world.GetNativeBuffer(), 0, receiver.world.GetBufferSize()}});
        descriptorManager->WriteBufferDescriptor(receiverSet, 14u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {{receiver.worldClaims.GetNativeBuffer(), 0, receiver.worldClaims.GetBufferSize()}});
        receiver.frame = 0; // 场景/布局/资源 generation 变化；光照参数更新不触发此重置。

        VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(manager->m_SSGIDescriptorSets[0],
            25u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{layoutBuffer.GetNativeBuffer(), 0, layoutBuffer.GetBufferSize()}});
		auto& motionVector = renderPassManager->GetMotionVector();
        for (uint32_t historyWrite = 0u; historyWrite < 2u; ++historyWrite)
        {
            const VkDescriptorSet probeCacheSet = manager->m_SSGIProbeCacheDescriptorSets[historyWrite];
            VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
                probeCacheSet, SSGI_PROBE_CACHE_BINDING_NORMAL,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {{ normal.GetSampler(), normal.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }}, 0);
            VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
                probeCacheSet, SSGI_PROBE_CACHE_BINDING_DEPTH,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {{ depth.GetSampler(), depth.GetImageView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL }}, 0);
            VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
                probeCacheSet, SSGI_PROBE_CACHE_BINDING_POSITION,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {{ positionGbuffer.GetSampler(), positionGbuffer.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }}, 0);
            VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
                probeCacheSet, SSGI_PROBE_CACHE_BINDING_MATERIAL,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {{ materialGbuffer.GetSampler(), materialGbuffer.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }}, 0);
            VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
                probeCacheSet, SSGI_PROBE_CACHE_BINDING_SKY_DIFFUSE,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {{ skyDiffuse.GetSampler(), skyDiffuse.GetImageView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }}, 0);
            VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
                probeCacheSet, SSGI_PROBE_CACHE_BINDING_OUTPUT_RADIANCE,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                {{ ssgiProbeCacheRadiance->GetImage().GetSampler(), ssgiProbeCacheRadiance->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }}, 0);
            VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
                probeCacheSet, SSGI_PROBE_CACHE_BINDING_OUTPUT_SURFACE,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                {{ ssgiProbeCacheSurface->GetImage().GetSampler(), ssgiProbeCacheSurface->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }}, 0);
            VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(
                probeCacheSet, SSGI_PROBE_CACHE_BINDING_INFO_UBO,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                {{ manager->m_SSGICBBuffer.GetNativeBuffer(), 0, manager->m_SSGICBBuffer.GetBufferSize() }}, 0);
            VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
                probeCacheSet, SSGI_PROBE_CACHE_BINDING_SCREEN_IRRADIANCE,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, irradianceInfos, 0);
            VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
                probeCacheSet, SSGI_PROBE_CACHE_BINDING_GI_VISIBILITY,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, visibilityInfos, 0);
            // New shader path indexes the eight state buffers through one native
            // descriptor array, removing an eight-way switch from every probe tap.
            VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(
                probeCacheSet,
                SSGI_PROBE_CACHE_BINDING_GI_PROBE_STATE,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                probeStateInfos,
                0);
            VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(probeCacheSet,
                20u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{layoutBuffer.GetNativeBuffer(), 0, layoutBuffer.GetBufferSize()}});
            VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
                probeCacheSet,
                SSGI_PROBE_CACHE_BINDING_COLOR,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {{ diffuseExitantRadianceHistory.GetSampler(), diffuseExitantRadianceHistory.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }},
                0);
            VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
                probeCacheSet,
                SSGI_PROBE_CACHE_BINDING_HIZ_DEPTH,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {{ hzbResult->GetImage().GetSampler(), hzbResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }},
                0);

            VansTexture* historySurface = historyWrite == 0u ? ssgiSurfaceHistoryB : ssgiSurfaceHistoryA;
            VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(probeCacheSet, SSGI_PROBE_CACHE_BINDING_HISTORY_SURFACE,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{historySurface->GetImage().GetSampler(), historySurface->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL}});
            VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(probeCacheSet, SSGI_PROBE_CACHE_BINDING_MOTION_VECTOR,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{motionVector.GetSampler(), motionVector.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
            VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(probeCacheSet, SSGI_PROBE_CACHE_BINDING_HISTORY_INFO,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {{manager->m_SSGITemporalCBBuffer.GetNativeBuffer(), 0, manager->m_SSGITemporalCBBuffer.GetBufferSize()}});
        }

		VansVKDescriptorManager::GetInstance()->CommitDescriptorUpdates();





		VansVKDescriptorManager::GetInstance()->BeginDescriptorUpdate();

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_POSITION, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { positionGbuffer.GetSampler(), positionGbuffer.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_MOTION_VECTOR, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { motionVector.GetSampler(), motionVector.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_HISTORY_GI, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { ssgiTemporalB->GetImage().GetSampler(), ssgiTemporalB->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_CURRENT_GI, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { { ssgiResult->GetImage().GetSampler(), ssgiResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_ACCUMULATED_GI, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { { ssgiTemporalA->GetImage().GetSampler(), ssgiTemporalA->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_INFO_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { { manager->m_SSGITemporalCBBuffer.GetNativeBuffer(), 0, manager->m_SSGITemporalCBBuffer.GetBufferSize() } }, 0);
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_HISTORY_MOMENTS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { ssgiMomentsB->GetImage().GetSampler(), ssgiMomentsB->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_OUTPUT_MOMENTS, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { { ssgiMomentsA->GetImage().GetSampler(), ssgiMomentsA->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_HISTORY_SURFACE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { ssgiSurfaceHistoryB->GetImage().GetSampler(), ssgiSurfaceHistoryB->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_CURRENT_NORMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { normal.GetSampler(), normal.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }, 0);
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_CURRENT_MATERIAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { materialGbuffer.GetSampler(), materialGbuffer.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }, 0);
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_OUTPUT_SURFACE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { { ssgiSurfaceHistoryA->GetImage().GetSampler(), ssgiSurfaceHistoryA->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);

		VansVKDescriptorManager::GetInstance()->CommitDescriptorUpdates();



		VansVKDescriptorManager::GetInstance()->BeginDescriptorUpdate();

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_POSITION, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { positionGbuffer.GetSampler(), positionGbuffer.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_MOTION_VECTOR, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { motionVector.GetSampler(), motionVector.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_HISTORY_GI, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { ssgiTemporalA->GetImage().GetSampler(), ssgiTemporalA->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_CURRENT_GI, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { { ssgiResult->GetImage().GetSampler(), ssgiResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_ACCUMULATED_GI, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { { ssgiTemporalB->GetImage().GetSampler(), ssgiTemporalB->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_INFO_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { { manager->m_SSGITemporalCBBuffer.GetNativeBuffer(), 0, manager->m_SSGITemporalCBBuffer.GetBufferSize() } }, 0);
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_HISTORY_MOMENTS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { ssgiMomentsA->GetImage().GetSampler(), ssgiMomentsA->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_OUTPUT_MOMENTS, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { { ssgiMomentsB->GetImage().GetSampler(), ssgiMomentsB->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_HISTORY_SURFACE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { ssgiSurfaceHistoryA->GetImage().GetSampler(), ssgiSurfaceHistoryA->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_CURRENT_NORMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { normal.GetSampler(), normal.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }, 0);
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_CURRENT_MATERIAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { materialGbuffer.GetSampler(), materialGbuffer.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }, 0);
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_OUTPUT_SURFACE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { { ssgiSurfaceHistoryB->GetImage().GetSampler(), ssgiSurfaceHistoryB->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);

		VansVKDescriptorManager::GetInstance()->CommitDescriptorUpdates();



		MarkFeatureDescriptorCurrent(m_GIDataDescSetGeneration);

	}



	void VansVKDevice::UpdateHIZSeedDescriptorSet(VansRenderPassManager* renderPassManager)

	{

		VansMaterialManager* manager = m_Scene->GetMaterialManager();



		if (IsFeatureDescriptorCurrent(m_HIZSeedDescSetGeneration))

			return;



		VansTexture* hzbResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_RESULT);

		if (hzbResult == nullptr)

			return;



		MarkFeatureDescriptorCurrent(m_HIZSeedDescSetGeneration);



		auto& position = renderPassManager->GetGbuffer2();



		VansVKDescriptorManager::GetInstance()->BeginDescriptorUpdate();



		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_HIZSeedDescriptorSets[0], HIZ_SEED_BINDING_POSITION, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						position.GetSampler(),

						position.GetImageView(),

						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

					}

				}, 0);



		// binding 1: HIZ mip 0 存储图像输出（r32f 线性深度）

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_HIZSeedDescriptorSets[0], HIZ_SEED_BINDING_HIZ_MIP0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {

					{

						hzbResult->GetImage().GetSampler(),

						hzbResult->GetImage().GetImageMipView(0),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);



		VansVKDescriptorManager::GetInstance()->CommitDescriptorUpdates();

	}



	void VansVKDevice::UpdateHZBDescriptorSets(VansRenderPassManager* renderPassManager)

	{

		VansMaterialManager* manager = m_Scene->GetMaterialManager();



		if (IsFeatureDescriptorCurrent(m_HZBDescSetGeneration))

		{

			return;

		}



		VansTexture* hzbResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_RESULT);

		if (hzbResult == nullptr)

		{

			return;

		}



		MarkFeatureDescriptorCurrent(m_HZBDescSetGeneration);



		for (uint32_t mipIndex = 1; mipIndex < manager->m_HIZMipCount; ++mipIndex)

		{

			VansVKDescriptorManager::GetInstance()->BeginDescriptorUpdate();



			VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_HZBDescriptorSets[mipIndex - 1], PassBinding::UAV_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {

						{

							hzbResult->GetImage().GetSampler(),

							hzbResult->GetImage().GetImageMipView(mipIndex - 1),

							VK_IMAGE_LAYOUT_GENERAL

						}

					}, 0);

			VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_HZBDescriptorSets[mipIndex - 1], PassBinding::UAV_IMAGE_0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {

						{

							hzbResult->GetImage().GetSampler(),

							hzbResult->GetImage().GetImageMipView(mipIndex),

							VK_IMAGE_LAYOUT_GENERAL

						}

					}, 0);



			VansVKDescriptorManager::GetInstance()->CommitDescriptorUpdates();

		}

	}

	void VansVKDevice::AtrousFilterSSGI(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager == nullptr || manager->m_SSGIAtrousShader == nullptr ||
			manager->m_SSGIAtrousDescriptorSets.size() < 2u)
			return;

		const uint32_t temporalWrite = manager->m_SSGITemporalFrame % 2u;
		VansTexture* temporalResult = manager->GetRuntimeRenderTexture(
			temporalWrite == 0u ? VansMaterialManager::RT_SSGI_TEMPORAL_A : VansMaterialManager::RT_SSGI_TEMPORAL_B);
		VansTexture* atrousIntermediate = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_SSGI_ATROUS_A);
		VansTexture* filteredResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_SSGI_FILTER_RESULT);
		if (temporalResult == nullptr || atrousIntermediate == nullptr || filteredResult == nullptr)
			return;

		auto& normal = renderPassManager->GetNormal();
		auto& position = renderPassManager->GetGbuffer2();
		auto& material = renderPassManager->GetGbuffer1();
		auto* descriptorManager = VansVKDescriptorManager::GetInstance();
		auto bindPass = [&](VkDescriptorSet set, VansTexture* input, VansTexture* output)
		{
			descriptorManager->WriteImageDescriptor(set, SSGI_ATROUS_BINDING_INPUT_GI,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ input->GetImage().GetSampler(), input->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
			descriptorManager->WriteImageDescriptor(set, SSGI_ATROUS_BINDING_NORMAL,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ normal.GetSampler(), normal.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
			descriptorManager->WriteImageDescriptor(set, SSGI_ATROUS_BINDING_POSITION,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ position.GetSampler(), position.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
			descriptorManager->WriteImageDescriptor(set, SSGI_ATROUS_BINDING_MATERIAL,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ material.GetSampler(), material.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
			descriptorManager->WriteImageDescriptor(set, SSGI_ATROUS_BINDING_OUTPUT_GI,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {{ output->GetImage().GetSampler(), output->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		};

		descriptorManager->BeginDescriptorUpdate();
		bindPass(manager->m_SSGIAtrousDescriptorSets[0], temporalResult, atrousIntermediate);
		bindPass(manager->m_SSGIAtrousDescriptorSets[1], atrousIntermediate, filteredResult);
		descriptorManager->CommitDescriptorUpdates();

		computeCmd.EnsureComputeShader(*manager->m_SSGIAtrousShader,
			{ m_Scene->GetGlobalDescriptorSetLayout(), manager->m_SSGIAtrousSetLayout });
		const VkCommandBuffer commandBuffer = computeCmd.GetVKCommandBuffer();
		const Vans::VansGpuQueueLane queueLane = m_AsyncComputeEnabled
			? Vans::VansGpuQueueLane::Compute
			: Vans::VansGpuQueueLane::Graphics;
		for (uint32_t iteration = 0u; iteration < 2u; ++iteration)
		{
			const char* scopeName = iteration == 0u ? "SSGI.Atrous1" : "SSGI.Atrous2";
			VANS_GPU_SCOPE_LANE(commandBuffer, scopeName, queueLane);
			SSGIAtrousPushConstants params{};
			params.stepWidth = 1u << iteration;
			// 第一轮保留小尺度法线细节，第二轮适度放宽曲面和法线贴图的邻域权重。
			// 深度与材质 ID 仍保持严格边界，不会用曲面降噪换来跨物体漏光。
			params.planeToleranceScale = iteration == 0u ? 1.0f : 1.25f;
			params.normalPower = iteration == 0u ? 24.0f : 12.0f;
			params.materialWeight = 0.0f;
			computeCmd.DispatchCompute(*manager->m_SSGIAtrousShader, (m_RenderWidth + 7u) / 8u,
				(m_RenderHeight + 7u) / 8u, 1u,
				{ m_Scene->GetGlobalDescriptorSet(), manager->m_SSGIAtrousDescriptorSets[iteration] },
				&params, sizeof(params));
			if (iteration == 0u)
			{
				VkMemoryBarrier barrier{};
				barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
				barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				computeCmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, { barrier });
			}
		}
	}

	void VansVKDevice::UpdateOcclusionHIZSeedDescriptorSet(VansRenderPassManager* renderPassManager)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (IsFeatureDescriptorCurrent(m_OcclusionHIZSeedDescSetGeneration))
			return;

		VansTexture* hzbResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_OCCLUSION_RESULT);
		if (hzbResult == nullptr || manager->m_OcclusionHIZSeedDescriptorSets.empty())
			return;

		MarkFeatureDescriptorCurrent(m_OcclusionHIZSeedDescSetGeneration);
		auto& position = renderPassManager->GetGbuffer2();

		VansVKDescriptorManager::GetInstance()->BeginDescriptorUpdate();
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
			manager->m_OcclusionHIZSeedDescriptorSets[0],
			HIZ_SEED_BINDING_POSITION,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ position.GetSampler(), position.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }},
			0);
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
			manager->m_OcclusionHIZSeedDescriptorSets[0],
			HIZ_SEED_BINDING_HIZ_MIP0,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ hzbResult->GetImage().GetSampler(), hzbResult->GetImage().GetImageMipView(0), VK_IMAGE_LAYOUT_GENERAL }},
			0);
		VansVKDescriptorManager::GetInstance()->CommitDescriptorUpdates();
	}

	void VansVKDevice::UpdateOcclusionHZBDescriptorSets(VansRenderPassManager* renderPassManager)
	{
		(void)renderPassManager;
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (IsFeatureDescriptorCurrent(m_OcclusionHZBDescSetGeneration))
			return;

		VansTexture* hzbResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_OCCLUSION_RESULT);
		if (hzbResult == nullptr)
			return;
		if (manager->m_OcclusionHZBDescriptorSets.size() < static_cast<size_t>(manager->m_HIZMipCount - 1))
			return;

		MarkFeatureDescriptorCurrent(m_OcclusionHZBDescSetGeneration);
		for (uint32_t mipIndex = 1; mipIndex < manager->m_HIZMipCount; ++mipIndex)
		{
			VansVKDescriptorManager::GetInstance()->BeginDescriptorUpdate();
			VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
				manager->m_OcclusionHZBDescriptorSets[mipIndex - 1],
				PassBinding::UAV_IMAGE,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{{ hzbResult->GetImage().GetSampler(), hzbResult->GetImage().GetImageMipView(mipIndex - 1), VK_IMAGE_LAYOUT_GENERAL }},
				0);
			VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
				manager->m_OcclusionHZBDescriptorSets[mipIndex - 1],
				PassBinding::UAV_IMAGE_0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{{ hzbResult->GetImage().GetSampler(), hzbResult->GetImage().GetImageMipView(mipIndex), VK_IMAGE_LAYOUT_GENERAL }},
				0);
			VansVKDescriptorManager::GetInstance()->CommitDescriptorUpdates();
		}
	}


	void VansVKDevice::UpdateMainCameraHiZCullDescriptorSets(VansRenderPassManager* renderPassManager)

	{
		(void)renderPassManager;
		VansMaterialManager* manager = m_Scene ? m_Scene->GetMaterialManager() : nullptr;
		const uint32_t frameSlot = m_MainCameraVisibilityState.GetActiveFrameSlotIndex();
		if (manager == nullptr || frameSlot >= manager->m_MainCameraHiZCullDescriptorSets.size())
			return;

		VansTexture* hzbResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_OCCLUSION_RESULT);
		if (hzbResult == nullptr)
			return;

		VansVKBuffer& objectBuffer = m_MainCameraVisibilityState.GetActiveCullObjectBuffer();
		VansVKBuffer& visibilityBuffer = m_MainCameraVisibilityState.GetActiveVisibilityBuffer();
		if (objectBuffer.GetNativeBuffer() == VK_NULL_HANDLE || visibilityBuffer.GetNativeBuffer() == VK_NULL_HANDLE)
			return;

		VansVKDescriptorManager::GetInstance()->BeginDescriptorUpdate();
		VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(
			manager->m_MainCameraHiZCullDescriptorSets[frameSlot],
			MAIN_CAMERA_HIZ_CULL_BINDING_OBJECTS,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ objectBuffer.GetNativeBuffer(), 0, objectBuffer.GetBufferSize() }});
		VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(
			manager->m_MainCameraHiZCullDescriptorSets[frameSlot],
			MAIN_CAMERA_HIZ_CULL_BINDING_VISIBILITY,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ visibilityBuffer.GetNativeBuffer(), 0, visibilityBuffer.GetBufferSize() }});
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
			manager->m_MainCameraHiZCullDescriptorSets[frameSlot],
			MAIN_CAMERA_HIZ_CULL_BINDING_HIZ,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ hzbResult->GetImage().GetSampler(), hzbResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		VansVKDescriptorManager::GetInstance()->CommitDescriptorUpdates();
		MarkFeatureDescriptorCurrent(m_MainCameraHiZCullDescSetGeneration);
	}

	void VansVKDevice::UpdateMainCameraHiZCull(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)

	{
		if (m_Scene == nullptr || !m_MainCameraVisibilityState.HasActiveCandidates())
			return;

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		const uint32_t frameSlot = m_MainCameraVisibilityState.GetActiveFrameSlotIndex();
		if (manager == nullptr || manager->m_MainCameraHiZCullShader == nullptr ||
			manager->m_MainCameraHiZCullSetLayout == VK_NULL_HANDLE ||
			frameSlot >= manager->m_MainCameraHiZCullDescriptorSets.size())
		{
			return;
		}
		VansTexture* hzbResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_OCCLUSION_RESULT);
		if (hzbResult == nullptr)
			return;
		if (!m_MainCameraVisibilityState.UploadActiveCandidates(*this))
			return;

		UpdateMainCameraHiZCullDescriptorSets(renderPassManager);

		VkMemoryBarrier hostToCompute = {};
		hostToCompute.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		hostToCompute.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
		hostToCompute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		computeCmd.PipelineBarrier(
			VK_PIPELINE_STAGE_HOST_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			{ hostToCompute });

		VansMainCameraHiZCullPushConstants pc{};
		pc.objectCount = m_MainCameraVisibilityState.GetActiveCandidateCount();
		pc.hizMipCount = manager->m_HIZMipCount;
		pc.hizEnabled = 1;
		pc.frameIndex = static_cast<uint32_t>(m_RenderFrameNumber);
		pc.depthBiasMeters = m_MainCameraVisibilityState.GetActiveSettings().depthBiasMeters;
		pc.maxScreenCoverageForCull = m_MainCameraVisibilityState.GetActiveSettings().maxScreenCoverageForCull;

		const uint32_t groups = (pc.objectCount + 63u) / 64u;
		computeCmd.EnsureComputeShader(*manager->m_MainCameraHiZCullShader,
			{ m_Scene->GetGlobalDescriptorSetLayout(), manager->m_MainCameraHiZCullSetLayout });
		computeCmd.DispatchCompute(*manager->m_MainCameraHiZCullShader,
			groups, 1, 1,
			{ m_Scene->GetGlobalDescriptorSet(), manager->m_MainCameraHiZCullDescriptorSets[frameSlot] },
			&pc,
			sizeof(pc));

		VkMemoryBarrier computeToHost = {};
		computeToHost.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		computeToHost.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		computeToHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
		computeCmd.PipelineBarrier(
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_HOST_BIT,
			{ computeToHost });

		m_MainCameraVisibilityState.MarkDispatched();
	}



	void VansVKDevice::UpdateSSRDescriptorSets(VansRenderPassManager* renderPassManager)

	{

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (renderPassManager == nullptr || manager == nullptr)
		{
			return;
		}

		const bool ssrResourcesReady =
			manager->m_SSRClassifyShader != nullptr &&
			manager->m_SSRPrepareIndirectShader != nullptr &&
			manager->m_SSRTraceShader != nullptr &&
			manager->m_SSRResolveShader != nullptr &&
			manager->m_SSRTemporalAAShader != nullptr &&
			manager->m_SSRTraceSetLayout != VK_NULL_HANDLE &&
			manager->m_SSRResolveSetLayout != VK_NULL_HANDLE &&
			manager->m_SSRAASetLayout != VK_NULL_HANDLE &&
			!manager->m_SSRTraceDescriptorSets.empty() &&
			!manager->m_SSRResolveDescriptorSets.empty() &&
			!manager->m_SSRAADescriptorSets.empty() &&
			manager->m_SSRTraceDescriptorSets[0] != VK_NULL_HANDLE &&
			manager->m_SSRResolveDescriptorSets[0] != VK_NULL_HANDLE &&
			manager->m_SSRAADescriptorSets[0] != VK_NULL_HANDLE &&
			manager->m_SSRRayListBuffer.GetNativeBuffer() != VK_NULL_HANDLE &&
			manager->m_SSRTraceControlBuffer.GetNativeBuffer() != VK_NULL_HANDLE;
		if (!ssrResourcesReady)
		{
			// SSR 的 shader / layout / descriptor 由渲染数据准备阶段创建。
			// DemoHall 首帧资源较重，开启 frame context ring 后可能更早进入本 pass；
			// 资源未完整就绪时必须跳过本帧，并保持 descriptor generation 为脏，下一帧继续尝试。
			return;
		}



		if (IsFeatureDescriptorCurrent(m_SSRDescSetGeneration))

		{

			return;

		}



		auto getRuntimeTexture = [manager](const char* key)

			{

				return manager->GetRuntimeRenderTexture(key);

			};



		VansTexture* hzbResult = getRuntimeTexture(VansMaterialManager::RT_HZB_RESULT);

		VansTexture* ssrHitInfo = getRuntimeTexture(VansMaterialManager::RT_SSR_HIT_INFO);

		VansTexture* ssrRayPdf = getRuntimeTexture(VansMaterialManager::RT_SSR_RAY_PDF);

		VansTexture* ssrResult = getRuntimeTexture(VansMaterialManager::RT_SSR_RESULT);

		VansTexture* ssrAaResultA = getRuntimeTexture(VansMaterialManager::RT_SSRAA_RESULT_A);

		VansTexture* ssrAaResultB = getRuntimeTexture(VansMaterialManager::RT_SSRAA_RESULT_B);

		VansTexture* ssrAaResult = getRuntimeTexture(VansMaterialManager::RT_SSRAA_RESULT);



		if (hzbResult == nullptr || ssrHitInfo == nullptr || ssrRayPdf == nullptr ||

			ssrResult == nullptr || ssrAaResultA == nullptr || ssrAaResultB == nullptr || ssrAaResult == nullptr)

		{

			return;

		}



		auto& normal = renderPassManager->GetNormal();

		auto& position = renderPassManager->GetGbuffer2();

		auto& roughness = renderPassManager->GetGbuffer0();

		auto& color = renderPassManager->GetColor();



		VansVKDescriptorManager::GetInstance()->BeginDescriptorUpdate();



		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSRTraceDescriptorSets[0], PassBinding::TEXTURE_0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						normal.GetSampler(),

						normal.GetImageView(),

						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSRTraceDescriptorSets[0], PassBinding::TEXTURE_1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						roughness.GetSampler(),

						roughness.GetImageView(),

						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSRTraceDescriptorSets[0], PassBinding::TEXTURE_2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						position.GetSampler(),

						position.GetImageView(),

						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSRTraceDescriptorSets[0], PassBinding::TEXTURE_3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						hzbResult->GetImage().GetSampler(),

						hzbResult->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);



		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSRTraceDescriptorSets[0], PassBinding::UAV_IMAGE_3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {

					{

						ssrHitInfo->GetImage().GetSampler(),

						ssrHitInfo->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);



		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSRTraceDescriptorSets[0], PassBinding::UAV_IMAGE_4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {

					{

						ssrRayPdf->GetImage().GetSampler(),

						ssrRayPdf->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(
			manager->m_SSRTraceDescriptorSets[0],
			SSRTracePassBinding::SSR_TRACE_BINDING_RAY_LIST,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ manager->m_SSRRayListBuffer.GetNativeBuffer(), 0, manager->m_SSRRayListBuffer.GetBufferSize() }},
			0);

		VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(
			manager->m_SSRTraceDescriptorSets[0],
			SSRTracePassBinding::SSR_TRACE_BINDING_CONTROL,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ manager->m_SSRTraceControlBuffer.GetNativeBuffer(), 0, manager->m_SSRTraceControlBuffer.GetBufferSize() }},
			0);



		VansVKDescriptorManager::GetInstance()->CommitDescriptorUpdates();



		VansVKDescriptorManager::GetInstance()->BeginDescriptorUpdate();



		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSRResolveDescriptorSets[0], PassBinding::TEXTURE_0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						color.GetSampler(),

						color.GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSRResolveDescriptorSets[0], PassBinding::TEXTURE_1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						roughness.GetSampler(),

						roughness.GetImageView(),

						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSRResolveDescriptorSets[0], PassBinding::TEXTURE_2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						normal.GetSampler(),

						normal.GetImageView(),

						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSRResolveDescriptorSets[0], PassBinding::TEXTURE_3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						position.GetSampler(),

						position.GetImageView(),

						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSRResolveDescriptorSets[0], PassBinding::UAV_IMAGE_3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {

					{

						ssrHitInfo->GetImage().GetSampler(),

						ssrHitInfo->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);



		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSRResolveDescriptorSets[0], PassBinding::UAV_IMAGE_4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {

					{

						ssrRayPdf->GetImage().GetSampler(),

						ssrRayPdf->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);



		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSRResolveDescriptorSets[0], PassBinding::UAV_IMAGE_5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {

					{

						ssrResult->GetImage().GetSampler(),

						ssrResult->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->CommitDescriptorUpdates();



		VansVKDescriptorManager::GetInstance()->BeginDescriptorUpdate();

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSRAADescriptorSets[0], PassBinding::TEXTURE_0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						ssrResult->GetImage().GetSampler(),

						ssrResult->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSRAADescriptorSets[0], PassBinding::TEXTURE_1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						position.GetSampler(),

						position.GetImageView(),

						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSRAADescriptorSets[0], PassBinding::UAV_IMAGE_1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {

					{

						ssrAaResultA->GetImage().GetSampler(),

						ssrAaResultA->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSRAADescriptorSets[0], PassBinding::UAV_IMAGE_2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {

					{

						ssrAaResultB->GetImage().GetSampler(),

						ssrAaResultB->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);



		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSRAADescriptorSets[0], PassBinding::UAV_IMAGE_3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {

					{

						ssrAaResult->GetImage().GetSampler(),

						ssrAaResult->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);



		auto& motionVectorSSR = renderPassManager->GetMotionVector();

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSRAADescriptorSets[0], SSRTemporalAAPassBinding::SSR_TAA_BINDING_MOTION_VECTOR, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						motionVectorSSR.GetSampler(),

						motionVectorSSR.GetImageView(),

						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->CommitDescriptorUpdates();

		MarkFeatureDescriptorCurrent(m_SSRDescSetGeneration);

	}



	void VansVKDevice::UpdateHZB(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)

	{

		UpdateHIZSeedDescriptorSet(renderPassManager);

		UpdateHZBDescriptorSets(renderPassManager);
		UpdateOcclusionHIZSeedDescriptorSet(renderPassManager);
		UpdateOcclusionHZBDescriptorSets(renderPassManager);



		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		VansTexture* hzbResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_RESULT);
		VansTexture* occlusionHZBResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_OCCLUSION_RESULT);

		if (hzbResult == nullptr || occlusionHZBResult == nullptr ||
			manager->m_HIZSeedShader == nullptr || manager->m_HZBShader == nullptr ||
			manager->m_OcclusionHIZSeedShader == nullptr || manager->m_OcclusionHZBShader == nullptr ||
			manager->m_HIZSeedDescriptorSets.empty() || manager->m_OcclusionHIZSeedDescriptorSets.empty() ||
			manager->m_HZBDescriptorSets.size() < static_cast<size_t>(manager->m_HIZMipCount - 1) ||
			manager->m_OcclusionHZBDescriptorSets.size() < static_cast<size_t>(manager->m_HIZMipCount - 1))

		{

			return;

		}



		int seedGroupsX = (int)std::ceilf(m_RenderWidth  / 16.0f);

		int seedGroupsY = (int)std::ceilf(m_RenderHeight / 16.0f);

		computeCmd.EnsureComputeShader(*manager->m_HIZSeedShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_HIZSeedSetLayout });

		computeCmd.DispatchCompute(*manager->m_HIZSeedShader, seedGroupsX, seedGroupsY, 1,

			{ m_Scene->GetGlobalDescriptorSet(), manager->m_HIZSeedDescriptorSets[0] });
		computeCmd.EnsureComputeShader(*manager->m_OcclusionHIZSeedShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_OcclusionHIZSeedSetLayout });
		computeCmd.DispatchCompute(*manager->m_OcclusionHIZSeedShader, seedGroupsX, seedGroupsY, 1,

			{ m_Scene->GetGlobalDescriptorSet(), manager->m_OcclusionHIZSeedDescriptorSets[0] });



		VkMemoryBarrier seedBarrier = {};

		seedBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;

		seedBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

		seedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		computeCmd.PipelineBarrier(

			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,

			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,

			{ seedBarrier });



		for (uint32_t mipIndex = 1; mipIndex < manager->m_HIZMipCount; ++mipIndex)

		{

			const uint32_t mipWidth = std::max(1u, m_RenderWidth >> mipIndex);
			const uint32_t mipHeight = std::max(1u, m_RenderHeight >> mipIndex);
			int threadGroupSizeX = (int)std::ceilf(mipWidth / 16.0f);

			int threadGroupSizeY = (int)std::ceilf(mipHeight / 16.0f);



			computeCmd.EnsureComputeShader(*manager->m_HZBShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_HZBTexSetLayouts[mipIndex - 1] });

			computeCmd.DispatchCompute(*manager->m_HZBShader, threadGroupSizeX, threadGroupSizeY, 1,

				{ m_Scene->GetGlobalDescriptorSet(), manager->m_HZBDescriptorSets[mipIndex - 1] });
			computeCmd.EnsureComputeShader(*manager->m_OcclusionHZBShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_OcclusionHZBTexSetLayouts[mipIndex - 1] });

			computeCmd.DispatchCompute(*manager->m_OcclusionHZBShader, threadGroupSizeX, threadGroupSizeY, 1,

				{ m_Scene->GetGlobalDescriptorSet(), manager->m_OcclusionHZBDescriptorSets[mipIndex - 1] });



			VkMemoryBarrier mipBarrier = {};

			mipBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;

			mipBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

			mipBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			computeCmd.PipelineBarrier(

				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,

				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,

				{ mipBarrier });

		}

	}



	void VansVKDevice::UpdateScreenSpaceShadowSets(VansRenderPassManager* renderPassManager)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (IsFeatureDescriptorCurrent(m_ScreenSpaceShadowDescSetGeneration)) return;

		VansTexture* hzb = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_RESULT);
		VansTexture* out = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_SCREEN_SPACE_SHADOW_RESULT);
		VansTexture* minMax = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_CASCADE_SHADOW_MIN_MAX);
		if (hzb == nullptr || out == nullptr || minMax == nullptr ||
			manager->m_ScreenSpaceShadowDescriptorSets.empty() ||
			manager->m_CascadeShadowMinMaxDescriptorSets.size() < manager->m_CascadeShadowMinMaxMipCount)
			return;

		auto& normal = renderPassManager->GetNormal();
		auto& gbuffer2 = renderPassManager->GetGbuffer2();
		auto* desc = VansVKDescriptorManager::GetInstance();
		desc->BeginDescriptorUpdate();
		// Seed mip: 4x4 D32 blocks become one (min,max) texel in each packed
		// cascade tile.
		desc->WriteImageDescriptor(manager->m_CascadeShadowMinMaxDescriptorSets[0], CASCADE_MIN_MAX_BINDING_INPUT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ renderPassManager->GetCascadeShadowSampler(), renderPassManager->GetCascadeShadowArrayView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL }});
		desc->WriteImageDescriptor(manager->m_CascadeShadowMinMaxDescriptorSets[0], CASCADE_MIN_MAX_BINDING_OUTPUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ minMax->GetImage().GetSampler(), minMax->GetImage().GetImageMipView(0), VK_IMAGE_LAYOUT_GENERAL }});
		for (uint32_t mip = 1; mip < manager->m_CascadeShadowMinMaxMipCount; ++mip)
		{
			desc->WriteImageDescriptor(manager->m_CascadeShadowMinMaxDescriptorSets[mip], CASCADE_MIN_MAX_BINDING_INPUT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{{ minMax->GetImage().GetSampler(), minMax->GetImage().GetImageMipView(int(mip - 1)), VK_IMAGE_LAYOUT_GENERAL }});
			desc->WriteImageDescriptor(manager->m_CascadeShadowMinMaxDescriptorSets[mip], CASCADE_MIN_MAX_BINDING_OUTPUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{{ minMax->GetImage().GetSampler(), minMax->GetImage().GetImageMipView(int(mip)), VK_IMAGE_LAYOUT_GENERAL }});
		}
		desc->WriteImageDescriptor(manager->m_ScreenSpaceShadowDescriptorSets[0], SSS_BINDING_NORMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ normal.GetSampler(), normal.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
		desc->WriteImageDescriptor(manager->m_ScreenSpaceShadowDescriptorSets[0], SSS_BINDING_GBUFFER2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ gbuffer2.GetSampler(), gbuffer2.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
		desc->WriteImageDescriptor(manager->m_ScreenSpaceShadowDescriptorSets[0], SSS_BINDING_HIZ, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ hzb->GetImage().GetSampler(), hzb->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		desc->WriteImageDescriptor(manager->m_ScreenSpaceShadowDescriptorSets[0], SSS_BINDING_RESULT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {{ out->GetImage().GetSampler(), out->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		desc->WriteBufferDescriptor(manager->m_ScreenSpaceShadowDescriptorSets[0], SSS_BINDING_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {{ manager->m_ScreenSpaceShadowParamsCBBuffer.GetNativeBuffer(), 0, manager->m_ScreenSpaceShadowParamsCBBuffer.GetBufferSize() }});
		desc->WriteImageDescriptor(manager->m_ScreenSpaceShadowDescriptorSets[0], SSS_BINDING_CASCADE_DEPTH, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ renderPassManager->GetCascadeShadowSampler(), renderPassManager->GetCascadeShadowArrayView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL }});
		desc->WriteImageDescriptor(manager->m_ScreenSpaceShadowDescriptorSets[0], SSS_BINDING_CASCADE_COMPARE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ renderPassManager->GetCascadeShadowCompareSampler(), renderPassManager->GetCascadeShadowArrayView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL }});
		desc->WriteImageDescriptor(manager->m_ScreenSpaceShadowDescriptorSets[0], SSS_BINDING_CASCADE_MIN_MAX, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ minMax->GetImage().GetSampler(), minMax->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		desc->CommitDescriptorUpdates();
		MarkFeatureDescriptorCurrent(m_ScreenSpaceShadowDescSetGeneration);
	}

	void VansVKDevice::UpdateScreenSpaceShadow(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		UpdateScreenSpaceShadowSets(renderPassManager);
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager->m_ScreenSpaceShadowShader == nullptr ||
			manager->m_CascadeShadowMinMaxSeedShader == nullptr ||
			manager->m_CascadeShadowMinMaxReduceShader == nullptr ||
			manager->m_ScreenSpaceShadowDescriptorSets.empty() ||
			manager->m_CascadeShadowMinMaxDescriptorSets.size() < manager->m_CascadeShadowMinMaxMipCount ||
			manager->m_CascadeShadowMinMaxMipCount == 0)
			return;

		const uint32_t cascadeSize = kVansRenderBootstrapSettings.cascadeShadowMapSize;
		const uint32_t baseSize = (std::max)(cascadeSize / 4u, 1u);
		computeCmd.EnsureComputeShader(*manager->m_CascadeShadowMinMaxSeedShader,
			{ manager->m_CascadeShadowMinMaxSetLayout });
		computeCmd.DispatchCompute(*manager->m_CascadeShadowMinMaxSeedShader,
			(baseSize + 7u) / 8u, (baseSize + 7u) / 8u, 4u,
			{ manager->m_CascadeShadowMinMaxDescriptorSets[0] });

		VkMemoryBarrier minMaxBarrier = {};
		minMaxBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		minMaxBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		minMaxBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		computeCmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, { minMaxBarrier });

		for (uint32_t mip = 1; mip < manager->m_CascadeShadowMinMaxMipCount; ++mip)
		{
			const uint32_t mipSize = (std::max)(baseSize >> mip, 1u);
			computeCmd.EnsureComputeShader(*manager->m_CascadeShadowMinMaxReduceShader,
				{ manager->m_CascadeShadowMinMaxSetLayout });
			computeCmd.DispatchCompute(*manager->m_CascadeShadowMinMaxReduceShader,
				(mipSize + 7u) / 8u, (mipSize + 7u) / 8u, 4u,
				{ manager->m_CascadeShadowMinMaxDescriptorSets[mip] });
			computeCmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, { minMaxBarrier });
		}

		uint32_t dispatchW = (std::max)(m_RenderWidth, 1u);
		uint32_t dispatchH = (std::max)(m_RenderHeight, 1u);
		computeCmd.EnsureComputeShader(*manager->m_ScreenSpaceShadowShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_ScreenSpaceShadowSetLayout });
		computeCmd.DispatchCompute(*manager->m_ScreenSpaceShadowShader, (dispatchW + 7) / 8, (dispatchH + 7) / 8, 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_ScreenSpaceShadowDescriptorSets[0] });
	}

	void VansVKDevice::UpdatePunctualShadowDebugPreview(
		VansRenderPassManager* renderPassManager,
		VansVKCommandBuffer& computeCmd)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager == nullptr || renderPassManager == nullptr)
			return;

		if (!m_PunctualShadowFrameState.ConsumeDebugPreviewRefreshRequest())
			return;

		VansTexture* preview = manager->GetRuntimeRenderTexture(
			VansMaterialManager::RT_PUNCTUAL_SHADOW_DEBUG_PREVIEW);
		if (preview == nullptr || manager->m_PunctualShadowDebugShader == nullptr ||
			manager->m_PunctualShadowDebugDescriptorSets.empty())
			return;

		auto* desc = VansVKDescriptorManager::GetInstance();
		desc->BeginDescriptorUpdate();
		// Use a non-comparison sampler. Comparison samplers are reserved for the
		// production shadow test and cannot expose raw D32 values to this resolve.
		desc->WriteImageDescriptor(
			manager->m_PunctualShadowDebugDescriptorSets[0],
			PUNCTUAL_SHADOW_DEBUG_BINDING_ATLAS,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			renderPassManager->GetPunctualShadowRawDescriptorInfos(
				renderPassManager->GetCascadeShadowSampler()));
		desc->WriteImageDescriptor(
			manager->m_PunctualShadowDebugDescriptorSets[0],
			PUNCTUAL_SHADOW_DEBUG_BINDING_RESULT,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ preview->GetImage().GetSampler(), preview->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		desc->CommitDescriptorUpdates();

		computeCmd.EnsureComputeShader(
			*manager->m_PunctualShadowDebugShader,
			{ manager->m_PunctualShadowDebugSetLayout });
		computeCmd.DispatchCompute(
			*manager->m_PunctualShadowDebugShader,
			(static_cast<uint32_t>(preview->GetWidth()) + 7u) / 8u,
			(static_cast<uint32_t>(preview->GetHeight()) + 7u) / 8u,
			1,
			{ manager->m_PunctualShadowDebugDescriptorSets[0] });
	}

	void VansVKDevice::UpdateSSR(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		UpdateSSRDescriptorSets(renderPassManager);
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager == nullptr ||
			manager->m_SSRClassifyShader == nullptr ||
			manager->m_SSRPrepareIndirectShader == nullptr ||
			manager->m_SSRTraceShader == nullptr ||
			manager->m_SSRResolveShader == nullptr ||
			manager->m_SSRTemporalAAShader == nullptr ||
			manager->m_SSRTraceSetLayout == VK_NULL_HANDLE ||
			manager->m_SSRResolveSetLayout == VK_NULL_HANDLE ||
			manager->m_SSRAASetLayout == VK_NULL_HANDLE ||
			manager->m_SSRTraceDescriptorSets.empty() ||
			manager->m_SSRResolveDescriptorSets.empty() ||
			manager->m_SSRAADescriptorSets.empty() ||
			manager->m_SSRTraceDescriptorSets[0] == VK_NULL_HANDLE ||
			manager->m_SSRResolveDescriptorSets[0] == VK_NULL_HANDLE ||
			manager->m_SSRAADescriptorSets[0] == VK_NULL_HANDLE ||
			manager->m_SSRRayListBuffer.GetNativeBuffer() == VK_NULL_HANDLE ||
			manager->m_SSRTraceControlBuffer.GetNativeBuffer() == VK_NULL_HANDLE ||
			!IsFeatureDescriptorCurrent(m_SSRDescSetGeneration))
		{
			return;
		}

		VkBuffer controlBuffer = manager->m_SSRTraceControlBuffer.GetNativeBuffer();
		const std::vector<VkDescriptorSet> traceSets = {
			m_Scene->GetGlobalDescriptorSet(), manager->m_SSRTraceDescriptorSets[0] };

		// rayCount and VkDispatchIndirectCommand share one 16-byte GPU buffer.
		// Resetting on the command buffer keeps the resource device-local and
		// avoids a host readback in the asynchronous rendering path.
		computeCmd.FillBuffer(controlBuffer, 0, manager->m_SSRTraceControlBuffer.GetBufferSize(), 0u);
		VkMemoryBarrier transferToClassify = {};
		transferToClassify.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		transferToClassify.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		transferToClassify.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		computeCmd.PipelineBarrier(
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			{ transferToClassify });

		computeCmd.EnsureComputeShader(*manager->m_SSRClassifyShader,
			{ m_Scene->GetGlobalDescriptorSetLayout(), manager->m_SSRTraceSetLayout });
		computeCmd.DispatchCompute(*manager->m_SSRClassifyShader,
			(m_RenderWidth + 7) / 8, (m_RenderHeight + 7) / 8, 1, traceSets);
		VkMemoryBarrier classifyToPrepare = {};
		classifyToPrepare.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		classifyToPrepare.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		classifyToPrepare.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		computeCmd.PipelineBarrier(
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			{ classifyToPrepare });

		computeCmd.EnsureComputeShader(*manager->m_SSRPrepareIndirectShader,
			{ m_Scene->GetGlobalDescriptorSetLayout(), manager->m_SSRTraceSetLayout });
		computeCmd.DispatchCompute(*manager->m_SSRPrepareIndirectShader, 1, 1, 1, traceSets);

		VkMemoryBarrier prepareToTrace = {};
		prepareToTrace.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		prepareToTrace.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		prepareToTrace.dstAccessMask =
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		computeCmd.PipelineBarrier(
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
			{ prepareToTrace });

		computeCmd.EnsureComputeShader(*manager->m_SSRTraceShader,
			{ m_Scene->GetGlobalDescriptorSetLayout(), manager->m_SSRTraceSetLayout });
		computeCmd.DispatchComputeIndirect(
			*manager->m_SSRTraceShader, controlBuffer, sizeof(uint32_t), traceSets);
		RecordShaderWriteToReadMemoryDependency(computeCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		computeCmd.EnsureComputeShader(*manager->m_SSRResolveShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_SSRResolveSetLayout });
		computeCmd.DispatchCompute(*manager->m_SSRResolveShader, (m_RenderWidth + 7) / 8, (m_RenderHeight + 7) / 8, 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_SSRResolveDescriptorSets[0] });
		RecordShaderWriteToReadMemoryDependency(computeCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		computeCmd.EnsureComputeShader(*manager->m_SSRTemporalAAShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_SSRAASetLayout });
		computeCmd.DispatchCompute(*manager->m_SSRTemporalAAShader, (m_RenderWidth + 7) / 8, (m_RenderHeight + 7) / 8, 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_SSRAADescriptorSets[0] });
	}

	void VansVKDevice::UpdateAmbientSkyCache(VansVKCommandBuffer& computeCmd)
	{
		VansMaterialManager* manager = m_Scene ? m_Scene->GetMaterialManager() : nullptr;
		const auto& settings = m_Scene->GetGISettings().ambientSkyCache;
		if (manager == nullptr || !settings.enabled || manager->m_AmbientSkyCacheShader == nullptr ||
			manager->m_AmbientSkyCacheDescriptorSets.empty() ||
			manager->m_AmbientSkyCacheParamsCBBuffer.GetNativeBuffer() == VK_NULL_HANDLE)
			return;

		VansTexture* cacheX = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_AMBIENT_SKY_CACHE_X);
		VansTexture* cacheY = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_AMBIENT_SKY_CACHE_Y);
		VansTexture* cacheZ = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_AMBIENT_SKY_CACHE_Z);
		if (cacheX == nullptr || cacheY == nullptr || cacheZ == nullptr || rayTracingContext.GetGIWorld() == nullptr)
			return;

		constexpr uint32_t gridX = 16u, gridY = 8u, gridZ = 16u;
		const float spacing = settings.gridSpacing;
		const glm::vec3 cameraPosition = m_Scene->GetCamera()
			? glm::vec3(m_Scene->GetCamera()->GetPosition()) : glm::vec3(0.0f);
		const glm::vec3 cacheDimensions = glm::vec3(gridX, gridY, gridZ);
		const glm::vec3 cacheSize = cacheDimensions * spacing;
		const glm::vec3 cacheMin = manager->m_AmbientSkyCacheOrigin;
		// Keep the current logical grid while the camera remains in its inner
		// half.  This avoids re-centering on ordinary camera jitter and preserves
		// all already queried cells when a real shift is needed.
		const bool outsideInnerCache =
			!manager->m_AmbientSkyCacheInitialized ||
			glm::any(glm::lessThan(cameraPosition, cacheMin + cacheSize * 0.25f)) ||
			glm::any(glm::greaterThanEqual(cameraPosition, cacheMin + cacheSize * 0.75f));
		const glm::vec3 previousOrigin = manager->m_AmbientSkyCacheOrigin;
		bool recentered = false;
		if (outsideInnerCache)
		{
			const glm::vec3 snapped = glm::floor(cameraPosition / spacing) * spacing;
			const glm::vec3 newOrigin = snapped - cacheSize * 0.5f;
			const glm::ivec3 deltaCells = glm::ivec3(glm::round((newOrigin - previousOrigin) / spacing));
			auto wrap = [](int value, int dimension)
			{
				const int remainder = value % dimension;
				return remainder < 0 ? remainder + dimension : remainder;
			};
			manager->m_AmbientSkyCacheRingOffset = glm::ivec3(
				wrap(manager->m_AmbientSkyCacheRingOffset.x + deltaCells.x, static_cast<int>(gridX)),
				wrap(manager->m_AmbientSkyCacheRingOffset.y + deltaCells.y, static_cast<int>(gridY)),
				wrap(manager->m_AmbientSkyCacheRingOffset.z + deltaCells.z, static_cast<int>(gridZ)));
			manager->m_AmbientSkyCacheOrigin = newOrigin;
			manager->m_AmbientSkyCacheFrameOffset = 0u;
			manager->m_AmbientSkyCacheInitialized = true;
			recentered = glm::dot(newOrigin - previousOrigin, newOrigin - previousOrigin) > 1e-6f;
			if (recentered)
			{
				VANS_LOG("[AmbientSkyCache] recenter origin=(" << newOrigin.x << "," << newOrigin.y << "," << newOrigin.z
					<< ") ring=(" << manager->m_AmbientSkyCacheRingOffset.x << ","
					<< manager->m_AmbientSkyCacheRingOffset.y << "," << manager->m_AmbientSkyCacheRingOffset.z << ")");
			}
		}

		AmbientSkyCacheParamsGPU params{};
		params.originAndSpacing = glm::vec4(manager->m_AmbientSkyCacheOrigin, spacing);
		params.ringOffset = glm::ivec4(manager->m_AmbientSkyCacheRingOffset, 0);
		// The compute shader owns one cell and evaluates its six axis directions.
		// Convert the public direction budget to a cell budget so the actual GI
		// trace work remains bounded by the configured value.
		const uint32_t directionBudget = std::clamp(settings.queriesPerFrame, 1u, 256u);
		const uint32_t cellQueries = std::max(1u, (directionBudget + 5u) / 6u);
		params.gridAndQuery = glm::uvec4(gridX, gridY, gridZ, cellQueries);
		manager->m_AmbientSkyCacheParamsCBBuffer.SetBufferData(&params, 0, sizeof(params));
		AmbientSkyCacheInfoGPU info = params;
		info.gridAndQuery.w = GetAmbientSkyCacheDebugMode();
		manager->m_AmbientSkyCacheInfoCBBuffer.SetBufferData(&info, 0, sizeof(info));

		auto* descriptors = VansVKDescriptorManager::GetInstance();
		descriptors->BeginDescriptorUpdate();
		descriptors->WriteImageDescriptor(manager->m_AmbientSkyCacheDescriptorSets[0], AMBIENT_SKY_CACHE_OUTPUT_X,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {{ cacheX->GetImage().GetSampler(), cacheX->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteImageDescriptor(manager->m_AmbientSkyCacheDescriptorSets[0], AMBIENT_SKY_CACHE_OUTPUT_Y,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {{ cacheY->GetImage().GetSampler(), cacheY->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteImageDescriptor(manager->m_AmbientSkyCacheDescriptorSets[0], AMBIENT_SKY_CACHE_OUTPUT_Z,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {{ cacheZ->GetImage().GetSampler(), cacheZ->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		descriptors->WriteBufferDescriptor(manager->m_AmbientSkyCacheDescriptorSets[0], AMBIENT_SKY_CACHE_PARAMS,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {{ manager->m_AmbientSkyCacheParamsCBBuffer.GetNativeBuffer(), 0, manager->m_AmbientSkyCacheParamsCBBuffer.GetBufferSize() }});
		descriptors->CommitDescriptorUpdates();

		computeCmd.EnsureComputeShader(*manager->m_AmbientSkyCacheShader,
			{ m_Scene->GetGlobalDescriptorSetLayout(), manager->m_AmbientSkyCacheSetLayout, rayTracingContext.GetGIWorld()->Layout() });
		if (recentered)
		{
			AmbientSkyCachePushConstants clearPush{};
			clearPush.control = glm::uvec4(0u, 1u, 0u, 0u);
			clearPush.previousOriginAndSpacing = glm::vec4(previousOrigin, spacing);
			computeCmd.DispatchCompute(*manager->m_AmbientSkyCacheShader,
				(gridX * gridY * gridZ + 63u) / 64u, 1u, 1u,
				{ m_Scene->GetGlobalDescriptorSet(), manager->m_AmbientSkyCacheDescriptorSets[0], rayTracingContext.GetGIWorld()->Descriptor() },
				&clearPush, sizeof(clearPush));
			RecordShaderWriteToReadMemoryDependency(computeCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		}

		const uint32_t frameOffset = manager->m_AmbientSkyCacheFrameOffset;
		manager->m_AmbientSkyCacheFrameOffset = (frameOffset + cellQueries) % (gridX * gridY * gridZ);
		if (manager->m_SSGITemporalFrame < 2u)
		{
			VANS_LOG("[AmbientSkyCache] update directionBudget=" << directionBudget
				<< " cells=" << cellQueries << " giTraces=" << (cellQueries * 6u)
				<< " dispatchGroups=" << ((cellQueries + 63u) / 64u));
		}
		AmbientSkyCachePushConstants updatePush{};
		updatePush.control = glm::uvec4(frameOffset, 0u, 0u, 0u);
		computeCmd.DispatchCompute(*manager->m_AmbientSkyCacheShader,
			(cellQueries + 63u) / 64u, 1u, 1u,
			{ m_Scene->GetGlobalDescriptorSet(), manager->m_AmbientSkyCacheDescriptorSets[0], rayTracingContext.GetGIWorld()->Descriptor() },
			&updatePush, sizeof(updatePush));
		RecordShaderWriteToReadMemoryDependency(computeCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	}

	void VansVKDevice::UpdateGIData(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		UpdateAmbientSkyCache(computeCmd);
		UpdateGIDataDescriptorSets(renderPassManager);
		if (!IsFeatureDescriptorCurrent(m_GIDataDescSetGeneration))
			return;

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager == nullptr ||
			manager->m_SSGIShader == nullptr ||
			manager->m_SSGIProbeCacheShader == nullptr ||
			manager->m_SSGITemporalShader == nullptr ||
			manager->m_SSGIAtrousShader == nullptr)
		{
			return;
		}

        // Cache trace 和 Temporal 必须读取同一帧历史与 jitter；在两者之前统一上传。
		SSGITemporalParamsGPU temporalData{};
		const float width = static_cast<float>(std::max(m_RenderWidth, 1u));
		const float height = static_cast<float>(std::max(m_RenderHeight, 1u));
		temporalData.screenSize = glm::vec4(width, height, 1.0f / width, 1.0f / height);
        const glm::vec2 jitterDelta =
            ExtractTemporalJitterUV(m_CameraData.lastViewProjectionMatrix, m_CameraData.lastUnjitteredViewProjectionMatrix) -
            ExtractTemporalJitterUV(m_CameraData.viewProjectionMatrix, m_CameraData.unjitteredViewProjectionMatrix);
        temporalData.frameParams = glm::vec4(static_cast<float>(manager->m_SSGITemporalFrame), jitterDelta, 0.0f);
		manager->m_SSGITemporalCBBuffer.SetBufferData(&temporalData, 0, sizeof(temporalData));

		const VkCommandBuffer commandBuffer = computeCmd.GetVKCommandBuffer();
		const Vans::VansGpuQueueLane queueLane = m_AsyncComputeEnabled
			? Vans::VansGpuQueueLane::Compute
			: Vans::VansGpuQueueLane::Graphics;
		UpdateGIReceiverVisibility(computeCmd, jitterDelta);
		{
			VANS_GPU_SCOPE_LANE(commandBuffer, "SSGI.ScreenProbeCacheTrace", queueLane);
			UpdateSSGIProbeCache(renderPassManager, computeCmd);
		}

		RecordShaderWriteToReadMemoryDependency(
			computeCmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

		{
			VANS_GPU_SCOPE_LANE(commandBuffer, "SSGI.Reconstruct", queueLane);
			UpdateSSGI(renderPassManager, computeCmd);
		}

		RecordShaderWriteToReadMemoryDependency(
			computeCmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

		{
			VANS_GPU_SCOPE_LANE(commandBuffer, "SSGI.Temporal", queueLane);
			TemporalFilterSSGI(renderPassManager, computeCmd);
		}

		RecordShaderWriteToReadMemoryDependency(
			computeCmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

		AtrousFilterSSGI(renderPassManager, computeCmd);
		RecordShaderWriteToReadMemoryDependency(
			computeCmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		++manager->m_SSGITemporalFrame;
	}

	void VansVKDevice::UpdateTileLightBuildSets()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (IsFeatureDescriptorCurrent(m_TileLightBuildDescSetGeneration)) return;
		if (manager->m_TileLightBuildDescriptorSets.empty()) return;
		auto* desc = VansVKDescriptorManager::GetInstance();
		desc->BeginDescriptorUpdate();
		desc->WriteBufferDescriptor(manager->m_TileLightBuildDescriptorSets[0], TILE_BUILD_BINDING_GRID, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{ manager->m_TileLightHeaderBuffer.GetNativeBuffer(), 0, manager->m_TileLightHeaderBuffer.GetBufferSize() }});
		desc->WriteBufferDescriptor(manager->m_TileLightBuildDescriptorSets[0], TILE_BUILD_BINDING_INDICES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{ manager->m_TileLightIndexBuffer.GetNativeBuffer(), 0, manager->m_TileLightIndexBuffer.GetBufferSize() }});
		desc->WriteBufferDescriptor(manager->m_TileLightBuildDescriptorSets[0], TILE_BUILD_BINDING_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {{ manager->m_TileLightBuildParamsCBBuffer.GetNativeBuffer(), 0, manager->m_TileLightBuildParamsCBBuffer.GetBufferSize() }});
		desc->CommitDescriptorUpdates();
		MarkFeatureDescriptorCurrent(m_TileLightBuildDescSetGeneration);
	}

	void VansVKDevice::BuildTileLightLists(VansVKCommandBuffer& cmd)
	{
		UpdateTileLightBuildSets();
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager->m_TileLightBuildShader == nullptr) return;
		if (manager->m_TileLightGridX == 0 || manager->m_TileLightGridY == 0 || manager->m_TileLightBuildDescriptorSets.empty()) return;
		uint32_t groupsX = (manager->m_TileLightGridX + 7) / 8;
		uint32_t groupsY = (manager->m_TileLightGridY + 7) / 8;
		cmd.EnsureComputeShader(*manager->m_TileLightBuildShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_TileLightBuildSetLayout });
		cmd.DispatchCompute(*manager->m_TileLightBuildShader, groupsX, groupsY, 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_TileLightBuildDescriptorSets[0] });
		// 计算队列只声明本队列的读阶段；Deferred 的跨队列读取由 semaphore 保证。
		RecordShaderWriteToReadMemoryDependency(
			cmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | (m_AsyncComputeEnabled?0u:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT));
	}

	void VansVKDevice::UploadPostProcessProfileIfDirty()
	{
		if (m_Scene == nullptr) return;
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager == nullptr) return;

		const VansRenderPostProcessFrameData& frame =
			m_CurrentRenderSceneSnapshot.postProcess;
		if (!frame.prepared) return;
		manager->m_PostProcessParamsCBBuffer.SetBufferData(
			&frame.params, 0, sizeof(frame.params));
		manager->m_ExposureAdaptParamsCBBuffer.SetBufferData(
			&frame.exposure, 0, sizeof(frame.exposure));

		if (frame.staticParametersDirty)
		{
			manager->m_BloomShapeParamsCBBuffer.SetBufferData(
				&frame.bloomShape, 0, sizeof(frame.bloomShape));
			manager->m_BloomParamsCBBuffer.SetBufferData(
				&frame.bloom, 0, sizeof(frame.bloom));
			manager->m_DepthOfFieldParamsCBBuffer.SetBufferData(
				&frame.depthOfField, 0, sizeof(frame.depthOfField));
			m_PPBloomDescSetGeneration = 0;
		}
	}

	void VansVKDevice::UpdateExposureDescriptorSets(VansRenderPassManager* renderPassManager)
	{
		if (IsFeatureDescriptorCurrent(m_PPExposureDescSetGeneration)) return;
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager->m_ExposureLuminanceDescriptorSets.empty()
			|| manager->m_ExposureAdaptDescriptorSets.empty()) return;

		VansTexture* luminance = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_EXPOSURE_LUMINANCE);
		VansTexture* exposure = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_EXPOSURE_CURRENT);
		VansTexture* upscalerExposure = manager->GetRuntimeRenderTexture(
			VansMaterialManager::RT_UPSCALER_EXPOSURE);
		if (luminance == nullptr || exposure == nullptr || upscalerExposure == nullptr) return;

		auto& sceneColor = renderPassManager->GetColor();
		auto* desc = VansVKDescriptorManager::GetInstance();
		desc->BeginDescriptorUpdate();
		desc->WriteImageDescriptor(
			manager->m_ExposureLuminanceDescriptorSets[0],
			EXPOSURE_LUM_BINDING_SRC_COLOR,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ sceneColor.GetSampler(), sceneColor.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
		desc->WriteImageDescriptor(
			manager->m_ExposureLuminanceDescriptorSets[0],
			EXPOSURE_LUM_BINDING_LUM_OUT,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ luminance->GetImage().GetSampler(), luminance->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		desc->CommitDescriptorUpdates();

		desc->BeginDescriptorUpdate();
		desc->WriteImageDescriptor(
			manager->m_ExposureAdaptDescriptorSets[0],
			EXPOSURE_ADAPT_BINDING_LUM_IN,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ luminance->GetImage().GetSampler(), luminance->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		desc->WriteImageDescriptor(
			manager->m_ExposureAdaptDescriptorSets[0],
			EXPOSURE_ADAPT_BINDING_EXP_OUT,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ exposure->GetImage().GetSampler(), exposure->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		desc->WriteBufferDescriptor(
			manager->m_ExposureAdaptDescriptorSets[0],
			EXPOSURE_ADAPT_BINDING_PARAMS,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{{ manager->m_ExposureAdaptParamsCBBuffer.GetNativeBuffer(), 0,
			   manager->m_ExposureAdaptParamsCBBuffer.GetBufferSize() }});
		desc->WriteImageDescriptor(
			manager->m_ExposureAdaptDescriptorSets[0],
			EXPOSURE_ADAPT_BINDING_FSR_EXP_OUT,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ upscalerExposure->GetImage().GetSampler(), upscalerExposure->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		desc->CommitDescriptorUpdates();
		MarkFeatureDescriptorCurrent(m_PPExposureDescSetGeneration);
	}

	void VansVKDevice::UpdateExposure(
		VansRenderPassManager* renderPassManager,
		VansVKCommandBuffer& computeCmd)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		UpdateExposureDescriptorSets(renderPassManager);
		if (manager->m_ExposureAdaptShader == nullptr
			|| manager->m_ExposureLuminanceDescriptorSets.empty()
			|| manager->m_ExposureAdaptDescriptorSets.empty()) return;

		if (m_CurrentRenderSceneSnapshot.postProcess.enableAutoExposure)
		{
			if (manager->m_ExposureLuminanceShader == nullptr) return;
			computeCmd.EnsureComputeShader(
				*manager->m_ExposureLuminanceShader,
				{ m_Scene->GetGlobalDescriptorSetLayout(), manager->m_ExposureLuminanceSetLayout });
			computeCmd.DispatchCompute(
				*manager->m_ExposureLuminanceShader,
				8, 8, 1,
				{ m_Scene->GetGlobalDescriptorSet(), manager->m_ExposureLuminanceDescriptorSets[0] });
			RecordShaderWriteToReadMemoryDependency(
				computeCmd,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		}

		computeCmd.EnsureComputeShader(
			*manager->m_ExposureAdaptShader,
			{ m_Scene->GetGlobalDescriptorSetLayout(), manager->m_ExposureAdaptSetLayout });
		computeCmd.DispatchCompute(
			*manager->m_ExposureAdaptShader,
			1, 1, 1,
			{ m_Scene->GetGlobalDescriptorSet(), manager->m_ExposureAdaptDescriptorSets[0] });
	}

	void VansVKDevice::UpdateDepthOfFieldDescriptorSets(VansRenderPassManager* renderPassManager)
	{
		if (IsFeatureDescriptorCurrent(m_PPDepthOfFieldDescSetGeneration)) return;
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager->m_DepthOfFieldDescriptorSets.empty()) return;

		VansTexture* dofResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_DOF_RESULT);
		if (dofResult == nullptr) return;

		auto& sceneColor = renderPassManager->GetColor();
		auto& gbuffer2 = renderPassManager->GetGbuffer2();
		auto* desc = VansVKDescriptorManager::GetInstance();
		desc->BeginDescriptorUpdate();
		desc->WriteImageDescriptor(
			manager->m_DepthOfFieldDescriptorSets[0],
			DOF_BINDING_SRC_COLOR,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ sceneColor.GetSampler(), sceneColor.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
		desc->WriteImageDescriptor(
			manager->m_DepthOfFieldDescriptorSets[0],
			DOF_BINDING_GBUFFER2,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ gbuffer2.GetSampler(), gbuffer2.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
		desc->WriteImageDescriptor(
			manager->m_DepthOfFieldDescriptorSets[0],
			DOF_BINDING_DST_COLOR,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ dofResult->GetImage().GetSampler(), dofResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		desc->WriteBufferDescriptor(
			manager->m_DepthOfFieldDescriptorSets[0],
			DOF_BINDING_PARAMS,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{{ manager->m_DepthOfFieldParamsCBBuffer.GetNativeBuffer(), 0,
			   manager->m_DepthOfFieldParamsCBBuffer.GetBufferSize() }});
		desc->CommitDescriptorUpdates();
		MarkFeatureDescriptorCurrent(m_PPDepthOfFieldDescSetGeneration);
	}

	void VansVKDevice::UpdateDepthOfField(
		VansRenderPassManager* renderPassManager,
		VansVKCommandBuffer& computeCmd)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (!m_CurrentRenderSceneSnapshot.postProcess.enableDepthOfField) return;
		UpdateDepthOfFieldDescriptorSets(renderPassManager);
		if (manager->m_DepthOfFieldShader == nullptr
			|| manager->m_DepthOfFieldDescriptorSets.empty()) return;

		const uint32_t groupsX = (std::max)(m_RenderWidth + 7, 8u) / 8u;
		const uint32_t groupsY = (std::max)(m_RenderHeight + 7, 8u) / 8u;
		computeCmd.EnsureComputeShader(
			*manager->m_DepthOfFieldShader,
			{ m_Scene->GetGlobalDescriptorSetLayout(), manager->m_DepthOfFieldSetLayout });
		computeCmd.DispatchCompute(
			*manager->m_DepthOfFieldShader,
			groupsX, groupsY, 1,
			{ m_Scene->GetGlobalDescriptorSet(), manager->m_DepthOfFieldDescriptorSets[0] });
		RecordShaderWriteToReadMemoryDependency(
			computeCmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}

	void VansVKDevice::UpdateBloomDescriptorSets(VansRenderPassManager* renderPassManager)
	{
		if (IsFeatureDescriptorCurrent(m_PPBloomDescSetGeneration)) return;
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager->m_BloomPrefilterDescriptorSets.empty() || manager->m_BloomDownsampleDescriptorSets.size() < 4 || manager->m_BloomUpsampleDescriptorSets.size() < 4) return;
		VansTexture* prefilter = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_PREFILTER);
		VansTexture* mip0 = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_MIP0);
		VansTexture* mip1 = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_MIP1);
		VansTexture* mip2 = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_MIP2);
		VansTexture* mip3 = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_MIP3);
		VansTexture* upMip0 = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_UP_MIP0);
		VansTexture* upMip1 = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_UP_MIP1);
		VansTexture* upMip2 = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_UP_MIP2);
		VansTexture* base = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_BASE);
		VansTexture* result = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_RESULT);
		VansTexture* dofResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_DOF_RESULT);
		if (!prefilter || !mip0 || !mip1 || !mip2 || !mip3 || !upMip0 || !upMip1 || !upMip2 || !result) return;
		const bool hasShapeFinalize =
			manager->m_BloomShapeShader != nullptr &&
			!manager->m_BloomShapeDescriptorSets.empty() &&
			base != nullptr;
		auto& sceneColor = renderPassManager->GetColor();
		const bool useDOFSource =
			m_CurrentRenderSceneSnapshot.postProcess.enableDepthOfField &&
			dofResult != nullptr;
		auto* desc = VansVKDescriptorManager::GetInstance();
		desc->BeginDescriptorUpdate();
		if (useDOFSource)
		{
			desc->WriteImageDescriptor(
				manager->m_BloomPrefilterDescriptorSets[0],
				BLOOM_PREFILTER_BINDING_SRC,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{{ dofResult->GetImage().GetSampler(), dofResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		}
		else
		{
			desc->WriteImageDescriptor(
				manager->m_BloomPrefilterDescriptorSets[0],
				BLOOM_PREFILTER_BINDING_SRC,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{{ sceneColor.GetSampler(), sceneColor.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
		}
		desc->WriteImageDescriptor(manager->m_BloomPrefilterDescriptorSets[0], BLOOM_PREFILTER_BINDING_DST, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {{ prefilter->GetImage().GetSampler(), prefilter->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		desc->WriteBufferDescriptor(manager->m_BloomPrefilterDescriptorSets[0], BLOOM_PREFILTER_BINDING_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {{ manager->m_BloomParamsCBBuffer.GetNativeBuffer(), 0, manager->m_BloomParamsCBBuffer.GetBufferSize() }});
		desc->CommitDescriptorUpdates();
		VansTexture* dsInputs[4] = { prefilter, mip0, mip1, mip2 };
		VansTexture* dsOutputs[4] = { mip0, mip1, mip2, mip3 };
		for (int i = 0; i < 4; ++i)
		{
			desc->BeginDescriptorUpdate();
			desc->WriteImageDescriptor(manager->m_BloomDownsampleDescriptorSets[i], BLOOM_DOWNSAMPLE_BINDING_SRC, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ dsInputs[i]->GetImage().GetSampler(), dsInputs[i]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
			desc->WriteImageDescriptor(manager->m_BloomDownsampleDescriptorSets[i], BLOOM_DOWNSAMPLE_BINDING_DST, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {{ dsOutputs[i]->GetImage().GetSampler(), dsOutputs[i]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
			desc->CommitDescriptorUpdates();
		}
		VansTexture* usLo[4] = { mip3, upMip2, upMip1, upMip0 };
		VansTexture* usHi[4] = { mip2, mip1, mip0, prefilter };
		VansTexture* usDst[4] = { upMip2, upMip1, upMip0, hasShapeFinalize ? base : result };
		for (int i = 0; i < 4; ++i)
		{
			desc->BeginDescriptorUpdate();
			desc->WriteImageDescriptor(manager->m_BloomUpsampleDescriptorSets[i], BLOOM_UPSAMPLE_BINDING_SRC_LO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ usLo[i]->GetImage().GetSampler(), usLo[i]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
			desc->WriteImageDescriptor(manager->m_BloomUpsampleDescriptorSets[i], BLOOM_UPSAMPLE_BINDING_SRC_HI, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ usHi[i]->GetImage().GetSampler(), usHi[i]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
			desc->WriteImageDescriptor(manager->m_BloomUpsampleDescriptorSets[i], BLOOM_UPSAMPLE_BINDING_DST, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {{ usDst[i]->GetImage().GetSampler(), usDst[i]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
			desc->WriteBufferDescriptor(manager->m_BloomUpsampleDescriptorSets[i], BLOOM_UPSAMPLE_BINDING_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {{ manager->m_BloomParamsCBBuffer.GetNativeBuffer(), 0, manager->m_BloomParamsCBBuffer.GetBufferSize() }});
			desc->CommitDescriptorUpdates();
		}
		MarkFeatureDescriptorCurrent(m_PPBloomDescSetGeneration);
	}

	void VansVKDevice::UpdateBloomShapeDescriptorSets()
	{
		if (IsFeatureDescriptorCurrent(m_PPBloomShapeDescSetGeneration)) return;
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager->m_BloomShapeDescriptorSets.empty()) return;

		VansTexture* base = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_BASE);
		VansTexture* result = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_RESULT);
		if (base == nullptr || result == nullptr) return;

		auto* desc = VansVKDescriptorManager::GetInstance();
		desc->BeginDescriptorUpdate();
		desc->WriteImageDescriptor(
			manager->m_BloomShapeDescriptorSets[0],
			BLOOM_SHAPE_BINDING_SRC,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ base->GetImage().GetSampler(), base->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		desc->WriteImageDescriptor(
			manager->m_BloomShapeDescriptorSets[0],
			BLOOM_SHAPE_BINDING_DST,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ result->GetImage().GetSampler(), result->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		desc->WriteBufferDescriptor(
			manager->m_BloomShapeDescriptorSets[0],
			BLOOM_SHAPE_BINDING_PARAMS,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{{ manager->m_BloomShapeParamsCBBuffer.GetNativeBuffer(), 0,
			   manager->m_BloomShapeParamsCBBuffer.GetBufferSize() }});
		desc->CommitDescriptorUpdates();
		MarkFeatureDescriptorCurrent(m_PPBloomShapeDescSetGeneration);
	}

	void VansVKDevice::UpdateBloom(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (!m_CurrentRenderSceneSnapshot.postProcess.enableBloom) return;
		UpdateBloomDescriptorSets(renderPassManager);
		if (manager->m_BloomPrefilterShader == nullptr || manager->m_BloomDownsampleShader == nullptr || manager->m_BloomUpsampleShader == nullptr) return;
		const uint32_t w2 = (std::max)(m_RenderWidth / 2, 1u), h2 = (std::max)(m_RenderHeight / 2, 1u);
		const uint32_t w4 = (std::max)(m_RenderWidth / 4, 1u), h4 = (std::max)(m_RenderHeight / 4, 1u);
		const uint32_t w8 = (std::max)(m_RenderWidth / 8, 1u), h8 = (std::max)(m_RenderHeight / 8, 1u);
		const uint32_t w16 = (std::max)(m_RenderWidth / 16, 1u), h16 = (std::max)(m_RenderHeight / 16, 1u);
		auto groups = [](uint32_t n) { return (n + 7) / 8; };
		auto barrier = [&]() { RecordShaderWriteToReadMemoryDependency(computeCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT); };
		computeCmd.EnsureComputeShader(*manager->m_BloomPrefilterShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_BloomPrefilterSetLayout });
		computeCmd.DispatchCompute(*manager->m_BloomPrefilterShader, groups(w2), groups(h2), 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_BloomPrefilterDescriptorSets[0] });
		barrier();
		computeCmd.EnsureComputeShader(*manager->m_BloomDownsampleShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_BloomDownsampleSetLayout });
		uint32_t dsW[4] = { w2, w4, w8, w16 };
		uint32_t dsH[4] = { h2, h4, h8, h16 };
		for (int i = 0; i < 4; ++i)
		{
			computeCmd.DispatchCompute(*manager->m_BloomDownsampleShader, groups(dsW[i]), groups(dsH[i]), 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_BloomDownsampleDescriptorSets[i] });
			barrier();
		}
		computeCmd.EnsureComputeShader(*manager->m_BloomUpsampleShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_BloomUpsampleSetLayout });
		uint32_t usW[4] = { w8, w4, w2, w2 };
		uint32_t usH[4] = { h8, h4, h2, h2 };
		for (int i = 0; i < 4; ++i)
		{
			computeCmd.DispatchCompute(*manager->m_BloomUpsampleShader, groups(usW[i]), groups(usH[i]), 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_BloomUpsampleDescriptorSets[i] });
			barrier();
		}

		if (manager->m_BloomShapeShader != nullptr && !manager->m_BloomShapeDescriptorSets.empty())
		{
			VansTexture* base = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_BASE);
			VansTexture* result = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_RESULT);
			if (base != nullptr && result != nullptr)
			{
				UpdateBloomShapeDescriptorSets();
				computeCmd.EnsureComputeShader(
					*manager->m_BloomShapeShader,
					{ m_Scene->GetGlobalDescriptorSetLayout(), manager->m_BloomShapeSetLayout });
				computeCmd.DispatchCompute(
					*manager->m_BloomShapeShader,
					groups(w2), groups(h2), 1,
					{ m_Scene->GetGlobalDescriptorSet(), manager->m_BloomShapeDescriptorSets[0] });
				barrier();
			}
		}
	}

} // namespace VansGraphics

void VansGraphics::VansVKDevice::PrepareGIReceiverVisibility(uint32_t width, uint32_t height)
{
    auto& receiver = m_Scene->GetMaterialManager()->m_GIReceiverVisibility;
    const auto usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    const VkDeviceSize bytes = 16 + VkDeviceSize(width) * height * sizeof(VansGIReceiverVisibility::Record);
    if (!m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
        throw std::runtime_error("Failed to begin GI receiver visibility initialization");
    for (auto* buffer : {&receiver.current, &receiver.history, &receiver.world, &receiver.bias})
    {
        const VkDeviceSize allocation = buffer == &receiver.bias ? 16 + VkDeviceSize(width) * height * 32 :
            (buffer == &receiver.world ? VansGIReceiverVisibility::WorldBytes : bytes);
        if (!buffer->CreatVulkanBuffer(m_VansVKLogicDevice, allocation, VK_FORMAT_R32_UINT, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            throw std::runtime_error("Failed to allocate GI receiver visibility history");
        m_VansVKCommandBuffer.FillBuffer(buffer->GetNativeBuffer(), 0, allocation, 0u);
    }
    VkMemoryBarrier initialized{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    initialized.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    initialized.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    m_VansVKCommandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        {initialized});
    if (!m_VansVKCommandBuffer.EndCommandBufferRecord() ||
        !VansVKCommandBuffer::SubmitCommands(GetGraphicsQueue(), m_VansVKLogicDevice,
            {m_VansVKCommandBuffer.GetVKCommandBuffer()}, {}, {}, m_VansVKCommandBuffer.m_CommandBufferFinishSubmitFence) ||
        !m_VansVKCommandBuffer.ResetCommandBuffer(false))
        throw std::runtime_error("Failed to initialize GI receiver visibility history");
    if (!receiver.work.CreatVulkanBuffer(m_VansVKLogicDevice, VansGIReceiverVisibility::WorkBytes, VK_FORMAT_R32_UINT,
        usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) throw std::runtime_error("Failed to allocate GI receiver visibility work");
    if (!receiver.anchors.CreatVulkanBuffer(m_VansVKLogicDevice, VkDeviceSize(width) * height * 4, VK_FORMAT_R32_UINT,
        usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ||
        !receiver.worldClaims.CreatVulkanBuffer(m_VansVKLogicDevice, VkDeviceSize(VansGIReceiverVisibility::WorldCapacity) * 4,
            VK_FORMAT_R32_UINT, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        throw std::runtime_error("Failed to allocate GI receiver visibility cache indices");
    const VkShaderStageFlags stages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (uint32_t i = 0; i < 4; ++i) bindings.push_back({i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, stages, nullptr});
    bindings.push_back({4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, stages, nullptr});
    for (uint32_t i : {6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u})
        bindings.push_back({i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (i == 7u || i == 8u) ? 8u : 1u, stages, nullptr});
    if (!VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom(bindings, receiver.layout, receiver.sets))
        throw std::runtime_error("Failed to create GI receiver visibility descriptors");
    receiver.prepareShader = VansShaderManager::Get().FindComputeShader("GIReceiverVisibilityPrepare");
    receiver.reprojectShader = VansShaderManager::Get().FindComputeShader("GIReceiverVisibilityReproject");
    receiver.worldShader = VansShaderManager::Get().FindComputeShader("GIReceiverVisibilityWorldCache");
    if (!receiver.prepareShader || !receiver.reprojectShader || !receiver.worldShader)
        throw std::runtime_error("GI receiver visibility shaders are unavailable");
    receiver.frame = 0;
    receiver.enabled = std::getenv("FOREST_DISABLE_GI_RECEIVER_VISIBILITY") == nullptr;
    VANS_LOG("[GIReceiverVisibility] cache=" << width << "x" << height << ", rayBudget=" << VansGIReceiverVisibility::RayBudget
        << ", historyBytes=" << bytes * 2 << ", worldBytes=" << VansGIReceiverVisibility::WorldBytes
        << ", anchorBytes=" << receiver.anchors.GetBufferSize() << ", enabled=" << receiver.enabled);
}

namespace
{
void GIReceiverComputeDependency(VansGraphics::VansVKCommandBuffer& command)
{
    VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    memory.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    memory.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {memory});
}
// 优化 4 独立 GPU 阶段。移除此调用后，已清空的映射使 Prepare 自动使用基础历史查找。
void RecordGIReceiverStableAnchors(VansGraphics::VansVKCommandBuffer& command,
    VansGraphics::VansGIReceiverVisibility& receiver, VkDescriptorSetLayout globalLayout,
    VkDescriptorSet globalSet, const glm::uvec4& frame, const glm::mat4& viewProjection)
{
    struct alignas(16) Params { glm::uvec4 frame; glm::mat4 viewProjection; };
    const Params params{frame, viewProjection};
    command.EnsureComputeShader(*receiver.reprojectShader, {globalLayout, receiver.layout});
    command.DispatchCompute(*receiver.reprojectShader, (frame.x * frame.y + 63u) / 64u, 1, 1,
        {globalSet, receiver.sets[0]}, &params, sizeof(params));
    GIReceiverComputeDependency(command);
}
// 优化 5 生命周期：读取前维护离屏记录，追踪后用 Claim -> Publish 分离写入竞争。
void RecordGIReceiverWorldCache(VansGraphics::VansVKCommandBuffer& command,
    VansGraphics::VansGIReceiverVisibility& receiver, VkDescriptorSetLayout globalLayout,
    VkDescriptorSet globalSet, const glm::uvec4& frame, bool publish)
{
    struct alignas(16) Params { glm::uvec4 frame; glm::uvec4 operation; };
    command.EnsureComputeShader(*receiver.worldShader, {globalLayout, receiver.layout});
    if (publish)
    {
        VkMemoryBarrier recycle{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        recycle.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        recycle.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        command.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, {recycle});
        command.FillBuffer(receiver.worldClaims.GetNativeBuffer(), 0, receiver.worldClaims.GetBufferSize(), 0xffffffffu);
        GIReceiverComputeDependency(command);
        const Params claim{frame, glm::uvec4(1u, 0u, 0u, 0u)};
        command.DispatchCompute(*receiver.worldShader, (frame.x * frame.y + 63u) / 64u, 1, 1,
            {globalSet, receiver.sets[0]}, &claim, sizeof(claim));
        GIReceiverComputeDependency(command);
    }
    const Params params{frame, glm::uvec4(publish ? 2u : 0u, 0u, 0u, 0u)};
    command.DispatchCompute(*receiver.worldShader, (VansGraphics::VansGIReceiverVisibility::WorldCapacity + 63u) / 64u,
        1, 1, {globalSet, receiver.sets[0]}, &params, sizeof(params));
    GIReceiverComputeDependency(command);
}
}

void VansGraphics::VansVKDevice::UpdateGIReceiverVisibility(VansVKCommandBuffer& command, const glm::vec2& jitterDelta)
{
    auto* manager = m_Scene->GetMaterialManager();
    auto& receiver = manager->m_GIReceiverVisibility;
    if (!receiver.prepareShader || receiver.sets.empty()) return;
    // 启动时的诊断关闭使用已清零的 header，连预计算和历史拷贝也跳过，便于测量真实增量。
    if (!receiver.enabled) return;
    if (receiver.geometryRevision != (m_Scene->GetRayTracingGeometryRevision() ^ rayTracingContext.GetWorldGeometryRevision()))
    {
        receiver.frame = 0;
        receiver.geometryRevision = m_Scene->GetRayTracingGeometryRevision() ^ rayTracingContext.GetWorldGeometryRevision();
    }
    const uint32_t width = (m_RenderWidth + 3u) / 4u, height = (m_RenderHeight + 3u) / 4u;
    struct alignas(16) Params { glm::uvec4 frame; glm::vec4 jitter; };
    const Params params{glm::uvec4(width, height, receiver.frame, receiver.enabled && rayTracingContext.IsReady()),
        glm::vec4(jitterDelta, 0.0f, 0.0f)};
    auto barrier = [&command](VkPipelineStageFlags source, VkPipelineStageFlags destination, VkAccessFlags read, VkAccessFlags write)
    {
        VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        memory.srcAccessMask = read; memory.dstAccessMask = write;
        command.PipelineBarrier(source, destination, {memory});
    };
    const auto handle = command.GetVKCommandBuffer();
    VANS_GPU_SCOPE_LANE(handle, "DDGI.ReceiverVisibility",
        (m_AsyncComputeEnabled ? Vans::VansGpuQueueLane::Compute : Vans::VansGpuQueueLane::Graphics));
    // 同一 compute queue 上按 Prepare -> RT -> Cache 消费顺序执行，无 CPU 回读或新增提交等待。
    {
        VANS_GPU_SCOPE_LANE(handle, "DDGI.ReceiverVisibilityPrepare", (m_AsyncComputeEnabled ? Vans::VansGpuQueueLane::Compute : Vans::VansGpuQueueLane::Graphics));
        barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
            VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        // 注释下方 DDGI.ReceiverBias 独立阶段即可恢复原偏移；清零 header 防止读取旧测量。
        command.FillBuffer(receiver.bias.GetNativeBuffer(), 0, 16, 0u);
        command.FillBuffer(receiver.work.GetNativeBuffer(), 0, 16, 0u);
        command.FillBuffer(receiver.work.GetNativeBuffer(), 16, VansGIReceiverVisibility::WorkBytes - 16, 0xffffffffu);
        command.FillBuffer(receiver.anchors.GetNativeBuffer(), 0, receiver.anchors.GetBufferSize(), 0xffffffffu);
        barrier(VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        {
            VANS_GPU_SCOPE_LANE(handle, "DDGI.ReceiverStableAnchors", (m_AsyncComputeEnabled ? Vans::VansGpuQueueLane::Compute : Vans::VansGpuQueueLane::Graphics));
            RecordGIReceiverStableAnchors(command, receiver, m_Scene->GetGlobalDescriptorSetLayout(),
                m_Scene->GetGlobalDescriptorSet(), params.frame, m_CameraData.viewProjectionMatrix);
        }
        {
            VANS_GPU_SCOPE_LANE(handle, "DDGI.ReceiverWorldCacheMaintain", (m_AsyncComputeEnabled ? Vans::VansGpuQueueLane::Compute : Vans::VansGpuQueueLane::Graphics));
            RecordGIReceiverWorldCache(command, receiver, m_Scene->GetGlobalDescriptorSetLayout(),
                m_Scene->GetGlobalDescriptorSet(), params.frame, false);
        }
        // 独立模块入口：仅注释 recordReceiverBias()，消费者便按清零 header 使用原偏移。
        const auto recordReceiverBias = [&]()
        {
            if (params.frame.w != 0u)
            {
                VANS_GPU_SCOPE_LANE(handle, "DDGI.ReceiverBias", (m_AsyncComputeEnabled ? Vans::VansGpuQueueLane::Compute : Vans::VansGpuQueueLane::Graphics));
                barrier(VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
                if (!rayTracingContext.DispatchReceiverBias(this, command, m_Scene, receiver.layout, receiver.sets[0], width, height))
                    throw std::runtime_error("GI receiver bias pipeline is unavailable");
                GIReceiverComputeDependency(command);
            }
        };
        recordReceiverBias();
        command.EnsureComputeShader(*receiver.prepareShader, {m_Scene->GetGlobalDescriptorSetLayout(), receiver.layout});
        command.DispatchCompute(*receiver.prepareShader, (width + 7) / 8, (height + 7) / 8, 1,
            {m_Scene->GetGlobalDescriptorSet(), receiver.sets[0]}, &params, sizeof(params));
    }
    barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    if (params.frame.w != 0u)
    {
        VANS_GPU_SCOPE_LANE(handle, "DDGI.ReceiverVisibilityTrace", (m_AsyncComputeEnabled ? Vans::VansGpuQueueLane::Compute : Vans::VansGpuQueueLane::Graphics));
        if (!rayTracingContext.DispatchReceiverVisibility(this, command, m_Scene, receiver.layout, receiver.sets[0]))
            throw std::runtime_error("GI receiver visibility trace pipeline is unavailable");
    }
    GIReceiverComputeDependency(command);
    {
        VANS_GPU_SCOPE_LANE(handle, "DDGI.ReceiverWorldCachePublish", (m_AsyncComputeEnabled ? Vans::VansGpuQueueLane::Compute : Vans::VansGpuQueueLane::Graphics));
        RecordGIReceiverWorldCache(command, receiver, m_Scene->GetGlobalDescriptorSetLayout(),
            m_Scene->GetGlobalDescriptorSet(), params.frame, true);
    }
    barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT);
    command.CopyBuffer(receiver.current.GetNativeBuffer(), receiver.history.GetNativeBuffer(), 0, 0, receiver.current.GetBufferSize());
    rayTracingContext.CopyPublishedProbeStateHistory(command);
    barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    ++receiver.frame;
}
