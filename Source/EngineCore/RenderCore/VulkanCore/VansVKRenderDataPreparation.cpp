#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKDevice.h"
#include "VansVKDescriptorManager.h"
#include "VansRenderPass.h"
#include "VansMesh.h"
#include "VansShader.h"
#include "../VansScene.h"
#include "../../Configration/VansConfigration.h"
#include <cmath>

namespace VansGraphics
{
	void VansVKDevice::PreparePBRMaterialData()
	{
		auto& allmaterials = m_Scene->m_Materials;
		auto materialManager = m_Scene->GetMaterialManager();
		int materialCount = allmaterials.size();
		int pbrMaterialIndex = 0;
		for (int materialIndex = 0; materialIndex < materialCount; ++materialIndex)
		{
			auto material = static_cast<VansMaterial*>(allmaterials[materialIndex]);
			if (material->m_MaterialType == VansMaterialType::VAN_PBR)
			{
				int index = pbrMaterialIndex++;
				material->m_MaterialPushConstant.materialIndex = index;
				materialManager->m_GlobalPBRMaterial.push_back(material);
				materialManager->m_GlobalPBRParamData.push_back(material->m_BasePBRParam);
				materialManager->m_GlobalPBRTextures.push_back(&(material->m_BaseColorTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(material->m_NormalTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(material->m_MetalTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(material->m_RoughnessTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(material->m_AoTexture->GetImage()));
			}
		}

		materialManager->m_GlobalPBRDataBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice,
			sizeof(VansBasePBRParam) * materialManager->m_GlobalPBRMaterial.size(),
			VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

		materialManager->m_GlobalPBRDataBuffer.SetBufferData(
			materialManager->m_GlobalPBRParamData.data(),
			0,
			sizeof(VansBasePBRParam) * materialManager->m_GlobalPBRMaterial.size());

		VkDescriptorSetLayoutBinding globalPBRMaterialBufferBinding =
		{
			PassBinding::BUFFER_0,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			1,
			VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR,
			nullptr
		};
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ globalPBRMaterialBufferBinding }, materialManager->m_GlobalPBRDataSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ materialManager->m_GlobalPBRDataSetLayout }, materialManager->m_GlobalPBRDataDescriptorSets);

		VansVKDescriptorManager::GetInstance()->ResetState();
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
			{
				materialManager->m_GlobalPBRDataDescriptorSets[0],
				PassBinding::BUFFER_0,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				{
					{
						materialManager->m_GlobalPBRDataBuffer.GetNativeBuffer(),
						0,
						materialManager->m_GlobalPBRDataBuffer.GetBufferSize()
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();

		VkDescriptorSetLayoutBinding bindlessTextureArrayBinding =
		{
			GLOBAL_BINDING_BINDLESS_TEXTURES,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			MAX_BINDLESS_TEXTURES,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			nullptr
		};

		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ bindlessTextureArrayBinding }, materialManager->m_GlobalPBRTexSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ materialManager->m_GlobalPBRTexSetLayout }, materialManager->m_GlobalPBRTexDescriptorSets);

		auto& bindlessTextures = materialManager->m_GlobalPBRTextures;
		std::vector<VkDescriptorImageInfo> bindlessTextureInfos;
		for (size_t i = 0; i < bindlessTextures.size(); i++)
		{
			bindlessTextureInfos.push_back(
				{
					bindlessTextures[i]->GetSampler(),
					bindlessTextures[i]->GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			);
		}
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				materialManager->m_GlobalPBRTexDescriptorSets[0],
				GLOBAL_BINDING_BINDLESS_TEXTURES,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				bindlessTextureInfos
			}
		);

		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
	}

	void VansVKDevice::PrepareInstanceTransformData()
	{
		std::vector<VansRenderNode*> allRenderNodes;
		auto& allCommonNodes = m_Scene->m_OpaqueRenderNodes;
		allRenderNodes.insert(allRenderNodes.end(), allCommonNodes.begin(), allCommonNodes.end());
		auto& allShadowNodes = m_Scene->m_ShadowRenderNodes;
		allRenderNodes.insert(allRenderNodes.end(), allShadowNodes.begin(), allShadowNodes.end());
		auto& allPunctualShadowNodes = m_Scene->m_PunctualShadowRenderNodes;
		allRenderNodes.insert(allRenderNodes.end(), allPunctualShadowNodes.begin(), allPunctualShadowNodes.end());

		int nodeCount = allRenderNodes.size();
		m_Scene->m_InstanceTransformDataBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice,
			sizeof(ModelDataStruct) * nodeCount,
			VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

		for (int nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
		{
			auto node = allRenderNodes[nodeIndex];
			node->BeforeDrawCall();
			m_Scene->m_InstanceTransformData.push_back(node->m_ModelData);
			node->m_TransfromIndex = nodeIndex;
		}
		m_Scene->m_InstanceTransformDataBuffer.SetBufferData(
			m_Scene->m_InstanceTransformData.data(),
			0,
			sizeof(ModelDataStruct) * nodeCount);

		VkDescriptorSetLayoutBinding instanceTransformBufferBinding =
		{
			PassBinding::BUFFER_0,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			1,
			VK_SHADER_STAGE_VERTEX_BIT,
			nullptr
		};
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ instanceTransformBufferBinding }, m_Scene->m_GlobalTransformDataSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_Scene->m_GlobalTransformDataSetLayout }, m_Scene->m_GlobalTransformDataDescriptorSets);
		VansVKDescriptorManager::GetInstance()->ResetState();
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
			{
				m_Scene->m_GlobalTransformDataDescriptorSets[0],
				PassBinding::BUFFER_0,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				{
					{
						m_Scene->m_InstanceTransformDataBuffer.GetNativeBuffer(),
						0,
						m_Scene->m_InstanceTransformDataBuffer.GetBufferSize()
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
	}

	void VansVKDevice::PrepareSkyRenderData()
	{
		auto vansConfigration = VansConfigration::GetInstance();
		std::string projectRoot = vansConfigration->GetProjectRootPath();
		VansTexture* texture = new VansTexture();
		texture->LoadCubeTexture(m_VansVKCommandBuffer, (projectRoot + "EngineAssets/Textures/SkyBox").c_str());

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		manager->m_PreConvDiffuse = new VansTexture();
		manager->m_PreConvDiffuse->InitTextureWithoutData(m_VansVKCommandBuffer, 512, 512, 1, 4, true, false, true);

		manager->m_PreConvSpecular = new VansTexture();
		manager->m_PreConvSpecular->InitTextureWithoutData(m_VansVKCommandBuffer, 512, 512, 1, 4, true, true, true);

		manager->m_BRDFIntegralLUT = new VansTexture();
		manager->m_BRDFIntegralLUT->LoadTexture(m_VansVKCommandBuffer, (projectRoot + "EngineAssets/Textures/BRDFIntegralLUT.png").c_str(), false, false, false);

		VansVKBuffer prefilterCBBuffer;
		uint32_t mipCount = log2(512);
		float data[4] = { 512,mipCount,512,512 };
		prefilterCBBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice, sizeof(float) * 4, VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		prefilterCBBuffer.SetBufferData(data, 0, sizeof(float) * 4);

		manager->m_SkySHResultBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice, sizeof(float) * 27, VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		manager->m_SkySHResultBuffer.SetBufferData(data, 0, sizeof(float) * 27);

		VkDescriptorSetLayoutBinding samplerLUTBinding =
		{
			PassBinding::TEXTURE_0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding sampleDiffuseConvBinding =
		{
			PassBinding::TEXTURE_1,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			nullptr
		};

		VkDescriptorSetLayoutBinding sampleSpecularConBinding =
		{
			PassBinding::TEXTURE_2,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			nullptr
		};

		VkDescriptorSetLayoutBinding environmentSHBuffer =
		{
			PassBinding::BUFFER_3,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			1,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			nullptr
		};
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ samplerLUTBinding,sampleDiffuseConvBinding,sampleSpecularConBinding,environmentSHBuffer }, manager->m_BRDFInterationTexSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ manager->m_BRDFInterationTexSetLayout }, manager->m_BRDFInterationTextDescriptorSets);

		VansComputeShader* m_PreConvDiffuseShader = new VansComputeShader();
		m_PreConvDiffuseShader->InitShader(m_VansVKLogicDevice, (projectRoot + "EngineAssets/Shaders/PreConDiffuseEnvironment").c_str());

		VansComputeShader* m_PreConvSpecularShader = new VansComputeShader();
		m_PreConvSpecularShader->InitShader(m_VansVKLogicDevice, (projectRoot + "EngineAssets/Shaders/PreConSpecularEnvironment").c_str());

		VkDescriptorSetLayoutBinding samplerCubeBinding =
		{
			PassBinding::TEXTURE_0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding uavCubeBinding0 =
		{
			PassBinding::UAV_IMAGE_0,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};

		VkDescriptorSetLayoutBinding uavCubeBinding1 =
		{
			PassBinding::UAV_IMAGE_1,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			mipCount,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};

		VkDescriptorSetLayoutBinding prefilterCB =
		{
			PassBinding::CBUFFER_3,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};

		VkDescriptorSetLayoutBinding shResultBuffer =
		{
			PassBinding::BUFFER_4,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayout m_PreConvSetLayout;
		std::vector<VkDescriptorSet> m_PreConvtDescriptorSets;
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ samplerCubeBinding,uavCubeBinding0,uavCubeBinding1,prefilterCB,shResultBuffer }, m_PreConvSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_PreConvSetLayout }, m_PreConvtDescriptorSets);

		VansVKDescriptorManager::GetInstance()->ResetState();
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
			{
				m_PreConvtDescriptorSets[0],
				PassBinding::CBUFFER_3,
				0,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				{
					{
						prefilterCBBuffer.GetNativeBuffer(),
						0,
						prefilterCBBuffer.GetBufferSize()
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
			{
				m_PreConvtDescriptorSets[0],
				PassBinding::BUFFER_4,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				{
					{
						manager->m_SkySHResultBuffer.GetNativeBuffer(),
						0,
						manager->m_SkySHResultBuffer.GetBufferSize()
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				m_PreConvtDescriptorSets[0],
				PassBinding::TEXTURE_0,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						texture->GetImage().GetSampler(),
						texture->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				m_PreConvtDescriptorSets[0],
				PassBinding::UAV_IMAGE_0,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						manager->m_PreConvDiffuse->GetImage().GetSampler(),
						manager->m_PreConvDiffuse->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		std::vector<VkDescriptorImageInfo> cubeMipImageInfos;
		for (int mipLevel = 0; mipLevel < mipCount; mipLevel++)
		{
			cubeMipImageInfos.push_back(
				{
					manager->m_PreConvSpecular->GetImage().GetSampler(),
					manager->m_PreConvSpecular->GetImage().GetImageMipView(mipLevel),
					VK_IMAGE_LAYOUT_GENERAL
				}
			);
		}
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				m_PreConvtDescriptorSets[0],
				PassBinding::UAV_IMAGE_1,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				cubeMipImageInfos
			}
		);
		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();

		m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		m_VansVKCommandBuffer.EnsureComputeShader(*m_PreConvDiffuseShader, { m_PreConvSetLayout });
		m_VansVKCommandBuffer.DispatchCompute(*m_PreConvDiffuseShader, 512, 512, 1, m_PreConvtDescriptorSets);

		m_VansVKCommandBuffer.EnsureComputeShader(*m_PreConvSpecularShader, { m_PreConvSetLayout });
		m_VansVKCommandBuffer.DispatchCompute(*m_PreConvSpecularShader, 512, 512, mipCount, m_PreConvtDescriptorSets);

		manager->m_AtmospherePBRDataBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice, sizeof(VansAtmospherePBRParam), VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

		VkDescriptorSetLayoutBinding stmosphereUnifomBuffer =
		{
			SKYBOX_BINDING_ATMOSPHERE_UBO,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			1,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			nullptr
		};
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ stmosphereUnifomBuffer }, manager->m_MaterialAtmosphereDataLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ manager->m_MaterialAtmosphereDataLayout }, manager->m_MaterialAtmosphereDataDescriptorSets);

		manager->UpdatePBRLutDescriptorSets();
		manager->UpdateAtmosphereDescriptorSets();

		manager->m_PreConvSpecular->GetImage().SetImageMemoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			{
				manager->m_PreConvSpecular->GetImage().m_VansVKImage,
				VK_ACCESS_NONE,
				VK_ACCESS_NONE,
				VK_IMAGE_LAYOUT_GENERAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				manager->m_PreConvSpecular->GetImage().m_ImageAspect
			});
		manager->m_PreConvDiffuse->GetImage().SetImageMemoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			{
				manager->m_PreConvDiffuse->GetImage().m_VansVKImage,
				VK_ACCESS_NONE,
				VK_ACCESS_NONE,
				VK_IMAGE_LAYOUT_GENERAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				manager->m_PreConvDiffuse->GetImage().m_ImageAspect
			});

		m_VansVKCommandBuffer.EndCommandBufferRecord();
		VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, {}, {}, m_VansVKCommandBuffer.m_CommandBufferFinishSubmitFence);
		m_VansVKCommandBuffer.ResetCommandBuffer(false);

		prefilterCBBuffer.DestroyVulkanBuffer(m_VansVKLogicDevice);
	}

	void VansVKDevice::PrepareSSAORenderData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		VansTexture* ssaoResult = new VansTexture();
		ssaoResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSAO_RESULT, ssaoResult);
	}

	void VansVKDevice::PrepareSSGIRenderData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		VansTexture* ssgiResult = new VansTexture();
		ssgiResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSGI_RESULT, ssgiResult);

		VansTexture* ssgiFilterResult = new VansTexture();
		ssgiFilterResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSGI_FILTER_RESULT, ssgiFilterResult);

		VansTexture* ssgiTemporalA = new VansTexture();
		ssgiTemporalA->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSGI_TEMPORAL_A, ssgiTemporalA);

		VansTexture* ssgiTemporalB = new VansTexture();
		ssgiTemporalB->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSGI_TEMPORAL_B, ssgiTemporalB);
		manager->m_SSGITemporalFrame = 0;

		auto vansConfigration = VansConfigration::GetInstance();
		std::string projectRoot = vansConfigration->GetProjectRootPath();
		manager->m_SSGIShader = new VansComputeShader();
		manager->m_SSGIShader->InitShader(m_VansVKLogicDevice, (projectRoot + "EngineAssets/Shaders/SSGI").c_str());

		manager->m_SSGITemporalShader = new VansComputeShader();
		manager->m_SSGITemporalShader->InitShader(m_VansVKLogicDevice, (projectRoot + "EngineAssets/Shaders/SSGITemporal").c_str());

		float data[4] = { std::floor(m_RenderWidth / 2) , std::floor(m_RenderHeight / 2), 2.0f / m_RenderWidth, 2.0f / m_RenderHeight };
		manager->m_SSGICBBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice, sizeof(float) * 4, VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		manager->m_SSGICBBuffer.SetBufferData(data, 0, sizeof(float) * 4);

		manager->m_SSGITemporalCBBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice, sizeof(float) * 4, VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		manager->m_SSGITemporalCBBuffer.SetBufferData(data, 0, sizeof(float) * 4);

		VkDescriptorSetLayoutBinding normalBinding =
		{
			PassBinding::TEXTURE_0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding depthBinding =
		{
			PassBinding::TEXTURE_1,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding colorBinding =
		{
			PassBinding::TEXTURE_2,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding positionBinding =
		{
			PassBinding::TEXTURE_3,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding skyDiffuseBinding =
		{
			PassBinding::TEXTURE_4,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding SSGIResultBinding =
		{
			PassBinding::UAV_IMAGE_4,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding SSGICBBinding =
		{
			PassBinding::CBUFFER_6,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding SHRBinding =
		{
			PassBinding::TEXTURE_7,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding SHGBinding =
		{
			PassBinding::TEXTURE_8,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding SHBBinding =
		{
			PassBinding::TEXTURE_9,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding HIZBinding =
		{
			PassBinding::TEXTURE_10,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ normalBinding,depthBinding,colorBinding,positionBinding,skyDiffuseBinding,SSGIResultBinding,SSGICBBinding, SHRBinding, SHGBinding, SHBBinding ,HIZBinding }, manager->m_SSGITexSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ manager->m_SSGITexSetLayout }, manager->m_SSGIDescriptorSets);

		VkDescriptorSetLayoutBinding temporalDepthBinding =
		{
			SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_DEPTH,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr
		};
		VkDescriptorSetLayoutBinding temporalMotionBinding =
		{
			SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_MOTION_VECTOR,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr
		};
		VkDescriptorSetLayoutBinding temporalHistoryBinding =
		{
			SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_HISTORY_GI,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr
		};
		VkDescriptorSetLayoutBinding temporalCurrentBinding =
		{
			SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_CURRENT_GI,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr
		};
		VkDescriptorSetLayoutBinding temporalAccumBinding =
		{
			SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_ACCUMULATED_GI,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr
		};
		VkDescriptorSetLayoutBinding temporalInfoBinding =
		{
			SSGITemporalPassBinding::SSGI_TEMPORAL_BINDING_INFO_UBO,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr
		};
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout(
			{ temporalDepthBinding, temporalMotionBinding, temporalHistoryBinding, temporalCurrentBinding, temporalAccumBinding, temporalInfoBinding },
			manager->m_SSGITemporalSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet(
			{ manager->m_SSGITemporalSetLayout, manager->m_SSGITemporalSetLayout },
			manager->m_SSGITemporalDescriptorSets);
	}

	void VansVKDevice::PrepareHZBRenderData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		VansTexture* hzbResult = new VansTexture();
		hzbResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 1, 1, false, true, true, MID_PRES_16);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_HZB_RESULT, hzbResult);

		auto vansConfigration = VansConfigration::GetInstance();
		std::string projectRoot = vansConfigration->GetProjectRootPath();
		manager->m_HZBShader = new VansComputeShader();
		manager->m_HZBShader->InitShader(m_VansVKLogicDevice, (projectRoot + "EngineAssets/Shaders/HIZ").c_str());

		manager->m_HIZMipCount = 1 + (int)std::floor(std::log2(std::min(m_RenderWidth, m_RenderHeight)));
		manager->m_HZBTexSetLayouts.resize(manager->m_HIZMipCount - 1);
		for (int mipIndex = 0; mipIndex < manager->m_HIZMipCount - 1; mipIndex++)
		{
			VkDescriptorSetLayoutBinding depthInput =
			{
				PassBinding::UAV_IMAGE,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				1,
				VK_SHADER_STAGE_COMPUTE_BIT,
				nullptr
			};
			VkDescriptorSetLayoutBinding depthOuput =
			{
				PassBinding::UAV_IMAGE_0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				1,
				VK_SHADER_STAGE_COMPUTE_BIT,
				nullptr
			};
			VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ depthInput,depthOuput }, manager->m_HZBTexSetLayouts[mipIndex]);
		}
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet(manager->m_HZBTexSetLayouts, manager->m_HZBDescriptorSets);
	}

	void VansVKDevice::PrepareSSRRenderData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		VansTexture* ssrHitInfo = new VansTexture();
		ssrHitInfo->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true, MID_PRES_16);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSR_HIT_INFO, ssrHitInfo);

		VansTexture* ssrRayPdf = new VansTexture();
		ssrRayPdf->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true, HIGH_PRES_32);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSR_RAY_PDF, ssrRayPdf);

		VansTexture* ssrResult = new VansTexture();
		ssrResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true, HIGH_PRES_32);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSR_RESULT, ssrResult);

		VansTexture* ssrAaResultA = new VansTexture();
		ssrAaResultA->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true, HIGH_PRES_32);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSRAA_RESULT_A, ssrAaResultA);

		VansTexture* ssrAaResultB = new VansTexture();
		ssrAaResultB->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true, HIGH_PRES_32);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSRAA_RESULT_B, ssrAaResultB);

		VansTexture* ssrAaResult = new VansTexture();
		ssrAaResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true, HIGH_PRES_32);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSRAA_RESULT, ssrAaResult);

		auto vansConfigration = VansConfigration::GetInstance();
		std::string projectRoot = vansConfigration->GetProjectRootPath();
		manager->m_SSRTraceShader = new VansComputeShader();
		manager->m_SSRTraceShader->InitShader(m_VansVKLogicDevice, (projectRoot + "EngineAssets/Shaders/SSR_TRACE").c_str());

		manager->m_SSRResolveShader = new VansComputeShader();
		manager->m_SSRResolveShader->InitShader(m_VansVKLogicDevice, (projectRoot + "EngineAssets/Shaders/SSR_RESOLVE").c_str());

		manager->m_SSRTemporalAAShader = new VansComputeShader();
		manager->m_SSRTemporalAAShader->InitShader(m_VansVKLogicDevice, (projectRoot + "EngineAssets/Shaders/SSR_TEMPORALAA").c_str());

		VkDescriptorSetLayoutBinding normalInput =
		{
			PassBinding::TEXTURE_0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding roughnessInput =
		{
			PassBinding::TEXTURE_1,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding positionInput =
		{
			PassBinding::TEXTURE_2,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding hizInput =
		{
			PassBinding::TEXTURE_3,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding tranceInfoResult =
		{
			PassBinding::UAV_IMAGE_3,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding trancePDFResult =
		{
			PassBinding::UAV_IMAGE_4,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ normalInput,roughnessInput,positionInput,hizInput,tranceInfoResult,trancePDFResult }, manager->m_SSRTraceSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ manager->m_SSRTraceSetLayout }, manager->m_SSRTraceDescriptorSets);

		VkDescriptorSetLayoutBinding colorInput =
		{
			PassBinding::TEXTURE_0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		roughnessInput =
		{
			PassBinding::TEXTURE_1,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		normalInput =
		{
			PassBinding::TEXTURE_2,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		positionInput =
		{
			PassBinding::TEXTURE_3,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		tranceInfoResult =
		{
			PassBinding::UAV_IMAGE_3,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		trancePDFResult =
		{
			PassBinding::UAV_IMAGE_4,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding resolveResult =
		{
			PassBinding::UAV_IMAGE_5,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ colorInput,roughnessInput,normalInput,positionInput, tranceInfoResult ,trancePDFResult,resolveResult }, manager->m_SSRResolveSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ manager->m_SSRResolveSetLayout }, manager->m_SSRResolveDescriptorSets);

		colorInput =
		{
			PassBinding::TEXTURE_0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		positionInput =
		{
			PassBinding::TEXTURE_1,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding aaResultA =
		{
			PassBinding::UAV_IMAGE_1,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding aaResultB =
		{
			PassBinding::UAV_IMAGE_2,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding aaResult =
		{
			PassBinding::UAV_IMAGE_3,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ colorInput,positionInput, tranceInfoResult ,aaResultA,aaResultB,aaResult }, manager->m_SSRAASetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ manager->m_SSRAASetLayout }, manager->m_SSRAADescriptorSets);
	}

	void VansVKDevice::PrepareVolumetricData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		VansTexture* volumetricFogResult = new VansTexture();
		volumetricFogResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true, HIGH_PRES_32);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_VOLUMETRIC_FOG_RESULT, volumetricFogResult);

		auto vansConfigration = VansConfigration::GetInstance();
		std::string projectRoot = vansConfigration->GetProjectRootPath();
		manager->m_VolumetrcFogShader = new VansComputeShader();
		manager->m_VolumetrcFogShader->InitShader(m_VansVKLogicDevice, (projectRoot + "EngineAssets/Shaders/VolumetricFog").c_str());

		VkDescriptorSetLayoutBinding positionInput =
		{
			PassBinding::TEXTURE_0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding mainlightShadow =
		{
			PassBinding::TEXTURE_1,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding fogResult =
		{
			PassBinding::UAV_IMAGE_1,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};

		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ positionInput,mainlightShadow, fogResult }, manager->m_VolumetricFogSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ manager->m_VolumetricFogSetLayout }, manager->m_VolumetricFogDescriptorSets);
	}

	void VansVKDevice::PrepareBilaterFilterData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		VansTexture* ssaoFilterResult = new VansTexture();
		ssaoFilterResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true, MID_PRES_16);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSAO_FILTER_RESULT, ssaoFilterResult);

		VkDescriptorSetLayoutBinding colorInput =
		{
			PassBinding::TEXTURE_0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding depthInput =
		{
			PassBinding::TEXTURE_1,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding result =
		{
			PassBinding::UAV_IMAGE_1,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};

		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ colorInput,depthInput,result }, manager->m_BilateralFilterSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ manager->m_BilateralFilterSetLayout,manager->m_BilateralFilterSetLayout,manager->m_BilateralFilterSetLayout }, manager->m_BilateralFilterDescriptorSets);

		manager->m_BilateralFilterPushConstant =
		{
			2.0f,
			0.02f,
			3,
			0.01f
		};

		auto vansConfigration = VansConfigration::GetInstance();
		std::string projectRoot = vansConfigration->GetProjectRootPath();
		manager->m_BilateralFilterShader = new VansComputeShader();
		manager->m_BilateralFilterShader->InitShader(m_VansVKLogicDevice, (projectRoot + "EngineAssets/Shaders/BilateralFilter").c_str());
		manager->m_BilateralFilterShader->SetPushConstant(sizeof(manager->m_BilateralFilterPushConstant));
		manager->m_BilateralFilterShader->SetPushConstantData(&(manager->m_BilateralFilterPushConstant));
	}

	void VansVKDevice::PrepareRenderingData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		manager->ClearRuntimeRenderTextures();

		PrepareSkyRenderData();
		PrepareSSAORenderData();
		PrepareSSGIRenderData();
		PrepareBilaterFilterData();
		PrepareHZBRenderData();
		PrepareSSRRenderData();
		PrepareVolumetricData();

#ifdef _DEBUG
		VkDebugUtilsObjectNameInfoEXT nameInfo = {};
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;

		VansTexture* ssaoResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_SSAO_RESULT);
		VansTexture* ssgiResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_SSGI_RESULT);
		VansTexture* ssrResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_SSR_RESULT);
		VansTexture* ssrHitInfo = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_SSR_HIT_INFO);
		VansTexture* ssrRayPdf = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_SSR_RAY_PDF);

		nameInfo.objectHandle = reinterpret_cast<uint64_t>(manager->m_PreConvDiffuse->GetImage().GetImage());
		nameInfo.pObjectName = "PreConvDiffuse";
		vkSetDebugUtilsObjectNameEXT(m_VansVKLogicDevice, &nameInfo);

		nameInfo.objectHandle = reinterpret_cast<uint64_t>(manager->m_PreConvSpecular->GetImage().GetImage());
		nameInfo.pObjectName = "PreConvSpecular";
		vkSetDebugUtilsObjectNameEXT(m_VansVKLogicDevice, &nameInfo);

		if (ssaoResult)
		{
			nameInfo.objectHandle = reinterpret_cast<uint64_t>(ssaoResult->GetImage().GetImage());
			nameInfo.pObjectName = "SSAOResult";
			vkSetDebugUtilsObjectNameEXT(m_VansVKLogicDevice, &nameInfo);
		}

		if (ssgiResult)
		{
			nameInfo.objectHandle = reinterpret_cast<uint64_t>(ssgiResult->GetImage().GetImage());
			nameInfo.pObjectName = "SSGIResult";
			vkSetDebugUtilsObjectNameEXT(m_VansVKLogicDevice, &nameInfo);
		}

		if (ssrResult)
		{
			nameInfo.objectHandle = reinterpret_cast<uint64_t>(ssrResult->GetImage().GetImage());
			nameInfo.pObjectName = "SSRResult";
			vkSetDebugUtilsObjectNameEXT(m_VansVKLogicDevice, &nameInfo);
		}

		if (ssrHitInfo)
		{
			nameInfo.objectHandle = reinterpret_cast<uint64_t>(ssrHitInfo->GetImage().GetImage());
			nameInfo.pObjectName = "SSRHitInfo";
			vkSetDebugUtilsObjectNameEXT(m_VansVKLogicDevice, &nameInfo);
		}

		if (ssrRayPdf)
		{
			nameInfo.objectHandle = reinterpret_cast<uint64_t>(ssrRayPdf->GetImage().GetImage());
			nameInfo.pObjectName = "SSRRayPDF";
			vkSetDebugUtilsObjectNameEXT(m_VansVKLogicDevice, &nameInfo);
		}
#endif
	}

	void VansVKDevice::PrepareRayTracingData()
	{
		m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		m_Scene->BuildRayTracingAS(this, &m_VansVKCommandBuffer);
		m_VansVKCommandBuffer.EndCommandBufferRecord();
		VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, {}, {}, m_VansVKCommandBuffer.m_CommandBufferFinishSubmitFence);
		m_VansVKCommandBuffer.ResetCommandBuffer(false);
		m_Scene->ReleaseASTempBuffer(this);
		rayTracingContext.CreateRayTracingResource(this, &m_VansVKCommandBuffer, m_Scene);
	}

	void VansVKDevice::PrepareGlobalIllumiationData()
	{
	}
}
