#include "VansVKDevice.h"
#include "VansVKDescriptorManager.h"
#include "VansRenderPass.h"
#include "../VansScene.h"
#include "../VansPostProcessProfile.h"
#include "../../VansTimer.h"
#include <cstddef>
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
		manager->m_BilateralFilterPushConstant.sigmaSpace = 2.0f;
		manager->m_BilateralFilterPushConstant.sigmaDepth = 0.12f;
		manager->m_BilateralFilterPushConstant.radius = 3;
		manager->m_BilateralFilterPushConstant.depthThreshold = 0.20f;
		manager->m_BilateralFilterPushConstant.depthMode = 0;
		manager->m_BilateralFilterShader->SetPushConstantData(&(manager->m_BilateralFilterPushConstant));
		computeCmd.EnsureComputeShader(*manager->m_BilateralFilterShader, { m_Scene->m_GlobalDescriptorSetLayout, manager->m_BilateralFilterSetLayout });
		computeCmd.DispatchCompute(*manager->m_BilateralFilterShader, (m_RenderWidth + 7) / 8, (m_RenderHeight + 7) / 8, 1, { m_Scene->m_GlobalDescriptorSet, manager->m_BilateralFilterDescriptorSets[bilateralSetIdx] });
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
		computeCmd.EnsureComputeShader(*manager->m_BilateralFilterShader, { m_Scene->m_GlobalDescriptorSetLayout, manager->m_BilateralFilterSetLayout });
		computeCmd.DispatchCompute(*manager->m_BilateralFilterShader, (halfResWidth + 7) / 8, (halfResHeight + 7) / 8, 1, { m_Scene->m_GlobalDescriptorSet, manager->m_BilateralFilterDescriptorSets[0] });
	}

	void VansVKDevice::UpdateGIDataDescriptorSets(VansRenderPassManager* renderPassManager)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		if (m_GIDataDescSetsUpdated)
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

		// 标记已更新：仅在所有纹理就绪、即将写�?descriptor 时才设置
		m_GIDataDescSetsUpdated = true;

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
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
			  { { depth.GetSampler(), depth.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } } });
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{ manager->m_SSGITemporalDescriptorSets[0], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_MOTION_VECTOR, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			  { { motionVector.GetSampler(), motionVector.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } } });
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
			  { { depth.GetSampler(), depth.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } } });
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{ manager->m_SSGITemporalDescriptorSets[1], SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_MOTION_VECTOR, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			  { { motionVector.GetSampler(), motionVector.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } } });
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
						positionGbuffer.GetSampler(),
						positionGbuffer.GetImageView(),
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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

	void VansVKDevice::UpdateHIZSeedDescriptorSet(VansRenderPassManager* renderPassManager)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		if (m_HIZSeedDescSetsUpdated)
			return;

		VansTexture* hzbResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_RESULT);
		if (hzbResult == nullptr)
			return;

		m_HIZSeedDescSetsUpdated = true;

		auto& position = renderPassManager->GetGbuffer2();

		VansVKDescriptorManager::GetInstance()->ResetState();

		// binding 0: GBuffer position 采样器输�?
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_HIZSeedDescriptorSets[0],
				HIZ_SEED_BINDING_POSITION,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						position.GetSampler(),
						position.GetImageView(),
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					}
				}
			}
		);

		// binding 1: HIZ mip 0 存储图像输出（r32f 线性深度）
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_HIZSeedDescriptorSets[0],
				HIZ_SEED_BINDING_HIZ_MIP0,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						hzbResult->GetImage().GetSampler(),
						hzbResult->GetImage().GetImageMipView(0),
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

		if (m_HZBDescSetsUpdated)
		{
			return;
		}

		VansTexture* hzbResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_RESULT);
		if (hzbResult == nullptr)
		{
			return;
		}

		m_HZBDescSetsUpdated = true;

		for (uint32_t mipIndex = 1; mipIndex < manager->m_HIZMipCount; ++mipIndex)
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

		if (m_SSRDescSetsUpdated)
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

		m_SSRDescSetsUpdated = true;

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
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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

		auto& motionVectorSSR = renderPassManager->GetMotionVector();
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRAADescriptorSets[0],
				SSRTemporalAAPassBinding::SSR_TAA_BINDING_MOTION_VECTOR,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						motionVectorSSR.GetSampler(),
						motionVectorSSR.GetImageView(),
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

		if (m_VolumetricFogDescSetsUpdated)
		{
			return;
		}

		VansTexture* volumetricFogResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_VOLUMETRIC_FOG_RESULT);
		if (volumetricFogResult == nullptr)
		{
			return;
		}

		m_VolumetricFogDescSetsUpdated = true;

		auto& position = renderPassManager->GetGbuffer2();

		VansVKDescriptorManager::GetInstance()->ResetState();

		// binding 0 �?inputPosition (COMBINED_IMAGE_SAMPLER)
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
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					}
				}
			}
		);

		// binding 1 �?fogResult (STORAGE_IMAGE)
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

		// binding 2 �?FogParams UBO (UNIFORM_BUFFER)
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

		// binding 3 �?voxelFogVolume (COMBINED_IMAGE_SAMPLER, ray-march result)
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

		// binding 4 �?FogVolumeParams UBO (volumeNear/Far for depth-to-slice in compose)
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
		UpdateHIZSeedDescriptorSet(renderPassManager);
		UpdateHZBDescriptorSets(renderPassManager);

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		VansTexture* hzbResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_RESULT);
		if (hzbResult == nullptr)
		{
			return;
		}

		// ── Pass 0: HIZ_SEED —�?�?GBuffer position.w 写入 HIZ mip 0（线性深度）──
		// 取代原来�?BlitImage，完全在 Compute 中完成，无需布局转换
		int seedGroupsX = (int)std::ceilf(m_RenderWidth  / 16.0f);
		int seedGroupsY = (int)std::ceilf(m_RenderHeight / 16.0f);
		computeCmd.EnsureComputeShader(*manager->m_HIZSeedShader, { m_Scene->m_GlobalDescriptorSetLayout, manager->m_HIZSeedSetLayout });
		computeCmd.DispatchCompute(*manager->m_HIZSeedShader, seedGroupsX, seedGroupsY, 1,
			{ m_Scene->m_GlobalDescriptorSet, manager->m_HIZSeedDescriptorSets[0] });

		// ── Barrier: mip 0 写入完毕 �?mip 1 读取可见 ──
		VkMemoryBarrier seedBarrier = {};
		seedBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		seedBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		seedBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		computeCmd.PipelineBarrier(
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			{ seedBarrier });

		// ── Pass 1+: HIZ.comp —�?�?mip 下采样（min-depth 金字塔）──
		for (uint32_t mipIndex = 1; mipIndex < manager->m_HIZMipCount; ++mipIndex)
		{
			int threadGroupSizeX = (int)std::ceilf((m_RenderWidth  >> mipIndex) / 16.0f);
			int threadGroupSizeY = (int)std::ceilf((m_RenderHeight >> mipIndex) / 16.0f);

			computeCmd.EnsureComputeShader(*manager->m_HZBShader, { m_Scene->m_GlobalDescriptorSetLayout, manager->m_HZBTexSetLayouts[mipIndex - 1] });
			computeCmd.DispatchCompute(*manager->m_HZBShader, threadGroupSizeX, threadGroupSizeY, 1,
				{ m_Scene->m_GlobalDescriptorSet, manager->m_HZBDescriptorSets[mipIndex - 1] });

			// ── Barrier: mip N 写入完毕 �?mip N+1 读取可见 ──
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

	void VansVKDevice::UpdateSSR(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
	{
		UpdateSSRDescriptorSets(renderPassManager);

		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		computeCmd.EnsureComputeShader(*manager->m_SSRTraceShader, { m_Scene->m_GlobalDescriptorSetLayout, manager->m_SSRTraceSetLayout });
		computeCmd.DispatchCompute(*manager->m_SSRTraceShader, (m_RenderWidth + 7) / 8, (m_RenderHeight + 7) / 8, 1, { m_Scene->m_GlobalDescriptorSet, manager->m_SSRTraceDescriptorSets[0] });

		// Trace 写入 traceHit / tracePDF，Resolve 会立即读取；必须显式保证 storage image 可见�?
		VkMemoryBarrier traceBarrier = {};
		traceBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		traceBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		traceBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		computeCmd.PipelineBarrier(
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			{ traceBarrier });

		computeCmd.EnsureComputeShader(*manager->m_SSRResolveShader, { m_Scene->m_GlobalDescriptorSetLayout, manager->m_SSRResolveSetLayout });
		// SSR_RESOLVE.comp 使用 8×8 local size，按 8 对齐取整覆盖全分辨率像素
		computeCmd.DispatchCompute(*manager->m_SSRResolveShader, (m_RenderWidth + 7) / 8, (m_RenderHeight + 7) / 8, 1, { m_Scene->m_GlobalDescriptorSet, manager->m_SSRResolveDescriptorSets[0] });

		// Resolve 写入 ssrResult，TemporalAA 会立即读取；避免�?wave/tile 可见性造成条纹状断层�?
		VkMemoryBarrier resolveBarrier = {};
		resolveBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		resolveBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		resolveBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		computeCmd.PipelineBarrier(
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			{ resolveBarrier });

		computeCmd.EnsureComputeShader(*manager->m_SSRTemporalAAShader, { m_Scene->m_GlobalDescriptorSetLayout, manager->m_SSRAASetLayout });
		computeCmd.DispatchCompute(*manager->m_SSRTemporalAAShader, (m_RenderWidth + 7) / 8, (m_RenderHeight + 7) / 8, 1, { m_Scene->m_GlobalDescriptorSet, manager->m_SSRAADescriptorSets[0] });
	}

	// ================================================================
	// Fog Light Injection �?descriptor set writes (ping-pong)
	//   set[0]: write→Injection, read←History
	//   set[1]: write→History,   read←Injection
	// ================================================================
	void VansVKDevice::UpdateFogLightInjectionSets(VansRenderPassManager* renderPassManager)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		if (m_FogLightInjectionDescSetsUpdated) return;

		VansTexture* fogVoxelInjection = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_INJECTION);
		VansTexture* fogVoxelHistory   = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_INJECTION_HISTORY);
		if (fogVoxelInjection == nullptr || fogVoxelHistory == nullptr) return;

		m_FogLightInjectionDescSetsUpdated = true;

		auto& shadowMap = renderPassManager->GetShadowMap();

		// For fog, use cascade layer 1 (matches FOG_CASCADE_INDEX in Common.glsl)
		VkImageView fogShadowView = renderPassManager->GetCascadeShadowLayerView(1);
		VkSampler fogShadowSampler = renderPassManager->GetCascadeShadowSampler();

		// Two ping-pong configurations:
		//   set[0]: output=Injection, history=History
		//   set[1]: output=History,   history=Injection
		VansTexture* writeTextures[2] = { fogVoxelInjection, fogVoxelHistory };
		VansTexture* readTextures[2]  = { fogVoxelHistory,   fogVoxelInjection };

		for (int i = 0; i < 2; i++)
		{
			VansVKDescriptorManager::GetInstance()->ResetState();

			// binding 0 �?i_VoxelGrid (STORAGE_IMAGE, 3D) �?write target
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

			// binding 1 �?fogShadowMap (COMBINED_IMAGE_SAMPLER)
			VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
				{
					manager->m_FogLightInjectionDescriptorSets[i],
					FOG_INJECT_BINDING_SHADOW_MAP,
					0,
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					{
						{
							fogShadowSampler,
							fogShadowView,
							VK_IMAGE_LAYOUT_GENERAL
						}
					}
				}
			);

			// binding 2 �?FogVolumeParams UBO
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

			// binding 3 �?s_History (COMBINED_IMAGE_SAMPLER, 3D) �?read from previous frame
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

			// binding 4 �?punctualShadowMap (tile-based point/spot shadow atlas)
			VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
				{
					manager->m_FogLightInjectionDescriptorSets[i],
					FOG_INJECT_BINDING_PUNCTUAL_SHADOW,
					0,
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					{
						{
							renderPassManager->GetPunctualShadowMap().GetSampler(),
							renderPassManager->GetPunctualShadowMap().GetImageView(),
							VK_IMAGE_LAYOUT_GENERAL
						}
					}
				}
			);

			VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
		}
	}

	// ================================================================
	// Fog Ray March �?descriptor set writes
	// ================================================================
	void VansVKDevice::UpdateFogRayMarchSets()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		// Must update each frame: the injection output ping-pongs between two textures
		uint32_t frameIdx = manager->m_FogTemporalFrame % 2;

		// frameIdx==0 �?injection wrote to RT_FOG_VOXEL_INJECTION
		// frameIdx==1 �?injection wrote to RT_FOG_VOXEL_INJECTION_HISTORY
		const char* currentInjectionRT = (frameIdx == 0)
			? VansMaterialManager::RT_FOG_VOXEL_INJECTION
			: VansMaterialManager::RT_FOG_VOXEL_INJECTION_HISTORY;

		VansTexture* fogVoxelInput    = manager->GetRuntimeRenderTexture(currentInjectionRT);
		VansTexture* fogVoxelRayMarch = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_RAYMARCH);
		if (fogVoxelInput == nullptr || fogVoxelRayMarch == nullptr) return;

		VansVKDescriptorManager::GetInstance()->ResetState();

		// binding 0 �?s_VoxelGrid (COMBINED_IMAGE_SAMPLER input from injection)
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

		// binding 1 �?i_RayMarchResult (STORAGE_IMAGE output)
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

		// binding 2 �?FogVolumeParams UBO (volumeNear/Far for slice thickness)
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
	// Fog Light Injection �?dispatch
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
	// Fog Ray March �?dispatch
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
	// Volumetric Fog compose �?dispatch (injection �?ray-march �?compose)
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

		uint32_t halfResWidth = m_RenderWidth / 2;
		uint32_t halfResHeight = m_RenderHeight / 2;

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

	// ================================================================
	// TileLight Build �?descriptor set writes (Set 1)
	// ================================================================
	void VansVKDevice::UpdateTileLightBuildSets()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		if (m_TileLightBuildDescSetsUpdated) return;
		m_TileLightBuildDescSetsUpdated = true;

		VansVKDescriptorManager::GetInstance()->ResetState();

		// binding 0 �?TileLightHeader SSBO (write)
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
			{
				manager->m_TileLightBuildDescriptorSets[0],
				TILE_BUILD_BINDING_GRID,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				{
					{
						manager->m_TileLightHeaderBuffer.GetNativeBuffer(),
						0,
						manager->m_TileLightHeaderBuffer.GetBufferSize()
					}
				}
			}
		);

		// binding 1 �?TileLight Index SSBO (write)
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
			{
				manager->m_TileLightBuildDescriptorSets[0],
				TILE_BUILD_BINDING_INDICES,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				{
					{
						manager->m_TileLightIndexBuffer.GetNativeBuffer(),
						0,
						manager->m_TileLightIndexBuffer.GetBufferSize()
					}
				}
			}
		);

		// binding 2 �?TileLightBuildParams UBO
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
			{
				manager->m_TileLightBuildDescriptorSets[0],
				TILE_BUILD_BINDING_PARAMS,
				0,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				{
					{
						manager->m_TileLightBuildParamsCBBuffer.GetNativeBuffer(),
						0,
						manager->m_TileLightBuildParamsCBBuffer.GetBufferSize()
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
	}

	// ================================================================
	// TileLight Build �?dispatch compute pass
	// ================================================================
	void VansVKDevice::BuildTileLightLists(VansVKCommandBuffer& cmd)
	{
		UpdateTileLightBuildSets();

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager->m_TileLightBuildShader == nullptr) return;
		if (manager->m_TileLightGridX == 0 || manager->m_TileLightGridY == 0) return;

		uint32_t groupsX = (manager->m_TileLightGridX + 7) / 8;
		uint32_t groupsY = (manager->m_TileLightGridY + 7) / 8;

		cmd.EnsureComputeShader(
			*manager->m_TileLightBuildShader,
			{ m_Scene->m_GlobalDescriptorSetLayout, manager->m_TileLightBuildSetLayout }
		);
		cmd.DispatchCompute(
			*manager->m_TileLightBuildShader,
			groupsX, groupsY, 1,
			{ m_Scene->m_GlobalDescriptorSet, manager->m_TileLightBuildDescriptorSets[0] }
		);

		// Barrier: compute SSBO write �?subsequent compute + fragment SSBO read
		VkMemoryBarrier tileLightBarrier      = {};
		tileLightBarrier.sType                = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		tileLightBarrier.srcAccessMask        = VK_ACCESS_SHADER_WRITE_BIT;
		tileLightBarrier.dstAccessMask        = VK_ACCESS_SHADER_READ_BIT;
		cmd.PipelineBarrier(
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			{ tileLightBarrier }
		);
	}
// ================================================================
// UploadPostProcessProfileIfDirty — Inspector 修改参数后将 CPU Profile
// 打包并写入三条 UBO Buffer。每帧开销可忽略（HOST_VISIBLE memcpy）。
// 注意：m_Time（Film Grain 动画种子）每帧都更新，不依赖 dirty 标记。
// ================================================================
void VansVKDevice::UploadPostProcessProfileIfDirty()
{
	VansMaterialManager* manager = m_Scene->GetMaterialManager();
	if (!manager) return;

	VansPostProcessProfile& profile = manager->m_PostProcessProfile;

	// 编辑器暂停时 GetDeltaTime() 返回 0，使用 GetEditorDeltaTime() 保证非零
	const float deltaTime = static_cast<float>(VansGraphics::VansTimer::GetEditorDeltaTime());
	const float time      = static_cast<float>(VansGraphics::VansTimer::GetFrameTime());

	if (profile.m_IsDirty)
	{
		// Profile 参数变化：重新打包三条 UBO 并上传
		VansPostProcessParamsGPU    ppParams    = profile.ToGPUParams(time);
		VansExposureAdaptParamsGPU  expParams   = profile.ToExposureAdaptParams(deltaTime);
		VansBloomParamsGPU          bloomParams = profile.ToBloomParams();

		manager->m_PostProcessParamsCBBuffer.SetBufferData(&ppParams,    0, sizeof(VansPostProcessParamsGPU));
		manager->m_ExposureAdaptParamsCBBuffer.SetBufferData(&expParams,  0, sizeof(VansExposureAdaptParamsGPU));
		manager->m_BloomParamsCBBuffer.SetBufferData(&bloomParams,        0, sizeof(VansBloomParamsGPU));

		profile.m_IsDirty = false;
	}
	else
	{
		// 即使没有参数修改，也需要每帧更新时间相关字段，否则 Film Grain 动画停止
		// 同时更新 ExposureAdapt 的 deltaTime，确保曝光适应在编辑器模式下正常工作
		static_assert(offsetof(VansPostProcessParamsGPU, m_Time) > 0, "m_Time offset check");
		manager->m_PostProcessParamsCBBuffer.SetBufferData(
			&time,
			static_cast<int>(offsetof(VansPostProcessParamsGPU, m_Time)),
			sizeof(float));

		manager->m_ExposureAdaptParamsCBBuffer.SetBufferData(
			&deltaTime,
			static_cast<int>(offsetof(VansExposureAdaptParamsGPU, m_DeltaTime)),
			sizeof(float));
	}
}

// ================================================================
// Exposure Luminance Compute���� SceneColorHDR ������������
// ================================================================
void VansVKDevice::UpdateExposureDescriptorSets(VansRenderPassManager* renderPassManager)
{
if (m_PPExposureDescSetsUpdated) return;

VansMaterialManager* manager = m_Scene->GetMaterialManager();
VansTexture* lumRT     = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_EXPOSURE_LUMINANCE);
VansTexture* currentRT = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_EXPOSURE_CURRENT);
if (lumRT == nullptr || currentRT == nullptr) return;

m_PPExposureDescSetsUpdated = true;

auto& sceneColor = renderPassManager->GetColor();

VansVKDescriptorManager::GetInstance()->ResetState();

// ExposureLuminance set��SRC��SceneColorHDR��+ DST��64x64 ���ȣ�
VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
{
manager->m_ExposureLuminanceDescriptorSets[0],
EXPOSURE_LUM_BINDING_SRC_COLOR,
0,
VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
{
{
sceneColor.GetSampler(),
sceneColor.GetImageView(),
VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
}
}
}
);
VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
{
manager->m_ExposureLuminanceDescriptorSets[0],
EXPOSURE_LUM_BINDING_LUM_OUT,
0,
VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
{
{
lumRT->GetImage().GetSampler(),
lumRT->GetImage().GetImageView(),
VK_IMAGE_LAYOUT_GENERAL
}
}
}
);

VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
VansVKDescriptorManager::GetInstance()->ResetState();

// ExposureAdapt set��LUM_IN��64x64��+ EXP_OUT��1x1��+ UBO
VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
{
manager->m_ExposureAdaptDescriptorSets[0],
EXPOSURE_ADAPT_BINDING_LUM_IN,
0,
VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
{
{
lumRT->GetImage().GetSampler(),
lumRT->GetImage().GetImageView(),
VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
}
}
}
);
VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
{
manager->m_ExposureAdaptDescriptorSets[0],
EXPOSURE_ADAPT_BINDING_EXP_OUT,
0,
VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
{
{
currentRT->GetImage().GetSampler(),
currentRT->GetImage().GetImageView(),
VK_IMAGE_LAYOUT_GENERAL
}
}
}
);
VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
{
manager->m_ExposureAdaptDescriptorSets[0],
EXPOSURE_ADAPT_BINDING_PARAMS,
0,
VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
{{ manager->m_ExposureAdaptParamsCBBuffer.GetNativeBuffer(), 0, manager->m_ExposureAdaptParamsCBBuffer.GetBufferSize() }}
}
);

VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}

// ================================================================
// UpdateExposure��ÿִ֡�� Luminance ��ͼ + Adapt ��������
// ================================================================
void VansVKDevice::UpdateExposure(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
{
UpdateExposureDescriptorSets(renderPassManager);

VansMaterialManager* manager = m_Scene->GetMaterialManager();

// Step 1��SceneColorHDR �� 64x64 ������ͼ
computeCmd.EnsureComputeShader(*manager->m_ExposureLuminanceShader,
{ m_Scene->m_GlobalDescriptorSetLayout, manager->m_ExposureLuminanceSetLayout });
computeCmd.DispatchCompute(*manager->m_ExposureLuminanceShader,
(64 + 7) / 8, (64 + 7) / 8, 1,
{ m_Scene->m_GlobalDescriptorSet, manager->m_ExposureLuminanceDescriptorSets[0] });

VkMemoryBarrier lumBarrier = {};
lumBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
lumBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
lumBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
computeCmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, { lumBarrier });

// Step 2��64x64 ���� �� 1x1 �ع�����Ӧ���������߳� dispatch��
computeCmd.EnsureComputeShader(*manager->m_ExposureAdaptShader,
{ m_Scene->m_GlobalDescriptorSetLayout, manager->m_ExposureAdaptSetLayout });
computeCmd.DispatchCompute(*manager->m_ExposureAdaptShader,
1, 1, 1,
{ m_Scene->m_GlobalDescriptorSet, manager->m_ExposureAdaptDescriptorSets[0] });

VkMemoryBarrier adaptBarrier = {};
adaptBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
adaptBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
adaptBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
computeCmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, { adaptBarrier });
}

// ================================================================
// Bloom Descriptor Sets��Prefilter + 4xDownsample + 4xUpsample
// ================================================================
void VansVKDevice::UpdateBloomDescriptorSets(VansRenderPassManager* renderPassManager)
{
if (m_PPBloomDescSetsUpdated) return;

VansMaterialManager* manager = m_Scene->GetMaterialManager();
VansTexture* prefilter = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_PREFILTER);
VansTexture* mip0      = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_MIP0);
VansTexture* mip1      = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_MIP1);
VansTexture* mip2      = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_MIP2);
VansTexture* mip3      = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_MIP3);
VansTexture* result    = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_RESULT);
if (!prefilter || !mip0 || !mip1 || !mip2 || !mip3 || !result) return;

m_PPBloomDescSetsUpdated = true;

auto& sceneColor = renderPassManager->GetColor();

// ---- Prefilter ----
VansVKDescriptorManager::GetInstance()->ResetState();
VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
{ manager->m_BloomPrefilterDescriptorSets[0], BLOOM_PREFILTER_BINDING_SRC, 0,
  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
  {{ sceneColor.GetSampler(), sceneColor.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }} });
VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
{ manager->m_BloomPrefilterDescriptorSets[0], BLOOM_PREFILTER_BINDING_DST, 0,
  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
  {{ prefilter->GetImage().GetSampler(), prefilter->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }} });
VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
{ manager->m_BloomPrefilterDescriptorSets[0], BLOOM_PREFILTER_BINDING_PARAMS, 0,
  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
  {{ manager->m_BloomParamsCBBuffer.GetNativeBuffer(), 0, manager->m_BloomParamsCBBuffer.GetBufferSize() }} });
VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();

// ---- Downsample��prefilter��mip0, mip0��mip1, mip1��mip2, mip2��mip3 ----
VansTexture* dsInputs[4]  = { prefilter, mip0, mip1, mip2 };
VansTexture* dsOutputs[4] = { mip0,      mip1, mip2, mip3 };
for (int i = 0; i < 4; ++i)
{
VansVKDescriptorManager::GetInstance()->ResetState();
VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
{ manager->m_BloomDownsampleDescriptorSets[i], BLOOM_DOWNSAMPLE_BINDING_SRC, 0,
  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
  {{ dsInputs[i]->GetImage().GetSampler(), dsInputs[i]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }} });
VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
{ manager->m_BloomDownsampleDescriptorSets[i], BLOOM_DOWNSAMPLE_BINDING_DST, 0,
  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
  {{ dsOutputs[i]->GetImage().GetSampler(), dsOutputs[i]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }} });
VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}

// ---- Upsample��mip3��mip2, mip2��mip1, mip1��mip0, mip0��result ----
// SRC_LO = �ϵͷֱ��ʣ���ǰ mip����SRC_HI = �ϸ߷ֱ��ʣ�ǰһ�������
VansTexture* usLo[4] = { mip3, mip2, mip1, mip0 };
VansTexture* usHi[4] = { mip2, mip1, mip0, prefilter };
VansTexture* usDst[4] = { mip2, mip1, mip0, result };
for (int i = 0; i < 4; ++i)
{
VansVKDescriptorManager::GetInstance()->ResetState();
VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
{ manager->m_BloomUpsampleDescriptorSets[i], BLOOM_UPSAMPLE_BINDING_SRC_LO, 0,
  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
  {{ usLo[i]->GetImage().GetSampler(), usLo[i]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }} });
VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
{ manager->m_BloomUpsampleDescriptorSets[i], BLOOM_UPSAMPLE_BINDING_SRC_HI, 0,
  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
  {{ usHi[i]->GetImage().GetSampler(), usHi[i]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }} });
VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
{ manager->m_BloomUpsampleDescriptorSets[i], BLOOM_UPSAMPLE_BINDING_DST, 0,
  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
  {{ usDst[i]->GetImage().GetSampler(), usDst[i]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }} });
VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
{ manager->m_BloomUpsampleDescriptorSets[i], BLOOM_UPSAMPLE_BINDING_PARAMS, 0,
  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
  {{ manager->m_BloomParamsCBBuffer.GetNativeBuffer(), 0, manager->m_BloomParamsCBBuffer.GetBufferSize() }} });
VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}
}

// ================================================================
// UpdateBloom��ÿִ֡�� Prefilter + 4xDownsample + 4xUpsample
// ================================================================
void VansVKDevice::UpdateBloom(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd)
{
UpdateBloomDescriptorSets(renderPassManager);

VansMaterialManager* manager = m_Scene->GetMaterialManager();

// ��������ֱ��ʵ� dispatch group
const uint32_t w2  = m_RenderWidth  / 2,  h2  = m_RenderHeight  / 2;
const uint32_t w4  = m_RenderWidth  / 4,  h4  = m_RenderHeight  / 4;
const uint32_t w8  = m_RenderWidth  / 8,  h8  = m_RenderHeight  / 8;
const uint32_t w16 = m_RenderWidth  / 16, h16 = m_RenderHeight  / 16;

auto groups = [](uint32_t n) { return (n + 7) / 8; };

VkMemoryBarrier stageBarrier = {};
stageBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
stageBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
stageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
auto Barrier = [&]() {
computeCmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, { stageBarrier });
};

// ---- Prefilter��SceneColorHDR �� prefilter��1/2 �ֱ��ʣ�----
computeCmd.EnsureComputeShader(*manager->m_BloomPrefilterShader,
{ m_Scene->m_GlobalDescriptorSetLayout, manager->m_BloomPrefilterSetLayout });
computeCmd.DispatchCompute(*manager->m_BloomPrefilterShader, groups(w2), groups(h2), 1,
{ m_Scene->m_GlobalDescriptorSet, manager->m_BloomPrefilterDescriptorSets[0] });
Barrier();

// ---- Downsample chain ----
computeCmd.EnsureComputeShader(*manager->m_BloomDownsampleShader,
{ m_Scene->m_GlobalDescriptorSetLayout, manager->m_BloomDownsampleSetLayout });

uint32_t dsW[4] = { w2, w4, w8, w16 };
uint32_t dsH[4] = { h2, h4, h8, h16 };
for (int i = 0; i < 4; ++i)
{
computeCmd.DispatchCompute(*manager->m_BloomDownsampleShader, groups(dsW[i]), groups(dsH[i]), 1,
{ m_Scene->m_GlobalDescriptorSet, manager->m_BloomDownsampleDescriptorSets[i] });
Barrier();
}

// ---- Upsample chain ----
computeCmd.EnsureComputeShader(*manager->m_BloomUpsampleShader,
{ m_Scene->m_GlobalDescriptorSetLayout, manager->m_BloomUpsampleSetLayout });

uint32_t usW[4] = { w16, w8, w4, w2 };
uint32_t usH[4] = { h16, h8, h4, h2 };
for (int i = 0; i < 4; ++i)
{
computeCmd.DispatchCompute(*manager->m_BloomUpsampleShader, groups(usW[i]), groups(usH[i]), 1,
{ m_Scene->m_GlobalDescriptorSet, manager->m_BloomUpsampleDescriptorSets[i] });
Barrier();
}
}

} // namespace VansGraphics
