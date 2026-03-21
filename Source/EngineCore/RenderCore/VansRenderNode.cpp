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
#include "../AnimationCore/VansAnimationNode.h"
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
	// Check all pass shaders for hot-reload (file watcher)
	for (auto& [passName, shader] : m_Material->m_PassShaders)
	{
		if (shader != nullptr && m_SceneFileWatcher->ConsumeUpdated(shader->GetShaderFolder()))
		{
			VANS_LOG("pipe update (" << passName << "): " << shader->GetShaderFolder());
			shader->RefreshShaderMoudle();
			shader->TriggerReCreateGraphicsPipeline();
			return false;
		}
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
	
	// Push updated data to GPU using the persistently mapped instance buffer in VansScene
	// Update at the offset specified by m_TransfromIndex
	if (m_Scene && m_TransfromIndex >= 0)
	{
		size_t offset = m_TransfromIndex * sizeof(ModelDataStruct);
		m_Scene->m_InstanceTransformDataBuffer.UpdateMapped(&m_ModelData, offset, sizeof(m_ModelData));
	}
}

// Helper: map node type to its primary render-pass name.
static const char* GetPrimaryPassName(VansGraphics::RenderNodeType type)
{
	using namespace VansGraphics;
	switch (type)
	{
	case OPAQUE_NODE:       return VansPass::GBUFFER;
	case TRANSPARENT_NODE:  return VansPass::FORWARD_TRANSPARENT;
	case SKY_BOX_NODE:      return VansPass::SKY_BOX;
	case POSTPROCESS_NODE:  return VansPass::POST_PROCESS;
	case DEFERRED_NODE:     return VansPass::DEFERRED;
	case SCREEN_SPACE_NODE: return VansPass::SCREEN_SPACE;
	default:                return VansPass::GBUFFER;
	}
}

void VansGraphics::VansRenderNode::Draw(VansVKCommandBuffer& cmd, GlobalStateData& globalStateData)
{
	if (!CheckRenderNodeState())
		return;

	VansGraphicsShader* shader = m_Material->GetPassShader(GetPrimaryPassName(m_NodeType));
	if (!shader) return;

	cmd.BindMesh(*m_Mesh, 0, globalStateData);

	cmd.EnsureGraphicsShader(*shader, globalStateData, m_UsedDescSetLayouts);

	cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *shader, 0, m_UsedDescSets, {});

	if (shader->GetPushConstantSize() > 0)
	{
		VansDrawPushConstant pc{};
		pc.materialIndex    = (m_Material->m_MaterialType == VansMaterialType::VAN_PBR)
			? static_cast<VansPBRMaterial*>(m_Material)->m_MaterialIndex : -1;
		pc.transformIndex   = m_TransfromIndex;
		pc.animationEnabled = m_AnimationEnabled ? 1 : 0;
		cmd.UpdatePushConstants(*shader->GetGraphicsPipeline(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, shader->GetPushConstantSize(), &pc);
	}

	cmd.DrawMesh(*m_Mesh, *shader, 1);
}

void VansGraphics::VansRenderNode::DrawPunctualShadow(VansVKCommandBuffer& cmd, GlobalStateData& global_state, int lightIndex, int shadowIndex)
{
	// Legacy fallback — punctual shadow draws now use DrawPunctualShadowWithPassShader().
}

void VansGraphics::VansRenderNode::DrawCascadeShadowWithPassShader(VansVKCommandBuffer& cmd, GlobalStateData& global_state,
                                                                     VansGraphicsShader* passShader,
                                                                     const std::vector<VkDescriptorSet>& descSets,
                                                                     const std::vector<VkDescriptorSetLayout>& descSetLayouts)
{
	if (!passShader) return;

	cmd.BindMesh(*m_Mesh, 0, global_state);

	cmd.EnsureGraphicsShader(*passShader, global_state, descSetLayouts);

	cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *passShader, 0, descSets, {});

	if (passShader->GetPushConstantSize() > 0)
	{
		// Shadow shader expects: { materialIndex, objectIndex, cascadeIndex }
		int matIdx = (m_Material->m_MaterialType == VansMaterialType::VAN_PBR)
			? static_cast<VansPBRMaterial*>(m_Material)->m_MaterialIndex : -1;
		int pushData[3] = { matIdx, m_TransfromIndex, global_state.cascadeIndex };
		cmd.UpdatePushConstants(*passShader->GetGraphicsPipeline(),
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, passShader->GetPushConstantSize(), pushData);
	}

	cmd.DrawMesh(*m_Mesh, *passShader, 1);
}

void VansGraphics::VansRenderNode::DrawPunctualShadowWithPassShader(VansVKCommandBuffer& cmd, GlobalStateData& global_state,
                                                                      VansGraphicsShader* passShader,
                                                                      const std::vector<VkDescriptorSet>& descSets,
                                                                      const std::vector<VkDescriptorSetLayout>& descSetLayouts,
                                                                      int lightIndex, int shadowIndex)
{
	if (!passShader) return;

	cmd.BindMesh(*m_Mesh, 0, global_state);

	cmd.EnsureGraphicsShader(*passShader, global_state, descSetLayouts);

	int data[4] = { lightIndex, shadowIndex, 0, m_TransfromIndex };
	cmd.UpdatePushConstants(*passShader->GetGraphicsPipeline(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		0, passShader->GetPushConstantSize(), data);

	cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *passShader, 0, descSets, {});

	cmd.DrawMesh(*m_Mesh, *passShader, 1);
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

	// Set 2: Per-Object — shared Transform SSBO (all nodes, animated or not)
	m_UsedDescSetLayouts.push_back(m_Scene->m_ObjectDescriptorSetLayout);
	m_UsedDescSets.push_back(m_Scene->m_ObjectDescriptorSet);

	// Set 3: Per-Node Animation (Bone IDs + Bone Matrices + Bone Weights)
	// Animated nodes get a freshly allocated descriptor set with real GPU buffers.
	// Each submesh has its own bone ID and weight buffers — no offset needed.
	// Static nodes reuse the scene-shared dummy set (never accessed when animationEnabled==0).
	m_UsedDescSetLayouts.push_back(m_Scene->m_AnimationDescriptorSetLayout);

	if (m_HasSkeletonBone && m_AnimOwner && m_AnimBoneIDBuffer && m_AnimBoneWeightBuffer)
	{
		auto* descManager = VansVKDescriptorManager::GetInstance();
		descManager->AllocateDescriptorSet({ m_Scene->m_AnimationDescriptorSetLayout }, modelBufferDescriptorSets);

		m_UsedDescSets.push_back(modelBufferDescriptorSets[0]);
		VANS_LOG("[VansCommonRenderNode] " << m_NodeName << ": per-node animation descriptor set (Set 3) created");
	}
	else
	{
		// Static node: bind shared dummy animation set — bone/weight data is never read
		m_UsedDescSets.push_back(m_Scene->m_AnimationDescriptorSet);
	}

	// Set 4: Per-Material Skin Texture (albedo + normal)
	// Owned by VansSkinMaterial; built once and shared by all nodes using this material.
	if (m_Material && m_Material->m_MaterialType == VansMaterialType::VAN_SKIN)
	{
		VansSkinMaterial* skin = static_cast<VansSkinMaterial*>(m_Material);
		if (skin->m_SkinOwnedLayout == VK_NULL_HANDLE)
		{
			skin->BuildSkinTextureDescriptors();
		}
		m_UsedDescSetLayouts.push_back(skin->m_SkinOwnedLayout);
		if (!skin->m_SkinOwnedDescSets.empty())
		{
			m_UsedDescSets.push_back(skin->m_SkinOwnedDescSets[0]);
		}
	}

	// Set 4: Per-Material Cloth Texture (albedo + normal)
	// Owned by VansClothMaterial; built once and shared by all nodes using this material.
	if (m_Material && m_Material->m_MaterialType == VansMaterialType::VAN_CLOTH)
	{
		VansClothMaterial* cloth = static_cast<VansClothMaterial*>(m_Material);
		if (cloth->m_ClothOwnedLayout == VK_NULL_HANDLE)
		{
			cloth->BuildClothTextureDescriptors();
		}
		m_UsedDescSetLayouts.push_back(cloth->m_ClothOwnedLayout);
		if (!cloth->m_ClothOwnedDescSets.empty())
		{
			m_UsedDescSets.push_back(cloth->m_ClothOwnedDescSets[0]);
		}
	}

	// ── Shadow descriptor sets (first 3 sets only: Global + EmptyPass + Object) ──
	// These are used when DrawWithPassShader is called for shadow/punctualShadow passes.
	m_ShadowDescSetLayouts = {
		m_Scene->m_GlobalDescriptorSetLayout,   // Set 0
		m_Scene->m_EmptyPassLayout,             // Set 1
		m_Scene->m_ObjectDescriptorSetLayout,   // Set 2
	};
	m_ShadowDescSets = {
		m_Scene->m_GlobalDescriptorSet,         // Set 0
		m_Scene->m_EmptyPassDescriptorSet,      // Set 1
		m_Scene->m_ObjectDescriptorSet,         // Set 2
	};
}

void VansGraphics::VansCommonRenderNode::SyncMaterialToGPU(VansMaterial* mat, VansMaterialManager& materialManager)
{
	if (mat && mat->m_MaterialType == VansMaterialType::VAN_PBR)
	{
		VansPBRMaterial* pbr = static_cast<VansPBRMaterial*>(mat);
		int idx = pbr->m_MaterialIndex;
		materialManager.m_GlobalPBRDataBuffer.UpdateMapped(
			&pbr->m_BasePBRParam,
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
	if (m_HasSkeletonBone && m_AnimOwner && m_AnimBoneIDBuffer && m_AnimBoneWeightBuffer)
	{
		auto* descManager = VansVKDescriptorManager::GetInstance();
		descManager->ResetState();
		// binding 0: Per-vertex Bone IDs SSBO (per-submesh)
		descManager->m_BufferDescInfos.push_back({
			modelBufferDescriptorSets[0], ANIMATION_BINDING_BONEID_SSBO, 0,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ m_AnimBoneIDBuffer->GetNativeBuffer(), 0, VK_WHOLE_SIZE }}
			});
		// binding 1: Bone Matrices SSBO (shared across all submeshes)
		descManager->m_BufferDescInfos.push_back({
			modelBufferDescriptorSets[0], ANIMATION_BINDING_BONE_SSBO, 0,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ m_AnimOwner->GetBoneBuffer(0).GetNativeBuffer(), 0, VK_WHOLE_SIZE }}
			});
		// binding 2: Per-vertex Bone Weights SSBO (per-submesh)
		descManager->m_BufferDescInfos.push_back({
			modelBufferDescriptorSets[0], ANIMATION_BINDING_BONEWEIGHT_SSBO, 0,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ m_AnimBoneWeightBuffer->GetNativeBuffer(), 0, VK_WHOLE_SIZE }}
			});
		descManager->UpdateDescriptorSets();
	}
}

// ============================================================
// VansTransparentRenderNode
// ============================================================

void VansGraphics::VansTransparentRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	// Set 0: Global (Camera UBO is universal, still needed for VP matrices)
	m_UsedDescSetLayouts.push_back(m_Scene->m_GlobalDescriptorSetLayout);
	m_UsedDescSets.push_back(m_Scene->m_GlobalDescriptorSet);

	// Set 1: Material-owned resources (layout held by VansTransparentMaterial)
	VansTransparentMaterial* trans = static_cast<VansTransparentMaterial*>(m_Material);
	if (trans->m_TransparentOwnedLayout == VK_NULL_HANDLE)
	{
		// Build layout, allocate set, and write texture bindings from shader slot order
		trans->BuildTransparentTextureDescriptors();
	}
	m_UsedDescSetLayouts.push_back(trans->m_TransparentOwnedLayout);
	if (!trans->m_TransparentOwnedDescSets.empty())
	{
		m_UsedDescSets.push_back(trans->m_TransparentOwnedDescSets[0]);
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
		return;

	VansGraphicsShader* shader = m_Material->GetPassShader(VansPass::FORWARD_TRANSPARENT);
	if (!shader) return;

	cmd.BindMesh(*m_Mesh, 0, globalStateData);

	cmd.EnsureGraphicsShader(*shader, globalStateData, m_UsedDescSetLayouts);

	cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *shader, 0, m_UsedDescSets, {});

	// Push objectIndex to look up model data from the transform SSBO
	if (shader->GetPushConstantSize() > 0)
	{
		int objectIndex = m_TransfromIndex;
		cmd.UpdatePushConstants(*shader->GetGraphicsPipeline(),
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(int), &objectIndex);
	}

	cmd.DrawMesh(*m_Mesh, *shader, 1);
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
			8, // Shadow map (cascade array)
			0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{
				{
					VansRenderPassManager::GetInstance()->GetCascadeShadowSampler(),
					VansRenderPassManager::GetInstance()->GetCascadeShadowArrayView(),
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
	static_cast<VansSkyBoxMaterial*>(m_Material)->UpdateAtmosphereMaterialData(materialManager, lightManager);

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

// VansShadowRenderNode removed – shadow pass now uses DrawWithPassShader() on opaque nodes

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
