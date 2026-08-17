#include "VansVKDevice.h"

#include "VansVKDescriptorManager.h"

#include "VansRenderPass.h"

#include "../VansScene.h"

#include "../VansPostProcessProfile.h"
#include "../../VansTimer.h"


#include <algorithm>

#include <cstddef>

#include <cmath>



namespace VansGraphics

{

	namespace
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
			for (const GIProbeRegionDesc* desc : BuildActiveGIRegionOrder(gi))
			{
				if (regionCount >= VANS_SSGI_MAX_GI_REGIONS)
					continue;
				const GIResolvedRegion region = ResolveGIRegion(*desc);
				SSGIRegionParamsGPU& destination = data.regions[regionCount++];
				destination.volumeMin = glm::vec4(region.volumeMin, 0.0f);
				destination.volumeSizeAndBias = glm::vec4(region.volumeSize, region.normalBias);
				destination.traceParams = glm::vec4(region.maxRayDistance, 0.75f, region.volumeFadeDistance, 0.0f);
				destination.gridDimensionsAndPriority = glm::vec4(glm::vec3(region.gridDimensions), region.priority);
			}
			data.regionInfo.x = static_cast<float>(regionCount);
			data.deferredProbeDebug = glm::vec4(
				IsGIProbeOnlyDeferredOutputEnabled(gi) ? 1.0f : 0.0f,
				gi.probeOnlyDeferredExposure,
				0.0f,
				0.0f);
			return data;
		}

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

	void VansVKDevice::UploadSSGIParamsFromGISettings()
	{
		if (m_Scene == nullptr)
			return;

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager == nullptr || manager->m_SSGICBBuffer.GetNativeBuffer() == VK_NULL_HANDLE)
			return;

		const SSGIParamsGPU data = BuildSSGIParamsFromGISettings(
			m_Scene->GetGISettings(),
			m_RenderWidth,
			m_RenderHeight);
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


	void VansVKDevice::TemporalFilterSSGI(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)

	{

		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		uint32_t writeIdx = manager->m_SSGITemporalFrame % 2;

		SSGITemporalParamsGPU temporalData{};
		const float width = static_cast<float>(std::max(m_RenderWidth, 1u));
		const float height = static_cast<float>(std::max(m_RenderHeight, 1u));
		temporalData.screenSize = glm::vec4(width, height, 1.0f / width, 1.0f / height);
		temporalData.frameParams = glm::vec4(static_cast<float>(manager->m_SSGITemporalFrame), 0.0f, 0.0f, 0.0f);
		manager->m_SSGITemporalCBBuffer.SetBufferData(&temporalData, 0, sizeof(temporalData));



		computeCmd.EnsureComputeShader(*manager->m_SSGITemporalShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_SSGITemporalSetLayout });

		computeCmd.DispatchCompute(*manager->m_SSGITemporalShader, (m_RenderWidth + 7) / 8, (m_RenderHeight + 7) / 8, 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_SSGITemporalDescriptorSets[writeIdx] });

	}



	void VansVKDevice::BilateralFilterSSGI(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)

	{

		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		uint32_t writeIdx = manager->m_SSGITemporalFrame % 2;

		uint32_t bilateralSetIdx = (writeIdx == 0) ? 1 : 2;

		manager->m_BilateralFilterPushConstant.sigmaSpace = 2.0f;

		manager->m_BilateralFilterPushConstant.sigmaDepth = 0.12f;

		manager->m_BilateralFilterPushConstant.radius = 3;

		manager->m_BilateralFilterPushConstant.depthThreshold = 0.20f;

		manager->m_BilateralFilterPushConstant.depthMode = 0;

		manager->m_BilateralFilterShader->SetPushConstantData(&(manager->m_BilateralFilterPushConstant));

		computeCmd.EnsureComputeShader(*manager->m_BilateralFilterShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_BilateralFilterSetLayout });

		computeCmd.DispatchCompute(*manager->m_BilateralFilterShader, (m_RenderWidth + 7) / 8, (m_RenderHeight + 7) / 8, 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_BilateralFilterDescriptorSets[bilateralSetIdx] });

	}



	void VansVKDevice::BilateralFilterSSAO(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)

	{

		uint32_t halfResWidth = m_RenderWidth / 2;

		uint32_t halfResHeight = m_RenderHeight / 2;



		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		manager->m_BilateralFilterPushConstant.sigmaSpace = 3.0f;

		manager->m_BilateralFilterPushConstant.sigmaDepth = 0.08f;

		manager->m_BilateralFilterPushConstant.radius = 4;

		manager->m_BilateralFilterPushConstant.depthThreshold = 0.18f;

		manager->m_BilateralFilterPushConstant.depthMode = 2;

		manager->m_BilateralFilterShader->SetPushConstantData(&(manager->m_BilateralFilterPushConstant));

		computeCmd.EnsureComputeShader(*manager->m_BilateralFilterShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_BilateralFilterSetLayout });

		computeCmd.DispatchCompute(*manager->m_BilateralFilterShader, (halfResWidth + 7) / 8, (halfResHeight + 7) / 8, 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_BilateralFilterDescriptorSets[0] });

	}



	void VansVKDevice::UpdateGIDataDescriptorSets(VansRenderPassManager* renderPassManager)

	{

		VansMaterialManager* manager = m_Scene->GetMaterialManager();



		if (IsFeatureDescriptorCurrent(m_GIDataDescSetGeneration))

		{

			return;

		}



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

		VansTexture* ssaoResult = getRuntimeTexture(VansMaterialManager::RT_SSAO_RESULT);

		VansTexture* ssaoFilterResult = getRuntimeTexture(VansMaterialManager::RT_SSAO_FILTER_RESULT);

		VansTexture* ssgiFilterResult = getRuntimeTexture(VansMaterialManager::RT_SSGI_FILTER_RESULT);



		if (ssgiResult == nullptr || giIrradianceAtlases.empty() || giVisibilityAtlases.empty() ||
			giProbeStateBuffers.empty() || hzbResult == nullptr || ssgiTemporalA == nullptr || ssgiTemporalB == nullptr ||

			ssaoResult == nullptr || ssaoFilterResult == nullptr || ssgiFilterResult == nullptr ||
			ssgiMomentsA == nullptr || ssgiMomentsB == nullptr ||
			ssgiSurfaceHistoryA == nullptr || ssgiSurfaceHistoryB == nullptr)

		{

			return;

		}



		MarkFeatureDescriptorCurrent(m_GIDataDescSetGeneration);



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

						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

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

						manager->m_PreConvDiffuse->GetImage().GetSampler(),

						manager->m_PreConvDiffuse->GetImage().GetImageView(),

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
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGIDescriptorSets[0], SSGI_BINDING_GI_IRRADIANCE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, irradianceInfos, 0);
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGIDescriptorSets[0], SSGI_BINDING_GI_VISIBILITY, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, visibilityInfos, 0);
		for (size_t regionSlot = 0; regionSlot < probeStateInfos.size(); ++regionSlot)
		{
			VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(
				manager->m_SSGIDescriptorSets[0],
				SSGI_BINDING_GI_PROBE_STATE + static_cast<uint32_t>(regionSlot),
				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				{ probeStateInfos[regionSlot] },
				0);
		}

		VansVKDescriptorManager::GetInstance()->CommitDescriptorUpdates();



		auto& motionVector = renderPassManager->GetMotionVector();

		VansVKDescriptorManager::GetInstance()->BeginDescriptorUpdate();

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_DEPTH, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { depth.GetSampler(), depth.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }, 0);

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

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_DEPTH, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { depth.GetSampler(), depth.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }, 0);

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



		VansVKDescriptorManager::GetInstance()->BeginDescriptorUpdate();

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_BilateralFilterDescriptorSets[0], PassBinding::TEXTURE_0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						ssaoResult->GetImage().GetSampler(),

						ssaoResult->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_BilateralFilterDescriptorSets[0], PassBinding::TEXTURE_1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						positionGbuffer.GetSampler(),

						positionGbuffer.GetImageView(),

						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_BilateralFilterDescriptorSets[0], PassBinding::UAV_IMAGE_1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {

					{

						ssaoFilterResult->GetImage().GetSampler(),

						ssaoFilterResult->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);



		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_BilateralFilterDescriptorSets[1], PassBinding::TEXTURE_0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						ssgiTemporalA->GetImage().GetSampler(),

						ssgiTemporalA->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_BilateralFilterDescriptorSets[1], PassBinding::TEXTURE_1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						depth.GetSampler(),

						depth.GetImageView(),

						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_BilateralFilterDescriptorSets[1], PassBinding::UAV_IMAGE_1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {

					{

						ssgiFilterResult->GetImage().GetSampler(),

						ssgiFilterResult->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);



		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_BilateralFilterDescriptorSets[2], PassBinding::TEXTURE_0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						ssgiTemporalB->GetImage().GetSampler(),

						ssgiTemporalB->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_BilateralFilterDescriptorSets[2], PassBinding::TEXTURE_1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						depth.GetSampler(),

						depth.GetImageView(),

						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_BilateralFilterDescriptorSets[2], PassBinding::UAV_IMAGE_1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {

					{

						ssgiFilterResult->GetImage().GetSampler(),

						ssgiFilterResult->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->CommitDescriptorUpdates();

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
		auto& depth = renderPassManager->GetDepth();
		auto& material = renderPassManager->GetGbuffer1();
		auto* descriptorManager = VansVKDescriptorManager::GetInstance();
		auto bindPass = [&](VkDescriptorSet set, VansTexture* input, VansTexture* output)
		{
			descriptorManager->WriteImageDescriptor(set, SSGI_ATROUS_BINDING_INPUT_GI,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ input->GetImage().GetSampler(), input->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
			descriptorManager->WriteImageDescriptor(set, SSGI_ATROUS_BINDING_NORMAL,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ normal.GetSampler(), normal.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
			descriptorManager->WriteImageDescriptor(set, SSGI_ATROUS_BINDING_DEPTH,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ depth.GetSampler(), depth.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
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
		for (uint32_t iteration = 0u; iteration < 2u; ++iteration)
		{
			SSGIAtrousPushConstants params{};
			params.stepWidth = 1u << iteration;
			params.depthSigma = 0.04f;
			params.normalPower = 32.0f;
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
		if (manager == nullptr || manager->m_MainCameraHiZCullDescriptorSets.empty())
			return;

		VansTexture* hzbResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_OCCLUSION_RESULT);
		if (hzbResult == nullptr)
			return;

		VansVKBuffer& objectBuffer = m_Scene->GetMainCameraCullObjectBuffer();
		VansVKBuffer& visibilityBuffer = m_Scene->GetMainCameraVisibilityBuffer();
		if (objectBuffer.GetNativeBuffer() == VK_NULL_HANDLE || visibilityBuffer.GetNativeBuffer() == VK_NULL_HANDLE)
			return;

		VansVKDescriptorManager::GetInstance()->BeginDescriptorUpdate();
		VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(
			manager->m_MainCameraHiZCullDescriptorSets[0],
			MAIN_CAMERA_HIZ_CULL_BINDING_OBJECTS,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ objectBuffer.GetNativeBuffer(), 0, objectBuffer.GetBufferSize() }});
		VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(
			manager->m_MainCameraHiZCullDescriptorSets[0],
			MAIN_CAMERA_HIZ_CULL_BINDING_VISIBILITY,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ visibilityBuffer.GetNativeBuffer(), 0, visibilityBuffer.GetBufferSize() }});
		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(
			manager->m_MainCameraHiZCullDescriptorSets[0],
			MAIN_CAMERA_HIZ_CULL_BINDING_HIZ,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ hzbResult->GetImage().GetSampler(), hzbResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		VansVKDescriptorManager::GetInstance()->CommitDescriptorUpdates();
		MarkFeatureDescriptorCurrent(m_MainCameraHiZCullDescSetGeneration);
	}

	void VansVKDevice::UpdateMainCameraHiZCull(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)

	{
		if (m_Scene == nullptr || !m_Scene->HasMainCameraHiZCullCandidates())
			return;

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager == nullptr || manager->m_MainCameraHiZCullShader == nullptr ||
			manager->m_MainCameraHiZCullSetLayout == VK_NULL_HANDLE ||
			manager->m_MainCameraHiZCullDescriptorSets.empty())
		{
			return;
		}
		VansTexture* hzbResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_OCCLUSION_RESULT);
		if (hzbResult == nullptr)
			return;
		if (!m_Scene->UploadMainCameraCullCandidates(*this))
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
		pc.objectCount = m_Scene->GetMainCameraHiZCullCandidateCount();
		pc.hizMipCount = manager->m_HIZMipCount;
		pc.hizEnabled = 1;
		pc.frameIndex = static_cast<uint32_t>(m_RenderFrameNumber);
		pc.depthBiasMeters = m_Scene->GetMainCameraHiZCullSettings().depthBiasMeters;
		pc.maxScreenCoverageForCull = m_Scene->GetMainCameraHiZCullSettings().maxScreenCoverageForCull;

		const uint32_t groups = (pc.objectCount + 63u) / 64u;
		computeCmd.EnsureComputeShader(*manager->m_MainCameraHiZCullShader,
			{ m_Scene->GetGlobalDescriptorSetLayout(), manager->m_MainCameraHiZCullSetLayout });
		computeCmd.DispatchCompute(*manager->m_MainCameraHiZCullShader,
			groups, 1, 1,
			{ m_Scene->GetGlobalDescriptorSet(), manager->m_MainCameraHiZCullDescriptorSets[0] },
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

		m_Scene->MarkMainCameraHiZCullDispatched();
	}



	void VansVKDevice::UpdateSSRDescriptorSets(VansRenderPassManager* renderPassManager)

	{

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (renderPassManager == nullptr || manager == nullptr)
		{
			return;
		}

		const bool ssrResourcesReady =
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
			manager->m_SSRAADescriptorSets[0] != VK_NULL_HANDLE;
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

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->CommitDescriptorUpdates();

		MarkFeatureDescriptorCurrent(m_SSRDescSetGeneration);

	}



	void VansVKDevice::UpdateVolumetricFogSets(VansRenderPassManager* renderPassManager)

	{

		VansMaterialManager* manager = m_Scene->GetMaterialManager();



		if (IsFeatureDescriptorCurrent(m_VolumetricFogDescSetGeneration))

		{

			return;

		}



		VansTexture* volumetricFogResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_VOLUMETRIC_FOG_RESULT);

		if (volumetricFogResult == nullptr)

		{

			return;

		}



		MarkFeatureDescriptorCurrent(m_VolumetricFogDescSetGeneration);



		auto& position = renderPassManager->GetGbuffer2();



		VansVKDescriptorManager::GetInstance()->BeginDescriptorUpdate();



		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_VolumetricFogDescriptorSets[0], FOG_BINDING_POSITION, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						position.GetSampler(),

						position.GetImageView(),

						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL

					}

				}, 0);



		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_VolumetricFogDescriptorSets[0], FOG_BINDING_RESULT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {

					{

						volumetricFogResult->GetImage().GetSampler(),

						volumetricFogResult->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);



		VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(manager->m_VolumetricFogDescriptorSets[0], FOG_BINDING_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {

					{

						manager->m_FogParamsCBBuffer.GetNativeBuffer(),

						0,

						manager->m_FogParamsCBBuffer.GetBufferSize()

					}

				}, 0);



		VansTexture* fogVoxelRayMarch = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_RAYMARCH);

		if (fogVoxelRayMarch != nullptr)

		{

			VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_VolumetricFogDescriptorSets[0], FOG_BINDING_VOXEL_VOLUME, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

						{

							fogVoxelRayMarch->GetImage().GetSampler(),

							fogVoxelRayMarch->GetImage().GetImageView(),

							VK_IMAGE_LAYOUT_GENERAL

						}

					}, 0);

		}



		VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(manager->m_VolumetricFogDescriptorSets[0], FOG_BINDING_VOLUME_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {

					{

						manager->m_FogVolumeParamsCBBuffer.GetNativeBuffer(),

						0,

						manager->m_FogVolumeParamsCBBuffer.GetBufferSize()

					}

				}, 0);



		VansVKDescriptorManager::GetInstance()->CommitDescriptorUpdates();

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
		if (hzb == nullptr || out == nullptr || manager->m_ScreenSpaceShadowDescriptorSets.empty()) return;

		auto& normal = renderPassManager->GetNormal();
		auto& gbuffer2 = renderPassManager->GetGbuffer2();
		auto* desc = VansVKDescriptorManager::GetInstance();
		desc->BeginDescriptorUpdate();
		desc->WriteImageDescriptor(manager->m_ScreenSpaceShadowDescriptorSets[0], SSS_BINDING_NORMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ normal.GetSampler(), normal.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
		desc->WriteImageDescriptor(manager->m_ScreenSpaceShadowDescriptorSets[0], SSS_BINDING_GBUFFER2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ gbuffer2.GetSampler(), gbuffer2.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
		desc->WriteImageDescriptor(manager->m_ScreenSpaceShadowDescriptorSets[0], SSS_BINDING_HIZ, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ hzb->GetImage().GetSampler(), hzb->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		desc->WriteImageDescriptor(manager->m_ScreenSpaceShadowDescriptorSets[0], SSS_BINDING_RESULT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {{ out->GetImage().GetSampler(), out->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		desc->WriteBufferDescriptor(manager->m_ScreenSpaceShadowDescriptorSets[0], SSS_BINDING_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {{ manager->m_ScreenSpaceShadowParamsCBBuffer.GetNativeBuffer(), 0, manager->m_ScreenSpaceShadowParamsCBBuffer.GetBufferSize() }});
		desc->CommitDescriptorUpdates();
		MarkFeatureDescriptorCurrent(m_ScreenSpaceShadowDescSetGeneration);
	}

	void VansVKDevice::UpdateScreenSpaceShadow(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		UpdateScreenSpaceShadowSets(renderPassManager);
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager->m_ScreenSpaceShadowShader == nullptr || manager->m_ScreenSpaceShadowDescriptorSets.empty()) return;
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
		VansLightManager* lightManager = m_Scene->GetLightManager();
		if (manager == nullptr || lightManager == nullptr || renderPassManager == nullptr)
			return;

		if (!lightManager->GetPunctualShadowManager().ConsumeDebugPreviewRefreshRequest())
			return;

		VansTexture* preview = manager->GetRuntimeRenderTexture(
			VansMaterialManager::RT_PUNCTUAL_SHADOW_DEBUG_PREVIEW);
		if (preview == nullptr || manager->m_PunctualShadowDebugShader == nullptr ||
			manager->m_PunctualShadowDebugDescriptorSets.empty())
			return;

		auto& atlas = renderPassManager->GetPunctualShadowMap();
		auto* desc = VansVKDescriptorManager::GetInstance();
		desc->BeginDescriptorUpdate();
		// Use a non-comparison sampler. Comparison samplers are reserved for the
		// production shadow test and cannot expose raw D32 values to this resolve.
		desc->WriteImageDescriptor(
			manager->m_PunctualShadowDebugDescriptorSets[0],
			PUNCTUAL_SHADOW_DEBUG_BINDING_ATLAS,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ renderPassManager->GetCascadeShadowSampler(), atlas.GetImageView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL }});
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
			!IsFeatureDescriptorCurrent(m_SSRDescSetGeneration))
		{
			return;
		}

		computeCmd.EnsureComputeShader(*manager->m_SSRTraceShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_SSRTraceSetLayout });
		computeCmd.DispatchCompute(*manager->m_SSRTraceShader, (m_RenderWidth + 7) / 8, (m_RenderHeight + 7) / 8, 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_SSRTraceDescriptorSets[0] });
		RecordShaderWriteToReadMemoryDependency(computeCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		computeCmd.EnsureComputeShader(*manager->m_SSRResolveShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_SSRResolveSetLayout });
		computeCmd.DispatchCompute(*manager->m_SSRResolveShader, (m_RenderWidth + 7) / 8, (m_RenderHeight + 7) / 8, 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_SSRResolveDescriptorSets[0] });
		RecordShaderWriteToReadMemoryDependency(computeCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		computeCmd.EnsureComputeShader(*manager->m_SSRTemporalAAShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_SSRAASetLayout });
		computeCmd.DispatchCompute(*manager->m_SSRTemporalAAShader, (m_RenderWidth + 7) / 8, (m_RenderHeight + 7) / 8, 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_SSRAADescriptorSets[0] });
	}

	void VansVKDevice::UpdateFogLightInjectionSets(VansRenderPassManager* renderPassManager)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (IsFeatureDescriptorCurrent(m_FogLightInjectionDescSetGeneration)) return;
		if (manager->m_FogLightInjectionDescriptorSets.size() < 2) return;

		VansTexture* fogVoxelInjection = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_INJECTION);
		VansTexture* fogVoxelHistory = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_INJECTION_HISTORY);
		if (fogVoxelInjection == nullptr || fogVoxelHistory == nullptr) return;

		VansTexture* voxelTargets[2] = { fogVoxelInjection, fogVoxelHistory };
		VansTexture* voxelHistory[2] = { fogVoxelHistory, fogVoxelInjection };
		auto* desc = VansVKDescriptorManager::GetInstance();
		for (uint32_t i = 0; i < 2; ++i)
		{
			desc->BeginDescriptorUpdate();
			desc->WriteImageDescriptor(manager->m_FogLightInjectionDescriptorSets[i], FOG_INJECT_BINDING_VOXEL_GRID, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {{ voxelTargets[i]->GetImage().GetSampler(), voxelTargets[i]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
			desc->WriteImageDescriptor(manager->m_FogLightInjectionDescriptorSets[i], FOG_INJECT_BINDING_SHADOW_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ renderPassManager->GetCascadeShadowSampler(), renderPassManager->GetCascadeShadowArrayView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
			desc->WriteBufferDescriptor(manager->m_FogLightInjectionDescriptorSets[i], FOG_INJECT_BINDING_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {{ manager->m_FogVolumeParamsCBBuffer.GetNativeBuffer(), 0, manager->m_FogVolumeParamsCBBuffer.GetBufferSize() }});
			desc->WriteImageDescriptor(manager->m_FogLightInjectionDescriptorSets[i], FOG_INJECT_BINDING_HISTORY, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ voxelHistory[i]->GetImage().GetSampler(), voxelHistory[i]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
			auto& punctualShadow = renderPassManager->GetPunctualShadowMap();
			desc->WriteImageDescriptor(manager->m_FogLightInjectionDescriptorSets[i], FOG_INJECT_BINDING_PUNCTUAL_SHADOW, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ punctualShadow.GetSampler(), punctualShadow.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
			desc->CommitDescriptorUpdates();
		}
		MarkFeatureDescriptorCurrent(m_FogLightInjectionDescSetGeneration);
	}

	void VansVKDevice::UpdateFogRayMarchSets()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (IsFeatureDescriptorCurrent(m_FogRayMarchDescSetGeneration)) return;
		if (manager->m_FogRayMarchDescriptorSets.size() < 2) return;
		VansTexture* fogVoxelInjection = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_INJECTION);
		VansTexture* fogVoxelHistory = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_INJECTION_HISTORY);
		VansTexture* fogVoxelRayMarch = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_RAYMARCH);
		if (fogVoxelInjection == nullptr || fogVoxelHistory == nullptr || fogVoxelRayMarch == nullptr) return;
		VansTexture* voxelInputs[2] = { fogVoxelInjection, fogVoxelHistory };
		auto* desc = VansVKDescriptorManager::GetInstance();
		for (uint32_t i = 0; i < 2; ++i)
		{
			desc->BeginDescriptorUpdate();
			desc->WriteImageDescriptor(manager->m_FogRayMarchDescriptorSets[i], FOG_MARCH_BINDING_INPUT_VOXEL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ voxelInputs[i]->GetImage().GetSampler(), voxelInputs[i]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
			desc->WriteImageDescriptor(manager->m_FogRayMarchDescriptorSets[i], FOG_MARCH_BINDING_RESULT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {{ fogVoxelRayMarch->GetImage().GetSampler(), fogVoxelRayMarch->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
			desc->WriteBufferDescriptor(manager->m_FogRayMarchDescriptorSets[i], FOG_MARCH_BINDING_VOLUME_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {{ manager->m_FogVolumeParamsCBBuffer.GetNativeBuffer(), 0, manager->m_FogVolumeParamsCBBuffer.GetBufferSize() }});
			desc->CommitDescriptorUpdates();
		}
		MarkFeatureDescriptorCurrent(m_FogRayMarchDescSetGeneration);
	}

	void VansVKDevice::UpdateFogLightInjection(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd, uint32_t frameIdx)
	{
		UpdateFogLightInjectionSets(renderPassManager);
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager->m_FogLightInjectionShader == nullptr || manager->m_FogLightInjectionDescriptorSets.size() < 2) return;
		VansTexture* fogTarget = manager->GetRuntimeRenderTexture(
			frameIdx == 0 ? VansMaterialManager::RT_FOG_VOXEL_INJECTION : VansMaterialManager::RT_FOG_VOXEL_INJECTION_HISTORY);
		if (fogTarget == nullptr) return;
		computeCmd.EnsureComputeShader(*manager->m_FogLightInjectionShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_FogLightInjectionSetLayout });
		computeCmd.DispatchCompute(*manager->m_FogLightInjectionShader,
			(fogTarget->GetWidth() + 3) / 4,
			(fogTarget->GetHeight() + 3) / 4,
			(fogTarget->GetSlice() + 3) / 4,
			{ m_Scene->GetGlobalDescriptorSet(), manager->m_FogLightInjectionDescriptorSets[frameIdx] });
	}

	void VansVKDevice::UpdateFogRayMarch(VansVKCommandBuffer& computeCmd, uint32_t frameIdx)
	{
		UpdateFogRayMarchSets();
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager->m_FogRayMarchShader == nullptr || manager->m_FogRayMarchDescriptorSets.size() < 2) return;
		VansTexture* fogVoxelRayMarch = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_RAYMARCH);
		if (fogVoxelRayMarch == nullptr) return;
		computeCmd.EnsureComputeShader(*manager->m_FogRayMarchShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_FogRayMarchSetLayout });
		computeCmd.DispatchCompute(*manager->m_FogRayMarchShader,
			(fogVoxelRayMarch->GetWidth() + 7) / 8,
			(fogVoxelRayMarch->GetHeight() + 7) / 8,
			1,
			{ m_Scene->GetGlobalDescriptorSet(), manager->m_FogRayMarchDescriptorSets[frameIdx] });
	}

	void VansVKDevice::UpdateCloudRayMarchSets(VansRenderPassManager* renderPassManager)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (IsFeatureDescriptorCurrent(m_CloudRayMarchDescSetGeneration)) return;
		if (manager->m_CloudRayMarchDescriptorSets.empty()) return;
		VansTexture* result = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_CLOUD_BUFFER);
		VansTexture* mainNoise = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_CLOUD_MAIN_NOISE);
		VansTexture* detailNoise = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_CLOUD_DETAIL_NOISE);
		if (result == nullptr || mainNoise == nullptr || detailNoise == nullptr) return;
		auto* desc = VansVKDescriptorManager::GetInstance();
		desc->BeginDescriptorUpdate();
		desc->WriteImageDescriptor(manager->m_CloudRayMarchDescriptorSets[0], CLOUD_MARCH_BINDING_RESULT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {{ result->GetImage().GetSampler(), result->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		desc->WriteBufferDescriptor(manager->m_CloudRayMarchDescriptorSets[0], CLOUD_MARCH_BINDING_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {{ manager->m_CloudParamsCBBuffer.GetNativeBuffer(), 0, manager->m_CloudParamsCBBuffer.GetBufferSize() }});
		desc->WriteImageDescriptor(manager->m_CloudRayMarchDescriptorSets[0], CLOUD_MARCH_BINDING_MAIN_NOISE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ mainNoise->GetImage().GetSampler(), mainNoise->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
		desc->WriteImageDescriptor(manager->m_CloudRayMarchDescriptorSets[0], CLOUD_MARCH_BINDING_DETAIL_NOISE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ detailNoise->GetImage().GetSampler(), detailNoise->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
		desc->CommitDescriptorUpdates();
		MarkFeatureDescriptorCurrent(m_CloudRayMarchDescSetGeneration);
	}

	void VansVKDevice::UpdateCloudRayMarch(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		UpdateCloudRayMarchSets(renderPassManager);
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager->m_CloudRayMarchShader == nullptr || manager->m_CloudRayMarchDescriptorSets.empty()) return;
		computeCmd.EnsureComputeShader(*manager->m_CloudRayMarchShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_CloudRayMarchSetLayout });
		computeCmd.DispatchCompute(*manager->m_CloudRayMarchShader, ((m_RenderWidth + 3) / 4 + 7) / 8, ((m_RenderHeight + 3) / 4 + 7) / 8, 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_CloudRayMarchDescriptorSets[0] });
	}

	void VansVKDevice::UpdateVolumetricFog(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		VansTexture* fogVoxelInjection = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_INJECTION);
		VansTexture* fogVoxelHistory = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_INJECTION_HISTORY);
		VansTexture* fogVoxelRayMarch = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_RAYMARCH);
		VansTexture* volumetricFogResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_VOLUMETRIC_FOG_RESULT);
		if (fogVoxelInjection == nullptr || fogVoxelHistory == nullptr || fogVoxelRayMarch == nullptr || volumetricFogResult == nullptr ||
			manager->m_FogLightInjectionShader == nullptr || manager->m_FogRayMarchShader == nullptr || manager->m_VolumetrcFogShader == nullptr ||
			manager->m_FogLightInjectionDescriptorSets.size() < 2 || manager->m_FogRayMarchDescriptorSets.size() < 2 ||
			manager->m_VolumetricFogDescriptorSets.empty())
		{
			manager->m_FogHistoryValid = false;
			return;
		}

		// padding is reserved as a GPU-only history-valid flag; keep the public settings unchanged.
		VansFogVolumeSettings gpuSettings = manager->GetFogVolumeSettings();
		gpuSettings.padding = manager->m_FogHistoryValid ? 1.0f : 0.0f;
		manager->m_FogVolumeParamsCBBuffer.SetBufferData(&gpuSettings, 0, sizeof(gpuSettings));

		const uint32_t frameIdx = manager->m_FogTemporalFrame & 1u;
		UpdateFogLightInjection(renderPassManager, computeCmd, frameIdx);
		RecordShaderWriteToReadMemoryDependency(computeCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		UpdateFogRayMarch(computeCmd, frameIdx);
		RecordShaderWriteToReadMemoryDependency(computeCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		UpdateVolumetricFogSets(renderPassManager);
		if (manager->m_VolumetrcFogShader == nullptr || manager->m_VolumetricFogDescriptorSets.empty()) return;
		computeCmd.EnsureComputeShader(*manager->m_VolumetrcFogShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_VolumetricFogSetLayout });
		computeCmd.DispatchCompute(*manager->m_VolumetrcFogShader,
			(volumetricFogResult->GetWidth() + 7) / 8,
			(volumetricFogResult->GetHeight() + 7) / 8,
			1,
			{ m_Scene->GetGlobalDescriptorSet(), manager->m_VolumetricFogDescriptorSets[0] });

		manager->m_FogTemporalFrame = frameIdx ^ 1u;
		manager->m_FogHistoryValid = true;
	}

	void VansVKDevice::UpdateGIData(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		UpdateGIDataDescriptorSets(renderPassManager);
		if (!IsFeatureDescriptorCurrent(m_GIDataDescSetGeneration))
			return;

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager == nullptr ||
			manager->m_SSGIShader == nullptr ||
			manager->m_SSGITemporalShader == nullptr ||
			manager->m_SSGIAtrousShader == nullptr)
		{
			return;
		}

		UpdateSSGI(renderPassManager, computeCmd);
		RecordShaderWriteToReadMemoryDependency(
			computeCmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

		TemporalFilterSSGI(renderPassManager, computeCmd);
		RecordShaderWriteToReadMemoryDependency(
			computeCmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

		BilateralFilterSSAO(renderPassManager, computeCmd);
		RecordShaderWriteToReadMemoryDependency(
			computeCmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

		AtrousFilterSSGI(renderPassManager, computeCmd);
		RecordShaderWriteToReadMemoryDependency(
			computeCmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
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
	}

	void VansVKDevice::UploadPostProcessProfileIfDirty()
	{
		if (m_Scene == nullptr) return;
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager == nullptr) return;

		VansPostProcessProfile& profile = manager->m_PostProcessProfile;
		const float deltaTime = static_cast<float>(VansGraphics::VansTimer::GetEditorDeltaTime());

		VansPostProcessParamsGPU ppParams = profile.ToGPUParams();
		ppParams.m_DebugPassthrough = IsGIProbeOnlyDeferredOutputEnabled(m_Scene->GetGISettings()) ? 1.0f : 0.0f;
		VansExposureAdaptParamsGPU exposureParams = profile.ToExposureAdaptParams(deltaTime);
		manager->m_PostProcessParamsCBBuffer.SetBufferData(&ppParams, 0, sizeof(VansPostProcessParamsGPU));
		manager->m_ExposureAdaptParamsCBBuffer.SetBufferData(
			&exposureParams, 0, sizeof(VansExposureAdaptParamsGPU));

		if (profile.m_IsDirty)
		{
			VansBloomParamsGPU bloomParams = profile.ToBloomParams();
			manager->m_BloomParamsCBBuffer.SetBufferData(&bloomParams, 0, sizeof(VansBloomParamsGPU));
			VansBloomShapeParamsGPU bloomShapeParams = profile.ToBloomShapeParams();
			manager->m_BloomShapeParamsCBBuffer.SetBufferData(
				&bloomShapeParams, 0, sizeof(VansBloomShapeParamsGPU));
			VansDepthOfFieldParamsGPU dofParams = profile.ToDepthOfFieldParams(m_RenderWidth, m_RenderHeight);
			manager->m_DepthOfFieldParamsCBBuffer.SetBufferData(
				&dofParams, 0, sizeof(VansDepthOfFieldParamsGPU));
			m_PPBloomDescSetGeneration = 0;
			profile.m_IsDirty = false;
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
		VansTexture* fsrExposure = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_FSR_EXPOSURE);
		if (luminance == nullptr || exposure == nullptr || fsrExposure == nullptr) return;

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
			{{ fsrExposure->GetImage().GetSampler(), fsrExposure->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
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

		if (manager->m_PostProcessProfile.m_EnableAutoExposure)
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
		if (!manager->m_PostProcessProfile.m_EnableDOF) return;
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
		const bool useDOFSource = manager->m_PostProcessProfile.m_EnableDOF && dofResult != nullptr;
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
		if (!manager->m_PostProcessProfile.m_EnableBloom) return;
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
