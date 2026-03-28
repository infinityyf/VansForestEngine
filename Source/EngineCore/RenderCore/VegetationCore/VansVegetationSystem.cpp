#include "VansVegetationSystem.h"
#include "../../Util/VansLog.h"
#include "../../Configration/VansConfigration.h"
#include <random>
#include <cmath>

using namespace VansGraphics;

// ============================================================================
// Destructor
// ============================================================================
VansVegetationSystem::~VansVegetationSystem()
{
	// Buffers are cleaned up via Cleanup() called explicitly with a VkDevice
}

// ============================================================================
// Init — creates all GPU resources for the vegetation system
// ============================================================================
void VansVegetationSystem::Init(VkDevice device, uint32_t instanceCount, uint32_t boneCountPerInstance)
{
	m_Device = device;
	m_InstanceCount = instanceCount;
	m_BoneCountPerInstance = boneCountPerInstance;

	CreateTemplateMesh(device);
	CreateInstanceBuffer(device);
	CreateBoneBuffer(device);
	CreateBoneMatrixBuffer(device);
	CreateSkinnedBuffers(device);
	CreateIndirectDrawBuffer(device);
	LoadComputeShaders(device);
	CreateDescriptorSets();

	VANS_LOG("VansVegetationSystem initialized: " << m_InstanceCount << " instances, "
		<< m_BoneCountPerInstance << " bones/instance, " << m_VertexCount << " verts/blade");
}

// ============================================================================
// CreateTemplateMesh — grass blade quad-strip
//
// With default 6 bones (5 segments):
// 11 vertices = 5 left/right pairs + 1 tip
//
//        Tip (v10)
//         /\
//        /  \
//      v8    v9      ← segment 4
//      |      |
//      v6    v7      ← segment 3
//      |      |
//      v4    v5      ← segment 2
//      |      |
//      v2    v3      ← segment 1
//      |      |
//      v0────v1      ← segment 0 (root)
// ============================================================================
void VansVegetationSystem::CreateTemplateMesh(VkDevice device)
{
	const uint32_t segments = m_BoneCountPerInstance - 1; // 5 segments
	const float h = m_BladeHeight;
	const float w = m_BladeWidthRoot;

	// 11 vertices (with 6 bones): 5 left/right pairs + 1 tip
	std::vector<GrassVertex> vertices;
	vertices.reserve(segments * 2 + 1);

	// Generate left/right pairs from root to one below tip
	for (uint32_t i = 0; i < segments; ++i)
	{
		float t = static_cast<float>(i) / static_cast<float>(segments);
		float y = t * h;
		float halfW = w * (1.0f - t) * 0.5f; // taper

		GrassVertex left = {};
		left.position = glm::vec4(-halfW, y, 0.0f, 0.0f);
		left.normal   = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
		left.uv       = glm::vec2(0.0f, t);
		vertices.push_back(left);

		GrassVertex right = {};
		right.position = glm::vec4(halfW, y, 0.0f, 0.0f);
		right.normal   = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
		right.uv       = glm::vec2(1.0f, t);
		vertices.push_back(right);
	}

	// Tip vertex
	GrassVertex tip = {};
	tip.position = glm::vec4(0.0f, h, 0.0f, 0.0f);
	tip.normal   = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
	tip.uv       = glm::vec2(0.5f, 1.0f);
	vertices.push_back(tip);

	m_VertexCount = static_cast<uint32_t>(vertices.size());

	// Indices: quads (2 triangles each) + tip triangles (2)
	std::vector<uint32_t> indices;
	for (uint32_t i = 0; i < segments - 1; ++i)
	{
		uint32_t bl = i * 2;       // bottom-left
		uint32_t br = i * 2 + 1;   // bottom-right
		uint32_t tl = (i + 1) * 2; // top-left
		uint32_t tr = (i + 1) * 2 + 1; // top-right

		// Quad as 2 triangles
		indices.push_back(bl); indices.push_back(br); indices.push_back(tr);
		indices.push_back(bl); indices.push_back(tr); indices.push_back(tl);
	}

	// Tip triangles (connect last quad pair to tip vertex — 2 triangles for front/back)
	uint32_t lastLeft  = (segments - 1) * 2;
	uint32_t lastRight = (segments - 1) * 2 + 1;
	uint32_t tipIdx    = m_VertexCount - 1;
	// Front face
	indices.push_back(lastLeft);  indices.push_back(lastRight); indices.push_back(tipIdx);
	// Back face
	indices.push_back(lastRight); indices.push_back(lastLeft);  indices.push_back(tipIdx);

	m_IndexCount = static_cast<uint32_t>(indices.size());

	// Create SSBO for compute to read template vertices
	VkDeviceSize vertBufferSize = sizeof(GrassVertex) * m_VertexCount;
	m_TemplateMeshBuffer.CreatVulkanBuffer(device, vertBufferSize, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_TemplateMeshBuffer.SetBufferData(vertices.data(), 0, static_cast<int>(vertBufferSize));

	// Create vertex buffer for the draw pass
	m_TemplateVertexBuffer.CreatVulkanBuffer(device,
		sizeof(GrassVertex) * m_VertexCount, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_TemplateVertexBuffer.SetBufferData(vertices.data(), 0, static_cast<int>(sizeof(GrassVertex) * m_VertexCount));

	// Create index buffer
	VkDeviceSize idxBufferSize = sizeof(uint32_t) * m_IndexCount;
	m_TemplateIndexBuffer.CreatVulkanBuffer(device, idxBufferSize, VK_FORMAT_R32_UINT,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_TemplateIndexBuffer.SetBufferData(indices.data(), 0, static_cast<int>(idxBufferSize));
}

// ============================================================================
// CreateInstanceBuffer — random grass positions in [-10, 10] XZ
// ============================================================================
void VansVegetationSystem::CreateInstanceBuffer(VkDevice device)
{
	std::vector<GrassInstance> instances(m_InstanceCount);
	std::mt19937 rng(42);
	std::uniform_real_distribution<float> posDist(-10.0f, 10.0f);
	std::uniform_real_distribution<float> scaleDist(0.8f, 1.2f);
	std::uniform_real_distribution<float> rotDist(0.0f, 6.28318530718f);

	for (uint32_t i = 0; i < m_InstanceCount; ++i)
	{
		instances[i].position = glm::vec3(posDist(rng), 0.0f, posDist(rng));
		instances[i].scale = scaleDist(rng);
		instances[i].rotation = rotDist(rng);
		instances[i].padding[0] = 0;
		instances[i].padding[1] = 0;
		instances[i].padding[2] = 0;
	}

	VkDeviceSize bufferSize = sizeof(GrassInstance) * m_InstanceCount;
	m_InstanceBuffer.CreatVulkanBuffer(device, bufferSize, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_InstanceBuffer.SetBufferData(instances.data(), 0, static_cast<int>(bufferSize));
}

// ============================================================================
// CreateBoneBuffer — initialise rest-pose bones for all instances
// ============================================================================
void VansVegetationSystem::CreateBoneBuffer(VkDevice device)
{
	uint32_t totalBones = m_InstanceCount * m_BoneCountPerInstance;
	std::vector<GrassBone> bones(totalBones);

	float segLength = m_BladeHeight / static_cast<float>(m_BoneCountPerInstance - 1);

	// Read back instance data for root positions
	// (we just generated it in-line, so regenerate with same seed)
	std::mt19937 rng(42);
	std::uniform_real_distribution<float> posDist(-10.0f, 10.0f);
	std::uniform_real_distribution<float> scaleDist(0.8f, 1.2f);
	std::uniform_real_distribution<float> rotDist(0.0f, 6.28318530718f);

	for (uint32_t i = 0; i < m_InstanceCount; ++i)
	{
		glm::vec3 pos(posDist(rng), 0.0f, posDist(rng));
		float scale = scaleDist(rng);
		float rot = rotDist(rng);
		(void)scale; (void)rot;

		for (uint32_t j = 0; j < m_BoneCountPerInstance; ++j)
		{
			uint32_t idx = i * m_BoneCountPerInstance + j;
			float y = static_cast<float>(j) * segLength;
			bones[idx].position  = glm::vec4(pos.x, pos.y + y, pos.z, 1.0f);
			bones[idx].velocity  = glm::vec4(pos.x, pos.y + y, pos.z, 0.0f); // prevPosition = position
			bones[idx].restOffset = glm::vec4(0.0f, segLength, 0.0f, 0.0f);
		}
	}

	VkDeviceSize bufferSize = sizeof(GrassBone) * totalBones;
	m_BoneBuffer.CreatVulkanBuffer(device, bufferSize, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_BoneBuffer.SetBufferData(bones.data(), 0, static_cast<int>(bufferSize));
}

// ============================================================================
// CreateBoneMatrixBuffer — uninitialized, written by compute
// ============================================================================
void VansVegetationSystem::CreateBoneMatrixBuffer(VkDevice device)
{
	uint32_t totalMatrices = m_InstanceCount * m_BoneCountPerInstance;
	VkDeviceSize bufferSize = sizeof(glm::mat4) * totalMatrices;
	m_BoneMatrixBuffer.CreatVulkanBuffer(device, bufferSize, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

// ============================================================================
// CreateSkinnedBuffers — skinned position + normal output
// ============================================================================
void VansVegetationSystem::CreateSkinnedBuffers(VkDevice device)
{
	uint32_t totalVerts = m_InstanceCount * m_VertexCount;
	VkDeviceSize bufferSize = sizeof(glm::vec4) * totalVerts;

	m_SkinnedVertexBuffer.CreatVulkanBuffer(device, bufferSize, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	m_SkinnedNormalBuffer.CreatVulkanBuffer(device, bufferSize, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

// ============================================================================
// CreateIndirectDrawBuffer
// ============================================================================
void VansVegetationSystem::CreateIndirectDrawBuffer(VkDevice device)
{
	VkDrawIndexedIndirectCommand indirectArgs = {};
	indirectArgs.indexCount    = m_IndexCount;
	indirectArgs.instanceCount = m_InstanceCount;
	indirectArgs.firstIndex    = 0;
	indirectArgs.vertexOffset  = 0;
	indirectArgs.firstInstance = 0;

	VkDeviceSize bufferSize = sizeof(VkDrawIndexedIndirectCommand);
	m_IndirectDrawBuffer.CreatVulkanBuffer(device, bufferSize, VK_FORMAT_R32_UINT,
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_IndirectDrawBuffer.SetBufferData(&indirectArgs, 0, static_cast<int>(bufferSize));
}

// ============================================================================
// LoadComputeShaders
// ============================================================================
void VansVegetationSystem::LoadComputeShaders(VkDevice device)
{
	std::string projectRoot = VansConfigration::GetInstance()->GetProjectRootPath();

	m_BoneSimShader = new VansComputeShader();
	m_BoneSimShader->InitShader(device, (projectRoot + "EngineAssets/Shaders/GrassBoneSim").c_str());
	m_BoneSimShader->SetPushConstant(sizeof(GrassSimPushConstants));

	m_SkinningShader = new VansComputeShader();
	m_SkinningShader->InitShader(device, (projectRoot + "EngineAssets/Shaders/GrassSkinning").c_str());
	m_SkinningShader->SetPushConstant(sizeof(GrassSkinningPushConstants));
}

// ============================================================================
// CreateDescriptorSets
// ============================================================================
void VansVegetationSystem::CreateDescriptorSets()
{
	VansDescriptorSetLayoutFactory::CreateAndAllocate_VegetationBoneSim(m_BoneSimLayout, m_BoneSimDescSets);
	VansDescriptorSetLayoutFactory::CreateAndAllocate_VegetationSkinning(m_SkinningLayout, m_SkinningDescSets);
	VansDescriptorSetLayoutFactory::CreateAndAllocate_VegetationDraw(m_VegDrawLayout, m_VegDrawDescSets);

	WriteBoneSimDescriptors();
	WriteSkinningDescriptors();
	WriteDrawDescriptors();
}

void VansVegetationSystem::WriteBoneSimDescriptors()
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->ResetState();

	descMgr->m_BufferDescInfos.push_back({
		m_BoneSimDescSets[0], VEG_SIM_BINDING_INSTANCE_DATA, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_InstanceBuffer.GetNativeBuffer(), 0, m_InstanceBuffer.GetBufferSize() }}
	});
	descMgr->m_BufferDescInfos.push_back({
		m_BoneSimDescSets[0], VEG_SIM_BINDING_BONE_DATA, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_BoneBuffer.GetNativeBuffer(), 0, m_BoneBuffer.GetBufferSize() }}
	});
	descMgr->m_BufferDescInfos.push_back({
		m_BoneSimDescSets[0], VEG_SIM_BINDING_BONE_MATRICES, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_BoneMatrixBuffer.GetNativeBuffer(), 0, m_BoneMatrixBuffer.GetBufferSize() }}
	});

	descMgr->UpdateDescriptorSets();
}

void VansVegetationSystem::WriteSkinningDescriptors()
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->ResetState();

	descMgr->m_BufferDescInfos.push_back({
		m_SkinningDescSets[0], VEG_SKIN_BINDING_TEMPLATE_VERTS, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_TemplateMeshBuffer.GetNativeBuffer(), 0, m_TemplateMeshBuffer.GetBufferSize() }}
	});
	descMgr->m_BufferDescInfos.push_back({
		m_SkinningDescSets[0], VEG_SKIN_BINDING_BONE_MATRICES, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_BoneMatrixBuffer.GetNativeBuffer(), 0, m_BoneMatrixBuffer.GetBufferSize() }}
	});
	descMgr->m_BufferDescInfos.push_back({
		m_SkinningDescSets[0], VEG_SKIN_BINDING_SKINNED_POS, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_SkinnedVertexBuffer.GetNativeBuffer(), 0, m_SkinnedVertexBuffer.GetBufferSize() }}
	});
	descMgr->m_BufferDescInfos.push_back({
		m_SkinningDescSets[0], VEG_SKIN_BINDING_SKINNED_NORM, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_SkinnedNormalBuffer.GetNativeBuffer(), 0, m_SkinnedNormalBuffer.GetBufferSize() }}
	});
	descMgr->m_BufferDescInfos.push_back({
		m_SkinningDescSets[0], VEG_SKIN_BINDING_INSTANCE_DATA, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_InstanceBuffer.GetNativeBuffer(), 0, m_InstanceBuffer.GetBufferSize() }}
	});

	descMgr->UpdateDescriptorSets();
}

void VansVegetationSystem::WriteDrawDescriptors()
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->ResetState();

	descMgr->m_BufferDescInfos.push_back({
		m_VegDrawDescSets[0], VEG_DRAW_BINDING_SKINNED_POS, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_SkinnedVertexBuffer.GetNativeBuffer(), 0, m_SkinnedVertexBuffer.GetBufferSize() }}
	});
	descMgr->m_BufferDescInfos.push_back({
		m_VegDrawDescSets[0], VEG_DRAW_BINDING_SKINNED_NORM, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_SkinnedNormalBuffer.GetNativeBuffer(), 0, m_SkinnedNormalBuffer.GetBufferSize() }}
	});
	descMgr->m_BufferDescInfos.push_back({
		m_VegDrawDescSets[0], VEG_DRAW_BINDING_INSTANCE_DATA, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_InstanceBuffer.GetNativeBuffer(), 0, m_InstanceBuffer.GetBufferSize() }}
	});
	descMgr->m_BufferDescInfos.push_back({
		m_VegDrawDescSets[0], VEG_DRAW_BINDING_TEMPLATE_MESH, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_TemplateMeshBuffer.GetNativeBuffer(), 0, m_TemplateMeshBuffer.GetBufferSize() }}
	});

	descMgr->UpdateDescriptorSets();
}

// ============================================================================
// Update — dispatches bone sim + skinning compute passes
// ============================================================================
void VansVegetationSystem::Update(VansVKCommandBuffer& computeCmd, float deltaTime, float time,
                                   const glm::vec2& windDirection, float windStrength,
                                   float windFrequency, float stiffness, float damping,
                                   float softness)
{
	if (!m_BoneSimShader || !m_SkinningShader ||
	    m_BoneSimDescSets.empty() || m_SkinningDescSets.empty())
	{
		VANS_LOG_WARN("[VegetationSystem] Update skipped — shaders or descriptor sets not ready.");
		return;
	}

	// ── Pass 1: Bone Simulation ─────────────────────────────────────
	GrassSimPushConstants simPC = {};
	simPC.deltaTime     = deltaTime;
	simPC.time          = time;
	simPC.windStrength  = windStrength;
	simPC.windFrequency = windFrequency;
	simPC.windDirX      = windDirection.x;
	simPC.windDirY      = windDirection.y;
	simPC.stiffness     = stiffness;
	simPC.damping       = damping;
	simPC.softness      = softness;

	m_BoneSimShader->SetPushConstantData(&simPC);

	// Ensure compute pipeline is created before dispatching (first frame)
	computeCmd.EnsureComputeShader(*m_BoneSimShader, { m_BoneSimLayout });

	uint32_t simGroupsX = (m_InstanceCount + 63) / 64;
	computeCmd.DispatchCompute(*m_BoneSimShader, simGroupsX, 1, 1, { m_BoneSimDescSets[0] });

	// ── Barrier: bone sim write → skinning read ─────────────────────
	VkMemoryBarrier simToSkinBarrier = {};
	simToSkinBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	simToSkinBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	simToSkinBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	computeCmd.PipelineBarrier(
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		{ simToSkinBarrier });

	// ── Pass 2: Vertex Skinning ─────────────────────────────────────
	GrassSkinningPushConstants skinPC = {};
	skinPC.instanceCount = m_InstanceCount;
	skinPC.vertexCount   = m_VertexCount;
	skinPC.boneCount     = m_BoneCountPerInstance;
	skinPC.grassHeight   = m_BladeHeight;

	m_SkinningShader->SetPushConstantData(&skinPC);

	// Ensure compute pipeline is created before dispatching (first frame)
	computeCmd.EnsureComputeShader(*m_SkinningShader, { m_SkinningLayout });

	uint32_t totalVerts   = m_InstanceCount * m_VertexCount;
	uint32_t skinGroupsX  = (totalVerts + 63) / 64;
	computeCmd.DispatchCompute(*m_SkinningShader, skinGroupsX, 1, 1, { m_SkinningDescSets[0] });

	// ── Barrier: skinning write → vertex shader read ────────────────
	VkMemoryBarrier skinToDrawBarrier = {};
	skinToDrawBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	skinToDrawBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	skinToDrawBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	computeCmd.PipelineBarrier(
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
		{ skinToDrawBarrier });
}

// ============================================================================
// Draw — issues indirect draw call
// ============================================================================
void VansVegetationSystem::Draw(VansVKCommandBuffer& graphicsCmd, VansGraphicsShader& shader,
                                 GlobalStateData& globalState,
                                 const std::vector<VkDescriptorSetLayout>& descSetLayouts,
                                 const std::vector<VkDescriptorSet>& descSets,
                                 int pushConstantMaterialIndex, int pushConstantTransformIndex)
{
	// Override vertex input to empty — vegetation fetches all data from SSBOs,
	// so the pipeline must be created with no vertex bindings / attributes.
	auto* savedBindings   = globalState.vertexInputBindingDescriptions;
	auto* savedAttributes = globalState.vertexInputAttributeDescriptions;
	globalState.vertexInputBindingDescriptions   = nullptr;
	globalState.vertexInputAttributeDescriptions = nullptr;

	graphicsCmd.EnsureGraphicsShader(shader, globalState, descSetLayouts);

	// Restore previous state so later draw calls are not affected
	globalState.vertexInputBindingDescriptions   = savedBindings;
	globalState.vertexInputAttributeDescriptions = savedAttributes;

	graphicsCmd.BindGraphicsPipeline(*shader.GetGraphicsPipeline());

	graphicsCmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, shader, 0, descSets, {});

	// Push constants: materialIndex, transformIndex, animationEnabled (always 1 for vegetation)
	if (shader.GetPushConstantSize() > 0)
	{
		int pushData[3] = { pushConstantMaterialIndex, pushConstantTransformIndex, 1 };
		graphicsCmd.UpdatePushConstants(*shader.GetGraphicsPipeline(),
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, shader.GetPushConstantSize(), pushData);
	}

	// Bind index buffer (no vertex buffer needed — all vertex data from SSBOs)
	graphicsCmd.BindIndexBuffer(m_TemplateIndexBuffer.GetNativeBuffer(), 0, VK_INDEX_TYPE_UINT32);

	// Issue indirect draw
	graphicsCmd.DrawIndexedIndirect(
		m_IndirectDrawBuffer.GetNativeBuffer(), 0, 1,
		sizeof(VkDrawIndexedIndirectCommand));
}

// ============================================================================
// Cleanup
// ============================================================================
void VansVegetationSystem::Cleanup(VkDevice device)
{
	m_InstanceBuffer.DestroyVulkanBuffer(device);
	m_BoneBuffer.DestroyVulkanBuffer(device);
	m_BoneMatrixBuffer.DestroyVulkanBuffer(device);
	m_SkinnedVertexBuffer.DestroyVulkanBuffer(device);
	m_SkinnedNormalBuffer.DestroyVulkanBuffer(device);
	m_TemplateMeshBuffer.DestroyVulkanBuffer(device);
	m_TemplateVertexBuffer.DestroyVulkanBuffer(device);
	m_TemplateIndexBuffer.DestroyVulkanBuffer(device);
	m_IndirectDrawBuffer.DestroyVulkanBuffer(device);

	if (m_BoneSimShader)
	{
		delete m_BoneSimShader;
		m_BoneSimShader = nullptr;
	}
	if (m_SkinningShader)
	{
		delete m_SkinningShader;
		m_SkinningShader = nullptr;
	}
}
