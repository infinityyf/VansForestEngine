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
			const glm::vec3 volumeSize = glm::vec3(gi.gridDimensions) * gi.probeSpacingAxes;
			const glm::vec3 volumeMin = gi.regionCenter - volumeSize * 0.5f;

			SSGIParamsGPU data{};
			data.screenSize = glm::vec4(width, height, 1.0f / width, 1.0f / height);
			data.giVolumeMin = glm::vec4(volumeMin, 0.0f);
			data.giVolumeSizeAndBias = glm::vec4(volumeSize, gi.normalBias);
			data.traceParams = glm::vec4(gi.maxRayDistance, 0.75f, gi.volumeFadeDistance, 0.0f);
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

		computeCmd.EnsureComputeShader(*manager->m_SSGIShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_SSGITexSetLayout });

		computeCmd.DispatchCompute(*manager->m_SSGIShader, (m_RenderWidth + 7) / 8, (m_RenderHeight + 7) / 8, 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_SSGIDescriptorSets[0] });

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

		VansTexture* shrResult = getRuntimeTexture(VansMaterialManager::RT_SH_R_RESULT);

		VansTexture* shgResult = getRuntimeTexture(VansMaterialManager::RT_SH_G_RESULT);

		VansTexture* shbResult = getRuntimeTexture(VansMaterialManager::RT_SH_B_RESULT);

		VansTexture* giVisibilityAtlas = getRuntimeTexture(VansMaterialManager::RT_GI_VISIBILITY_ATLAS);

		VansTexture* hzbResult = getRuntimeTexture(VansMaterialManager::RT_HZB_RESULT);

		VansTexture* ssgiTemporalA = getRuntimeTexture(VansMaterialManager::RT_SSGI_TEMPORAL_A);

		VansTexture* ssgiTemporalB = getRuntimeTexture(VansMaterialManager::RT_SSGI_TEMPORAL_B);

		VansTexture* ssaoResult = getRuntimeTexture(VansMaterialManager::RT_SSAO_RESULT);

		VansTexture* ssaoFilterResult = getRuntimeTexture(VansMaterialManager::RT_SSAO_FILTER_RESULT);

		VansTexture* ssgiFilterResult = getRuntimeTexture(VansMaterialManager::RT_SSGI_FILTER_RESULT);



		if (ssgiResult == nullptr || shrResult == nullptr || shgResult == nullptr || shbResult == nullptr ||

			giVisibilityAtlas == nullptr || hzbResult == nullptr || ssgiTemporalA == nullptr || ssgiTemporalB == nullptr ||

			ssaoResult == nullptr || ssaoFilterResult == nullptr || ssgiFilterResult == nullptr)

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

		auto& color = renderPassManager->GetColor();

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

						color.GetSampler(),

						color.GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

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



		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGIDescriptorSets[0], PassBinding::TEXTURE_7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						shrResult->GetImage().GetSampler(),

						shrResult->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGIDescriptorSets[0], PassBinding::TEXTURE_8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						shgResult->GetImage().GetSampler(),

						shgResult->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGIDescriptorSets[0], PassBinding::TEXTURE_9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						shbResult->GetImage().GetSampler(),

						shbResult->GetImage().GetImageView(),

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

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGIDescriptorSets[0], SSGI_BINDING_GI_VISIBILITY, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {

					{

						giVisibilityAtlas->GetImage().GetSampler(),

						giVisibilityAtlas->GetImage().GetImageView(),

						VK_IMAGE_LAYOUT_GENERAL

					}

				}, 0);

		VansVKDescriptorManager::GetInstance()->CommitDescriptorUpdates();



		auto& motionVector = renderPassManager->GetMotionVector();

		VansVKDescriptorManager::GetInstance()->BeginDescriptorUpdate();

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_DEPTH, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { depth.GetSampler(), depth.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_MOTION_VECTOR, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { motionVector.GetSampler(), motionVector.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_HISTORY_GI, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { ssgiTemporalB->GetImage().GetSampler(), ssgiTemporalB->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_CURRENT_GI, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { { ssgiResult->GetImage().GetSampler(), ssgiResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_ACCUMULATED_GI, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { { ssgiTemporalA->GetImage().GetSampler(), ssgiTemporalA->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_INFO_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { { manager->m_SSGITemporalCBBuffer.GetNativeBuffer(), 0, manager->m_SSGITemporalCBBuffer.GetBufferSize() } }, 0);

		VansVKDescriptorManager::GetInstance()->CommitDescriptorUpdates();



		VansVKDescriptorManager::GetInstance()->BeginDescriptorUpdate();

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_DEPTH, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { depth.GetSampler(), depth.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_MOTION_VECTOR, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { motionVector.GetSampler(), motionVector.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_HISTORY_GI, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { { ssgiTemporalA->GetImage().GetSampler(), ssgiTemporalA->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_CURRENT_GI, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { { ssgiResult->GetImage().GetSampler(), ssgiResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteImageDescriptor(manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_ACCUMULATED_GI, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { { ssgiTemporalB->GetImage().GetSampler(), ssgiTemporalB->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }, 0);

		VansVKDescriptorManager::GetInstance()->WriteBufferDescriptor(manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_INFO_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { { manager->m_SSGITemporalCBBuffer.GetNativeBuffer(), 0, manager->m_SSGITemporalCBBuffer.GetBufferSize() } }, 0);

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



	void VansVKDevice::UpdateSSRDescriptorSets(VansRenderPassManager* renderPassManager)

	{

		VansMaterialManager* manager = m_Scene->GetMaterialManager();



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



		MarkFeatureDescriptorCurrent(m_SSRDescSetGeneration);



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



		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		VansTexture* hzbResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_RESULT);

		if (hzbResult == nullptr)

		{

			return;

		}



		int seedGroupsX = (int)std::ceilf(m_RenderWidth  / 16.0f);

		int seedGroupsY = (int)std::ceilf(m_RenderHeight / 16.0f);

		computeCmd.EnsureComputeShader(*manager->m_HIZSeedShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_HIZSeedSetLayout });

		computeCmd.DispatchCompute(*manager->m_HIZSeedShader, seedGroupsX, seedGroupsY, 1,

			{ m_Scene->GetGlobalDescriptorSet(), manager->m_HIZSeedDescriptorSets[0] });



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

			int threadGroupSizeX = (int)std::ceilf((m_RenderWidth  >> mipIndex) / 16.0f);

			int threadGroupSizeY = (int)std::ceilf((m_RenderHeight >> mipIndex) / 16.0f);



			computeCmd.EnsureComputeShader(*manager->m_HZBShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_HZBTexSetLayouts[mipIndex - 1] });

			computeCmd.DispatchCompute(*manager->m_HZBShader, threadGroupSizeX, threadGroupSizeY, 1,

				{ m_Scene->GetGlobalDescriptorSet(), manager->m_HZBDescriptorSets[mipIndex - 1] });



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

	void VansVKDevice::UpdateSSR(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		UpdateSSRDescriptorSets(renderPassManager);
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
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
			manager->m_BilateralFilterShader == nullptr)
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

		BilateralFilterSSGI(renderPassManager, computeCmd);
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
		VansExposureAdaptParamsGPU expParams = profile.ToExposureAdaptParams(deltaTime);
		manager->m_PostProcessParamsCBBuffer.SetBufferData(&ppParams, 0, sizeof(VansPostProcessParamsGPU));
		manager->m_ExposureAdaptParamsCBBuffer.SetBufferData(&expParams, 0, sizeof(VansExposureAdaptParamsGPU));

		if (profile.m_IsDirty)
		{
			VansBloomParamsGPU bloomParams = profile.ToBloomParams();
			manager->m_BloomParamsCBBuffer.SetBufferData(&bloomParams, 0, sizeof(VansBloomParamsGPU));
			profile.m_IsDirty = false;
		}
	}

	void VansVKDevice::UpdateExposureDescriptorSets(VansRenderPassManager* renderPassManager)
	{
		if (IsFeatureDescriptorCurrent(m_PPExposureDescSetGeneration)) return;
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager->m_ExposureLuminanceDescriptorSets.empty() || manager->m_ExposureAdaptDescriptorSets.empty()) return;
		VansTexture* lumRT = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_EXPOSURE_LUMINANCE);
		VansTexture* currentRT = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_EXPOSURE_CURRENT);
		if (lumRT == nullptr || currentRT == nullptr) return;
		auto& sceneColor = renderPassManager->GetColor();
		auto* desc = VansVKDescriptorManager::GetInstance();
		desc->BeginDescriptorUpdate();
		desc->WriteImageDescriptor(manager->m_ExposureLuminanceDescriptorSets[0], EXPOSURE_LUM_BINDING_SRC_COLOR, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ sceneColor.GetSampler(), sceneColor.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
		desc->WriteImageDescriptor(manager->m_ExposureLuminanceDescriptorSets[0], EXPOSURE_LUM_BINDING_LUM_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {{ lumRT->GetImage().GetSampler(), lumRT->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		desc->CommitDescriptorUpdates();
		desc->BeginDescriptorUpdate();
		desc->WriteImageDescriptor(manager->m_ExposureAdaptDescriptorSets[0], EXPOSURE_ADAPT_BINDING_LUM_IN, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ lumRT->GetImage().GetSampler(), lumRT->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
		desc->WriteImageDescriptor(manager->m_ExposureAdaptDescriptorSets[0], EXPOSURE_ADAPT_BINDING_EXP_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {{ currentRT->GetImage().GetSampler(), currentRT->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		desc->WriteBufferDescriptor(manager->m_ExposureAdaptDescriptorSets[0], EXPOSURE_ADAPT_BINDING_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {{ manager->m_ExposureAdaptParamsCBBuffer.GetNativeBuffer(), 0, manager->m_ExposureAdaptParamsCBBuffer.GetBufferSize() }});
		desc->CommitDescriptorUpdates();
		MarkFeatureDescriptorCurrent(m_PPExposureDescSetGeneration);
	}

	void VansVKDevice::UpdateExposure(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		UpdateExposureDescriptorSets(renderPassManager);
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager->m_ExposureLuminanceShader == nullptr || manager->m_ExposureAdaptShader == nullptr) return;
		computeCmd.EnsureComputeShader(*manager->m_ExposureLuminanceShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_ExposureLuminanceSetLayout });
		computeCmd.DispatchCompute(*manager->m_ExposureLuminanceShader, (64 + 7) / 8, (64 + 7) / 8, 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_ExposureLuminanceDescriptorSets[0] });
		RecordShaderWriteToReadMemoryDependency(computeCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		computeCmd.EnsureComputeShader(*manager->m_ExposureAdaptShader, { m_Scene->GetGlobalDescriptorSetLayout(), manager->m_ExposureAdaptSetLayout });
		computeCmd.DispatchCompute(*manager->m_ExposureAdaptShader, 1, 1, 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_ExposureAdaptDescriptorSets[0] });
		RecordShaderWriteToReadMemoryDependency(computeCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
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
		VansTexture* result = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_RESULT);
		if (!prefilter || !mip0 || !mip1 || !mip2 || !mip3 || !result) return;
		auto& sceneColor = renderPassManager->GetColor();
		auto* desc = VansVKDescriptorManager::GetInstance();
		desc->BeginDescriptorUpdate();
		desc->WriteImageDescriptor(manager->m_BloomPrefilterDescriptorSets[0], BLOOM_PREFILTER_BINDING_SRC, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ sceneColor.GetSampler(), sceneColor.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
		desc->WriteImageDescriptor(manager->m_BloomPrefilterDescriptorSets[0], BLOOM_PREFILTER_BINDING_DST, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {{ prefilter->GetImage().GetSampler(), prefilter->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
		desc->WriteBufferDescriptor(manager->m_BloomPrefilterDescriptorSets[0], BLOOM_PREFILTER_BINDING_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {{ manager->m_BloomParamsCBBuffer.GetNativeBuffer(), 0, manager->m_BloomParamsCBBuffer.GetBufferSize() }});
		desc->CommitDescriptorUpdates();
		VansTexture* dsInputs[4] = { prefilter, mip0, mip1, mip2 };
		VansTexture* dsOutputs[4] = { mip0, mip1, mip2, mip3 };
		for (int i = 0; i < 4; ++i)
		{
			desc->BeginDescriptorUpdate();
			desc->WriteImageDescriptor(manager->m_BloomDownsampleDescriptorSets[i], BLOOM_DOWNSAMPLE_BINDING_SRC, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ dsInputs[i]->GetImage().GetSampler(), dsInputs[i]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
			desc->WriteImageDescriptor(manager->m_BloomDownsampleDescriptorSets[i], BLOOM_DOWNSAMPLE_BINDING_DST, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {{ dsOutputs[i]->GetImage().GetSampler(), dsOutputs[i]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
			desc->CommitDescriptorUpdates();
		}
		VansTexture* usLo[4] = { mip3, mip2, mip1, mip0 };
		VansTexture* usHi[4] = { mip2, mip1, mip0, prefilter };
		VansTexture* usDst[4] = { mip2, mip1, mip0, result };
		for (int i = 0; i < 4; ++i)
		{
			desc->BeginDescriptorUpdate();
			desc->WriteImageDescriptor(manager->m_BloomUpsampleDescriptorSets[i], BLOOM_UPSAMPLE_BINDING_SRC_LO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ usLo[i]->GetImage().GetSampler(), usLo[i]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
			desc->WriteImageDescriptor(manager->m_BloomUpsampleDescriptorSets[i], BLOOM_UPSAMPLE_BINDING_SRC_HI, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {{ usHi[i]->GetImage().GetSampler(), usHi[i]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
			desc->WriteImageDescriptor(manager->m_BloomUpsampleDescriptorSets[i], BLOOM_UPSAMPLE_BINDING_DST, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, {{ usDst[i]->GetImage().GetSampler(), usDst[i]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
			desc->WriteBufferDescriptor(manager->m_BloomUpsampleDescriptorSets[i], BLOOM_UPSAMPLE_BINDING_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {{ manager->m_BloomParamsCBBuffer.GetNativeBuffer(), 0, manager->m_BloomParamsCBBuffer.GetBufferSize() }});
			desc->CommitDescriptorUpdates();
		}
		MarkFeatureDescriptorCurrent(m_PPBloomDescSetGeneration);
	}

	void VansVKDevice::UpdateBloom(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		UpdateBloomDescriptorSets(renderPassManager);
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
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
		uint32_t usW[4] = { w16, w8, w4, w2 };
		uint32_t usH[4] = { h16, h8, h4, h2 };
		for (int i = 0; i < 4; ++i)
		{
			computeCmd.DispatchCompute(*manager->m_BloomUpsampleShader, groups(usW[i]), groups(usH[i]), 1, { m_Scene->GetGlobalDescriptorSet(), manager->m_BloomUpsampleDescriptorSets[i] });
			barrier();
		}
	}

} // namespace VansGraphics
