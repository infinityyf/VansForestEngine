#include "VansRenderNode.h"
#include "PcgCore/VansPcgSplineFieldResources.h"
#include "VansPostProcessProfile.h"
#include "VansDrawSubmission.h"
#include "VansCamera.h"
#include "VansScene.h"
#include "VulkanCore/VansMesh.h"
#include "VulkanCore/VansVKDevice.h"
#include "VulkanCore/VansVKDescriptorManager.h"
#include "VulkanCore/VansDescriptorSetLayouts.h"
#include "VulkanCore/VansRenderPass.h"
#include "../../EngineCore/RenderCore/TerrainCore/VansTerrain.h"
#include "../Util/VansLog.h"
#include "../Util/VansProfiler.h"
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
	VansVKDescriptorManager::GetInstance()->ReleaseDescriptorSetLayout(modelBufferLayout);
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(modelBufferDescriptorSets);

	VansVKDescriptorManager::GetInstance()->ReleaseDescriptorSetLayout(textureResourceLayout);
	VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(textureResourceDescriptorSets);

	VansVKDescriptorManager::GetInstance()->ReleaseDescriptorSetLayout(frameBufferInputLayout);
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
	m_ModelData.Postion = glm::vec4(transform.m_Position, static_cast<float>(m_DecalReceiverId));
	m_ModelData.Scale = glm::vec4(transform.m_Scale, m_DecalMinimumNormalDot);
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

void VansGraphics::VansRenderNode::PrepareModelDataForRenderFrame()
{
	// Save current ModelMatrix as previous before computing new one
	m_ModelData.PrevModelMatrix = m_ModelData.ModelMatrix;

	ComputeModelDataFromTransform();
	UpdateWorldBoundsFromModelData();
}

// Helper: map node type to its primary render-pass name.
static const char* GetPrimaryPassName(VansGraphics::RenderNodeType type, bool roadDecal = false)
{
	using namespace VansGraphics;
	switch (type)
	{
	case OPAQUE_NODE:       return VansPass::GBUFFER;
	case HAIR_NODE:         return VansPass::HAIR_VISIBILITY;
	case FORWARD_OPAQUE_PRE_ATMOSPHERE_NODE:
		return VansPass::FORWARD_OPAQUE_PRE_ATMOSPHERE;
	case TRANSPARENT_NODE:  return VansPass::FORWARD_TRANSPARENT;
	case POSTPROCESS_NODE:  return VansPass::POST_PROCESS;
	case DEFERRED_NODE:     return VansPass::DEFERRED;
	case SCREEN_SPACE_NODE: return VansPass::SCREEN_SPACE;
	case DECAL_NODE:        return roadDecal ? VansPass::ROAD_DECAL_MODIFIER : VansPass::DECAL_MODIFIER;
	default:                return VansPass::GBUFFER;
	}
}

static bool UsesDrawSubmission(VansGraphics::RenderNodeType type);

void VansGraphics::VansRenderNode::Draw(VansVKCommandBuffer& cmd, GlobalStateData& globalStateData)
{
	if (!CheckRenderNodeState())
		return;
	if (UsesDrawSubmission(m_NodeType))
	{
		VANS_LOG_ERROR("[VansRenderNode] Submission-managed node '" << m_NodeName
			<< "' cannot be recorded through VansRenderNode::Draw.");
		return;
	}

	VansGraphicsShader* shader = m_Material->GetPassShader(GetPrimaryPassName(m_NodeType, m_UsesRoadDecalPass));
	if (!shader) return;

	if (!ValidateDescriptorBindings(GetPrimaryPassName(m_NodeType, m_UsesRoadDecalPass), m_UsedDescSetLayouts, m_UsedDescSets))
		return;

	VansMesh* drawMesh = GetDrawMesh();
	if (drawMesh == nullptr)
		return;
	cmd.BindMesh(*drawMesh, 0, globalStateData);

	VansVKGraphicsPipeline* pipeline = cmd.EnsureGraphicsShader(*shader, globalStateData, m_UsedDescSetLayouts);
	if (pipeline == nullptr)
		return;

	cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, m_UsedDescSets, {});

	cmd.DrawMesh(*drawMesh, *pipeline, 1);
}

static bool UsesDrawSubmission(VansGraphics::RenderNodeType type)
{
	using namespace VansGraphics;
	switch (type)
	{
	case OPAQUE_NODE:
	case HAIR_NODE:
	case FORWARD_OPAQUE_PRE_ATMOSPHERE_NODE:
	case TRANSPARENT_NODE:
	case DECAL_NODE:
		return true;
	default:
		return false;
	}
}

bool VansGraphics::VansRenderNode::BuildPrimaryDrawPacket(
	VkDevice& device,
	GlobalStateData globalState,
	const char* passName,
	int passUser0,
	std::uint64_t orderGroup,
	std::uint64_t stableOrder,
	float cameraDepth,
	VansDrawPacket& packet)
{
	if (m_Material == nullptr || passName == nullptr)
		return false;
	return BuildPassDrawPacket(
		device,
		globalState,
		passName,
		m_Material->GetPassShader(passName),
		m_UsedDescSets,
		m_UsedDescSetLayouts,
		passUser0,
		orderGroup,
		stableOrder,
		cameraDepth,
		packet);
}

bool VansGraphics::VansRenderNode::BuildPassDrawPacket(
	VkDevice& device,
	GlobalStateData globalState,
	const char* passName,
	VansGraphicsShader* shader,
	const std::vector<VkDescriptorSet>& descriptorSets,
	const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
	int passUser0,
	std::uint64_t orderGroup,
	std::uint64_t stableOrder,
	float cameraDepth,
	VansDrawPacket& packet)
{
	if (shader == nullptr || !CheckRenderNodeState())
		return false;
	if (!ValidateDescriptorBindings(passName, descriptorSetLayouts, descriptorSets))
		return false;
	return VansDrawSubmission::BuildPacket(
		device,
		*this,
		*shader,
		descriptorSetLayouts,
		descriptorSets,
		globalState,
		passUser0,
		orderGroup,
		stableOrder,
		cameraDepth,
		packet);
}

bool VansGraphics::VansRenderNode::PreparePipelineForDraw(VkDevice& device, GlobalStateData global_state)
{
	if (GetDrawMesh() == nullptr || m_Material == nullptr)
		return true;

	VansGraphicsShader* shader = m_Material->GetPassShader(GetPrimaryPassName(m_NodeType, m_UsesRoadDecalPass));
	return PreparePipelineForShader(device, global_state, shader, m_UsedDescSetLayouts, m_UsedDescSets);
}

bool VansGraphics::VansRenderNode::PreparePipelineForShader(
	VkDevice& device,
	GlobalStateData global_state,
	VansGraphicsShader* shader,
	const std::vector<VkDescriptorSetLayout>& layouts,
	const std::vector<VkDescriptorSet>& sets)
{
	VansMesh* drawMesh = GetDrawMesh();
	if (drawMesh == nullptr || shader == nullptr)
		return true;

	if (layouts.size() != sets.size())
		return true;
	for (size_t i = 0; i < layouts.size(); ++i)
	{
		if (layouts[i] == VK_NULL_HANDLE || sets[i] == VK_NULL_HANDLE)
			return true;
	}

	global_state.vertexInputAttributeDescriptions = &drawMesh->m_VertexInputAttributeDescriptions;
	global_state.vertexInputBindingDescriptions = &drawMesh->m_VertexInputBindingDescriptions;
	return shader->GetGraphicsPipeline(device, global_state, layouts) != nullptr;
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

void VansGraphics::VansCommonRenderNode::UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera)
{
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
	VansDescriptorSetLayoutFactory::CreateAndAllocate_DeferredLighting(frameBufferInputLayout, frameBufferInputDescriptorSets, 2);

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
	VansTexture* screenSpaceShadow = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_SCREEN_SPACE_SHADOW_RESULT);
	VansTexture* screenSpaceShadowHZB = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_RESULT);
	VansTexture* rectLightEmissive = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_RECT_LIGHT_EMISSIVE);
	VansTexture* ambientSkyCacheX = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_AMBIENT_SKY_CACHE_X);
	VansTexture* ambientSkyCacheY = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_AMBIENT_SKY_CACHE_Y);
	VansTexture* ambientSkyCacheZ = materialManager.GetRuntimeRenderTexture(VansMaterialManager::RT_AMBIENT_SKY_CACHE_Z);
	VansVKDevice* runtimeDevice = m_Scene->GetRuntimeResourceDevice();

	if (ssaoFilterResult == nullptr || ssgiFilterResult == nullptr || ssrAaResult == nullptr ||
		screenSpaceShadow == nullptr || screenSpaceShadowHZB == nullptr || rectLightEmissive == nullptr ||
		ambientSkyCacheX == nullptr || ambientSkyCacheY == nullptr || ambientSkyCacheZ == nullptr ||
		runtimeDevice == nullptr ||
		materialManager.m_SSGICBBuffer.GetNativeBuffer() == VK_NULL_HANDLE ||
		materialManager.m_AmbientSkyCacheInfoCBBuffer.GetNativeBuffer() == VK_NULL_HANDLE ||
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
    // 两套不可变 descriptor：无贴花的帧选择全零纹理，避免上一帧残留或逐帧改写绑定。
    for (size_t setIndex = 0; setIndex < frameBufferInputDescriptorSets.size(); ++setIndex)
    {

	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { rp->GetNormal().GetSampler(), rp->GetNormal().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex], 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { rp->GetGbuffer0().GetSampler(), rp->GetGbuffer0().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex], 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { rp->GetGbuffer1().GetSampler(), rp->GetGbuffer1().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex], 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { rp->GetGbuffer2().GetSampler(), rp->GetGbuffer2().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex], 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { rp->GetDepth().GetSampler(), rp->GetDepth().GetImageView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex], 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		{ { ssaoFilterResult->GetImage().GetSampler(), ssaoFilterResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex], 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { ssgiFilterResult->GetImage().GetSampler(), ssgiFilterResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex], 7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		{ { ssrAaResult->GetImage().GetSampler(), ssrAaResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex], 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { rp->GetCascadeShadowSampler(), rp->GetCascadeShadowArrayView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex], 9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		rp->GetPunctualShadowDescriptorInfos());
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex], DEFERRED_BINDING_SCREEN_SPACE_SHADOW, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { screenSpaceShadow->GetImage().GetSampler(), screenSpaceShadow->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex], DEFERRED_BINDING_RECT_LIGHT_EMISSIVE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { rectLightEmissive->GetImage().GetSampler(), rectLightEmissive->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex], DEFERRED_BINDING_IES_PROFILES, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { m_Scene->GetIESProfileManager()->GetIESProfileTexture().GetSampler(), m_Scene->GetIESProfileManager()->GetIESProfileArrayView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex], DEFERRED_BINDING_SCREEN_SPACE_SHADOW_HIZ, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { screenSpaceShadowHZB->GetImage().GetSampler(), screenSpaceShadowHZB->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex], DEFERRED_BINDING_AMBIENT_SKY_CACHE_X, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { ambientSkyCacheX->GetImage().GetSampler(), ambientSkyCacheX->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex], DEFERRED_BINDING_AMBIENT_SKY_CACHE_Y, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { ambientSkyCacheY->GetImage().GetSampler(), ambientSkyCacheY->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex], DEFERRED_BINDING_AMBIENT_SKY_CACHE_Z, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		{ { ambientSkyCacheZ->GetImage().GetSampler(), ambientSkyCacheZ->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	descMgr->WriteBufferDescriptor(frameBufferInputDescriptorSets[setIndex], DEFERRED_BINDING_AMBIENT_SKY_CACHE_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		{ { materialManager.m_AmbientSkyCacheInfoCBBuffer.GetNativeBuffer(), 0, materialManager.m_AmbientSkyCacheInfoCBBuffer.GetBufferSize() } });
	descMgr->WriteBufferDescriptor(frameBufferInputDescriptorSets[setIndex], DEFERRED_BINDING_SCREEN_SPACE_SHADOW_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		{ { materialManager.m_ScreenSpaceShadowParamsCBBuffer.GetNativeBuffer(), 0, materialManager.m_ScreenSpaceShadowParamsCBBuffer.GetBufferSize() } });
	descMgr->WriteBufferDescriptor(frameBufferInputDescriptorSets[setIndex], DEFERRED_BINDING_GI_INFO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		{ { materialManager.m_SSGICBBuffer.GetNativeBuffer(), 0, materialManager.m_SSGICBBuffer.GetBufferSize() } });
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex], DEFERRED_BINDING_GI_VISIBILITY, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		giVisibilityInfos);
	descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex], DEFERRED_BINDING_GI_IRRADIANCE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		giIrradianceInfos);
    const auto& receiverVisibility = materialManager.m_GIReceiverVisibility.current;
    descMgr->WriteBufferDescriptor(frameBufferInputDescriptorSets[setIndex], 34u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{receiverVisibility.GetNativeBuffer(), 0, receiverVisibility.GetBufferSize()}});
    const auto& layoutBuffer = rayTracing.GetGIProbeLayoutBuffer();
    descMgr->WriteBufferDescriptor(frameBufferInputDescriptorSets[setIndex], 33u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{layoutBuffer.GetNativeBuffer(), 0, layoutBuffer.GetBufferSize()}});
	for (size_t regionSlot = 0; regionSlot < giProbeStateInfos.size(); ++regionSlot)
	{
		descMgr->WriteBufferDescriptor(
			frameBufferInputDescriptorSets[setIndex],
			DEFERRED_BINDING_GI_PROBE_STATE + static_cast<uint32_t>(regionSlot),
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{ giProbeStateInfos[regionSlot] });
	}
        VansVKImage* modifiers[] = { &rp->GetDecalColor(), &rp->GetDecalNormal(), &rp->GetDecalRoughness() };
        for (uint32_t channel = 0; channel < 3; ++channel)
        {
            auto& image = setIndex == 0 ? *modifiers[channel] : rp->GetEmptyDecal();
            descMgr->WriteImageDescriptor(frameBufferInputDescriptorSets[setIndex],
                DEFERRED_BINDING_DECAL_COLOR + channel, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {{image.GetSampler(), image.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
        }
    }
	descMgr->CommitDescriptorUpdates();
	m_DescriptorsetsDirty = false;
}

void VansGraphics::VansDeferredRenderNode::Draw(VansVKCommandBuffer& cmd, GlobalStateData& globalState)
{
    const auto* device = m_Scene->GetRuntimeResourceDevice();
    const bool hasDecal = device && device->GetCurrentRenderSceneSnapshot().features.hasDecal;
    m_UsedDescSets[1] = frameBufferInputDescriptorSets[hasDecal ? 0 : 1];
    VansRenderNode::Draw(cmd, globalState);
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
		{ { VansRenderPassManager::GetInstance()->GetDepth().GetSampler(), VansRenderPassManager::GetInstance()->GetDepth().GetImageView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL } });
	descMgr->WriteImageDescriptor(
		textureResourceDescriptorSets[0], 5, // SSAO output
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		{ { ssaoResult->GetImage().GetSampler(), ssaoResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
	descMgr->CommitDescriptorUpdates();
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
	auto layouts=m_UsedDescSetLayouts;auto sets=m_UsedDescSets;
	if (const auto* fields=m_Scene->GetSplineFieldResources())
	{layouts.push_back(fields->Layout());sets.push_back(fields->DescriptorSet());}
	m_Terrain->Draw(cmd, global_state, layouts, sets);
}

void VansGraphics::VansTerrainRenderNode::DrawShadow(VansVKCommandBuffer& cmd, GlobalStateData& global_state)
{
	auto layouts=m_UsedDescSetLayouts;auto sets=m_UsedDescSets;
	if (const auto* fields=m_Scene->GetSplineFieldResources())
	{layouts.push_back(fields->Layout());sets.push_back(fields->DescriptorSet());}
	m_Terrain->DrawShadow(cmd, global_state, layouts, sets);
}

// ═══════════════════════════════════════════════════════════════════════════════
// VansVegetationRenderNode — GPU-driven grass (indirect draw)
// ═══════════════════════════════════════════════════════════════════════════════
#include "VegetationCore/VansVegetationSystem.h"
#include "VegetationCore/VansVegetationCollection.h"

void VansGraphics::VansVegetationRenderNode::CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	if (!m_VegetationCollection)
	{
		VANS_LOG_WARN("[VegetationRenderNode] Missing vegetation collection.");
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

	m_VegetationCollection->ForEach([&](VansVegetationSystem& system) {
	system.SetGlobalDescriptorSet(
		m_Scene->GetGlobalDescriptorSetLayout(),
		m_Scene->GetGlobalDescriptorSet());

	// Ensure grass texture descriptors are built for every material in configs
	for (auto& cfg : system.GetRenderConfigsGPU())
	{
		if (cfg.material && cfg.material->m_MaterialType == VansMaterialType::VAN_GRASS)
		{
			VansGrassMaterial* grass = static_cast<VansGrassMaterial*>(cfg.material);
			if (grass->m_GrassOwnedLayout == VK_NULL_HANDLE)
				grass->BuildGrassTextureDescriptors();
		}
	}

	});
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
    if (!m_VegetationCollection) return;
    {
    VANS_GPU_SCOPE(cmd.GetVKCommandBuffer(), "Vegetation Grass GBuffer");
    m_VegetationCollection->ForEach([&](VansVegetationSystem& system) {
        system.Draw(cmd,global_state,m_UsedDescSetLayouts,m_UsedDescSets,m_TransfromIndex);
    });
    }
    {
    VANS_GPU_SCOPE(cmd.GetVKCommandBuffer(), "Vegetation Tree GBuffer");
    m_VegetationCollection->ForEach([&](VansVegetationSystem& system) {
        system.DrawTrees(cmd,global_state,m_UsedDescSetLayouts,m_UsedDescSets,m_TransfromIndex);
    });
    }
}
void VansGraphics::VansVegetationRenderNode::DrawShadow(VansVKCommandBuffer& cmd, GlobalStateData& global_state)
{
    if (!m_VegetationCollection) return;
    // 植被仅参与最近两级平行光阴影；远级联不遍历或提交任何植被批次。
    if (global_state.cascadeIndex < 0 || global_state.cascadeIndex >= 2) return;
    {
    VANS_GPU_SCOPE(cmd.GetVKCommandBuffer(), "Vegetation Grass Cascade Shadow");
    m_VegetationCollection->ForEach([&](VansVegetationSystem& system) {
        system.DrawGrassCascadeShadow(cmd,global_state,m_UsedDescSetLayouts,m_UsedDescSets,global_state.cascadeIndex);
    });
    }
    {
    VANS_GPU_SCOPE(cmd.GetVKCommandBuffer(), "Vegetation Tree Cascade Shadow");
    m_VegetationCollection->ForEach([&](VansVegetationSystem& system) {
        system.DrawTreeCascadeShadow(cmd,global_state,m_UsedDescSetLayouts,m_UsedDescSets,m_TransfromIndex);
    });
    }
}
// ── VansDecalRenderNode ────────────────────────────────────────────────────
void VansGraphics::VansDecalRenderNode::CreateDescriptorSets(
	VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager)
{
	// Set 0: Global（Camera / Lights / 材质 payload / Bindless 纹理）
	m_UsedDescSetLayouts.push_back(m_Scene->GetGlobalDescriptorSetLayout());
	m_UsedDescSets.push_back(m_Scene->GetGlobalDescriptorSet());

	// Set 1: DecalPass（GBuffer2 重建世界坐标，GBuffer1 判定接收材质）
	m_UsedDescSetLayouts.push_back(m_Scene->GetDecalPassLayout());
	m_UsedDescSets.push_back(m_Scene->GetDecalPassDescriptorSet());

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
	m_DescriptorsetsDirty = false;
	m_Scene->UpdateDecalPassDescriptorSet();
}
