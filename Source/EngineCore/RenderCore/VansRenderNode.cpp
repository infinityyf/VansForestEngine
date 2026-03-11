#include "VansRenderNode.h"
#include "VansCamera.h"
#include "VansScene.h"
#include "../../EngineCore/EditorCore/AssetsSystem/VansAssetsFileWatcher.h"
#include "VulkanCore/VansVKDevice.h"
#include "VulkanCore/VansVKDescriptorManager.h"
#include "VulkanCore/VansDescriptorSetLayouts.h"
#include "VulkanCore/VansRenderPass.h"
#include "../../EngineCore/RenderCore/TerrainCore/VansTerrain.h"
#include "../Util/VansLog.h"
#include <iostream>
using namespace VansGraphics;

VansGraphics::VansRenderNode::VansRenderNode(VkDevice& device, RenderNodeType typee)
{
	m_NodeType = typee;

	// Allocate ECS Data
    m_TransformID = VansTransformStore::AllocateTransform();
	SetTransformData();
	m_DescriptorsetsDirty = true;

	m_DescriptorsetsSetDone = false;
}

VansGraphics::VansRenderNode::~VansRenderNode()
{
	DestroyDescriptorSets();

	if (m_OwnsTransform)
		VansTransformStore::FreeTransform(m_TransformID);
	//m_RenderNodeDataBuffer.DestroyVulkanBuffer();
}

bool VansGraphics::VansRenderNode::CheckRenderNodeState()
{
	auto shader = m_Material->m_Shader;
	if (shader!= nullptr && m_SceneFileWatcher->ConsumeUpdated(shader->GetShaderFolder()))
	{
		VANS_LOG("pipe update: " << shader->GetShaderFolder());
		shader->RefreshShaderMoudle();
		shader->TriggerReCreateGraphicsPipeline();
		return false;
	}

	return true;
}

void VansGraphics::VansRenderNode::DestroyDescriptorSets()
{
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSetLayout(modelBufferLayout);
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(modelBufferDescriptorSets);

	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSetLayout(textureResourceLayout);
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(textureResourceDescriptorSets);

	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSetLayout(frameBufferInputLayout);
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(frameBufferInputDescriptorSets);
}

void VansGraphics::VansRenderNode::ComputeModelDataFromTransform()
{
	VansTransform& transform = VansTransformStore::GetTransform(m_TransformID);
	
	// Build model matrix
	m_ModelData.ModelMatrix = glm::translate(glm::mat4x4(1.0f), transform.m_Position);
	
	glm::vec3 radians = glm::radians(transform.m_Rotation);
	glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), radians.x, glm::vec3(1, 0, 0));
	glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), radians.y, glm::vec3(0, 1, 0));
	glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), radians.z, glm::vec3(0, 0, 1));
	
	// XYZ order: first X, then Y, then Z
	glm::mat4 rotationMatrix = rotZ * rotY * rotX;
	m_ModelData.ModelMatrix = m_ModelData.ModelMatrix * rotationMatrix;
	
	m_ModelData.ModelMatrix = glm::scale(m_ModelData.ModelMatrix, transform.m_Scale);
	m_ModelData.NormalMatrix = glm::transpose(glm::inverse(m_ModelData.ModelMatrix));
	m_ModelData.Postion = glm::vec4(transform.m_Position, 1.0f);
	m_ModelData.Scale = glm::vec4(transform.m_Scale, 1.0f);
}

void VansGraphics::VansRenderNode::BeforeDrawCall()
{
	ComputeModelDataFromTransform();
}

void VansGraphics::VansRenderNode::UpdateModelData()
{
	ComputeModelDataFromTransform();
	
	// Push updated data to GPU using the global instance buffer in VansScene
	// Update at the offset specified by m_TransfromIndex
	if (m_Scene && m_TransfromIndex >= 0)
	{
		size_t offset = m_TransfromIndex * sizeof(ModelDataStruct);
		m_Scene->m_InstanceTransformDataBuffer.SetBufferData(&m_ModelData, offset, sizeof(m_ModelData));
	}
}

void VansGraphics::VansRenderNode::Draw(VansVKCommandBuffer& cmd, GlobalStateData& globalStateData)
{
	if (!CheckRenderNodeState())
	{
		return;
	}

	//BeforeDrawCall();

	//apply mesh
	cmd.BindMesh(*m_Mesh, 0, globalStateData);

	//apply shader，确认Pipeline以及创建完毕
	VansGraphicsShader& shader = *(m_Material->m_Shader);
	cmd.EnsureGraphicsShader(shader, globalStateData, m_UsedDescSetLayouts);

	//切换shader进行一次处理
	if (VansVKGraphicsPipeline::CurrentValidGraphicsPipeline != (shader.GetGraphicsPipeline()->GetNativePipeline()))
	{
		cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, shader, 0, m_UsedDescSets, {});
	}

	if (m_Material->m_Shader->GetPushConstantSize() > 0)
	{
		m_Material->m_MaterialPushConstant.transfromIndex = m_TransfromIndex;
		cmd.UpdatePushConstants(*shader.GetGraphicsPipeline(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, m_Material->m_Shader->GetPushConstantSize(), &(m_Material->m_MaterialPushConstant));
	}

	cmd.DrawMesh(*m_Mesh, shader, 1);
}

void VansGraphics::VansRenderNode::DrawPunctualShadow(VansVKCommandBuffer& cmd, GlobalStateData& global_state, int lightIndex, int shadowIndex)
{
	//BeforeDrawCall();

	//apply mesh
	cmd.BindMesh(*m_Mesh, 0, global_state);

	//apply shader，确认Pipeline以及创建完毕
	cmd.EnsureGraphicsShader(*(m_Material->m_Shader), global_state, m_UsedDescSetLayouts);

	int data[4] = { lightIndex, shadowIndex , 0, m_Material->m_MaterialPushConstant.transfromIndex};
	cmd.UpdatePushConstants(*(m_Material->m_Shader->GetGraphicsPipeline()), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		0, m_Material->m_Shader->GetPushConstantSize(), data);

	cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *(m_Material->m_Shader), 0, m_UsedDescSets, {});

	cmd.DrawMesh(*m_Mesh, *(m_Material->m_Shader), 1);
}

//void VansGraphics::VansRenderNode::DrawWithMaterial(VansMaterial* material, VansVKCommandBuffer& cmd, GlobalStateData& global_state)
//{
//	BeforeDrawCall();
//
//	//apply mesh
//	cmd.BindMesh(*m_Mesh, 0, global_state);
//
//	cmd.EnsureGraphicsShader(*(material->m_Shader), global_state, m_UsedDescSetLayouts);
//
//	cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *(material->m_Shader), 0, m_UsedDescSets, {});
//
//	cmd.DrawMesh(*m_Mesh, *(material->m_Shader), 1);
//}

void VansGraphics::VansCommonRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	// Set 0: Global (Camera + Lights + Materials + IBL + Bindless)
	m_UsedDescSetLayouts.push_back(m_Scene->m_GlobalDescriptorSetLayout);
	m_UsedDescSets.push_back(m_Scene->m_GlobalDescriptorSet);

	// Set 1: Per-Pass (empty for common geometry pass)
	m_UsedDescSetLayouts.push_back(m_Scene->m_EmptyPassLayout);
	m_UsedDescSets.push_back(m_Scene->m_EmptyPassDescriptorSet);

	// Set 2: Per-Object (Transform SSBO)
	m_UsedDescSetLayouts.push_back(m_Scene->m_ObjectDescriptorSetLayout);
	m_UsedDescSets.push_back(m_Scene->m_ObjectDescriptorSet);
}

void VansGraphics::VansCommonRenderNode::SyncMaterialToGPU(VansMaterial* mat, VansMaterialManager& materialManager)
{
	if (mat && mat->m_MaterialType == VansMaterialType::VAN_PBR)
	{
		int idx = mat->m_MaterialPushConstant.materialIndex;
		materialManager.m_GlobalPBRDataBuffer.SetBufferData(
			&mat->m_BasePBRParam,
			sizeof(VansBasePBRParam) * idx,
			sizeof(VansBasePBRParam));
	}
}

void VansGraphics::VansCommonRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	// Sync CPU material params to the global GPU PBR buffer so editor changes take effect.
	SyncMaterialToGPU(m_Material, materialManager);
	UpdateDescripterSets(materialManager);
}

void VansGraphics::VansCommonRenderNode::UpdateDescripterSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}
	m_DescriptorsetsDirty = false;

	// All resources are now in the global descriptor set (Set 0)
	// No per-object descriptor updates needed
}

// ============================================================
// VansTransparentRenderNode
// ============================================================

void VansGraphics::VansTransparentRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	// Set 0: Global (Camera UBO is universal, still needed for VP matrices)
	m_UsedDescSetLayouts.push_back(m_Scene->m_GlobalDescriptorSetLayout);
	m_UsedDescSets.push_back(m_Scene->m_GlobalDescriptorSet);

	// Set 1: Material-owned resources (layout held by VansMaterial, not the factory)
	if (m_Material->m_TransparentOwnedLayout == VK_NULL_HANDLE)
	{
		// Build layout, allocate set, and write texture bindings from shader slot order
		m_Material->BuildTransparentTextureDescriptors();
	}
	m_UsedDescSetLayouts.push_back(m_Material->m_TransparentOwnedLayout);
	if (!m_Material->m_TransparentOwnedDescSets.empty())
	{
		m_UsedDescSets.push_back(m_Material->m_TransparentOwnedDescSets[0]);
	}

	// Set 2: Object Transforms SSBO (accessed via objectIndex push constant)
	m_UsedDescSetLayouts.push_back(m_Scene->m_ObjectDescriptorSetLayout);
	m_UsedDescSets.push_back(m_Scene->m_ObjectDescriptorSet);
}

void VansGraphics::VansTransparentRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	UpdateDescripterSets(materialManager);
}

void VansGraphics::VansTransparentRenderNode::UpdateDescripterSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}
	m_DescriptorsetsDirty = false;

	// Per-material descriptor updates will be added per shader variant.
}

void VansGraphics::VansTransparentRenderNode::Draw(VansVKCommandBuffer& cmd, GlobalStateData& globalStateData)
{
	if (!CheckRenderNodeState())
	{
		return;
	}

	cmd.BindMesh(*m_Mesh, 0, globalStateData);

	VansGraphicsShader& shader = *(m_Material->m_Shader);
	cmd.EnsureGraphicsShader(shader, globalStateData, m_UsedDescSetLayouts);

	cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, shader, 0, m_UsedDescSets, {});

	// Push objectIndex to look up model data from the transform SSBO
	if (m_Material->m_Shader->GetPushConstantSize() > 0)
	{
		int objectIndex = m_TransfromIndex;
		cmd.UpdatePushConstants(*shader.GetGraphicsPipeline(),
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(int), &objectIndex);
	}

	cmd.DrawMesh(*m_Mesh, shader, 1);
}

void VansGraphics::VansPostProcessRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	// Set 0: Global
	m_UsedDescSetLayouts.push_back(m_Scene->m_GlobalDescriptorSetLayout);
	m_UsedDescSets.push_back(m_Scene->m_GlobalDescriptorSet);

	// Set 1: Per-Pass (post-process input attachment)
	VansDescriptorSetLayoutFactory::CreateAndAllocate_PostProcess(frameBufferInputLayout, frameBufferInputDescriptorSets);

	m_UsedDescSetLayouts.push_back(frameBufferInputLayout);
	m_UsedDescSets.push_back(frameBufferInputDescriptorSets[0]);
}

void VansGraphics::VansPostProcessRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	UpdateDescripterSets(materialManager);
}

void VansGraphics::VansPostProcessRenderNode::UpdateDescripterSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}
	m_DescriptorsetsDirty = false;

	VansVKDescriptorManager::GetInstance()->ResetState();
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			frameBufferInputDescriptorSets[0],
			POSTPROCESS_BINDING_COLOR_INPUT,
			0,
			VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			{
				{
					VK_NULL_HANDLE,
					VansRenderPassManager::GetInstance()->GetColor().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);

	VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}

void VansGraphics::VansDeferredRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	// Set 0: Global (Camera + Lights + Materials + IBL + Bindless)
	m_UsedDescSetLayouts.push_back(m_Scene->m_GlobalDescriptorSetLayout);
	m_UsedDescSets.push_back(m_Scene->m_GlobalDescriptorSet);

	// Set 1: Per-Pass (GBuffer inputs + screen-space effect textures merged)
	VansDescriptorSetLayoutFactory::CreateAndAllocate_DeferredLighting(frameBufferInputLayout, frameBufferInputDescriptorSets);

	m_UsedDescSetLayouts.push_back(frameBufferInputLayout);
	m_UsedDescSets.push_back(frameBufferInputDescriptorSets[0]);
}

void VansGraphics::VansDeferredRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	UpdateDescripterSets(materialManager);
}

void VansGraphics::VansDeferredRenderNode::UpdateDescripterSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}
	m_DescriptorsetsDirty = false;

	VansVKDescriptorManager::GetInstance()->ResetState();

	// Bindings 0-4: GBuffer subpass inputs
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			frameBufferInputDescriptorSets[0],
			0, // Normal
			0,
			VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			{
				{
					VK_NULL_HANDLE,
					VansRenderPassManager::GetInstance()->GetNormal().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			frameBufferInputDescriptorSets[0],
			1, // Gbuffer0
			0,
			VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			{
				{
					VK_NULL_HANDLE,
					VansRenderPassManager::GetInstance()->GetGbuffer0().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			frameBufferInputDescriptorSets[0],
			2, // Gbuffer1
			0,
			VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			{
				{
					VK_NULL_HANDLE,
					VansRenderPassManager::GetInstance()->GetGbuffer1().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			frameBufferInputDescriptorSets[0],
			3, // Gbuffer2
			0,
			VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			{
				{
					VK_NULL_HANDLE,
					VansRenderPassManager::GetInstance()->GetGbuffer2().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			frameBufferInputDescriptorSets[0],
			4, // Depth
			0,
			VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			{
				{
					VK_NULL_HANDLE,
					VansRenderPassManager::GetInstance()->GetDepth().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);

	VansTexture* ssaoFilterResult = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_SSAO_FILTER_RESULT);
	VansTexture* ssgiFilterResult = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_SSGI_FILTER_RESULT);
	VansTexture* ssrAaResult = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_SSRAA_RESULT);
	VansTexture* shRResult = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_SH_R_RESULT);
	VansTexture* shGResult = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_SH_G_RESULT);
	VansTexture* shBResult = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_SH_B_RESULT);
	VansTexture* volumetricFogResult = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_VOLUMETRIC_FOG_RESULT);

	if (ssaoFilterResult == nullptr || ssgiFilterResult == nullptr || ssrAaResult == nullptr ||
		shRResult == nullptr || shGResult == nullptr || shBResult == nullptr || volumetricFogResult == nullptr)
	{
		return;
	}

	// Bindings 5-13: Screen-space effect results
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			frameBufferInputDescriptorSets[0],
			5, // SSAO
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
			frameBufferInputDescriptorSets[0],
			6, // SSGI
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
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
			frameBufferInputDescriptorSets[0],
			7, // SSR
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
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			frameBufferInputDescriptorSets[0],
			8, // Shadow map
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					VansRenderPassManager::GetInstance()->GetShadowMap().GetSampler(),
					VansRenderPassManager::GetInstance()->GetShadowMap().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			frameBufferInputDescriptorSets[0],
			9, // Punctual shadow map
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					VansRenderPassManager::GetInstance()->GetPunctualShadowMap().GetSampler(),
					VansRenderPassManager::GetInstance()->GetPunctualShadowMap().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);

	// GI SH coefficient textures
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			frameBufferInputDescriptorSets[0],
			10, // SH R
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					shRResult->GetImage().GetSampler(),
					shRResult->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			frameBufferInputDescriptorSets[0],
			11, // SH G
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					shGResult->GetImage().GetSampler(),
					shGResult->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			frameBufferInputDescriptorSets[0],
			12, // SH B
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					shBResult->GetImage().GetSampler(),
					shBResult->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);

	// Volumetric fog
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			frameBufferInputDescriptorSets[0],
			13, // Fog
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					volumetricFogResult->GetImage().GetSampler(),
					volumetricFogResult->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);

	VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}

void VansGraphics::VansScreenSpaceRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	// Set 0: Global
	m_UsedDescSetLayouts.push_back(m_Scene->m_GlobalDescriptorSetLayout);
	m_UsedDescSets.push_back(m_Scene->m_GlobalDescriptorSet);

	// Set 1: Per-Pass (screen-space textures)
	VansDescriptorSetLayoutFactory::CreateAndAllocate_ScreenSpace(textureResourceLayout, textureResourceDescriptorSets);

	m_UsedDescSetLayouts.push_back(textureResourceLayout);
	m_UsedDescSets.push_back(textureResourceDescriptorSets[0]);
}

void VansGraphics::VansScreenSpaceRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	UpdateDescripterSets(materialManager);
}

void VansGraphics::VansScreenSpaceRenderNode::UpdateDescripterSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}
	m_DescriptorsetsDirty = false;

	VansVKDescriptorManager::GetInstance()->ResetState();
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			0, // Normal
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					VansRenderPassManager::GetInstance()->GetNormal().GetSampler(),
					VansRenderPassManager::GetInstance()->GetNormal().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			1, // Gbuffer0
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					VansRenderPassManager::GetInstance()->GetGbuffer0().GetSampler(),
					VansRenderPassManager::GetInstance()->GetGbuffer0().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			2, // Gbuffer1
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					VansRenderPassManager::GetInstance()->GetGbuffer1().GetSampler(),
					VansRenderPassManager::GetInstance()->GetGbuffer1().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			3, // Gbuffer2
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					VansRenderPassManager::GetInstance()->GetGbuffer2().GetSampler(),
					VansRenderPassManager::GetInstance()->GetGbuffer2().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			4, // Depth
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					VansRenderPassManager::GetInstance()->GetDepth().GetSampler(),
					VansRenderPassManager::GetInstance()->GetDepth().GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			}
		}
	);
	VansTexture* ssaoResult = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_SSAO_RESULT);
	if (ssaoResult == nullptr)
	{
		return;
	}
	VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
		{
			textureResourceDescriptorSets[0],
			5, // SSAO output
			0,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{
				{
					ssaoResult->GetImage().GetSampler(),
					ssaoResult->GetImage().GetImageView(),
					VK_IMAGE_LAYOUT_GENERAL
				}
			}
		}
	);
	VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}

void VansGraphics::VansSkyBoxRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	// Set 0: Global
	m_UsedDescSetLayouts.push_back(m_Scene->m_GlobalDescriptorSetLayout);
	m_UsedDescSets.push_back(m_Scene->m_GlobalDescriptorSet);

	// Set 1: Per-Pass (Atmosphere UBO)
	m_UsedDescSetLayouts.push_back(materialManager.m_MaterialAtmosphereDataLayout);
	m_UsedDescSets.push_back(materialManager.m_MaterialAtmosphereDataDescriptorSets[0]);
}

void VansGraphics::VansSkyBoxRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	m_Material->UpdateAtmosphereMaterialData(materialManager, lightManager);

	UpdateDescripterSets(materialManager);
}

void VansGraphics::VansSkyBoxRenderNode::UpdateDescripterSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}
	m_DescriptorsetsDirty = false;
}

void VansGraphics::VansShadowRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	// Set 0: Global
	m_UsedDescSetLayouts.push_back(m_Scene->m_GlobalDescriptorSetLayout);
	m_UsedDescSets.push_back(m_Scene->m_GlobalDescriptorSet);

	// Set 1: Per-Pass (empty for shadow pass)
	m_UsedDescSetLayouts.push_back(m_Scene->m_EmptyPassLayout);
	m_UsedDescSets.push_back(m_Scene->m_EmptyPassDescriptorSet);

	// Set 2: Per-Object (Transform SSBO)
	m_UsedDescSetLayouts.push_back(m_Scene->m_ObjectDescriptorSetLayout);
	m_UsedDescSets.push_back(m_Scene->m_ObjectDescriptorSet);
}

void VansGraphics::VansShadowRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	UpdateDescripterSets(materialManager);
}

void VansGraphics::VansShadowRenderNode::UpdateDescripterSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}
	m_DescriptorsetsDirty = false;

	// All resources are now in the global descriptor set (Set 0)
	// No per-pass or per-object descriptor updates needed for shadows
}

VansGraphics::VansTerrainRenderNode::VansTerrainRenderNode(VansVKDevice* device, const TerrainConfig& config, RenderNodeType type) : VansRenderNode(device->GetLogicDevice(), TERRAIN_NODE)
{
	m_Terrain = new VansTerrain();
	m_Terrain->Init(device, config);
}


void VansGraphics::VansTerrainRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	// Set 0: Global (Camera + Lights + Materials + IBL + Bindless)
	m_UsedDescSetLayouts.push_back(m_Scene->m_GlobalDescriptorSetLayout);
	m_UsedDescSets.push_back(m_Scene->m_GlobalDescriptorSet);

	// Set 1: Per-Pass (terrain-specific: heightmap + albedo)
	m_UsedDescSetLayouts.push_back(m_Terrain->m_DescriptorSetLayout);
	m_UsedDescSets.push_back(m_Terrain->m_DescriptorSets[0]);
}

void VansGraphics::VansTerrainRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	UpdateDescripterSets(materialManager);

	m_Terrain->Update(camera);
}

void VansGraphics::VansTerrainRenderNode::UpdateDescripterSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}
	m_DescriptorsetsDirty = false;


}

void VansGraphics::VansTerrainRenderNode::Draw(VansVKCommandBuffer& cmd, GlobalStateData& global_state)
{
	m_Terrain->Draw(cmd, global_state, m_UsedDescSetLayouts, m_UsedDescSets);
}

void VansGraphics::VansTerrainRenderNode::DrawShadow(VansVKCommandBuffer& cmd, GlobalStateData& global_state)
{
	m_Terrain->DrawShadow(cmd, global_state, m_UsedDescSetLayouts, m_UsedDescSets);
}
