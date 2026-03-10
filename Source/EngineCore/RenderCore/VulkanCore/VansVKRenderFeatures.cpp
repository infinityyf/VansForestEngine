#include "VansVKDevice.h"
#include "VansVKDescriptorManager.h"
#include "VansRenderPass.h"
#include "../VansScene.h"
#include <cmath>

namespace VansGraphics
{
	void VansVKDevice::UpdateSSGI(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		computeCmd.EnsureComputeShader(*manager->m_SSGIShader, { m_Scene->m_GlobalDescriptorSetLayout, manager->m_SSGITexSetLayout });
		computeCmd.DispatchCompute(*manager->m_SSGIShader, (m_RenderWidth + 7) / 8, (m_RenderHeight + 7) / 8, 1, { m_Scene->m_GlobalDescriptorSet, manager->m_SSGIDescriptorSets[0] });
	}

	void VansVKDevice::TemporalFilterSSGI(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		uint32_t writeIdx = manager->m_SSGITemporalFrame % 2;

		computeCmd.EnsureComputeShader(*manager->m_SSGITemporalShader, { m_Scene->m_GlobalDescriptorSetLayout, manager->m_SSGITemporalSetLayout });
		computeCmd.DispatchCompute(*manager->m_SSGITemporalShader, (m_RenderWidth + 7) / 8, (m_RenderHeight + 7) / 8, 1, { m_Scene->m_GlobalDescriptorSet, manager->m_SSGITemporalDescriptorSets[writeIdx] });
	}

	void VansVKDevice::BilateralFilterSSGI(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		uint32_t writeIdx = manager->m_SSGITemporalFrame % 2;
		uint32_t bilateralSetIdx = (writeIdx == 0) ? 1 : 2;
		computeCmd.EnsureComputeShader(*manager->m_BilateralFilterShader, { m_Scene->m_GlobalDescriptorSetLayout, manager->m_BilateralFilterSetLayout });
		computeCmd.DispatchCompute(*manager->m_BilateralFilterShader, (m_RenderWidth + 7) / 8, (m_RenderHeight + 7) / 8, 1, { m_Scene->m_GlobalDescriptorSet, manager->m_BilateralFilterDescriptorSets[bilateralSetIdx] });
	}

	void VansVKDevice::BilateralFilterSSAO(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		uint32_t halfResWidth = std::floor(m_RenderWidth / 2);
		uint32_t halfResHeight = std::floor(m_RenderHeight / 2);

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		computeCmd.EnsureComputeShader(*manager->m_BilateralFilterShader, { m_Scene->m_GlobalDescriptorSetLayout, manager->m_BilateralFilterSetLayout });
		computeCmd.DispatchCompute(*manager->m_BilateralFilterShader, (halfResWidth + 7) / 8, (halfResHeight + 7) / 8, 1, { m_Scene->m_GlobalDescriptorSet, manager->m_BilateralFilterDescriptorSets[0] });
	}

	void VansVKDevice::UpdateGIDataDescriptorSets(VansRenderPassManager* renderPassManager)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		static bool updatedSets = false;
		if (updatedSets)
		{
			return;
		}
		updatedSets = true;

		auto getRuntimeTexture = [manager](const char* key)
			{
				return manager->GetRuntimeRenderTexture(key);
			};

		VansTexture* ssgiResult = getRuntimeTexture(VansMaterialManager::RT_SSGI_RESULT);
		VansTexture* shrResult = getRuntimeTexture(VansMaterialManager::RT_SH_R_RESULT);
		VansTexture* shgResult = getRuntimeTexture(VansMaterialManager::RT_SH_G_RESULT);
		VansTexture* shbResult = getRuntimeTexture(VansMaterialManager::RT_SH_B_RESULT);
		VansTexture* hzbResult = getRuntimeTexture(VansMaterialManager::RT_HZB_RESULT);
		VansTexture* ssgiTemporalA = getRuntimeTexture(VansMaterialManager::RT_SSGI_TEMPORAL_A);
		VansTexture* ssgiTemporalB = getRuntimeTexture(VansMaterialManager::RT_SSGI_TEMPORAL_B);
		VansTexture* ssaoResult = getRuntimeTexture(VansMaterialManager::RT_SSAO_RESULT);
		VansTexture* ssaoFilterResult = getRuntimeTexture(VansMaterialManager::RT_SSAO_FILTER_RESULT);
		VansTexture* ssgiFilterResult = getRuntimeTexture(VansMaterialManager::RT_SSGI_FILTER_RESULT);

		if (ssgiResult == nullptr || shrResult == nullptr || shgResult == nullptr || shbResult == nullptr ||
			hzbResult == nullptr || ssgiTemporalA == nullptr || ssgiTemporalB == nullptr ||
			ssaoResult == nullptr || ssaoFilterResult == nullptr || ssgiFilterResult == nullptr)
		{
			return;
		}

		VansVKDescriptorManager::GetInstance()->ResetState();
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
			{
				manager->m_SSGIDescriptorSets[0],
				PassBinding::CBUFFER_6,
				0,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				{
					{
						manager->m_SSGICBBuffer.GetNativeBuffer(),
						0,
						manager->m_SSGICBBuffer.GetBufferSize()
					}
				}
			}
		);

		auto& normal = renderPassManager->GetNormal();
		auto& depth = renderPassManager->GetDepth();
		auto& color = renderPassManager->GetColor();
		auto& positionGbuffer = renderPassManager->GetGbuffer2();
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSGIDescriptorSets[0],
				PassBinding::TEXTURE_0,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						normal.GetSampler(),
						normal.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSGIDescriptorSets[0],
				PassBinding::TEXTURE_1,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						depth.GetSampler(),
						depth.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSGIDescriptorSets[0],
				PassBinding::TEXTURE_2,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						color.GetSampler(),
						color.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSGIDescriptorSets[0],
				PassBinding::TEXTURE_3,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						positionGbuffer.GetSampler(),
						positionGbuffer.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSGIDescriptorSets[0],
				PassBinding::TEXTURE_4,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						manager->m_PreConvDiffuse->GetImage().GetSampler(),
						manager->m_PreConvDiffuse->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSGIDescriptorSets[0],
				PassBinding::UAV_IMAGE_4,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						ssgiResult->GetImage().GetSampler(),
						ssgiResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSGIDescriptorSets[0],
				PassBinding::TEXTURE_7,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						shrResult->GetImage().GetSampler(),
						shrResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSGIDescriptorSets[0],
				PassBinding::TEXTURE_8,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						shgResult->GetImage().GetSampler(),
						shgResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSGIDescriptorSets[0],
				PassBinding::TEXTURE_9,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						shbResult->GetImage().GetSampler(),
						shbResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSGIDescriptorSets[0],
				PassBinding::TEXTURE_10,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						hzbResult->GetImage().GetSampler(),
						hzbResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();

		auto& motionVector = renderPassManager->GetMotionVector();
		VansVKDescriptorManager::GetInstance()->ResetState();
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{ manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_DEPTH, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			  { { depth.GetSampler(), depth.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } } });
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{ manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_MOTION_VECTOR, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			  { { motionVector.GetSampler(), motionVector.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } } });
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{ manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_HISTORY_GI, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			  { { ssgiTemporalB->GetImage().GetSampler(), ssgiTemporalB->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } } });
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{ manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_CURRENT_GI, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			  { { ssgiResult->GetImage().GetSampler(), ssgiResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } } });
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{ manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_ACCUMULATED_GI, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			  { { ssgiTemporalA->GetImage().GetSampler(), ssgiTemporalA->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } } });
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
			{ manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_INFO_UBO, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			  { { manager->m_SSGITemporalCBBuffer.GetNativeBuffer(), 0, manager->m_SSGITemporalCBBuffer.GetBufferSize() } } });
		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();

		VansVKDescriptorManager::GetInstance()->ResetState();
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{ manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_DEPTH, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			  { { depth.GetSampler(), depth.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } } });
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{ manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_MOTION_VECTOR, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			  { { motionVector.GetSampler(), motionVector.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } } });
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{ manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_HISTORY_GI, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			  { { ssgiTemporalA->GetImage().GetSampler(), ssgiTemporalA->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } } });
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{ manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_CURRENT_GI, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			  { { ssgiResult->GetImage().GetSampler(), ssgiResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } } });
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{ manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_ACCUMULATED_GI, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			  { { ssgiTemporalB->GetImage().GetSampler(), ssgiTemporalB->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } } });
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
			{ manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_INFO_UBO, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			  { { manager->m_SSGITemporalCBBuffer.GetNativeBuffer(), 0, manager->m_SSGITemporalCBBuffer.GetBufferSize() } } });
		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();

		VansVKDescriptorManager::GetInstance()->ResetState();
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_BilateralFilterDescriptorSets[0],
				PassBinding::TEXTURE_0,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						ssaoResult->GetImage().GetSampler(),
						ssaoResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_BilateralFilterDescriptorSets[0],
				PassBinding::TEXTURE_1,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						depth.GetSampler(),
						depth.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_BilateralFilterDescriptorSets[0],
				PassBinding::UAV_IMAGE_1,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						ssaoFilterResult->GetImage().GetSampler(),
						ssaoFilterResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_BilateralFilterDescriptorSets[1],
				PassBinding::TEXTURE_0,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						ssgiTemporalA->GetImage().GetSampler(),
						ssgiTemporalA->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_BilateralFilterDescriptorSets[1],
				PassBinding::TEXTURE_1,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						depth.GetSampler(),
						depth.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_BilateralFilterDescriptorSets[1],
				PassBinding::UAV_IMAGE_1,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						ssgiFilterResult->GetImage().GetSampler(),
						ssgiFilterResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_BilateralFilterDescriptorSets[2],
				PassBinding::TEXTURE_0,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						ssgiTemporalB->GetImage().GetSampler(),
						ssgiTemporalB->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_BilateralFilterDescriptorSets[2],
				PassBinding::TEXTURE_1,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						depth.GetSampler(),
						depth.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_BilateralFilterDescriptorSets[2],
				PassBinding::UAV_IMAGE_1,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						ssgiFilterResult->GetImage().GetSampler(),
						ssgiFilterResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
	}

	void VansVKDevice::UpdateHZBDescriptorSets(VansRenderPassManager* renderPassManager)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		static bool updatedSets = false;
		if (updatedSets)
		{
			return;
		}
		updatedSets = true;

		VansTexture* hzbResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_RESULT);
		if (hzbResult == nullptr)
		{
			return;
		}

		for (int mipIndex = 1; mipIndex < manager->m_HIZMipCount; mipIndex++)
		{
			VansVKDescriptorManager::GetInstance()->ResetState();

			VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
				{
					manager->m_HZBDescriptorSets[mipIndex - 1],
					PassBinding::UAV_IMAGE,
					0,
					VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					{
						{
							hzbResult->GetImage().GetSampler(),
							hzbResult->GetImage().GetImageMipView(mipIndex - 1),
							VK_IMAGE_LAYOUT_GENERAL
						}
					}
				}
			);
			VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
				{
					manager->m_HZBDescriptorSets[mipIndex - 1],
					PassBinding::UAV_IMAGE_0,
					0,
					VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					{
						{
							hzbResult->GetImage().GetSampler(),
							hzbResult->GetImage().GetImageMipView(mipIndex),
							VK_IMAGE_LAYOUT_GENERAL
						}
					}
				}
			);

			VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
		}
	}

	void VansVKDevice::UpdateSSRDescriptorSets(VansRenderPassManager* renderPassManager)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		static bool updatedSets = false;
		if (updatedSets)
		{
			return;
		}
		updatedSets = true;

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

		VansVKDescriptorManager::GetInstance()->ResetState();

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRTraceDescriptorSets[0],
				PassBinding::TEXTURE_0,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						normal.GetSampler(),
						normal.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRTraceDescriptorSets[0],
				PassBinding::TEXTURE_1,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						roughness.GetSampler(),
						roughness.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRTraceDescriptorSets[0],
				PassBinding::TEXTURE_2,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						position.GetSampler(),
						position.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRTraceDescriptorSets[0],
				PassBinding::TEXTURE_3,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						hzbResult->GetImage().GetSampler(),
						hzbResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRTraceDescriptorSets[0],
				PassBinding::UAV_IMAGE_3,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						ssrHitInfo->GetImage().GetSampler(),
						ssrHitInfo->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRTraceDescriptorSets[0],
				PassBinding::UAV_IMAGE_4,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						ssrRayPdf->GetImage().GetSampler(),
						ssrRayPdf->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();

		VansVKDescriptorManager::GetInstance()->ResetState();

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRResolveDescriptorSets[0],
				PassBinding::TEXTURE_0,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						color.GetSampler(),
						color.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRResolveDescriptorSets[0],
				PassBinding::TEXTURE_1,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						roughness.GetSampler(),
						roughness.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRResolveDescriptorSets[0],
				PassBinding::TEXTURE_2,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						normal.GetSampler(),
						normal.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRResolveDescriptorSets[0],
				PassBinding::TEXTURE_3,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						position.GetSampler(),
						position.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRResolveDescriptorSets[0],
				PassBinding::UAV_IMAGE_3,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						ssrHitInfo->GetImage().GetSampler(),
						ssrHitInfo->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRResolveDescriptorSets[0],
				PassBinding::UAV_IMAGE_4,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						ssrRayPdf->GetImage().GetSampler(),
						ssrRayPdf->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRResolveDescriptorSets[0],
				PassBinding::UAV_IMAGE_5,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						ssrResult->GetImage().GetSampler(),
						ssrResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();

		VansVKDescriptorManager::GetInstance()->ResetState();
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRAADescriptorSets[0],
				PassBinding::TEXTURE_0,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						ssrResult->GetImage().GetSampler(),
						ssrResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRAADescriptorSets[0],
				PassBinding::TEXTURE_1,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						position.GetSampler(),
						position.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRAADescriptorSets[0],
				PassBinding::UAV_IMAGE_1,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						ssrAaResultA->GetImage().GetSampler(),
						ssrAaResultA->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRAADescriptorSets[0],
				PassBinding::UAV_IMAGE_2,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						ssrAaResultB->GetImage().GetSampler(),
						ssrAaResultB->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRAADescriptorSets[0],
				PassBinding::UAV_IMAGE_3,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						ssrAaResult->GetImage().GetSampler(),
						ssrAaResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
	}

	void VansVKDevice::UpdateVolumetricFogSets(VansRenderPassManager* renderPassManager)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		static bool updatedSets = false;
		if (updatedSets)
		{
			return;
		}
		updatedSets = true;

		VansTexture* volumetricFogResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_VOLUMETRIC_FOG_RESULT);
		if (volumetricFogResult == nullptr)
		{
			return;
		}

		auto& position = renderPassManager->GetGbuffer2();

		VansVKDescriptorManager::GetInstance()->ResetState();

		// binding 0 — inputPosition (COMBINED_IMAGE_SAMPLER)
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_VolumetricFogDescriptorSets[0],
				FOG_BINDING_POSITION,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						position.GetSampler(),
						position.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		// binding 1 — fogResult (STORAGE_IMAGE)
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_VolumetricFogDescriptorSets[0],
				FOG_BINDING_RESULT,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						volumetricFogResult->GetImage().GetSampler(),
						volumetricFogResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		// binding 2 — FogParams UBO (UNIFORM_BUFFER)
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
			{
				manager->m_VolumetricFogDescriptorSets[0],
				FOG_BINDING_PARAMS,
				0,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				{
					{
						manager->m_FogParamsCBBuffer.GetNativeBuffer(),
						0,
						manager->m_FogParamsCBBuffer.GetBufferSize()
					}
				}
			}
		);

		// binding 3 — voxelFogVolume (COMBINED_IMAGE_SAMPLER, ray-march result)
		VansTexture* fogVoxelRayMarch = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_RAYMARCH);
		if (fogVoxelRayMarch != nullptr)
		{
			VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
				{
					manager->m_VolumetricFogDescriptorSets[0],
					FOG_BINDING_VOXEL_VOLUME,
					0,
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					{
						{
							fogVoxelRayMarch->GetImage().GetSampler(),
							fogVoxelRayMarch->GetImage().GetImageView(),
							VK_IMAGE_LAYOUT_GENERAL
						}
					}
				}
			);
		}

		// binding 4 — FogVolumeParams UBO (volumeNear/Far for depth-to-slice in compose)
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
			{
				manager->m_VolumetricFogDescriptorSets[0],
				FOG_BINDING_VOLUME_PARAMS,
				0,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				{
					{
						manager->m_FogVolumeParamsCBBuffer.GetNativeBuffer(),
						0,
						manager->m_FogVolumeParamsCBBuffer.GetBufferSize()
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
	}

	void VansVKDevice::UpdateHZB(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		UpdateHZBDescriptorSets(renderPassManager);

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		auto& depth = renderPassManager->GetDepth();
		VansTexture* hzbResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_RESULT);
		if (hzbResult == nullptr)
		{
			return;
		}

		computeCmd.BlitImage(depth, 0, hzbResult->GetImage(), 0);

		for (int mipIndex = 1; mipIndex < manager->m_HIZMipCount; mipIndex++)
		{
			int threadGroupSizeX = m_RenderWidth >> (mipIndex);
			int threadGroupSizeY = m_RenderHeight >> (mipIndex);
			threadGroupSizeX = std::ceilf(threadGroupSizeX / 16.0f);
			threadGroupSizeY = std::ceilf(threadGroupSizeY / 16.0f);

			computeCmd.EnsureComputeShader(*manager->m_HZBShader, { m_Scene->m_GlobalDescriptorSetLayout, manager->m_HZBTexSetLayouts[mipIndex - 1] });
			computeCmd.DispatchCompute(*manager->m_HZBShader, threadGroupSizeX, threadGroupSizeY, 1, { m_Scene->m_GlobalDescriptorSet, manager->m_HZBDescriptorSets[mipIndex - 1] });
		}
	}

	void VansVKDevice::UpdateSSR(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		UpdateSSRDescriptorSets(renderPassManager);

		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		uint32_t halfResWidth = m_RenderWidth / 2;
		uint32_t halfResHeight = m_RenderHeight / 2;

		computeCmd.EnsureComputeShader(*manager->m_SSRTraceShader, { m_Scene->m_GlobalDescriptorSetLayout, manager->m_SSRTraceSetLayout });
		computeCmd.DispatchCompute(*manager->m_SSRTraceShader, halfResWidth, halfResHeight, 1, { m_Scene->m_GlobalDescriptorSet, manager->m_SSRTraceDescriptorSets[0] });

		computeCmd.EnsureComputeShader(*manager->m_SSRResolveShader, { m_Scene->m_GlobalDescriptorSetLayout, manager->m_SSRResolveSetLayout });
		computeCmd.DispatchCompute(*manager->m_SSRResolveShader, halfResWidth, halfResHeight, 1, { m_Scene->m_GlobalDescriptorSet, manager->m_SSRResolveDescriptorSets[0] });

		computeCmd.EnsureComputeShader(*manager->m_SSRTemporalAAShader, { m_Scene->m_GlobalDescriptorSetLayout, manager->m_SSRAASetLayout });
		computeCmd.DispatchCompute(*manager->m_SSRTemporalAAShader, (halfResWidth + 7) / 8, (halfResHeight + 7) / 8, 1, { m_Scene->m_GlobalDescriptorSet, manager->m_SSRAADescriptorSets[0] });
	}

	// ================================================================
	// Fog Light Injection — descriptor set writes (ping-pong)
	//   set[0]: write→Injection, read←History
	//   set[1]: write→History,   read←Injection
	// ================================================================
	void VansVKDevice::UpdateFogLightInjectionSets(VansRenderPassManager* renderPassManager)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		static bool updatedSets = false;
		if (updatedSets) return;
		updatedSets = true;

		VansTexture* fogVoxelInjection = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_INJECTION);
		VansTexture* fogVoxelHistory   = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_INJECTION_HISTORY);
		if (fogVoxelInjection == nullptr || fogVoxelHistory == nullptr) return;

		auto& shadowMap = renderPassManager->GetShadowMap();

		// Two ping-pong configurations:
		//   set[0]: output=Injection, history=History
		//   set[1]: output=History,   history=Injection
		VansTexture* writeTextures[2] = { fogVoxelInjection, fogVoxelHistory };
		VansTexture* readTextures[2]  = { fogVoxelHistory,   fogVoxelInjection };

		for (int i = 0; i < 2; i++)
		{
			VansVKDescriptorManager::GetInstance()->ResetState();

			// binding 0 — i_VoxelGrid (STORAGE_IMAGE, 3D) — write target
			VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
				{
					manager->m_FogLightInjectionDescriptorSets[i],
					FOG_INJECT_BINDING_VOXEL_GRID,
					0,
					VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					{
						{
							writeTextures[i]->GetImage().GetSampler(),
							writeTextures[i]->GetImage().GetImageView(),
							VK_IMAGE_LAYOUT_GENERAL
						}
					}
				}
			);

			// binding 1 — fogShadowMap (COMBINED_IMAGE_SAMPLER)
			VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
				{
					manager->m_FogLightInjectionDescriptorSets[i],
					FOG_INJECT_BINDING_SHADOW_MAP,
					0,
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					{
						{
							shadowMap.GetSampler(),
							shadowMap.GetImageView(),
							VK_IMAGE_LAYOUT_GENERAL
						}
					}
				}
			);

			// binding 2 — FogVolumeParams UBO
			VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
				{
					manager->m_FogLightInjectionDescriptorSets[i],
					FOG_INJECT_BINDING_PARAMS,
					0,
					VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					{
						{
							manager->m_FogVolumeParamsCBBuffer.GetNativeBuffer(),
							0,
							manager->m_FogVolumeParamsCBBuffer.GetBufferSize()
						}
					}
				}
			);

			// binding 3 — s_History (COMBINED_IMAGE_SAMPLER, 3D) — read from previous frame
			VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
				{
					manager->m_FogLightInjectionDescriptorSets[i],
					FOG_INJECT_BINDING_HISTORY,
					0,
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					{
						{
							readTextures[i]->GetImage().GetSampler(),
							readTextures[i]->GetImage().GetImageView(),
							VK_IMAGE_LAYOUT_GENERAL
						}
					}
				}
			);

			VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
		}
	}

	// ================================================================
	// Fog Ray March — descriptor set writes
	// ================================================================
	void VansVKDevice::UpdateFogRayMarchSets()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		// Must update each frame: the injection output ping-pongs between two textures
		uint32_t frameIdx = manager->m_FogTemporalFrame % 2;

		// frameIdx==0 → injection wrote to RT_FOG_VOXEL_INJECTION
		// frameIdx==1 → injection wrote to RT_FOG_VOXEL_INJECTION_HISTORY
		const char* currentInjectionRT = (frameIdx == 0)
			? VansMaterialManager::RT_FOG_VOXEL_INJECTION
			: VansMaterialManager::RT_FOG_VOXEL_INJECTION_HISTORY;

		VansTexture* fogVoxelInput    = manager->GetRuntimeRenderTexture(currentInjectionRT);
		VansTexture* fogVoxelRayMarch = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_RAYMARCH);
		if (fogVoxelInput == nullptr || fogVoxelRayMarch == nullptr) return;

		VansVKDescriptorManager::GetInstance()->ResetState();

		// binding 0 — s_VoxelGrid (COMBINED_IMAGE_SAMPLER input from injection)
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_FogRayMarchDescriptorSets[0],
				FOG_MARCH_BINDING_INPUT_VOXEL,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						fogVoxelInput->GetImage().GetSampler(),
						fogVoxelInput->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		// binding 1 — i_RayMarchResult (STORAGE_IMAGE output)
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_FogRayMarchDescriptorSets[0],
				FOG_MARCH_BINDING_RESULT,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						fogVoxelRayMarch->GetImage().GetSampler(),
						fogVoxelRayMarch->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		// binding 2 — FogVolumeParams UBO (volumeNear/Far for slice thickness)
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
			{
				manager->m_FogRayMarchDescriptorSets[0],
				FOG_MARCH_BINDING_VOLUME_PARAMS,
				0,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				{
					{
						manager->m_FogVolumeParamsCBBuffer.GetNativeBuffer(),
						0,
						manager->m_FogVolumeParamsCBBuffer.GetBufferSize()
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
	}

	// ================================================================
	// Fog Light Injection — dispatch
	// ================================================================
	void VansVKDevice::UpdateFogLightInjection(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		UpdateFogLightInjectionSets(renderPassManager);

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager->m_FogLightInjectionShader == nullptr) return;

		static constexpr int TILE_SIZE    = 8;
		static constexpr int VOXEL_GRID_Z = 256;
		uint32_t gridX = (m_RenderWidth  + TILE_SIZE - 1) / TILE_SIZE;
		uint32_t gridY = (m_RenderHeight + TILE_SIZE - 1) / TILE_SIZE;

		uint32_t groupsX = (gridX + 3) / 4;  // localSize = 4
		uint32_t groupsY = (gridY + 3) / 4;
		uint32_t groupsZ = (VOXEL_GRID_Z + 3) / 4;

		uint32_t frameIdx = manager->m_FogTemporalFrame % 2;

		computeCmd.EnsureComputeShader(*manager->m_FogLightInjectionShader,
			{ m_Scene->m_GlobalDescriptorSetLayout, manager->m_FogLightInjectionSetLayout });
		computeCmd.DispatchCompute(*manager->m_FogLightInjectionShader,
			groupsX, groupsY, groupsZ,
			{ m_Scene->m_GlobalDescriptorSet, manager->m_FogLightInjectionDescriptorSets[frameIdx] });
	}

	// ================================================================
	// Fog Ray March — dispatch
	// ================================================================
	void VansVKDevice::UpdateFogRayMarch(VansVKCommandBuffer& computeCmd)
	{
		UpdateFogRayMarchSets();

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager->m_FogRayMarchShader == nullptr) return;

		static constexpr int TILE_SIZE = 8;
		uint32_t gridX = (m_RenderWidth  + TILE_SIZE - 1) / TILE_SIZE;
		uint32_t gridY = (m_RenderHeight + TILE_SIZE - 1) / TILE_SIZE;
		uint32_t groupsX = (gridX + 7) / 8;  // localSize = 8×8×1
		uint32_t groupsY = (gridY + 7) / 8;

		computeCmd.EnsureComputeShader(*manager->m_FogRayMarchShader,
			{ m_Scene->m_GlobalDescriptorSetLayout, manager->m_FogRayMarchSetLayout });
		computeCmd.DispatchCompute(*manager->m_FogRayMarchShader,
			groupsX, groupsY, 1,
			{ m_Scene->m_GlobalDescriptorSet, manager->m_FogRayMarchDescriptorSets[0] });
	}

	// ================================================================
	// Volumetric Fog compose — dispatch (injection → ray-march → compose)
	// ================================================================
	void VansVKDevice::UpdateVolumetricFog(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		// Pass 1: Light injection into 3D voxel grid
		UpdateFogLightInjection(renderPassManager, computeCmd);

		// Pass 2: Front-to-back ray march accumulation
		UpdateFogRayMarch(computeCmd);

		// Pass 3: Compose with height-exponential fog
		UpdateVolumetricFogSets(renderPassManager);

		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		uint32_t halfResWidth = std::floor(m_RenderWidth / 2);
		uint32_t halfResHeight = std::floor(m_RenderHeight / 2);

		uint32_t groupsX = (halfResWidth + 7) / 8;
		uint32_t groupsY = (halfResHeight + 7) / 8;

		computeCmd.EnsureComputeShader(*manager->m_VolumetrcFogShader, { m_Scene->m_GlobalDescriptorSetLayout, manager->m_VolumetricFogSetLayout });
		computeCmd.DispatchCompute(*manager->m_VolumetrcFogShader, groupsX, groupsY, 1, { m_Scene->m_GlobalDescriptorSet, manager->m_VolumetricFogDescriptorSets[0] });

		// Advance temporal ping-pong counter after all fog passes are done
		manager->m_FogTemporalFrame++;
	}

	void VansVKDevice::UpdateGIData(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		UpdateGIDataDescriptorSets(renderPassManager);
		UpdateSSGI(renderPassManager, computeCmd);
		TemporalFilterSSGI(renderPassManager, computeCmd);
		BilateralFilterSSGI(renderPassManager, computeCmd);
		m_Scene->GetMaterialManager()->m_SSGITemporalFrame++;
		BilateralFilterSSAO(renderPassManager, computeCmd);
	}
}
