#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKDevice.h"
#include "VansVKDescriptorManager.h"
#include "VansDescriptorSetLayouts.h"
#include "VansRenderPass.h"
#include "VansMesh.h"
#include "VansShader.h"
#include "../VansScene.h"
#include "../../Configration/VansConfigration.h"
#include "../../Util/VansLog.h"
#include "../LTC/LTCData.h"
#include "../VansPostProcessProfile.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstdint>

namespace VansGraphics
{
	void VansVKDevice::PreparePBRMaterialData()
	{
		const auto& allmaterials = m_Scene->GetMaterialAssets();
		auto materialManager = m_Scene->GetMaterialManager();
		int materialCount = static_cast<int>(allmaterials.size());
		int pbrMaterialIndex = 0;
		for (int materialIndex = 0; materialIndex < materialCount; ++materialIndex)
		{
			auto material = static_cast<VansMaterial*>(allmaterials[materialIndex]);
			if (material->m_MaterialType == VansMaterialType::VAN_PBR)
			{
				VansPBRMaterial* pbr = static_cast<VansPBRMaterial*>(material);
				int index = pbrMaterialIndex++;
				pbr->m_MaterialIndex = index;
				materialManager->m_GlobalPBRMaterial.push_back(pbr);
				materialManager->m_GlobalPBRParamData.push_back(pbr->m_BasePBRParam);
				materialManager->m_GlobalPBRTextures.push_back(&(pbr->m_BaseColorTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(pbr->m_NormalTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(pbr->m_MetalTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(pbr->m_RoughnessTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(pbr->m_AoTexture->GetImage()));
			}
			else if (material->m_MaterialType == VansMaterialType::VAN_EMISSIVE)
			{
				VansEmissiveMaterial* emissive = static_cast<VansEmissiveMaterial*>(material);
				emissive->m_MaterialIndex = pbrMaterialIndex++;
				materialManager->m_GlobalPBRParamData.push_back(emissive->m_BasePBRParam);

				// Slots 1-4: not used by Emissive.frag but must be present to keep the 5-slot stride intact
				materialManager->m_GlobalPBRTextures.push_back(&(emissive->m_EmissiveTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(emissive->m_EmissiveTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(emissive->m_EmissiveTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(emissive->m_EmissiveTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(emissive->m_EmissiveTexture->GetImage()));
			}
			else if (material->m_MaterialType == VansMaterialType::VAN_DECAL)
			{
				VansDecalMaterial* decal = static_cast<VansDecalMaterial*>(material);
				decal->m_MaterialIndex = pbrMaterialIndex++;
				materialManager->m_GlobalPBRParamData.push_back(decal->m_BasePBRParam);
				materialManager->m_GlobalPBRTextures.push_back(&(decal->m_BaseColorTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(decal->m_NormalTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(decal->m_MetalTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(decal->m_RoughnessTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(decal->m_AoTexture->GetImage()));
			}
			else if (material->m_MaterialType == VansMaterialType::VAN_SUBSURFACE)
			{
				VansSubsurfaceMaterial* sss = static_cast<VansSubsurfaceMaterial*>(material);
				sss->m_MaterialIndex = pbrMaterialIndex++;
				sss->m_BasePBRParam.m_albedo = sss->m_SubsurfaceColor;
				sss->m_BasePBRParam.m_roughness = sss->m_SubsurfacePower;
				sss->m_BasePBRParam.m_metallic = sss->m_Thickness;
				sss->m_BasePBRParam.m_ao = sss->m_SubsurfaceAmount;
				sss->m_BasePBRParam.padding = sss->m_CurvatureInfluence;

				materialManager->m_GlobalPBRParamData.push_back(sss->m_BasePBRParam);
				materialManager->m_GlobalPBRTextures.push_back(&(sss->m_BaseColorTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(sss->m_NormalTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(sss->m_BaseColorTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(sss->m_RoughnessTexture->GetImage()));
				materialManager->m_GlobalPBRTextures.push_back(&(sss->m_BaseColorTexture->GetImage()));
			}
		}

		const VkDeviceSize materialDataSize = sizeof(VansBasePBRParam) * materialManager->m_GlobalPBRParamData.size();
		materialManager->m_GlobalPBRDataBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice,
			std::max<VkDeviceSize>(materialDataSize, sizeof(VansBasePBRParam)),
			VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

		if (materialDataSize > 0)
		{
			materialManager->m_GlobalPBRDataBuffer.SetBufferData(
				materialManager->m_GlobalPBRParamData.data(), 0, static_cast<int>(materialDataSize));
		}

		// Keep the PBR material buffer persistently mapped for fast per-frame CPU writes
		materialManager->m_GlobalPBRDataBuffer.PersistentMap();

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
		std::vector<VansRenderNode*> allRenderNodes =
			m_Scene->CollectSSBOManagedRenderNodes();

		const uint32_t maxCapacity = m_Scene->m_TransformSlotAllocator.GetMaxCapacity();
		const VkDeviceSize bufferSize = sizeof(ModelDataStruct) * static_cast<VkDeviceSize>(maxCapacity);

		m_Scene->m_InstanceTransformDataBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice,
			std::max<VkDeviceSize>(bufferSize, sizeof(ModelDataStruct)),
			VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

		for (auto* node : allRenderNodes)
		{
			uint32_t slot = m_Scene->m_TransformSlotAllocator.AllocateSlot();
			assert(slot != TransformSlotAllocator::INVALID_SLOT && "Initial load exceeded max capacity");

			node->BeforeDrawCall();
			node->m_TransfromIndex = static_cast<int>(slot);

			VkDeviceSize offset = slot * sizeof(ModelDataStruct);
			m_Scene->m_InstanceTransformDataBuffer.SetBufferData(
				&node->m_ModelData,
				static_cast<int>(offset),
				sizeof(ModelDataStruct));
		}

		// ── Step 2.5: m_InstanceTransformData CPU 镜像不再写入 ────────────────

		// ── Step 3: 持久映射 ─────────────────────────────────────────────────
		m_Scene->m_InstanceTransformDataBuffer.PersistentMap();

		VkDescriptorSetLayoutBinding instanceTransformBufferBinding =
		{
			PassBinding::BUFFER_0,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			1,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
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

		manager->m_SkinBSDFLUT = new VansTexture();
		manager->m_SkinBSDFLUT->LoadTexture(m_VansVKCommandBuffer, (projectRoot + "EngineAssets/Textures/SkinBSDFLUT.png").c_str(), false, false, false,LOW_PRES_8, 4 , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

		manager->m_ClothBRDFLUT = new VansTexture();
		manager->m_ClothBRDFLUT->LoadTexture(m_VansVKCommandBuffer, (projectRoot + "EngineAssets/Textures/ClothBRDFLUT.png").c_str(), false, false, false, LOW_PRES_8, 4, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

		// ------------------------------------------------------------
		// LTC LUTs (area-light BRDF). 64x64 RGBA16F, uploaded from the
		// embedded float arrays in LTCData.h.  See AreaLightLTC plan §2.1.
		// ------------------------------------------------------------
		{
			auto FloatToHalf = [](float f) -> uint16_t
			{
				// IEEE 754 binary32 -> binary16 (round-to-nearest-even, no NaN handling needed for LUT data).
				union { float f; uint32_t u; } v{ f };
				uint32_t x = v.u;
				uint32_t sign = (x >> 16) & 0x8000u;
				int32_t  exp  = ((x >> 23) & 0xFFu) - 127 + 15;
				uint32_t mant = x & 0x7FFFFFu;
				uint16_t h;
				if (exp <= 0)
				{
					if (exp < -10) { h = (uint16_t)sign; }
					else
					{
						mant |= 0x800000u;
						uint32_t shift = 14u - (uint32_t)exp;
						uint32_t m = mant >> shift;
						if ((mant >> (shift - 1u)) & 1u) m += 1u; // round
						h = (uint16_t)(sign | m);
					}
				}
				else if (exp >= 31) { h = (uint16_t)(sign | 0x7C00u); } // inf / overflow
				else
				{
					uint32_t m = mant >> 13;
					if (mant & 0x1000u) m += 1u; // round
					if (m & 0x400u) { m = 0; ++exp; if (exp >= 31) { h = (uint16_t)(sign | 0x7C00u); m = 0; } }
					if (exp < 31) h = (uint16_t)(sign | ((uint32_t)exp << 10) | m);
				}
				return h;
			};

			constexpr int kSize = LTC::kLUTSize;
			constexpr size_t kCount = LTC::kLUTFloats;
			std::vector<uint16_t> half1(kCount), half2(kCount);
			for (size_t i = 0; i < kCount; ++i)
			{
				half1[i] = FloatToHalf(LTC::kLTC1[i]);
				half2[i] = FloatToHalf(LTC::kLTC2[i]);
			}

			manager->m_LTC1 = new VansTexture();
			manager->m_LTC1->LoadFromMemory(m_VansVKCommandBuffer,
				half1.data(), half1.size() * sizeof(uint16_t),
				kSize, kSize, VK_FORMAT_R16G16B16A16_SFLOAT,
				VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

			manager->m_LTC2 = new VansTexture();
			manager->m_LTC2->LoadFromMemory(m_VansVKCommandBuffer,
				half2.data(), half2.size() * sizeof(uint16_t),
				kSize, kSize, VK_FORMAT_R16G16B16A16_SFLOAT,
				VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

			manager->m_RectLightEmissiveArray = new VansTexture();
			manager->m_RectLightEmissiveArray->InitTextureArray(m_VansVKCommandBuffer,
				256, 256, 32, 4, /*generateMip=*/true,
				VansGraphics::LOW_PRES_8, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
			manager->RegisterRuntimeRenderTexture(
				VansMaterialManager::RT_RECT_LIGHT_EMISSIVE, manager->m_RectLightEmissiveArray);
		}

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
		VkDescriptorSetLayoutBinding skinBSDFLUTBinding =
		{
			PassBinding::TEXTURE_4,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding clothBRDFLUTBinding =
		{
			PassBinding::TEXTURE_5,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			nullptr
		};
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ samplerLUTBinding,sampleDiffuseConvBinding,sampleSpecularConBinding,environmentSHBuffer,skinBSDFLUTBinding,clothBRDFLUTBinding }, manager->m_BRDFInterationTexSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ manager->m_BRDFInterationTexSetLayout }, manager->m_BRDFInterationTextDescriptorSets);

		VansComputeShader* m_PreConvDiffuseShader = VansGraphics::VansShaderManager::Get().FindComputeShader("PreConDiffuseEnvironment");

		VansComputeShader* m_PreConvSpecularShader = VansGraphics::VansShaderManager::Get().FindComputeShader("PreConSpecularEnvironment");

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

		VansDescriptorSetLayoutFactory::CreateAndAllocate_SkyBox(manager->m_MaterialAtmosphereDataLayout, manager->m_MaterialAtmosphereDataDescriptorSets);

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
		ssgiResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 1, 4, false, false, true);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSGI_RESULT, ssgiResult);

		VansTexture* ssgiFilterResult = new VansTexture();
		ssgiFilterResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 1, 4, false, false, true);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSGI_FILTER_RESULT, ssgiFilterResult);

		VansTexture* ssgiTemporalA = new VansTexture();
		ssgiTemporalA->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 1, 4, false, false, true);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSGI_TEMPORAL_A, ssgiTemporalA);

		VansTexture* ssgiTemporalB = new VansTexture();
		ssgiTemporalB->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 1, 4, false, false, true);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSGI_TEMPORAL_B, ssgiTemporalB);
		manager->m_SSGITemporalFrame = 0;

		auto vansConfigration = VansConfigration::GetInstance();
		std::string projectRoot = vansConfigration->GetProjectRootPath();
		manager->m_SSGIShader = VansGraphics::VansShaderManager::Get().FindComputeShader("SSGI");

		manager->m_SSGITemporalShader = VansGraphics::VansShaderManager::Get().FindComputeShader("SSGITemporal");

		const VansGISettings& gi = m_Scene->GetGISettings();
		const float volumeSize = static_cast<float>(gi.gridSize) * gi.probeSpacing;
		const glm::vec3 volumeMin = gi.regionCenter - glm::vec3(volumeSize * 0.5f);
		SSGIParamsGPU data{};
		data.screenSize = glm::vec4(
			(float)m_RenderWidth, (float)m_RenderHeight, 1.0f / m_RenderWidth, 1.0f / m_RenderHeight);
		data.giVolumeMin = glm::vec4(volumeMin, 0.0f);
		data.giVolumeSizeAndBias = glm::vec4(volumeSize, volumeSize, volumeSize, gi.normalBias);
		data.traceParams = glm::vec4(gi.maxRayDistance, 0.75f, 0.0f, 0.0f);
		manager->m_SSGICBBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice, sizeof(data), VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		manager->m_SSGICBBuffer.SetBufferData(&data, 0, sizeof(data));

		manager->m_SSGITemporalCBBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice, sizeof(float) * 4, VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		manager->m_SSGITemporalCBBuffer.SetBufferData(&data.screenSize, 0, sizeof(data.screenSize));

		VansDescriptorSetLayoutFactory::CreateAndAllocate_SSGI(manager->m_SSGITexSetLayout, manager->m_SSGIDescriptorSets);
		VansDescriptorSetLayoutFactory::CreateAndAllocate_SSGITemporal(manager->m_SSGITemporalSetLayout, manager->m_SSGITemporalDescriptorSets, 2);
	}

	void VansVKDevice::PrepareHZBRenderData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();

	// HIZ stores positive linear view-space depth in meters.
		VansTexture* hzbResult = new VansTexture();
		hzbResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 1, 1, false, true, true, HIGH_PRES_32);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_HZB_RESULT, hzbResult);

		auto vansConfigration = VansConfigration::GetInstance();
		std::string projectRoot = vansConfigration->GetProjectRootPath();

		manager->m_HZBShader = VansGraphics::VansShaderManager::Get().FindComputeShader("HIZ");

		// HIZ_SEED shader: GBuffer position.w -> HIZ mip 0.
		manager->m_HIZSeedShader = VansGraphics::VansShaderManager::Get().FindComputeShader("HIZSeed");
		VansDescriptorSetLayoutFactory::CreateAndAllocate_HIZSeed(
			manager->m_HIZSeedSetLayout, manager->m_HIZSeedDescriptorSets, 1);

		manager->m_HIZMipCount = 1 + (int)std::floor(std::log2(std::min(m_RenderWidth, m_RenderHeight)));
		VansDescriptorSetLayoutFactory::CreateAndAllocate_HIZ(manager->m_HZBTexSetLayouts, manager->m_HZBDescriptorSets, manager->m_HIZMipCount - 1);
	}

	void VansVKDevice::PrepareScreenSpaceShadowRenderData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		VansTexture* sssResult = new VansTexture();
		sssResult->InitTextureWithoutData(
			m_VansVKCommandBuffer,
			m_RenderWidth, m_RenderHeight,
			1, 4, false, false, true, MID_PRES_16);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SCREEN_SPACE_SHADOW_RESULT, sssResult);

		VansTexture* sssFilterResult = new VansTexture();
		sssFilterResult->InitTextureWithoutData(
			m_VansVKCommandBuffer,
			m_RenderWidth, m_RenderHeight,
			1, 4, false, false, true, MID_PRES_16);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SCREEN_SPACE_SHADOW_FILTER_RESULT, sssFilterResult);

		ScreenSpaceShadowParamsGPU data{};
		data.screenSize = glm::vec4(
			static_cast<float>(m_RenderWidth), static_cast<float>(m_RenderHeight),
			1.0f / static_cast<float>(m_RenderWidth), 1.0f / static_cast<float>(m_RenderHeight));
		data.halfSize = glm::vec4(
			static_cast<float>(m_RenderWidth), static_cast<float>(m_RenderHeight),
			1.0f / static_cast<float>(m_RenderWidth), 1.0f / static_cast<float>(m_RenderHeight));
		data.rayParams = glm::vec4(0.75f, 0.065f, 0.018f, 40.0f);
		data.fadeParams = glm::vec4(32.0f, 45.0f, 0.75f, 0.25f);

		manager->m_ScreenSpaceShadowParamsCBBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice,
			sizeof(data),
			VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		manager->m_ScreenSpaceShadowParamsCBBuffer.SetBufferData(&data, 0, sizeof(data));

		manager->m_ScreenSpaceShadowShader = VansGraphics::VansShaderManager::Get().FindComputeShader("ScreenSpaceShadow");
		VansDescriptorSetLayoutFactory::CreateAndAllocate_ScreenSpaceShadow(
			manager->m_ScreenSpaceShadowSetLayout,
			manager->m_ScreenSpaceShadowDescriptorSets);
	}

	void VansVKDevice::PrepareSSRRenderData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		VansTexture* ssrHitInfo = new VansTexture();
		ssrHitInfo->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 1, 4, false, false, true, MID_PRES_16);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSR_HIT_INFO, ssrHitInfo);

		VansTexture* ssrRayPdf = new VansTexture();
		ssrRayPdf->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 1, 4, false, false, true, HIGH_PRES_32);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSR_RAY_PDF, ssrRayPdf);

		VansTexture* ssrResult = new VansTexture();
		ssrResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 1, 4, false, false, true, HIGH_PRES_32);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSR_RESULT, ssrResult);

		VansTexture* ssrAaResultA = new VansTexture();
		ssrAaResultA->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 1, 4, false, false, true, HIGH_PRES_32);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSRAA_RESULT_A, ssrAaResultA);

		VansTexture* ssrAaResultB = new VansTexture();
		ssrAaResultB->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 1, 4, false, false, true, HIGH_PRES_32);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSRAA_RESULT_B, ssrAaResultB);

		VansTexture* ssrAaResult = new VansTexture();
		ssrAaResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 1, 4, false, false, true, HIGH_PRES_32);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSRAA_RESULT, ssrAaResult);

		auto vansConfigration = VansConfigration::GetInstance();
		std::string projectRoot = vansConfigration->GetProjectRootPath();
		manager->m_SSRTraceShader = VansGraphics::VansShaderManager::Get().FindComputeShader("SSRTrace");

		manager->m_SSRResolveShader = VansGraphics::VansShaderManager::Get().FindComputeShader("SSRResolve");

		manager->m_SSRTemporalAAShader = VansGraphics::VansShaderManager::Get().FindComputeShader("SSRTemporalAA");

		VansDescriptorSetLayoutFactory::CreateAndAllocate_SSR_Trace(manager->m_SSRTraceSetLayout, manager->m_SSRTraceDescriptorSets);
		VansDescriptorSetLayoutFactory::CreateAndAllocate_SSR_Resolve(manager->m_SSRResolveSetLayout, manager->m_SSRResolveDescriptorSets);
		VansDescriptorSetLayoutFactory::CreateAndAllocate_SSR_TemporalAA(manager->m_SSRAASetLayout, manager->m_SSRAADescriptorSets);
	}

	void VansVKDevice::PrepareVolumetricData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		VansTexture* volumetricFogResult = new VansTexture();
		volumetricFogResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true, HIGH_PRES_32);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_VOLUMETRIC_FOG_RESULT, volumetricFogResult);
		
		// ================================================================
		// 3D voxel textures for frustum-aligned volumetric fog
		// XY = ceil(screenRes / TILE_SIZE),  Z = 128 slices
		// Format: RGBA16F
		// ================================================================
		static constexpr int TILE_SIZE    = 8;
		static constexpr int VOXEL_GRID_Z = 256;
		uint32_t gridX = (m_RenderWidth  + TILE_SIZE - 1) / TILE_SIZE;
		uint32_t gridY = (m_RenderHeight + TILE_SIZE - 1) / TILE_SIZE;

		VansTexture* fogVoxelInjection = new VansTexture();
		fogVoxelInjection->InitTextureWithoutData(
			m_VansVKCommandBuffer,
			gridX, gridY, VOXEL_GRID_Z,
			4, false, false, true, MID_PRES_16);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_INJECTION, fogVoxelInjection);

		// History texture for temporal reprojection (ping-pong with injection)
		VansTexture* fogVoxelInjectionHistory = new VansTexture();
		fogVoxelInjectionHistory->InitTextureWithoutData(
			m_VansVKCommandBuffer,
			gridX, gridY, VOXEL_GRID_Z,
			4, false, false, true, MID_PRES_16);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_INJECTION_HISTORY, fogVoxelInjectionHistory);

		VansTexture* fogVoxelRayMarch = new VansTexture();
		fogVoxelRayMarch->InitTextureWithoutData(
			m_VansVKCommandBuffer,
			gridX, gridY, VOXEL_GRID_Z,
			4, false, false, true, MID_PRES_16);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_FOG_VOXEL_RAYMARCH, fogVoxelRayMarch);

		auto vansConfigration = VansConfigration::GetInstance();
		std::string projectRoot = vansConfigration->GetProjectRootPath();

		// Height-exp fog compose shader (existing)
		manager->m_VolumetrcFogShader = VansGraphics::VansShaderManager::Get().FindComputeShader("VolumetricFog");

		// Light injection compute shader
		manager->m_FogLightInjectionShader = VansGraphics::VansShaderManager::Get().FindComputeShader("FogLightInjection");

		// Ray march accumulation compute shader
		manager->m_FogRayMarchShader = VansGraphics::VansShaderManager::Get().FindComputeShader("FogRayMarch");

		// FogParams UBO (height-exp fog): { fogDensity, heightFalloff, sunScatterScale, ambientScale, fogMinHeight, skyFogDistance }
		struct FogParamsData { float fogDensity; float heightFalloff; float sunScatterScale; float ambientScale; float fogMinHeight; float skyFogDistance; };
		FogParamsData fogDefaults = { 0.002f, 0.08f, 0.3f, 0.5f, -100.0f, 10000.0f };
		manager->m_FogParamsCBBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice, sizeof(FogParamsData), VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		manager->m_FogParamsCBBuffer.SetBufferData(&fogDefaults, 0, sizeof(FogParamsData));

		// FogVolumeParams UBO (voxel fog): matches FogVolumeParams in shaders (std140, 64 bytes)
		struct FogVolumeParamsData {
			float density;       float anisotropy;    float scatterScale;  float ambientScale;
			float volumeNear;    float volumeFar;     float slicePower;    float _pad1;
			float fogBoxMin[4];  // xyz + pad
			float fogBoxMax[4];  // xyz + pad
		};
		FogVolumeParamsData volumeDefaults = {
			0.05f, 0.6f, 1.0f, 0.05f,           // density, anisotropy, scatterScale, ambientScale
			2.0f, 200.0f, 2.0f, 0.0f,            // volumeNear, volumeFar, slicePower, pad
			{-50.0f, -50.0f, -50.0f, 0.0f},     // fogBoxMin
			{ 50.0f,  50.0f,  50.0f, 0.0f}      // fogBoxMax
		};
		manager->m_FogVolumeParamsCBBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice, sizeof(FogVolumeParamsData), VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		manager->m_FogVolumeParamsCBBuffer.SetBufferData(&volumeDefaults, 0, sizeof(FogVolumeParamsData));

		// Descriptor set layouts + allocation
		VansDescriptorSetLayoutFactory::CreateAndAllocate_VolumetricFog(manager->m_VolumetricFogSetLayout, manager->m_VolumetricFogDescriptorSets);
		VansDescriptorSetLayoutFactory::CreateAndAllocate_FogLightInjection(manager->m_FogLightInjectionSetLayout, manager->m_FogLightInjectionDescriptorSets, 2); // 2 sets for ping-pong
		VansDescriptorSetLayoutFactory::CreateAndAllocate_FogRayMarch(manager->m_FogRayMarchSetLayout, manager->m_FogRayMarchDescriptorSets);
		manager->m_FogTemporalFrame = 0;

		manager->UpdateAtmosphereDescriptorSets();
	}

	void VansVKDevice::PrepareCloudRenderData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		auto vansConfigration = VansConfigration::GetInstance();
		std::string projectRoot = vansConfigration->GetProjectRootPath();

		// 1/4 分辨率云层结果纹理（RGB=内散射，A=透射率），RGBA16F
		VansTexture* cloudBuffer = new VansTexture();
		cloudBuffer->InitTextureWithoutData(
			m_VansVKCommandBuffer,
			m_RenderWidth / 4, m_RenderHeight / 4, 1,
			4, false, false, true, MID_PRES_16);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_CLOUD_BUFFER, cloudBuffer);

		manager->m_CloudParamsCBBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice, sizeof(VansCloudParamsGPU), VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		manager->UploadCloudParamsToGPU();

		VansTexture* cloudMainNoise = new VansTexture();
		if (cloudMainNoise->LoadTexture3DFromSlices(
			m_VansVKCommandBuffer,
			projectRoot + "EngineAssets/Textures/VolumeCloud/Slice_Z_%03d.png",
			128, 4, VK_SAMPLER_ADDRESS_MODE_REPEAT))
		{
			manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_CLOUD_MAIN_NOISE, cloudMainNoise);
		}
		else
		{
			delete cloudMainNoise;
			cloudMainNoise = nullptr;
		}

		VansTexture* cloudDetailNoise = new VansTexture();
		if (cloudDetailNoise->LoadTexture3DFromSlices(
			m_VansVKCommandBuffer,
			projectRoot + "EngineAssets/Textures/VolumeCloud/Detail/Detail_Z_%03d.png",
			32, 4, VK_SAMPLER_ADDRESS_MODE_REPEAT))
		{
			manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_CLOUD_DETAIL_NOISE, cloudDetailNoise);
		}
		else
		{
			delete cloudDetailNoise;
			cloudDetailNoise = nullptr;
		}

		// Cloud Ray March compute shader
		manager->m_CloudRayMarchShader = VansGraphics::VansShaderManager::Get().FindComputeShader("CloudRayMarch");

		// Descriptor set layout + allocation for cloud ray march pass
		VansDescriptorSetLayoutFactory::CreateAndAllocate_CloudRayMarch(
			manager->m_CloudRayMarchSetLayout,
			manager->m_CloudRayMarchDescriptorSets);

		manager->UpdateAtmosphereDescriptorSets();
	}

	void VansVKDevice::PrepareBilaterFilterData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		VansTexture* ssaoFilterResult = new VansTexture();
		ssaoFilterResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true, MID_PRES_16);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SSAO_FILTER_RESULT, ssaoFilterResult);

		VansDescriptorSetLayoutFactory::CreateAndAllocate_BilateralFilter(manager->m_BilateralFilterSetLayout, manager->m_BilateralFilterDescriptorSets, 3);

		// Wider spatial filter to smooth residual noise after temporal accumulation.
		// radius=5 (11×11 kernel) with sigmaSpace=4.0 provides better coverage for
		// 1-SPP GI; depth parameters unchanged to preserve geometric edges.
		manager->m_BilateralFilterPushConstant =
		{
			4.0f,
			0.02f,
			5,
			0.01f,
			0
		};

		auto vansConfigration = VansConfigration::GetInstance();
		std::string projectRoot = vansConfigration->GetProjectRootPath();
		manager->m_BilateralFilterShader = VansGraphics::VansShaderManager::Get().FindComputeShader("BilateralFilter");
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
		PrepareScreenSpaceShadowRenderData();
		PrepareSSRRenderData();
		PrepareVolumetricData();
		PrepareCloudRenderData();
		PrepareTileLightData();
		PreparePostProcessRenderData();
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

	// =============================================================================
	// =============================================================================
	void VansVKDevice::PrepareIESProfileData()
	{
		VansIESProfileManager& iesMgr = *m_Scene->GetIESProfileManager();
		iesMgr.CreateGPUResources(m_VansVKLogicDevice);
		iesMgr.UploadAllProfiles(this, m_VansVKCommandBuffer);
	}

	// ============================================================
	// ============================================================
	void VansVKDevice::PreparePostProcessRenderData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		auto* config = VansConfigration::GetInstance();
		const std::string projectRoot = config->GetProjectRootPath();

		VansTexture* exposureLum = new VansTexture();
		exposureLum->InitTextureWithoutData(m_VansVKCommandBuffer, 64, 64, 1, 1, false, false, true, MID_PRES_16);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_EXPOSURE_LUMINANCE, exposureLum);

		VansTexture* exposureCurrent = new VansTexture();
		exposureCurrent->InitTextureWithoutData(m_VansVKCommandBuffer, 1, 1, 1, 1, false, false, true, MID_PRES_16);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_EXPOSURE_CURRENT, exposureCurrent);

		VansTexture* bloomPrefilter = new VansTexture();
		bloomPrefilter->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true, MID_PRES_16);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_PREFILTER, bloomPrefilter);

		VansTexture* bloomMip0 = new VansTexture();
		bloomMip0->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true, MID_PRES_16);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_MIP0, bloomMip0);

		VansTexture* bloomMip1 = new VansTexture();
		bloomMip1->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 4, m_RenderHeight / 4, 1, 4, false, false, true, MID_PRES_16);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_MIP1, bloomMip1);

		VansTexture* bloomMip2 = new VansTexture();
		bloomMip2->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 8, m_RenderHeight / 8, 1, 4, false, false, true, MID_PRES_16);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_MIP2, bloomMip2);

		VansTexture* bloomMip3 = new VansTexture();
		bloomMip3->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 16, m_RenderHeight / 16, 1, 4, false, false, true, MID_PRES_16);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_MIP3, bloomMip3);

		VansTexture* bloomResult = new VansTexture();
		bloomResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true, MID_PRES_16);
		manager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_RESULT, bloomResult);

		// ---- Shader 创建 ----
		manager->m_ExposureLuminanceShader = VansGraphics::VansShaderManager::Get().FindComputeShader("ExposureLuminance");

		manager->m_ExposureAdaptShader = VansGraphics::VansShaderManager::Get().FindComputeShader("ExposureAdapt");

		manager->m_BloomPrefilterShader = VansGraphics::VansShaderManager::Get().FindComputeShader("BloomPrefilter");

		manager->m_BloomDownsampleShader = VansGraphics::VansShaderManager::Get().FindComputeShader("BloomDownsample");

		manager->m_BloomUpsampleShader = VansGraphics::VansShaderManager::Get().FindComputeShader("BloomUpsample");

		// ---- Descriptor Set Layouts + Allocation ----
		VansDescriptorSetLayoutFactory::CreateAndAllocate_ExposureLuminance(
			manager->m_ExposureLuminanceSetLayout, manager->m_ExposureLuminanceDescriptorSets);
		VansDescriptorSetLayoutFactory::CreateAndAllocate_ExposureAdapt(
			manager->m_ExposureAdaptSetLayout, manager->m_ExposureAdaptDescriptorSets);
		VansDescriptorSetLayoutFactory::CreateAndAllocate_BloomPrefilter(
			manager->m_BloomPrefilterSetLayout, manager->m_BloomPrefilterDescriptorSets);
		VansDescriptorSetLayoutFactory::CreateAndAllocate_BloomDownsample(
			manager->m_BloomDownsampleSetLayout, manager->m_BloomDownsampleDescriptorSets, 4);
		VansDescriptorSetLayoutFactory::CreateAndAllocate_BloomUpsample(
			manager->m_BloomUpsampleSetLayout, manager->m_BloomUpsampleDescriptorSets, 4);

		// ---- UBO 创建与初始化 ----
		VansPostProcessProfile& defaultProfile = manager->m_PostProcessProfile;
		VansPostProcessParamsGPU ppParams  = defaultProfile.ToGPUParams(0.0f);
		VansExposureAdaptParamsGPU expParams = defaultProfile.ToExposureAdaptParams(0.016f);
		VansBloomParamsGPU bloomParams     = defaultProfile.ToBloomParams();

		manager->m_PostProcessParamsCBBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice, sizeof(VansPostProcessParamsGPU), VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		manager->m_PostProcessParamsCBBuffer.SetBufferData(&ppParams, 0, sizeof(VansPostProcessParamsGPU));

		manager->m_ExposureAdaptParamsCBBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice, sizeof(VansExposureAdaptParamsGPU), VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		manager->m_ExposureAdaptParamsCBBuffer.SetBufferData(&expParams, 0, sizeof(VansExposureAdaptParamsGPU));

		manager->m_BloomParamsCBBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice, sizeof(VansBloomParamsGPU), VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		manager->m_BloomParamsCBBuffer.SetBufferData(&bloomParams, 0, sizeof(VansBloomParamsGPU));
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

	// ================================================================
	// ================================================================
	void VansVKDevice::PrepareTileLightData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		const uint32_t TILE_SIZE = 8;
		uint32_t gridX     = (m_RenderWidth  + TILE_SIZE - 1) / TILE_SIZE;
		uint32_t gridY     = (m_RenderHeight + TILE_SIZE - 1) / TILE_SIZE;
		uint32_t totalTiles = gridX * gridY;

		manager->m_TileLightGridX = gridX;
		manager->m_TileLightGridY = gridY;

		// --- TileLight Header SSBO: 1 × TileLightHeader per tile (8 × uint32 = 32 bytes) ---
		const uint32_t kHeaderStride = 8 * sizeof(uint32_t); // { pointOffset, pointCount, spotOffset, spotCount, rectOffset, rectCount, pad0, pad1 }
		manager->m_TileLightHeaderBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice,
			static_cast<uint32_t>(totalTiles * kHeaderStride),
			VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);

		// --- TileLight Index SSBO: fixed-stride point + spot + rect index slots ---
		// Point slot for tile T = T * 64; Spot slot = totalTiles*64 + T*64;
		// Rect  slot           = totalTiles*64 + totalTiles*64 + T*16
		const uint32_t MAX_PT_PER_TILE   = 64; // matches TILE_LIGHT_MAX_PT_PER_TILE in shader
		const uint32_t MAX_SP_PER_TILE   = 64; // matches TILE_LIGHT_MAX_SP_PER_TILE in shader
		const uint32_t MAX_RECT_PER_TILE = 16; // matches TILE_LIGHT_MAX_RECT_PER_TILE in shader
		const uint32_t indexBufSize      = totalTiles * (MAX_PT_PER_TILE + MAX_SP_PER_TILE + MAX_RECT_PER_TILE) * sizeof(uint32_t);
		manager->m_TileLightIndexBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice,
			indexBufSize,
			VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);

		// --- TileLightBuildParams UBO (host-visible, written once) ---
		struct TileLightBuildParams { uint32_t gridX; uint32_t gridY; uint32_t totalTiles; uint32_t _pad; };
		TileLightBuildParams params = { gridX, gridY, totalTiles, 0u };
		manager->m_TileLightBuildParamsCBBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice,
			static_cast<uint32_t>(sizeof(TileLightBuildParams)),
			VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);
		manager->m_TileLightBuildParamsCBBuffer.SetBufferData(&params, 0, sizeof(TileLightBuildParams));

		// --- Build shader ---
		auto vansConfigration = VansConfigration::GetInstance();
		std::string projectRoot = vansConfigration->GetProjectRootPath();
		manager->m_TileLightBuildShader = VansGraphics::VansShaderManager::Get().FindComputeShader("TileLightBuild");

		// --- Descriptor set layout + allocation for Set 1 (write access) ---
		VansDescriptorSetLayoutFactory::CreateAndAllocate_TileLightBuild(
			manager->m_TileLightBuildSetLayout,
			manager->m_TileLightBuildDescriptorSets
		);

		// NOTE: UpdateGlobalTileLightDescriptors() is intentionally NOT called here.
		// m_GlobalDescriptorSet is VK_NULL_HANDLE until LoadSceneForRendering() runs.
		// The call is deferred to VansSceneLoader.cpp::LoadSceneForRendering(),
		// after CreateGlobalDescriptorSet() allocates the set.
	}

} // namespace VansGraphics
