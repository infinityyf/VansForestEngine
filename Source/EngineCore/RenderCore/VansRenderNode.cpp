#include "VansRenderNode.h"
#include "VansPostProcessProfile.h"
#include "VansCamera.h"
#include "VansScene.h"
#include "VulkanCore/VansMesh.h"
#include "VulkanCore/VansVKDevice.h"
#include "VulkanCore/VansVKDescriptorManager.h"
#include "VulkanCore/VansDescriptorSetLayouts.h"
#include "VulkanCore/VansRenderPass.h"
#include "../../EngineCore/RenderCore/TerrainCore/VansTerrain.h"
#include "../Util/VansLog.h"
#include "../AnimationCore/VansAnimationNode.h"
#include <atomic>
#include <iostream>
using namespace VansGraphics;

namespace
{
	std::atomic<std::uint64_t> g_RenderNodeDescriptorValidationFailures{ 0 };
}

VansGraphics::VansRenderNode::VansRenderNode(VkDevice& device, RenderNodeType typee)
{
	m_Device = device;
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
	if (!m_Mesh)
	{
		VANS_LOG_ERROR("[VansRenderNode] Skipping draw for node '" << m_NodeName << "': missing mesh.");
		return false;
	}

	if (!m_Material)
	{
		VANS_LOG_ERROR("[VansRenderNode] Skipping draw for node '" << m_NodeName << "': missing material.");
		return false;
	}

	return true;
}

bool VansGraphics::VansRenderNode::ValidateDescriptorBindings(
	const char* passName,
	const std::vector<VkDescriptorSetLayout>& layouts,
	const std::vector<VkDescriptorSet>& sets) const
{
	if (layouts.size() != sets.size())
	{
		g_RenderNodeDescriptorValidationFailures.fetch_add(1, std::memory_order_relaxed);
		VANS_LOG_ERROR("[VansRenderNode] Skipping draw for node '" << m_NodeName
			<< "' pass '" << (passName ? passName : "Unknown")
			<< "': descriptor layout/set count mismatch. layouts=" << layouts.size()
			<< ", sets=" << sets.size());
		return false;
	}

	for (size_t i = 0; i < layouts.size(); ++i)
	{
		if (layouts[i] == VK_NULL_HANDLE || sets[i] == VK_NULL_HANDLE)
		{
			g_RenderNodeDescriptorValidationFailures.fetch_add(1, std::memory_order_relaxed);
			VANS_LOG_ERROR("[VansRenderNode] Skipping draw for node '" << m_NodeName
				<< "' pass '" << (passName ? passName : "Unknown")
				<< "': null descriptor binding at set " << i);
			return false;
		}
	}

	return true;
}

std::uint64_t VansGraphics::VansRenderNode::GetDescriptorValidationFailureCount()
{
	return g_RenderNodeDescriptorValidationFailures.load(std::memory_order_relaxed);
}

void VansGraphics::VansRenderNode::ResetDescriptorValidationFailureCount()
{
	g_RenderNodeDescriptorValidationFailures.store(0, std::memory_order_relaxed);
}

void VansGraphics::VansRenderNode::DestroyDescriptorSets()
{
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSetLayout(modelBufferLayout);
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(modelBufferDescriptorSets);

	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSetLayout(textureResourceLayout);
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(textureResourceDescriptorSets);

	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSetLayout(frameBufferInputLayout);
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(frameBufferInputDescriptorSets);

	modelBufferLayout = VK_NULL_HANDLE;
	textureResourceLayout = VK_NULL_HANDLE;
	frameBufferInputLayout = VK_NULL_HANDLE;
	modelBufferDescriptorSets.clear();
	textureResourceDescriptorSets.clear();
	frameBufferInputDescriptorSets.clear();
	m_UsedDescSetLayouts.clear();
	m_UsedDescSets.clear();
	m_DescriptorsetsDirty = true;
	m_DescriptorsetsSetDone = false;
}

void VansGraphics::VansRenderNode::RecreateDescriptorSets(
	VansCamera* camera,
	VansLightManager& lightManager,
	VansMaterialManager& materialManager)
{
	DestroyDescriptorSets();
	CreateDescriptorSets(camera, lightManager, materialManager);
	m_DescriptorsetsDirty = true;
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

void VansGraphics::VansRenderNode::UpdateWorldBoundsFromModelData()
{
	m_HasWorldBounds = false;
	m_WorldBounds = VansRenderBounds{};
	if (m_Mesh == nullptr || !m_Mesh->HasLocalOBB() || m_HasSkeletonBone)
		return;

	const VansMeshLocalOBB& localOBB = m_Mesh->GetLocalOBB();
	m_WorldBounds = MakeRenderBoundsFromLocalOBB(
		localOBB.center,
		localOBB.axes,
		localOBB.halfExtent,
		m_ModelData.ModelMatrix);
	m_HasWorldBounds = m_WorldBounds.IsValid();
}

void VansGraphics::VansRenderNode::UpdateWorldBoundsFromTransform()
{
	m_HasWorldBounds = false;
	m_WorldBounds = VansRenderBounds{};
	if (m_Mesh == nullptr || !m_Mesh->HasLocalOBB() || m_HasSkeletonBone)
		return;

	const VansMeshLocalOBB& localOBB = m_Mesh->GetLocalOBB();
	m_WorldBounds = MakeRenderBoundsFromLocalOBB(
		localOBB.center,
		localOBB.axes,
		localOBB.halfExtent,
		GetTransformMatrix());
	m_HasWorldBounds = m_WorldBounds.IsValid();
}

void VansGraphics::VansRenderNode::BeforeDrawCall()
{
	ComputeModelDataFromTransform();
	UpdateWorldBoundsFromModelData();
	// Initialize PrevModelMatrix to current so first frame has zero motion
	m_ModelData.PrevModelMatrix = m_ModelData.ModelMatrix;
}

bool VansGraphics::VansRenderNode::HasValidSkeletalSkinningResources() const
{
	if (m_VertexDeformationState.HasValidSkeletalSkinningResources())
		return true;

	return m_HasSkeletonBone &&
		m_AnimOwner != nullptr &&
		m_AnimBoneIDBuffer != nullptr &&
		m_AnimBoneWeightBuffer != nullptr;
}

std::uint32_t VansGraphics::VansRenderNode::BuildVertexFeatureMask() const
{
	std::uint32_t mask = m_VertexDeformationState.BuildFeatureMask();
	if (HasValidSkeletalSkinningResources())
		mask |= VANS_VERTEX_FEATURE_SKELETAL_SKINNING;
	return mask;
}

void VansGraphics::VansRenderNode::UpdateModelData()
{
	// Save current ModelMatrix as previous before computing new one
	m_ModelData.PrevModelMatrix = m_ModelData.ModelMatrix;

	ComputeModelDataFromTransform();
	UpdateWorldBoundsFromModelData();
	
	// Push updated data to GPU using the persistently mapped instance buffer in VansScene
	// Update at the offset specified by m_TransfromIndex
	if (m_Scene && m_TransfromIndex >= 0)
	{
		m_Scene->UpdateMappedInstanceTransformData(m_ModelData, static_cast<uint32_t>(m_TransfromIndex));
	}
}

// Helper: map node type to its primary render-pass name.
static const char* GetPrimaryPassName(VansGraphics::RenderNodeType type)
{
	using namespace VansGraphics;
	switch (type)
	{
	case OPAQUE_NODE:       return VansPass::GBUFFER;
	case HAIR_NODE:         return VansPass::HAIR_VISIBILITY;
	case FORWARD_OPAQUE_AFTER_DEFERRED_NODE:
		return VansPass::FORWARD_OPAQUE_AFTER_DEFERRED;
	case TRANSPARENT_NODE:  return VansPass::FORWARD_TRANSPARENT;
	case SKY_BOX_NODE:      return VansPass::SKY_BOX;
	case POSTPROCESS_NODE:  return VansPass::POST_PROCESS;
	case DEFERRED_NODE:     return VansPass::DEFERRED;
	case SCREEN_SPACE_NODE: return VansPass::SCREEN_SPACE;
	case DECAL_NODE:        return VansPass::DECAL_GBUFFER;
	default:                return VansPass::GBUFFER;
	}
}

void VansGraphics::VansRenderNode::Draw(VansVKCommandBuffer& cmd, GlobalStateData& globalStateData)
{
	if (!CheckRenderNodeState())
		return;

	VansGraphicsShader* shader = m_Material->GetPassShader(GetPrimaryPassName(m_NodeType));
	if (!shader) return;

	if (!ValidateDescriptorBindings(GetPrimaryPassName(m_NodeType), m_UsedDescSetLayouts, m_UsedDescSets))
		return;

	cmd.BindMesh(*m_Mesh, 0, globalStateData);

	VansVKGraphicsPipeline* pipeline = cmd.EnsureGraphicsShader(*shader, globalStateData, m_UsedDescSetLayouts);
	if (pipeline == nullptr)
		return;

	cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, m_UsedDescSets, {});

	if (shader->GetPushConstantSize() > 0)
	{
		VansDrawPushConstant pc{};
		switch (m_Material->m_MaterialType)
		{
		case VansMaterialType::VAN_PBR:
			pc.materialIndex = static_cast<VansPBRMaterial*>(m_Material)->m_MaterialIndex;
			break;
		case VansMaterialType::VAN_EMISSIVE:
		case VansMaterialType::VAN_PBR_EMISSIVE:
			pc.materialIndex = static_cast<VansEmissiveMaterial*>(m_Material)->m_MaterialIndex;
			break;
		case VansMaterialType::VAN_DECAL:
			pc.materialIndex = static_cast<VansDecalMaterial*>(m_Material)->m_MaterialIndex;
			break;
		case VansMaterialType::VAN_SUBSURFACE:
			pc.materialIndex = static_cast<VansSubsurfaceMaterial*>(m_Material)->m_MaterialIndex;
			break;
		case VansMaterialType::VAN_CLOTH:
			pc.materialIndex = m_Material->m_MaterialIndex;
			break;
		case VansMaterialType::VAN_SKIN:
			pc.materialIndex = m_Material->m_MaterialIndex;
			break;
		case VansMaterialType::VAN_CUSTOM_SHADER:
			pc.materialIndex = m_Material->m_MaterialIndex;
			break;
		default:
			pc.materialIndex = -1;
			break;
		}
		if (m_Material->m_MaterialType == VansMaterialType::VAN_CUSTOM_SHADER)
		{
			const auto* manager = m_Scene ? m_Scene->GetMaterialManager() : nullptr;
			const int customPayloadCount = manager
				? static_cast<int>(manager->m_GlobalCustomMaterialParamData.size())
				: 0;
			if (pc.materialIndex < 0 || pc.materialIndex >= customPayloadCount)
			{
				VANS_LOG_ERROR("[VansRenderNode] Skipping custom shader draw for material '"
					<< m_Material->m_AssetName
					<< "': materialIndex=" << pc.materialIndex
					<< ", customPayloadCount=" << customPayloadCount);
				return;
			}
		}
		pc.transformIndex = m_TransfromIndex;
		pc.vertexFeatureMask = BuildVertexFeatureMask();
		cmd.UpdatePushConstants(*pipeline, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, shader->GetPushConstantSize(), &pc);
	}

	cmd.DrawMesh(*m_Mesh, *pipeline, 1);
}

bool VansGraphics::VansRenderNode::PreparePipelineForDraw(VkDevice& device, GlobalStateData global_state)
{
	if (m_Mesh == nullptr || m_Material == nullptr)
		return true;

	VansGraphicsShader* shader = m_Material->GetPassShader(GetPrimaryPassName(m_NodeType));
	return PreparePipelineForShader(device, global_state, shader, m_UsedDescSetLayouts, m_UsedDescSets);
}

bool VansGraphics::VansRenderNode::PreparePipelineForShader(
	VkDevice& device,
	GlobalStateData global_state,
	VansGraphicsShader* shader,
	const std::vector<VkDescriptorSetLayout>& layouts,
	const std::vector<VkDescriptorSet>& sets)
{
	if (m_Mesh == nullptr || shader == nullptr)
		return true;

	if (layouts.size() != sets.size())
		return true;
	for (size_t i = 0; i < layouts.size(); ++i)
	{
		if (layouts[i] == VK_NULL_HANDLE || sets[i] == VK_NULL_HANDLE)
			return true;
	}

	global_state.vertexInputAttributeDescriptions = &m_Mesh->m_VertexInputAttributeDescriptions;
	global_state.vertexInputBindingDescriptions = &m_Mesh->m_VertexInputBindingDescriptions;
	return shader->GetGraphicsPipeline(device, global_state, layouts) != nullptr;
}

void VansGraphics::VansRenderNode::DrawCascadeShadowWithPassShader(VansVKCommandBuffer& cmd, GlobalStateData& global_state,
                                                                     VansGraphicsShader* passShader,
                                                                     const std::vector<VkDescriptorSet>& descSets,
                                                                     const std::vector<VkDescriptorSetLayout>& descSetLayouts)
{
	if (!passShader) return;

	if (!CheckRenderNodeState())
		return;

	if (!ValidateDescriptorBindings("CascadeShadow", descSetLayouts, descSets))
		return;

	cmd.BindMesh(*m_Mesh, 0, global_state);

	VansVKGraphicsPipeline* pipeline = cmd.EnsureGraphicsShader(*passShader, global_state, descSetLayouts);
	if (pipeline == nullptr)
		return;

	cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, descSets, {});

	if (passShader->GetPushConstantSize() > 0)
	{
		// Shadow shader expects: { materialIndex, objectIndex, cascadeIndex, vertexFeatureMask }
		int matIdx = -1;
		if (m_Material->m_MaterialType == VansMaterialType::VAN_PBR)
			matIdx = static_cast<VansPBRMaterial*>(m_Material)->m_MaterialIndex;
		else if (m_Material->m_MaterialType == VansMaterialType::VAN_EMISSIVE ||
			m_Material->m_MaterialType == VansMaterialType::VAN_PBR_EMISSIVE)
			matIdx = static_cast<VansEmissiveMaterial*>(m_Material)->m_MaterialIndex;
		int pushData[4] = {
			matIdx,
			m_TransfromIndex,
			global_state.cascadeIndex,
			static_cast<int>(BuildVertexFeatureMask())
		};
		cmd.UpdatePushConstants(*pipeline,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, passShader->GetPushConstantSize(), pushData);
	}

	cmd.DrawMesh(*m_Mesh, *pipeline, 1);
}

void VansGraphics::VansRenderNode::DrawPunctualShadowWithPassShader(VansVKCommandBuffer& cmd, GlobalStateData& global_state,
                                                                      VansGraphicsShader* passShader,
                                                                      const std::vector<VkDescriptorSet>& descSets,
                                                                      const std::vector<VkDescriptorSetLayout>& descSetLayouts,
	                                                                  int shadowViewIndex)
{
	if (!passShader) return;

	if (!CheckRenderNodeState())
		return;

	if (!ValidateDescriptorBindings("PunctualShadow", descSetLayouts, descSets))
		return;

	cmd.BindMesh(*m_Mesh, 0, global_state);

	VansVKGraphicsPipeline* pipeline = cmd.EnsureGraphicsShader(*passShader, global_state, descSetLayouts);
	if (pipeline == nullptr)
		return;

	// The render job owns the exact view. Passing it directly prevents a light
	// array reorder from redirecting this draw into another light's tile.
	int data[5] = {
		shadowViewIndex,
		0,
		0,
		m_TransfromIndex,
		static_cast<int>(BuildVertexFeatureMask())
	};
	cmd.UpdatePushConstants(*pipeline, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		0, passShader->GetPushConstantSize(), data);

	cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, descSets, {});

	cmd.DrawMesh(*m_Mesh, *pipeline, 1);
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
	m_UsedDescSetLayouts.push_back(m_Scene->GetGlobalDescriptorSetLayout());
	m_UsedDescSets.push_back(m_Scene->GetGlobalDescriptorSet());

	// Set 1: Per-Pass (empty for common geometry pass)
	m_UsedDescSetLayouts.push_back(m_Scene->GetEmptyPassLayout());
	m_UsedDescSets.push_back(m_Scene->GetEmptyPassDescriptorSet());

	// Set 2: Per-Object — shared Transform SSBO (all nodes, animated or not)
	m_UsedDescSetLayouts.push_back(m_Scene->GetObjectDescriptorSetLayout());
	m_UsedDescSets.push_back(m_Scene->GetObjectDescriptorSet());

	// Set 3: Per-Node Animation (Bone IDs + Bone Matrices + Bone Weights)
	// Animated nodes get a freshly allocated descriptor set with real GPU buffers.
	// Each submesh has its own bone ID and weight buffers — no offset needed.
	// Static nodes reuse the scene-shared dummy set, guarded by vertexFeatureMask.
	m_UsedDescSetLayouts.push_back(m_Scene->GetVertexDeformationDescriptorSetLayout());

	if (HasValidSkeletalSkinningResources())
	{
		auto* descManager = VansVKDescriptorManager::GetInstance();
		descManager->AllocateDescriptorSet(
			{ m_Scene->GetVertexDeformationDescriptorSetLayout() },
			modelBufferDescriptorSets,
			VansDescriptorLifetimeRole::ScenePersistent);

		m_UsedDescSets.push_back(modelBufferDescriptorSets[0]);
		VANS_LOG("[VansCommonRenderNode] " << m_NodeName << ": per-node animation descriptor set (Set 3) created");
	}
	else
	{
		// Static node: bind shared dummy animation set — bone/weight data is never read
		m_UsedDescSets.push_back(m_Scene->GetVertexDeformationDescriptorSet());
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
		if (skin->m_SkinOwnedLayout != VK_NULL_HANDLE && !skin->m_SkinOwnedDescSets.empty())
		{
			m_UsedDescSetLayouts.push_back(skin->m_SkinOwnedLayout);
			m_UsedDescSets.push_back(skin->m_SkinOwnedDescSets[0]);
		}
		else
		{
			VANS_LOG_ERROR("[VansCommonRenderNode] " << m_NodeName << ": skin material descriptors are not ready.");
		}
	}

	// Set 4: Per-Material Cloth Texture (albedo + normal + roughness + ao)
	// Owned by VansClothMaterial; built once and shared by all nodes using this material.
	if (m_Material && m_Material->m_MaterialType == VansMaterialType::VAN_CLOTH)
	{
		VansClothMaterial* cloth = static_cast<VansClothMaterial*>(m_Material);
		if (cloth->m_ClothOwnedLayout == VK_NULL_HANDLE)
		{
			cloth->BuildClothTextureDescriptors();
		}
		if (cloth->m_ClothOwnedLayout != VK_NULL_HANDLE && !cloth->m_ClothOwnedDescSets.empty())
		{
			m_UsedDescSetLayouts.push_back(cloth->m_ClothOwnedLayout);
			m_UsedDescSets.push_back(cloth->m_ClothOwnedDescSets[0]);
		}
		else
		{
			VANS_LOG_ERROR("[VansCommonRenderNode] " << m_NodeName << ": cloth material descriptors are not ready.");
		}
	}

	// Set 4: Per-Material Hair Texture (albedo+alpha, normal, roughness, ao, shift)
	// Owned by VansHairMaterial; built once and shared by all nodes using this material.
	if (m_Material && m_Material->m_MaterialType == VansMaterialType::VAN_HAIR)
	{
		VansHairMaterial* hair = static_cast<VansHairMaterial*>(m_Material);
		if (hair->m_HairOwnedLayout == VK_NULL_HANDLE)
		{
			hair->BuildHairDescriptors(m_Device);
		}
		if (hair->m_HairOwnedLayout != VK_NULL_HANDLE && !hair->m_HairOwnedDescSets.empty())
		{
			m_UsedDescSetLayouts.push_back(hair->m_HairOwnedLayout);
			m_UsedDescSets.push_back(hair->m_HairOwnedDescSets[0]);
		}
		else
		{
			VANS_LOG_ERROR("[VansCommonRenderNode] " << m_NodeName << ": hair material descriptors are not ready.");
		}
	}

	// ── Shadow descriptor sets (Global + EmptyPass + Object + Animation) ──
	// 动画节点使用独立的每节点骨骼描述符集；静态节点使用场景共享的 dummy set。
	VkDescriptorSet shadowAnimSet = HasValidSkeletalSkinningResources()
		? modelBufferDescriptorSets[0]
		: m_Scene->GetVertexDeformationDescriptorSet();

	m_ShadowDescSetLayouts = {
		m_Scene->GetGlobalDescriptorSetLayout(),        // Set 0
		m_Scene->GetEmptyPassLayout(),                  // Set 1
		m_Scene->GetObjectDescriptorSetLayout(),        // Set 2
		m_Scene->GetVertexDeformationDescriptorSetLayout(),     // Set 3
	};
	m_ShadowDescSets = {
		m_Scene->GetGlobalDescriptorSet(),              // Set 0
		m_Scene->GetEmptyPassDescriptorSet(),           // Set 1
		m_Scene->GetObjectDescriptorSet(),              // Set 2
		shadowAnimSet,                               // Set 3
	};

	if (m_Material && m_Material->m_MaterialType == VansMaterialType::VAN_HAIR)
	{
		VansHairMaterial* hair = static_cast<VansHairMaterial*>(m_Material);
		if (hair->m_HairOwnedLayout != VK_NULL_HANDLE && !hair->m_HairOwnedDescSets.empty())
		{
			m_ShadowDescSetLayouts.push_back(hair->m_HairOwnedLayout);
			m_ShadowDescSets.push_back(hair->m_HairOwnedDescSets[0]);
		}
	}
}

void VansGraphics::VansCommonRenderNode::RefreshAnimationDescriptorSet()
{
	if (!m_Scene)
		return;

	if (!HasValidSkeletalSkinningResources())
		return;

	auto* descManager = VansVKDescriptorManager::GetInstance();
	if (modelBufferDescriptorSets.empty())
	{
		descManager->AllocateDescriptorSet(
			{ m_Scene->GetVertexDeformationDescriptorSetLayout() },
			modelBufferDescriptorSets,
			VansDescriptorLifetimeRole::ScenePersistent);
	}

	if (modelBufferDescriptorSets.empty())
		return;

	VkDescriptorSet animationSet = modelBufferDescriptorSets[0];
	if (m_UsedDescSets.size() > 3)
	{
		m_UsedDescSets[3] = animationSet;
	}
	if (m_ShadowDescSets.size() > 3)
	{
		m_ShadowDescSets[3] = animationSet;
	}

	m_DescriptorsetsDirty = true;
}

void VansGraphics::VansCommonRenderNode::MarkAnimationDescriptorDirty()
{
	m_DescriptorsetsDirty = true;
}

void VansGraphics::VansCommonRenderNode::SyncMaterialToGPU(VansMaterial* mat, VansMaterialManager& materialManager)
{
	if (!mat) return;
	if (mat->m_MaterialType == VansMaterialType::VAN_PBR)
	{
		VansPBRMaterial* pbr = static_cast<VansPBRMaterial*>(mat);
		int idx = pbr->m_MaterialIndex;
		materialManager.m_GlobalPBRDataBuffer.UpdateMapped(
			&pbr->m_BasePBRParam,
			sizeof(VansBasePBRParam) * idx,
			sizeof(VansBasePBRParam));
	}
	else if (mat->m_MaterialType == VansMaterialType::VAN_EMISSIVE ||
		mat->m_MaterialType == VansMaterialType::VAN_PBR_EMISSIVE)
	{
		VansEmissiveMaterial* emissive = static_cast<VansEmissiveMaterial*>(mat);
		int idx = emissive->m_MaterialIndex;
		materialManager.m_GlobalPBRDataBuffer.UpdateMapped(
			&emissive->m_BasePBRParam,
			sizeof(VansBasePBRParam) * idx,
			sizeof(VansBasePBRParam));
	}
	else if (mat->m_MaterialType == VansMaterialType::VAN_SUBSURFACE)
	{
		VansSubsurfaceMaterial* sss = static_cast<VansSubsurfaceMaterial*>(mat);
		int idx = sss->m_MaterialIndex;
		sss->m_BasePBRParam.m_albedo = sss->m_SubsurfaceColor;
		sss->m_BasePBRParam.m_roughness = sss->m_SubsurfacePower;
		sss->m_BasePBRParam.m_metallic = sss->m_Thickness;
		sss->m_BasePBRParam.m_ao = sss->m_SubsurfaceAmount;
		sss->m_BasePBRParam.padding = sss->m_IOR;
		materialManager.m_GlobalPBRDataBuffer.UpdateMapped(
			&sss->m_BasePBRParam,
			sizeof(VansBasePBRParam) * idx,
			sizeof(VansBasePBRParam));
	}
	else if (mat->m_MaterialType == VansMaterialType::VAN_SKIN)
	{
		VansSkinMaterial* skin = static_cast<VansSkinMaterial*>(mat);
		int idx = skin->m_MaterialIndex;
		if (idx < 0) return;
		materialManager.m_GlobalPBRDataBuffer.UpdateMapped(
			&skin->m_BasePBRParam,
			sizeof(VansBasePBRParam) * idx,
			sizeof(VansBasePBRParam));
		VansSkinGPUParam skinPayload = skin->BuildGPUParam();
		materialManager.ResolveSkinProfileLUTForMaterial(*skin, skinPayload, nullptr);
		materialManager.m_GlobalSkinDataBuffer.UpdateMapped(
			&skinPayload,
			sizeof(VansSkinGPUParam) * idx,
			sizeof(VansSkinGPUParam));
		if (idx < static_cast<int>(materialManager.m_GlobalSkinParamData.size()))
			materialManager.m_GlobalSkinParamData[idx] = skinPayload;
	}
	else if (mat->m_MaterialType == VansMaterialType::VAN_CLOTH)
	{
		VansClothMaterial* cloth = static_cast<VansClothMaterial*>(mat);
		int idx = cloth->m_MaterialIndex;
		if (idx < 0) return;
		materialManager.m_GlobalPBRDataBuffer.UpdateMapped(
			&cloth->m_BasePBRParam,
			sizeof(VansBasePBRParam) * idx,
			sizeof(VansBasePBRParam));
		const VansClothGPUParam clothPayload = cloth->BuildGPUParam();
		materialManager.m_GlobalClothDataBuffer.UpdateMapped(
			&clothPayload,
			sizeof(VansClothGPUParam) * idx,
			sizeof(VansClothGPUParam));
		if (idx < static_cast<int>(materialManager.m_GlobalClothParamData.size()))
			materialManager.m_GlobalClothParamData[idx] = clothPayload;
	}
	else if (mat->m_MaterialType == VansMaterialType::VAN_CUSTOM_SHADER)
	{
		int idx = mat->m_MaterialIndex;
		if (idx < 0 || idx >= static_cast<int>(materialManager.m_GlobalCustomMaterialParamData.size()))
			return;

		auto& gpuPayload = materialManager.m_GlobalCustomMaterialParamData[idx];
		for (int valueIndex = 0; valueIndex < VANS_CUSTOM_MATERIAL_VEC4_COUNT; ++valueIndex)
			gpuPayload.values[valueIndex] = mat->m_CustomMaterialPayload.values[valueIndex];

		materialManager.m_GlobalCustomMaterialDataBuffer.UpdateMapped(
			&gpuPayload,
			sizeof(VansCustomMaterialPayload) * idx,
			sizeof(VansCustomMaterialPayload));
	}
}

void VansGraphics::VansCommonRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	// Sync CPU material params to the global GPU PBR buffer so editor changes take effect.
	SyncMaterialToGPU(m_Material, materialManager);
	UpdateDescriptorSets(materialManager);
}

void VansGraphics::VansCommonRenderNode::UpdateDescriptorSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}

	// All resources are now in the global descriptor set (Set 0)
	// No per-object descriptor updates needed
	if (HasValidSkeletalSkinningResources())
	{
		RefreshAnimationDescriptorSet();
		if (modelBufferDescriptorSets.empty())
		{
			return;
		}

		auto* descManager = VansVKDescriptorManager::GetInstance();
		descManager->BeginDescriptorUpdate();
		// binding 0: Per-vertex Bone IDs SSBO (per-submesh)
		descManager->WriteBufferDescriptor(
			modelBufferDescriptorSets[0], VERTEX_DEFORMATION_BINDING_BONEID_SSBO,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ (m_VertexDeformationState.boneIDBuffer ? m_VertexDeformationState.boneIDBuffer : m_AnimBoneIDBuffer)->GetNativeBuffer(), 0, VK_WHOLE_SIZE }});
		// binding 1: Bone Matrices SSBO (shared across all submeshes)
		descManager->WriteBufferDescriptor(
			modelBufferDescriptorSets[0], VERTEX_DEFORMATION_BINDING_BONE_SSBO,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ (m_VertexDeformationState.skinningOwner ? m_VertexDeformationState.skinningOwner : m_AnimOwner)->GetBoneBuffer(0).GetNativeBuffer(), 0, VK_WHOLE_SIZE }});
		// binding 2: Per-vertex Bone Weights SSBO (per-submesh)
		descManager->WriteBufferDescriptor(
			modelBufferDescriptorSets[0], VERTEX_DEFORMATION_BINDING_BONEWEIGHT_SSBO,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ (m_VertexDeformationState.boneWeightBuffer ? m_VertexDeformationState.boneWeightBuffer : m_AnimBoneWeightBuffer)->GetNativeBuffer(), 0, VK_WHOLE_SIZE }});
		descManager->WriteBufferDescriptor(
			modelBufferDescriptorSets[0], VERTEX_DEFORMATION_BINDING_PREVIOUS_BONE_SSBO,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ (m_VertexDeformationState.skinningOwner ? m_VertexDeformationState.skinningOwner : m_AnimOwner)->GetPreviousBoneBuffer(0).GetNativeBuffer(), 0, VK_WHOLE_SIZE }});
		descManager->CommitDescriptorUpdates();
	}

	m_DescriptorsetsDirty = false;
}

// ============================================================
// VansTransparentRenderNode
// ============================================================

void VansGraphics::VansTransparentRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	// Set 0: Global (Camera UBO is universal, still needed for VP matrices)
	m_UsedDescSetLayouts.push_back(m_Scene->GetGlobalDescriptorSetLayout());
	m_UsedDescSets.push_back(m_Scene->GetGlobalDescriptorSet());

	if (m_Material->m_MaterialType == VansMaterialType::VAN_PBR_TRANSMISSION)
	{
		VansVKDevice* device = camera ? static_cast<VansVKDevice*>(camera->GetGraphicsDevice()) : nullptr;
		const VkDescriptorSetLayout passLayout = device ? device->GetTransmissionGlassPassLayout() : VK_NULL_HANDLE;
		const VkDescriptorSet passSet = device ? device->GetTransmissionGlassPassDescriptorSet() : VK_NULL_HANDLE;
		if (passLayout != VK_NULL_HANDLE && passSet != VK_NULL_HANDLE)
		{
			m_UsedDescSetLayouts.push_back(passLayout);
			m_UsedDescSets.push_back(passSet);
		}
		else
		{
			m_UsedDescSetLayouts.push_back(m_Scene->GetEmptyPassLayout());
			m_UsedDescSets.push_back(m_Scene->GetEmptyPassDescriptorSet());
		}
	}
	else if (m_Material->m_MaterialType == VansMaterialType::VAN_CUSTOM_SHADER)
	{
		// Custom forward shaders consume global custom material payloads from set 0.
		// Keep set numbering contiguous so they can still bind set 2 for transforms.
		m_UsedDescSetLayouts.push_back(m_Scene->GetEmptyPassLayout());
		m_UsedDescSets.push_back(m_Scene->GetEmptyPassDescriptorSet());
	}
	else
	{
		// Set 1: Material-owned resources (layout held by VansTransparentMaterial)
		VansTransparentMaterial* trans = static_cast<VansTransparentMaterial*>(m_Material);
		if (trans->m_TransparentOwnedLayout == VK_NULL_HANDLE)
		{
			// Build layout, allocate set, and write texture bindings from shader slot order
			trans->BuildTransparentTextureDescriptors();
		}
		if (trans->m_TransparentOwnedLayout != VK_NULL_HANDLE && !trans->m_TransparentOwnedDescSets.empty())
		{
			m_UsedDescSetLayouts.push_back(trans->m_TransparentOwnedLayout);
			m_UsedDescSets.push_back(trans->m_TransparentOwnedDescSets[0]);
		}
		else
		{
			VANS_LOG_ERROR("[VansTransparentRenderNode] " << m_NodeName << ": transparent material descriptors are not ready.");
		}
	}

	// Set 2: Object Transforms SSBO (accessed via objectIndex push constant)
	m_UsedDescSetLayouts.push_back(m_Scene->GetObjectDescriptorSetLayout());
	m_UsedDescSets.push_back(m_Scene->GetObjectDescriptorSet());

	// Set 3: Vertex deformation. Static transparent nodes bind the shared dummy set.
	m_UsedDescSetLayouts.push_back(m_Scene->GetVertexDeformationDescriptorSetLayout());
	if (HasValidSkeletalSkinningResources())
	{
		auto* descManager = VansVKDescriptorManager::GetInstance();
		descManager->AllocateDescriptorSet(
			{ m_Scene->GetVertexDeformationDescriptorSetLayout() },
			modelBufferDescriptorSets,
			VansDescriptorLifetimeRole::ScenePersistent);
		m_UsedDescSets.push_back(modelBufferDescriptorSets[0]);
	}
	else
	{
		m_UsedDescSets.push_back(m_Scene->GetVertexDeformationDescriptorSet());
	}
}

void VansGraphics::VansTransparentRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	UpdateDescriptorSets(materialManager);
}

void VansGraphics::VansTransparentRenderNode::UpdateDescriptorSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}
	m_DescriptorsetsDirty = false;

	if (HasValidSkeletalSkinningResources())
	{
		if (modelBufferDescriptorSets.empty())
			return;

		auto* descManager = VansVKDescriptorManager::GetInstance();
		descManager->BeginDescriptorUpdate();
		descManager->WriteBufferDescriptor(
			modelBufferDescriptorSets[0], VERTEX_DEFORMATION_BINDING_BONEID_SSBO,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ (m_VertexDeformationState.boneIDBuffer ? m_VertexDeformationState.boneIDBuffer : m_AnimBoneIDBuffer)->GetNativeBuffer(), 0, VK_WHOLE_SIZE }});
		descManager->WriteBufferDescriptor(
			modelBufferDescriptorSets[0], VERTEX_DEFORMATION_BINDING_BONE_SSBO,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ (m_VertexDeformationState.skinningOwner ? m_VertexDeformationState.skinningOwner : m_AnimOwner)->GetBoneBuffer(0).GetNativeBuffer(), 0, VK_WHOLE_SIZE }});
		descManager->WriteBufferDescriptor(
			modelBufferDescriptorSets[0], VERTEX_DEFORMATION_BINDING_BONEWEIGHT_SSBO,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ (m_VertexDeformationState.boneWeightBuffer ? m_VertexDeformationState.boneWeightBuffer : m_AnimBoneWeightBuffer)->GetNativeBuffer(), 0, VK_WHOLE_SIZE }});
		descManager->WriteBufferDescriptor(
			modelBufferDescriptorSets[0], VERTEX_DEFORMATION_BINDING_PREVIOUS_BONE_SSBO,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ (m_VertexDeformationState.skinningOwner ? m_VertexDeformationState.skinningOwner : m_AnimOwner)->GetPreviousBoneBuffer(0).GetNativeBuffer(), 0, VK_WHOLE_SIZE }});
		descManager->CommitDescriptorUpdates();
	}
}

void VansGraphics::VansTransparentRenderNode::Draw(VansVKCommandBuffer& cmd, GlobalStateData& globalStateData)
{
	if (!CheckRenderNodeState())
		return;

	VansGraphicsShader* shader = m_Material->GetPassShader(VansPass::FORWARD_TRANSPARENT);
	if (!shader) return;

	if (!ValidateDescriptorBindings(VansPass::FORWARD_TRANSPARENT, m_UsedDescSetLayouts, m_UsedDescSets))
		return;

	cmd.BindMesh(*m_Mesh, 0, globalStateData);

	cmd.EnsureGraphicsShader(*shader, globalStateData, m_UsedDescSetLayouts);

	cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *shader, 0, m_UsedDescSets, {});

	if (shader->GetPushConstantSize() > 0)
	{
		if (m_Material->m_MaterialType == VansMaterialType::VAN_CUSTOM_SHADER ||
			m_Material->m_MaterialType == VansMaterialType::VAN_PBR_TRANSMISSION)
		{
			const auto* manager = m_Scene ? m_Scene->GetMaterialManager() : nullptr;
			const int customPayloadCount = manager
				? static_cast<int>(manager->m_GlobalCustomMaterialParamData.size())
				: 0;
			if (m_Material->m_MaterialIndex < 0 || m_Material->m_MaterialIndex >= customPayloadCount)
			{
				VANS_LOG_ERROR("[VansTransparentRenderNode] Skipping custom shader draw for material '"
					<< m_Material->m_AssetName
					<< "': materialIndex=" << m_Material->m_MaterialIndex
					<< ", customPayloadCount=" << customPayloadCount);
				return;
			}

			VansDrawPushConstant pc{};
			pc.materialIndex = m_Material->m_MaterialIndex;
			pc.transformIndex = m_TransfromIndex;
			pc.vertexFeatureMask = BuildVertexFeatureMask();
			cmd.UpdatePushConstants(*shader->GetGraphicsPipeline(),
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, shader->GetPushConstantSize(), &pc);
		}
		else
		{
			// Legacy transparent shaders expect objectIndex as the first int.
			int objectIndex = m_TransfromIndex;
			cmd.UpdatePushConstants(*shader->GetGraphicsPipeline(),
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof(int), &objectIndex);
		}
	}

	cmd.DrawMesh(*m_Mesh, *shader, 1);
}

void VansGraphics::VansPostProcessRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	// Set 0: Global
	m_UsedDescSetLayouts.push_back(m_Scene->GetGlobalDescriptorSetLayout());
	m_UsedDescSets.push_back(m_Scene->GetGlobalDescriptorSet());

	// Set 1: Per-Pass display conversion resources.
	VansDescriptorSetLayoutFactory::CreateAndAllocate_PostProcess(frameBufferInputLayout, frameBufferInputDescriptorSets);

	m_UsedDescSetLayouts.push_back(frameBufferInputLayout);
	m_UsedDescSets.push_back(frameBufferInputDescriptorSets[0]);
}

void VansGraphics::VansPostProcessRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	UpdateDescriptorSets(materialManager);
}

void VansGraphics::VansPostProcessRenderNode::UpdateDescriptorSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	VansVKImage* colorInput = VansRenderPassManager::GetInstance()->GetDisplayPostProcessInput();
	if (colorInput == nullptr)
		return;
	m_DescriptorsetsDirty = false;
	descMgr->BeginDescriptorUpdate();
	descMgr->WriteImageDescriptor(
		frameBufferInputDescriptorSets[0],
		POSTPROCESS_BINDING_COLOR_INPUT,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ {
			colorInput->GetSampler(),
			colorInput->GetImageView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		} });

	// 绑定 Bloom 结果贴图
	VansTexture* bloomResult = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_BLOOM_RESULT);
	if (bloomResult)
	{
		descMgr->WriteImageDescriptor(
			frameBufferInputDescriptorSets[0],
			POSTPROCESS_BINDING_BLOOM_RESULT,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{ {
				bloomResult->GetImage().GetSampler(),
				bloomResult->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_GENERAL
			} });
	}

	VansTexture* exposureCurrent = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_EXPOSURE_CURRENT);
	if (exposureCurrent)
	{
		descMgr->WriteImageDescriptor(
			frameBufferInputDescriptorSets[0],
			POSTPROCESS_BINDING_EXPOSURE_VAL,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{ {
				exposureCurrent->GetImage().GetSampler(),
				exposureCurrent->GetImage().GetImageView(),
				VK_IMAGE_LAYOUT_GENERAL
			} });
	}

	// 绑定后处理参数 UBO
	if (materialManager.m_PostProcessParamsCBBuffer.GetNativeBuffer() != VK_NULL_HANDLE)
	{
		descMgr->WriteBufferDescriptor(
			frameBufferInputDescriptorSets[0],
			POSTPROCESS_BINDING_PP_PARAMS,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			{{ materialManager.m_PostProcessParamsCBBuffer.GetNativeBuffer(), 0, materialManager.m_PostProcessParamsCBBuffer.GetBufferSize() }});
	}

	descMgr->CommitDescriptorUpdates();
}

void VansGraphics::VansDeferredRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	// Set 0: Global (Camera + Lights + Materials + IBL + Bindless)
	m_UsedDescSetLayouts.push_back(m_Scene->GetGlobalDescriptorSetLayout());
	m_UsedDescSets.push_back(m_Scene->GetGlobalDescriptorSet());

	// Set 1: Per-Pass (GBuffer inputs + screen-space effect textures merged)
	VansDescriptorSetLayoutFactory::CreateAndAllocate_DeferredLighting(frameBufferInputLayout, frameBufferInputDescriptorSets);

	m_UsedDescSetLayouts.push_back(frameBufferInputLayout);
	m_UsedDescSets.push_back(frameBufferInputDescriptorSets[0]);
}

void VansGraphics::VansDeferredRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	UpdateDescriptorSets(materialManager);
}

void VansGraphics::VansDeferredRenderNode::UpdateDescriptorSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}

	VansTexture* ssaoFilterResult = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_SSAO_FILTER_RESULT);
	VansTexture* ssgiFilterResult = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_SSGI_FILTER_RESULT);
	VansTexture* ssrAaResult = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_SSRAA_RESULT);
	VansTexture* volumetricFogResult = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_VOLUMETRIC_FOG_RESULT);
	VansTexture* screenSpaceShadow = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_SCREEN_SPACE_SHADOW_RESULT);
	VansTexture* screenSpaceShadowHZB = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_RESULT);
	VansTexture* rectLightEmissive = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_RECT_LIGHT_EMISSIVE);
	VansVKDevice* runtimeDevice = m_Scene->GetRuntimeResourceDevice();

	if (ssaoFilterResult == nullptr || ssgiFilterResult == nullptr || ssrAaResult == nullptr ||
		volumetricFogResult == nullptr ||
		screenSpaceShadow == nullptr || screenSpaceShadowHZB == nullptr || rectLightEmissive == nullptr ||
		runtimeDevice == nullptr ||
		materialManager.m_SSGICBBuffer.GetNativeBuffer() == VK_NULL_HANDLE ||
		!m_Scene->GetIESProfileManager()->IsGPUResourcesCreated())
	{
		// 不清除 dirty 标记，下帧重试（运行时纹理尚未就绪）
		m_DescriptorsetsDirty = true;
		return;
	}

	std::vector<VkDescriptorImageInfo> giIrradianceInfos;
	std::vector<VkDescriptorImageInfo> giVisibilityInfos;
	std::vector<VkDescriptorBufferInfo> giProbeStateInfos;
	giIrradianceInfos.reserve(VANS_SSGI_MAX_GI_REGIONS);
	giVisibilityInfos.reserve(VANS_SSGI_MAX_GI_REGIONS);
	giProbeStateInfos.reserve(VANS_SSGI_MAX_GI_REGIONS);
	auto& rayTracing = runtimeDevice->GetRayTracingContext();
	const uint32_t availableGIRegions = std::min(rayTracing.GetGIRegionCount(), VANS_SSGI_MAX_GI_REGIONS);
	for (uint32_t regionIndex = 0u; regionIndex < availableGIRegions; ++regionIndex)
	{
		VansTexture* irradianceAtlas = rayTracing.GetGIRegionIrradianceAtlas(regionIndex);
		VansTexture* visibilityAtlas = rayTracing.GetGIRegionVisibilityAtlas(regionIndex);
		const VansVKBuffer* probeState = rayTracing.GetGIRegionProbeStateBuffer(regionIndex);
		if (irradianceAtlas == nullptr || visibilityAtlas == nullptr ||
			probeState == nullptr || probeState->GetNativeBuffer() == VK_NULL_HANDLE)
			continue;
		giIrradianceInfos.push_back({
			irradianceAtlas->GetImage().GetSampler(),
			irradianceAtlas->GetImage().GetImageView(),
			VK_IMAGE_LAYOUT_GENERAL });
		giVisibilityInfos.push_back({
			visibilityAtlas->GetImage().GetSampler(),
			visibilityAtlas->GetImage().GetImageView(),
			VK_IMAGE_LAYOUT_GENERAL });
		giProbeStateInfos.push_back({
			probeState->GetNativeBuffer(),
			0,
			probeState->GetBufferSize() });
	}
	if (giIrradianceInfos.empty() || giVisibilityInfos.empty() || giProbeStateInfos.empty())
	{
		m_DescriptorsetsDirty = true;
		return;
	}
	while (giIrradianceInfos.size() < VANS_SSGI_MAX_GI_REGIONS)
	{
		giIrradianceInfos.push_back(giIrradianceInfos.front());
		giVisibilityInfos.push_back(giVisibilityInfos.front());
		giProbeStateInfos.push_back(giProbeStateInfos.front());
	}

	auto* descMgr = VansVKDescriptorManager::GetInstance();
	auto* rp = VansRenderPassManager::GetInstance();
	descMgr->BeginDescriptorUpdate();

	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[0], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { rp->GetNormal().GetSampler(), rp->GetNormal().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[0], 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { rp->GetGbuffer0().GetSampler(), rp->GetGbuffer0().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[0], 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { rp->GetGbuffer1().GetSampler(), rp->GetGbuffer1().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[0], 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { rp->GetGbuffer2().GetSampler(), rp->GetGbuffer2().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[0], 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { rp->GetDepth().GetSampler(), rp->GetDepth().GetImageView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[0], 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		{ { ssaoFilterResult->GetImage().GetSampler(), ssaoFilterResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[0], 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { ssgiFilterResult->GetImage().GetSampler(), ssgiFilterResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[0], 7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		{ { ssrAaResult->GetImage().GetSampler(), ssrAaResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[0], 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { rp->GetCascadeShadowSampler(), rp->GetCascadeShadowArrayView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[0], 9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { rp->GetPunctualShadowMap().GetSampler(), rp->GetPunctualShadowMap().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[0], 13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { volumetricFogResult->GetImage().GetSampler(), volumetricFogResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[0], DEFERRED_BINDING_SCREEN_SPACE_SHADOW, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { screenSpaceShadow->GetImage().GetSampler(), screenSpaceShadow->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[0], DEFERRED_BINDING_RECT_LIGHT_EMISSIVE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { rectLightEmissive->GetImage().GetSampler(), rectLightEmissive->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[0], DEFERRED_BINDING_IES_PROFILES, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { m_Scene->GetIESProfileManager()->GetIESProfileTexture().GetSampler(), m_Scene->GetIESProfileManager()->GetIESProfileTexture().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[0], DEFERRED_BINDING_SCREEN_SPACE_SHADOW_HIZ, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { screenSpaceShadowHZB->GetImage().GetSampler(), screenSpaceShadowHZB->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	descMgr->WriteBufferDescriptor(frameBufferInputDescriptorSets[0], DEFERRED_BINDING_SCREEN_SPACE_SHADOW_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		{ { materialManager.m_ScreenSpaceShadowParamsCBBuffer.GetNativeBuffer(), 0, materialManager.m_ScreenSpaceShadowParamsCBBuffer.GetBufferSize() } });
	descMgr->WriteBufferDescriptor(frameBufferInputDescriptorSets[0], DEFERRED_BINDING_GI_INFO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		{ { materialManager.m_SSGICBBuffer.GetNativeBuffer(), 0, materialManager.m_SSGICBBuffer.GetBufferSize() } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[0], DEFERRED_BINDING_GI_VISIBILITY, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		giVisibilityInfos);
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[0], DEFERRED_BINDING_GI_IRRADIANCE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		giIrradianceInfos);
	for (size_t regionSlot = 0; regionSlot < giProbeStateInfos.size(); ++regionSlot)
	{
		descMgr->WriteBufferDescriptor(
			frameBufferInputDescriptorSets[0],
			DEFERRED_BINDING_GI_PROBE_STATE + static_cast<uint32_t>(regionSlot),
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{ giProbeStateInfos[regionSlot] });
	}
	descMgr->CommitDescriptorUpdates();
	m_DescriptorsetsDirty = false;
}

void VansGraphics::VansScreenSpaceRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	// Set 0: Global
	m_UsedDescSetLayouts.push_back(m_Scene->GetGlobalDescriptorSetLayout());
	m_UsedDescSets.push_back(m_Scene->GetGlobalDescriptorSet());

	// Set 1: Per-Pass (screen-space textures)
	VansDescriptorSetLayoutFactory::CreateAndAllocate_ScreenSpace(textureResourceLayout, textureResourceDescriptorSets);

	m_UsedDescSetLayouts.push_back(textureResourceLayout);
	m_UsedDescSets.push_back(textureResourceDescriptorSets[0]);
}

void VansGraphics::VansScreenSpaceRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	UpdateDescriptorSets(materialManager);
}

void VansGraphics::VansScreenSpaceRenderNode::UpdateDescriptorSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}
	m_DescriptorsetsDirty = false;

	VansTexture* ssaoResult = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_SSAO_RESULT);
	if (ssaoResult == nullptr)
	{
		m_DescriptorsetsDirty = true;
		return;
	}

	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->BeginDescriptorUpdate();
	descMgr->WriteImageDescriptor(
		textureResourceDescriptorSets[0], 0, // Normal
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { VansRenderPassManager::GetInstance()->GetNormal().GetSampler(), VansRenderPassManager::GetInstance()->GetNormal().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(
		textureResourceDescriptorSets[0], 1, // Gbuffer0
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { VansRenderPassManager::GetInstance()->GetGbuffer0().GetSampler(), VansRenderPassManager::GetInstance()->GetGbuffer0().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(
		textureResourceDescriptorSets[0], 2, // Gbuffer1
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { VansRenderPassManager::GetInstance()->GetGbuffer1().GetSampler(), VansRenderPassManager::GetInstance()->GetGbuffer1().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(
		textureResourceDescriptorSets[0], 3, // Gbuffer2
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { VansRenderPassManager::GetInstance()->GetGbuffer2().GetSampler(), VansRenderPassManager::GetInstance()->GetGbuffer2().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(
		textureResourceDescriptorSets[0], 4, // Depth
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { VansRenderPassManager::GetInstance()->GetDepth().GetSampler(), VansRenderPassManager::GetInstance()->GetDepth().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(
		textureResourceDescriptorSets[0], 5, // SSAO output
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		{ { ssaoResult->GetImage().GetSampler(), ssaoResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	descMgr->CommitDescriptorUpdates();
}

void VansGraphics::VansSkyBoxRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	// Set 0: Global
	m_UsedDescSetLayouts.push_back(m_Scene->GetGlobalDescriptorSetLayout());
	m_UsedDescSets.push_back(m_Scene->GetGlobalDescriptorSet());

	// Set 1: Per-Pass (Atmosphere UBO)
	m_UsedDescSetLayouts.push_back(materialManager.m_MaterialAtmosphereDataLayout);
	m_UsedDescSets.push_back(materialManager.m_MaterialAtmosphereDataDescriptorSets[0]);
}

void VansGraphics::VansSkyBoxRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	static_cast<VansSkyBoxMaterial*>(m_Material)->UpdateAtmosphereMaterialData(materialManager, lightManager);

	UpdateDescriptorSets(materialManager);
}

void VansGraphics::VansSkyBoxRenderNode::UpdateDescriptorSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
	{
		return;
	}
	m_DescriptorsetsDirty = false;
}

// VansShadowRenderNode removed – shadow pass now uses DrawWithPassShader() on opaque nodes

// ===========================================================================
// VansWaterRenderNode — 水面渲染节点（复用 Common GBuffer 管线绘制水面平面）
// ===========================================================================
#include "WaterCore/VansWaterMaterial.h"

void VansGraphics::VansWaterRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	// Set 0: Global（Camera + Lights + Materials + IBL + Bindless）
	m_UsedDescSetLayouts.push_back(m_Scene->GetGlobalDescriptorSetLayout());
	m_UsedDescSets.push_back(m_Scene->GetGlobalDescriptorSet());

	// Set 1: Per-Pass（empty，水面暂用空集占位）
	m_UsedDescSetLayouts.push_back(m_Scene->GetEmptyPassLayout());
	m_UsedDescSets.push_back(m_Scene->GetEmptyPassDescriptorSet());

	// Set 2: Per-Object（Transform SSBO）
	m_UsedDescSetLayouts.push_back(m_Scene->GetObjectDescriptorSetLayout());
	m_UsedDescSets.push_back(m_Scene->GetObjectDescriptorSet());

	// Set 3: Animation（水面为静态，绑定共享 dummy set）
	m_UsedDescSetLayouts.push_back(m_Scene->GetVertexDeformationDescriptorSetLayout());
	m_UsedDescSets.push_back(m_Scene->GetVertexDeformationDescriptorSet());
}

void VansGraphics::VansWaterRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	UpdateDescriptorSets(materialManager);
	UpdateModelData();
}

void VansGraphics::VansWaterRenderNode::UpdateDescriptorSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
		return;
	m_DescriptorsetsDirty = false;
}

void VansGraphics::VansWaterRenderNode::Draw(VansVKCommandBuffer& cmd, GlobalStateData& global_state)
{
	// 复用基类 Common Draw（使用 m_Material 中记录的 PBR/Water shader）
	VansRenderNode::Draw(cmd, global_state);
}

// ===========================================================================

VansGraphics::VansTerrainRenderNode::VansTerrainRenderNode(VansVKDevice* device, const TerrainConfig& config, RenderNodeType type) : VansRenderNode(device->GetLogicDevice(), TERRAIN_NODE)
{
	m_Terrain = new VansTerrain();
	m_Terrain->Init(device, config);
}

VansGraphics::VansTerrainRenderNode::~VansTerrainRenderNode()
{
	delete m_Terrain;
	m_Terrain = nullptr;
}

void VansGraphics::VansTerrainRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	// Set 0: Global (Camera + Lights + Materials + IBL + Bindless)
	m_UsedDescSetLayouts.push_back(m_Scene->GetGlobalDescriptorSetLayout());
	m_UsedDescSets.push_back(m_Scene->GetGlobalDescriptorSet());

	// Set 1: Per-Pass (terrain-specific: heightmap + albedo)
	m_UsedDescSetLayouts.push_back(m_Terrain->m_DescriptorSetLayout);
	m_UsedDescSets.push_back(m_Terrain->m_DescriptorSets[0]);
}

void VansGraphics::VansTerrainRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	UpdateDescriptorSets(materialManager);

	m_Terrain->Update(camera);
}

void VansGraphics::VansTerrainRenderNode::UpdateDescriptorSets(VansMaterialManager& materialManager)
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

void VansGraphics::VansTerrainRenderNode::DrawMotionVector(VansVKCommandBuffer& cmd, GlobalStateData& global_state)
{
	m_Terrain->DrawMotionVector(cmd, global_state, m_UsedDescSetLayouts, m_UsedDescSets);
}

// ═══════════════════════════════════════════════════════════════════════════════
// VansVegetationRenderNode — GPU-driven grass (indirect draw)
// ═══════════════════════════════════════════════════════════════════════════════
#include "VegetationCore/VansVegetationSystem.h"

void VansGraphics::VansVegetationRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	if (!m_VegetationSystem)
	{
		VANS_LOG_WARN("[VegetationRenderNode] Missing VegetationSystem — skipping descriptor set creation.");
		return;
	}

	// Set 0: Global (Camera + Lights + Materials + IBL + Bindless)
	m_UsedDescSetLayouts.push_back(m_Scene->GetGlobalDescriptorSetLayout());
	m_UsedDescSets.push_back(m_Scene->GetGlobalDescriptorSet());

	// Set 1: Per-Pass (empty — vegetation has no pass-specific uniforms)
	m_UsedDescSetLayouts.push_back(m_Scene->GetEmptyPassLayout());
	m_UsedDescSets.push_back(m_Scene->GetEmptyPassDescriptorSet());

	// Set 2: Per-Object — shared Transform SSBO
	m_UsedDescSetLayouts.push_back(m_Scene->GetObjectDescriptorSetLayout());
	m_UsedDescSets.push_back(m_Scene->GetObjectDescriptorSet());

	// Set 3+ (draw desc + grass textures) are bound per-config inside
	// VansVegetationSystem::Draw() and are NOT stored here.

	// Wire global camera descriptor set into the vegetation system for bone sim compute
	m_VegetationSystem->SetGlobalDescriptorSet(
		m_Scene->GetGlobalDescriptorSetLayout(),
		m_Scene->GetGlobalDescriptorSet());

	// Ensure grass texture descriptors are built for every material in configs
	for (auto& cfg : m_VegetationSystem->GetRenderConfigsGPU())
	{
		if (cfg.material && cfg.material->m_MaterialType == VansMaterialType::VAN_GRASS)
		{
			VansGrassMaterial* grass = static_cast<VansGrassMaterial*>(cfg.material);
			if (grass->m_GrassOwnedLayout == VK_NULL_HANDLE)
				grass->BuildGrassTextureDescriptors();
		}
	}

	m_DescriptorsetsSetDone = true;
}

void VansGraphics::VansVegetationRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
	// GPU-driven — no per-frame CPU data upload needed
	UpdateDescriptorSets(materialManager);
}

void VansGraphics::VansVegetationRenderNode::UpdateDescriptorSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
		return;
	m_DescriptorsetsDirty = false;
}

void VansGraphics::VansVegetationRenderNode::Draw(VansVKCommandBuffer& cmd, GlobalStateData& global_state)
{
	if (!m_VegetationSystem)
		return;

	const bool hasTrees = m_VegetationSystem->HasTrees();

	// Use the first config's material as the shader source (all configs share the
	// same vertex/fragment shader; they only differ in descriptor bindings).
	VansMaterial* drawMaterial = m_Material;
	if (!drawMaterial)
	{
		const auto& configs = m_VegetationSystem->GetRenderConfigsGPU();
		if (!configs.empty() && configs[0].material)
			drawMaterial = configs[0].material;
	}
	if (drawMaterial)
	{
		auto* gbufferShader = drawMaterial->GetPassShader(VansPass::GBUFFER);
		if (gbufferShader)
		{
			m_VegetationSystem->Draw(cmd, *gbufferShader, global_state,
				m_UsedDescSetLayouts, m_UsedDescSets,
				m_TransfromIndex);
		}
	}
	else if (!hasTrees)
	{
		return;
	}

	m_VegetationSystem->DrawTrees(cmd, global_state,
		m_UsedDescSetLayouts, m_UsedDescSets,
		m_TransfromIndex);
}

void VansGraphics::VansVegetationRenderNode::DrawShadow(VansVKCommandBuffer& cmd, GlobalStateData& global_state)
{
	if (!m_VegetationSystem)
		return;
	m_VegetationSystem->DrawTreeCascadeShadow(cmd, global_state,
		m_UsedDescSetLayouts, m_UsedDescSets,
		m_TransfromIndex);
}

void VansGraphics::VansVegetationRenderNode::DrawPunctualShadow(VansVKCommandBuffer& cmd, GlobalStateData& global_state, int shadowViewIndex)
{
	if (!m_VegetationSystem)
		return;
	m_VegetationSystem->DrawTreePunctualShadow(cmd, global_state,
		m_UsedDescSetLayouts, m_UsedDescSets,
		m_TransfromIndex, shadowViewIndex);
}

// ── VansDecalRenderNode ────────────────────────────────────────────────────
void VansGraphics::VansDecalRenderNode::CreateDescriptorSets(
	VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	// Set 0: Global（Camera / Lights / PBR SSBO / Bindless 纹理）
	m_UsedDescSetLayouts.push_back(m_Scene->GetGlobalDescriptorSetLayout());
	m_UsedDescSets.push_back(m_Scene->GetGlobalDescriptorSet());

	// Set 1: DecalPass（仅绑定 GBuffer2 用于世界坐标重建）
	VansDescriptorSetLayoutFactory::CreateAndAllocate_DecalPass(textureResourceLayout, textureResourceDescriptorSets);
	m_UsedDescSetLayouts.push_back(textureResourceLayout);
	m_UsedDescSets.push_back(textureResourceDescriptorSets[0]);

	// Set 2: Object（变换 SSBO）
	m_UsedDescSetLayouts.push_back(m_Scene->GetObjectDescriptorSetLayout());
	m_UsedDescSets.push_back(m_Scene->GetObjectDescriptorSet());
}

void VansGraphics::VansDecalRenderNode::UpdateRenderData(
	VansVKDevice* device, VansMaterialManager& materialManager,
	VansLightManager& lightManager, VansCamera* camera)
{
	UpdateDescriptorSets(materialManager);
}

void VansGraphics::VansDecalRenderNode::UpdateDescriptorSets(VansMaterialManager& materialManager)
{
	if (!m_DescriptorsetsDirty)
		return;
	m_DescriptorsetsDirty = false;

	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->BeginDescriptorUpdate();
	// binding 0: GBuffer2（世界坐标 / 深度重建）
	descMgr->WriteImageDescriptor(
		textureResourceDescriptorSets[0],
		DECAL_PASS_BINDING_GBUFFER2,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ {
			VansRenderPassManager::GetInstance()->GetGbuffer2().GetSampler(),
			VansRenderPassManager::GetInstance()->GetGbuffer2().GetImageView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		} });
	descMgr->CommitDescriptorUpdates();
}

