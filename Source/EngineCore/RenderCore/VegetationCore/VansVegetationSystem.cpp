#include "VansVegetationSystem.h"
#include "../VulkanCore/VansMesh.h"
#include "../VansMaterial.h"
#include "../VansShaderManager.h"
#include "../../Util/VansLog.h"
#include "../../Configration/VansConfigration.h"
#include <glm/gtc/matrix_transform.hpp>
#include <random>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <stdexcept>
#include <limits>

using namespace VansGraphics;

namespace
{
	bool SupportsTreeMaterial(const VansMaterial* material)
	{
		if (!material)
			return false;

		switch (material->m_MaterialType)
		{
		case VansMaterialType::VAN_PBR:
		case VansMaterialType::VAN_EMISSIVE:
		case VansMaterialType::VAN_PBR_EMISSIVE:
		case VansMaterialType::VAN_DECAL:
		case VansMaterialType::VAN_SUBSURFACE:
		case VansMaterialType::VAN_CUSTOM_SHADER:
			return true;
		default:
			return false;
		}
	}

	int ResolveTreeMaterialIndex(const VansMaterial* material)
	{
		return SupportsTreeMaterial(material) ? material->GetGlobalMaterialIndex() : -1;
	}
}

// ============================================================================
// Destructor
// ============================================================================
VansVegetationSystem::~VansVegetationSystem()
{
	// Buffers are cleaned up via Cleanup() called explicitly with a VkDevice
}

// ============================================================================
// Init  - creates all GPU resources for the vegetation system
// ============================================================================
void VansVegetationSystem::Init(VkDevice device, std::vector<GrassInstance> grass,
	std::vector<TreeInstanceGPU> trees, uint32_t boneCountPerInstance)
{
	if (!grass.empty() && (boneCountPerInstance < 2 || boneCountPerInstance > 64 ||
		grass.size() > static_cast<size_t>(std::numeric_limits<int>::max()) / (sizeof(glm::mat4) * boneCountPerInstance) ||
		grass.size() > std::numeric_limits<uint32_t>::max() / m_SubBladeCount))
		throw std::invalid_argument("Vegetation batch exceeds bone/upload/indirect capacity.");
	m_Device = device;
	m_GrassInstancesCPU = std::move(grass);
	m_TreeInstancesCPU = std::move(trees);
	m_InstanceCount = static_cast<uint32_t>(m_GrassInstancesCPU.size());
	m_BoneCountPerInstance = boneCountPerInstance;
	if (m_InstanceCount > 0)
	{
		if (std::any_of(m_RenderConfigs.begin(), m_RenderConfigs.end(), [](const auto& part) { return part.proceduralBlade; }))
			CreateTemplateMesh(device);
		CreateInstanceBuffer(device);
		CreateBoneBuffer(device);
		CreateBoneMatrixBuffer(device);
		CreateLodFactorsBuffer(device);
		CreateScatterOffsetUBO(device);
		CreateCullBuffers(device);
		LoadComputeShaders(device);
		CreateDescriptorSets();
		BuildRenderConfigs();
	}
	if (!m_TreeInstancesCPU.empty())
	{
		LoadTreeShaders(device);
		CreateTreeDescriptorSets();
		BuildTreeResources();
	}
}

// ============================================================================
// CreateTemplateMesh  - grass blade quad-strip
//
// With default 6 bones (5 segments), we subdivide each segment into
// SUB_DIVS rows so that bone weight interpolation is smooth everywhere.
// Extra rows near the tip ensure no visible normal jump.
//
//        Tip
//         /\
//        /  \          - tip triangle
//      ──────          - sub-row N  (t close to 1.0)
//      |      |
//      ──────         ...intermediate sub-rows...
//      |      |
//      v0────v1        - sub-row 0 (root, t = 0)
// ============================================================================
void VansVegetationSystem::CreateTemplateMesh(VkDevice device)
{
	const uint32_t segments = m_BoneCountPerInstance - 1; // 5 bone segments
	const uint32_t SUB_DIVS = 3;  // subdivisions per segment
	const uint32_t totalRows = segments * SUB_DIVS; // number of left/right pair rows
	const float h = m_BladeHeight;
	const float w = m_BladeWidthRoot;

	std::vector<GrassVertex> vertices;
	vertices.reserve(totalRows * 2 + 1);

	// Raw position data for GenerateBoneWeights (x,y,z per vertex)
	std::vector<float> rawPositions;
	rawPositions.reserve((totalRows * 2 + 1) * 3);

	// Generate left/right pairs from root to just below tip
	for (uint32_t i = 0; i < totalRows; ++i)
	{
		float t = static_cast<float>(i) / static_cast<float>(totalRows);
		float y = t * h;
		float halfW = w * (1.0f - t) * 0.5f; // taper

		GrassVertex left = {};
		left.position = glm::vec3(-halfW, y, 0.0f);
		left.uv       = glm::vec2(0.0f, t);
		left.normal   = glm::vec3(0.0f, 0.0f, 1.0f);
		vertices.push_back(left);
		rawPositions.push_back(-halfW); rawPositions.push_back(y); rawPositions.push_back(0.0f);

		GrassVertex right = {};
		right.position = glm::vec3(halfW, y, 0.0f);
		right.uv       = glm::vec2(1.0f, t);
		right.normal   = glm::vec3(0.0f, 0.0f, 1.0f);
		vertices.push_back(right);
		rawPositions.push_back(halfW); rawPositions.push_back(y); rawPositions.push_back(0.0f);
	}

	// Tip vertex
	GrassVertex tip = {};
	tip.position = glm::vec3(0.0f, h, 0.0f);
	tip.uv       = glm::vec2(0.5f, 1.0f);
	tip.normal   = glm::vec3(0.0f, 0.0f, 1.0f);
	vertices.push_back(tip);
	rawPositions.push_back(0.0f); rawPositions.push_back(h); rawPositions.push_back(0.0f);

	m_VertexCount = static_cast<uint32_t>(vertices.size());

	// Indices: quads (2 triangles each) for all adjacent rows + tip triangles
	std::vector<uint32_t> indices;
	for (uint32_t i = 0; i < totalRows - 1; ++i)
	{
		uint32_t bl = i * 2;           // bottom-left
		uint32_t br = i * 2 + 1;       // bottom-right
		uint32_t tl = (i + 1) * 2;     // top-left
		uint32_t tr = (i + 1) * 2 + 1; // top-right

		// Quad as 2 triangles
		indices.push_back(bl); indices.push_back(br); indices.push_back(tr);
		indices.push_back(bl); indices.push_back(tr); indices.push_back(tl);
	}

	// Tip triangle
	uint32_t lastLeft  = (totalRows - 1) * 2;
	uint32_t lastRight = (totalRows - 1) * 2 + 1;
	uint32_t tipIdx    = m_VertexCount - 1;
	indices.push_back(lastLeft);  indices.push_back(lastRight); indices.push_back(tipIdx);

	m_IndexCount = static_cast<uint32_t>(indices.size());

	// GrassVertex、Grass.vert 与标准导入 Mesh 使用相同的顶点语义顺序。
	//   loc 0: vec3 position
	//   loc 1: vec2 uv
	//   loc 2: vec3 normal
	std::vector<VkVertexInputBindingDescription> bindings = {
		{ 0, static_cast<uint32_t>(sizeof(GrassVertex)), VK_VERTEX_INPUT_RATE_VERTEX }
	};
	std::vector<VkVertexInputAttributeDescription> attribs = {
		{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(GrassVertex, position)) },
		{ 1, 0, VK_FORMAT_R32G32_SFLOAT,    static_cast<uint32_t>(offsetof(GrassVertex, uv)) },
		{ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(GrassVertex, normal)) },
	};

	// Build a real VansMesh from the generated data
	m_TemplateMesh = new VansMesh(/*needCPUData=*/true);
	m_TemplateMesh->InitFromRawData(
		device,
		vertices.data(), m_VertexCount, static_cast<uint32_t>(sizeof(GrassVertex)),
		indices.data(), m_IndexCount,
		bindings, attribs,
		rawPositions);
}

// 只上传 PCG 的确定结果，渲染层不拥有 Mask 或随机摆放规则。
void VansVegetationSystem::CreateInstanceBuffer(VkDevice device)
{
	const VkDeviceSize bytes = sizeof(GrassInstance) * m_GrassInstancesCPU.size();
	m_InstanceBuffer.CreatVulkanBuffer(device, bytes, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_InstanceBuffer.SetBufferData(m_GrassInstancesCPU.data(), 0, static_cast<int>(bytes));
}

// ============================================================================
// CreateBoneBuffer  - initialise rest-pose bones for all instances
// ============================================================================
void VansVegetationSystem::CreateBoneBuffer(VkDevice device)
{
	uint32_t totalBones = m_InstanceCount * m_BoneCountPerInstance;
	std::vector<GrassBone> bones(totalBones);

	float baseSegLength = m_BladeHeight / static_cast<float>(m_BoneCountPerInstance - 1);

	const float maxTiltRad = glm::radians(m_RestTipBendDegrees);

	// Bone roots come directly from the generated instances so mask rejection stays in sync.
	const float windAngle = atan2f(m_InitWindDir.y, m_InitWindDir.x);
	const float twoPi     = 6.28318530718f;

	for (uint32_t i = 0; i < m_InstanceCount; ++i)
	{
		// Reuse the accepted grass instance for the corresponding bone chain.
		const GrassInstance& instance = m_GrassInstancesCPU[i];
		glm::vec3 pos = glm::vec3(instance.modelMatrix[3]);
		const glm::vec3 axisY = glm::normalize(glm::vec3(instance.modelMatrix[1]));
		const float scale = glm::length(glm::vec3(instance.modelMatrix[1]));
		const glm::vec3 axisX = glm::normalize(glm::vec3(instance.modelMatrix[0]));
		float rot = static_cast<float>(instance.randomSeed) / static_cast<float>(UINT32_MAX) * twoPi;
		if (rot < 0.0f)
			rot += twoPi;

		// Map rot uniformly over [0,2π]  - deviation  - [-m_InitLeanDeviation, +m_InitLeanDeviation].
		// (rot/twoPi)  - [0,1), remapped to [-1,+1] then scaled by the deviation limit.
		float deviation = (rot / twoPi * 2.0f - 1.0f) * m_InitLeanDeviation;
		float leanAngle = windAngle + deviation;
		glm::vec3 leanDir(cosf(leanAngle), 0.0f, sinf(leanAngle));
		leanDir -= axisY * glm::dot(leanDir, axisY);
		leanDir = glm::length(leanDir) > 0.0001f ? glm::normalize(leanDir)
			: glm::normalize(axisX - axisY * glm::dot(axisX, axisY));

		// Per-instance segment length based on scale
		float segLength = baseSegLength * scale;

		// Accumulate position along the pre-bent arc
		glm::vec3 accumPos = pos;

		for (uint32_t j = 0; j < m_BoneCountPerInstance; ++j)
		{
			uint32_t idx = i * m_BoneCountPerInstance + j;

			if (j == 0)
			{
				// Root bone: anchored at ground; slight lean already in restOffset
				glm::vec3 rootRestDir = glm::normalize(
					axisY * cosf(glm::radians(m_RestRootBendDegrees)) +
					leanDir * sinf(glm::radians(m_RestRootBendDegrees)));
				bones[idx].position   = glm::vec4(accumPos, 1.0f);
				bones[idx].velocity   = glm::vec4(accumPos, 0.0f);
				bones[idx].restOffset = glm::vec4(rootRestDir * segLength, 0.0f);
			}
			else
			{
				// Progressive tilt: each bone tilts more toward leanDir
				float t = static_cast<float>(j) / static_cast<float>(m_BoneCountPerInstance - 1);
				float tiltAngle = maxTiltRad * t;

				// Rest offset: rotate the up vector toward leanDir by tiltAngle
				glm::vec3 restDir = glm::normalize(
					axisY * cosf(tiltAngle) +
					leanDir * sinf(tiltAngle));

				bones[idx].restOffset = glm::vec4(restDir * segLength, 0.0f);

				// Place bone along the pre-bent arc
				accumPos += restDir * segLength;
				bones[idx].position  = glm::vec4(accumPos, 1.0f);
				bones[idx].velocity  = glm::vec4(accumPos, 0.0f); // prevPos = pos
			}
		}
	}

	VkDeviceSize bufferSize = sizeof(GrassBone) * std::max(totalBones, 1u);
	m_BoneBuffer.CreatVulkanBuffer(device, bufferSize, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (totalBones > 0)
		m_BoneBuffer.SetBufferData(bones.data(), 0, static_cast<int>(sizeof(GrassBone) * totalBones));
}

// ============================================================================
// CreateBoneMatrixBuffer  - uninitialized, written by compute
// ============================================================================
void VansVegetationSystem::CreateBoneMatrixBuffer(VkDevice device)
{
	uint32_t totalMatrices = m_InstanceCount * m_BoneCountPerInstance;
	VkDeviceSize bufferSize = sizeof(glm::mat4) * std::max(totalMatrices, 1u);

	// Pre-fill with identity matrices so that the first rendered frame (before the
	// first compute dispatch) shows blades in rest-pose instead of at position (0,0,0).
	std::vector<glm::mat4> identities(totalMatrices, glm::mat4(1.0f));

	m_BoneMatrixBuffer.CreatVulkanBuffer(device, bufferSize, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (totalMatrices > 0)
		m_BoneMatrixBuffer.SetBufferData(identities.data(), 0, static_cast<int>(sizeof(glm::mat4) * totalMatrices));
}

// (CreateSkinnedBuffers and CreateIndirectDrawBuffer removed  - skinning
//  moved to vertex shader; indirect draw buffers are per-config now.)

// ============================================================================
// CreateLodFactorsBuffer  - one float per instance, written by bone sim
// ============================================================================
void VansVegetationSystem::CreateLodFactorsBuffer(VkDevice device)
{
	VkDeviceSize bufferSize = sizeof(float) * std::max(m_InstanceCount, 1u);
	m_LodFactorsBuffer.CreatVulkanBuffer(device, bufferSize, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

// ============================================================================
// CreateScatterOffsetUBO
//
// P6a 优化: 仅存 - m_SubBladeCount 个共享散布偏 - (vec4)，所有实例复用 - 
// Sub-blade 0 = 零偏移（主根 -  1..N-1 = 随机 XZ 散布 - 
// 相比原来 2M×10×16B = 320 MB  - SubBladeRoots SSBO，此 UBO  - ~160 字节 - 
// Terrain Y 的采样移到了顶点着色器中执行 - 
// ============================================================================
void VansVegetationSystem::CreateScatterOffsetUBO(VkDevice device)
{
	// 每个散布偏移 - vec4(dx, 0, dz, 0)，sub-blade 0 = (0,0,0,0)
	std::vector<glm::vec4> offsets(32, glm::vec4(0.0f));

	std::mt19937 rngTuft(m_SubBladeScatterSeed);
	std::uniform_real_distribution<float> radiusDist(m_SubBladeScatterRadiusMin, m_SubBladeScatterRadiusMax);
	std::uniform_real_distribution<float> angleDist(0.0f, 6.28318530718f);

	for (uint32_t s = 1; s < m_SubBladeCount; ++s)
	{
		float r   = radiusDist(rngTuft);
		float ang = angleDist(rngTuft);
		offsets[s] = glm::vec4(r * cosf(ang), 0.0f, r * sinf(ang), 0.0f);
	}

	VkDeviceSize bufferSize = sizeof(glm::vec4) * m_SubBladeCount;
	m_ScatterOffsetUBO.CreatVulkanBuffer(device, bufferSize, VK_FORMAT_R32G32B32A32_SFLOAT,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_ScatterOffsetUBO.SetBufferData(offsets.data(), 0, static_cast<int>(bufferSize));

	VANS_LOG("[VegetationSystem] Scatter offset UBO: " << m_SubBladeCount
		<< " sub-blade offsets (" << bufferSize << " bytes)");
}

// ============================================================================
// CreateCullBuffers  - P0: GPU frustum + distance cull buffers
//
// VisibilityBuffer    : uint per instance (1=visible, 0=culled), device local
// VisibleCountBuffer  : 主画面、阴影各一个计数器，在 GPU 命令流内清零
// VisibleIndexBuffer  : compact list of visible instance indices, device local
// ============================================================================
void VansVegetationSystem::CreateCullBuffers(VkDevice device)
{
	// Visibility flags  - one uint per instance
	VkDeviceSize visFlagSize = sizeof(uint32_t) * std::max(m_InstanceCount, 1u);
	m_VisibilityBuffer.CreatVulkanBuffer(device, visFlagSize, VK_FORMAT_R32_UINT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// 两个计数器分别驱动主画面与仅按距离筛选的阴影。
	// TRANSFER_SRC 用于 GPU  - CopyBuffer  - indirect draw buffer  - instanceCount 字段
	VkDeviceSize countSize = sizeof(uint32_t) * 2;
	m_VisibleCountBuffer.CreatVulkanBuffer(device, countSize, VK_FORMAT_R32_UINT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	const uint32_t zeros[2] = {0, 0};
	m_VisibleCountBuffer.SetBufferData(zeros, 0, sizeof(zeros));

	// Visible index list  - uint per instance (worst case all visible)
	VkDeviceSize idxSize = sizeof(uint32_t) * std::max(m_InstanceCount, 1u) * 2;
	m_VisibleIndexBuffer.CreatVulkanBuffer(device, idxSize, VK_FORMAT_R32_UINT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VANS_LOG("[VegetationSystem] Cull buffers created: visibility=" << visFlagSize
		<< "B, visibleIdx=" << idxSize << "B (" << m_InstanceCount << " instances)");
}

// ============================================================================
// LoadComputeShaders
// ============================================================================
void VansVegetationSystem::LoadComputeShaders(VkDevice device)
{
	(void)device;
	m_BoneSimShader = VansShaderManager::Get().FindComputeShader("GrassBoneSim");
	m_CullShader = VansShaderManager::Get().FindComputeShader("GrassCull");
	m_GrassShadowShader = VansShaderManager::Get().FindGraphicsShader("GrassShadow");
	if (m_CastShadows && !m_GrassShadowShader)
		throw std::invalid_argument("Grass shadow shaders are unavailable.");
	if (!m_BoneSimShader || !m_CullShader)
		throw std::invalid_argument("Grass simulation or culling shader is unavailable.");
}

void VansVegetationSystem::LoadTreeShaders(VkDevice device)
{
	m_TreeGBufferShader = VansShaderManager::Get().FindGraphicsShader("TreeGBuffer");
	m_TreeShadowShader = VansShaderManager::Get().FindGraphicsShader("TreeShadow");
	m_TreeCullShader = VansShaderManager::Get().FindComputeShader("TreeCull");
	if (!m_TreeGBufferShader)
		VANS_LOG_WARN("[VegetationSystem] TreeGBuffer shader not found.");
	if (!m_TreeShadowShader)
		VANS_LOG_WARN("[VegetationSystem] TreeShadow shader not found.");
	if (!m_TreeCullShader)
		VANS_LOG_WARN("[VegetationSystem] TreeCull shader not found.");
}

// ============================================================================
// CreateDescriptorSets
// ============================================================================
void VansVegetationSystem::CreateDescriptorSets()
{
	VansDescriptorSetLayoutFactory::CreateAndAllocate_VegetationBoneSim(m_BoneSimLayout, m_BoneSimDescSets);

	// Draw layout is created here; actual per-config descriptor sets are
	// allocated later in BuildRenderConfigs().
	{
		std::vector<VkDescriptorSet> unused;
		VansDescriptorSetLayoutFactory::CreateAndAllocate_VegetationDraw(m_VegDrawLayout, unused, 0);
		// We only need the layout handle; per-config sets are allocated individually.
	}

	// P0: Cull descriptor set (set=1 in GrassCull.comp)
	VansDescriptorSetLayoutFactory::CreateAndAllocate_VegetationCull(m_CullLayout, m_CullDescSets);

	WriteBoneSimDescriptors();
	WriteCullDescriptors();
}

void VansVegetationSystem::CreateTreeDescriptorSets()
{
	std::vector<VkDescriptorSet> unused;
	VansDescriptorSetLayoutFactory::CreateAndAllocate_VegetationTreeDraw(m_TreeDrawLayout, unused, 0);
	VansDescriptorSetLayoutFactory::CreateAndAllocate_VegetationTreeCull(m_TreeCullLayout, m_TreeCullDescSets);
}

void VansVegetationSystem::WriteBoneSimDescriptors()
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->BeginDescriptorUpdate();

	descMgr->WriteBufferDescriptor(
		m_BoneSimDescSets[0], VEG_SIM_BINDING_INSTANCE_DATA,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_InstanceBuffer.GetNativeBuffer(), 0, m_InstanceBuffer.GetBufferSize() }});
	descMgr->WriteBufferDescriptor(
		m_BoneSimDescSets[0], VEG_SIM_BINDING_BONE_DATA,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_BoneBuffer.GetNativeBuffer(), 0, m_BoneBuffer.GetBufferSize() }});
	descMgr->WriteBufferDescriptor(
		m_BoneSimDescSets[0], VEG_SIM_BINDING_BONE_MATRICES,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_BoneMatrixBuffer.GetNativeBuffer(), 0, m_BoneMatrixBuffer.GetBufferSize() }});

	// Terrain heightmap (binding 3)  - always write a valid descriptor


	// LOD factors buffer (binding 4)
	descMgr->WriteBufferDescriptor(m_BoneSimDescSets[0], VEG_SIM_BINDING_LOD_FACTORS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{ m_LodFactorsBuffer.GetNativeBuffer(), 0, m_LodFactorsBuffer.GetBufferSize() }});

	// P6a: Scatter offset UBO (binding 5)  -  - subBladeCount 个共享散布偏 - 
	descMgr->WriteBufferDescriptor(m_BoneSimDescSets[0], VEG_SIM_BINDING_SCATTER_OFFSETS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {{ m_ScatterOffsetUBO.GetNativeBuffer(), 0, m_ScatterOffsetUBO.GetBufferSize() }});

	// Binding 6：读取本帧 cull 输出的可见性标记，用于跳过不可见草实例的模拟。
	descMgr->WriteBufferDescriptor(m_BoneSimDescSets[0], VEG_SIM_BINDING_VISIBILITY_FLAGS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{ m_VisibilityBuffer.GetNativeBuffer(), 0, m_VisibilityBuffer.GetBufferSize() }});

	descMgr->CommitDescriptorUpdates();
}

// ============================================================================
// WriteCullDescriptors  - P0: bind cull buffers to cull descriptor set
// ============================================================================
void VansVegetationSystem::WriteCullDescriptors()
{
	if (m_CullDescSets.empty()) return;

	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->BeginDescriptorUpdate();

	// Binding 0: Instance data (read)
	descMgr->WriteBufferDescriptor(
		m_CullDescSets[0], VEG_CULL_BINDING_INSTANCE_DATA,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_InstanceBuffer.GetNativeBuffer(), 0, m_InstanceBuffer.GetBufferSize() }});

	// Binding 1: Visibility flags (write)
	descMgr->WriteBufferDescriptor(
		m_CullDescSets[0], VEG_CULL_BINDING_VISIBILITY,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_VisibilityBuffer.GetNativeBuffer(), 0, m_VisibilityBuffer.GetBufferSize() }});

	// Binding 2: Visible count (atomic counter)
	descMgr->WriteBufferDescriptor(
		m_CullDescSets[0], VEG_CULL_BINDING_VISIBLE_COUNT,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_VisibleCountBuffer.GetNativeBuffer(), 0, m_VisibleCountBuffer.GetBufferSize() }});

	// Binding 3: Visible index list (write)
	descMgr->WriteBufferDescriptor(
		m_CullDescSets[0], VEG_CULL_BINDING_VISIBLE_INDICES,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_VisibleIndexBuffer.GetNativeBuffer(), 0, m_VisibleIndexBuffer.GetBufferSize() }});

	// Binding 4: Terrain heightmap  - 用于采样实例的实际地面高度，修正包围 - Y 位置


	// Binding 5: Hi-Z depth pyramid  - 用于保守遮挡剔除，判断实例是否被地形或建筑物遮挡
	// 注意: HZB 全程保持 VK_IMAGE_LAYOUT_GENERAL ( - HIZ compute  - STORAGE_IMAGE 写入) - 
	//       必须与此 - descriptor 声明 - layout 一致，否则 Vulkan 采样结果未定义 - 
	if (m_HiZEnabled && m_HiZView != VK_NULL_HANDLE && m_HiZSampler != VK_NULL_HANDLE)
	{
		descMgr->WriteImageDescriptor(
			m_CullDescSets[0], VEG_CULL_BINDING_HIZ,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ m_HiZSampler, m_HiZView, VK_IMAGE_LAYOUT_GENERAL }});
	}

	descMgr->CommitDescriptorUpdates();
}

// (WriteSkinningDescriptors removed  - skinning moved to vertex shader)

void VansVegetationSystem::WriteDrawDescriptors(GrassRenderConfigGPU& cfg)
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->BeginDescriptorUpdate();

	// Binding 0: Bone matrices (compute output, VS reads)
	descMgr->WriteBufferDescriptor(cfg.drawDescSet, VEG_DRAW_BINDING_BONE_MATRICES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{ m_BoneMatrixBuffer.GetNativeBuffer(), 0, m_BoneMatrixBuffer.GetBufferSize() }});

	// Binding 1: Bone weights (static, per-vertex)
	descMgr->WriteBufferDescriptor(cfg.drawDescSet, VEG_DRAW_BINDING_BONE_WEIGHTS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{ cfg.boneWeightBuffer.GetNativeBuffer(), 0, cfg.boneWeightBuffer.GetBufferSize() }});

	// 一个批次的所有部件使用同一压缩列表，不再按材质拆分随机实例。
	descMgr->WriteBufferDescriptor(cfg.drawDescSet, VEG_DRAW_BINDING_INSTANCE_REMAP,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{ m_VisibleIndexBuffer.GetNativeBuffer(), 0, m_VisibleIndexBuffer.GetBufferSize() }});

	// Binding 3: P6a  - Scatter offset UBO (shared sub-blade XZ offsets)
	descMgr->WriteBufferDescriptor(cfg.drawDescSet, VEG_DRAW_BINDING_SCATTER_OFFSETS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {{ m_ScatterOffsetUBO.GetNativeBuffer(), 0, m_ScatterOffsetUBO.GetBufferSize() }});

	// Binding 4: LOD factors
	descMgr->WriteBufferDescriptor(cfg.drawDescSet, VEG_DRAW_BINDING_LOD_FACTORS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{ m_LodFactorsBuffer.GetNativeBuffer(), 0, m_LodFactorsBuffer.GetBufferSize() }});

	// Binding 5: Instance data (positions, rotations, etc.)
	descMgr->WriteBufferDescriptor(cfg.drawDescSet, VEG_DRAW_BINDING_INSTANCE_DATA, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{ m_InstanceBuffer.GetNativeBuffer(), 0, m_InstanceBuffer.GetBufferSize() }});

	// Binding 6: P6a  - Terrain heightmap for VS sub-blade Y sampling


	// Binding 7: P0  - Per-instance visibility flags from GPU cull
	descMgr->WriteBufferDescriptor(cfg.drawDescSet, VEG_DRAW_BINDING_VISIBILITY_FLAGS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{ m_VisibilityBuffer.GetNativeBuffer(), 0, m_VisibilityBuffer.GetBufferSize() }});

	descMgr->CommitDescriptorUpdates();
}

void VansVegetationSystem::WriteTreeCullDescriptors()
{
	if (m_TreeCullDescSets.empty() || !m_TreeEnabled)
		return;

	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->BeginDescriptorUpdate();

	descMgr->WriteBufferDescriptor(
		m_TreeCullDescSets[0], VEG_TREE_CULL_BINDING_INSTANCES,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_TreeInstanceBuffer.GetNativeBuffer(), 0, m_TreeInstanceBuffer.GetBufferSize() }});
	descMgr->WriteBufferDescriptor(
		m_TreeCullDescSets[0], VEG_TREE_CULL_BINDING_VISIBLE_COUNTS,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_TreeVisibleCountsBuffer.GetNativeBuffer(), 0, m_TreeVisibleCountsBuffer.GetBufferSize() }});
	descMgr->WriteBufferDescriptor(
		m_TreeCullDescSets[0], VEG_TREE_CULL_BINDING_VISIBLE_INDICES,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_TreeVisibleIndexBuffer.GetNativeBuffer(), 0, m_TreeVisibleIndexBuffer.GetBufferSize() }});
	descMgr->WriteBufferDescriptor(
		m_TreeCullDescSets[0], VEG_TREE_CULL_BINDING_SPECIES_INFOS,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_TreeSpeciesInfoBuffer.GetNativeBuffer(), 0, m_TreeSpeciesInfoBuffer.GetBufferSize() }});

	if (m_HiZEnabled && m_HiZView != VK_NULL_HANDLE && m_HiZSampler != VK_NULL_HANDLE)
	{
		descMgr->WriteImageDescriptor(
			m_TreeCullDescSets[0], VEG_TREE_CULL_BINDING_HIZ,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ m_HiZSampler, m_HiZView, VK_IMAGE_LAYOUT_GENERAL }});
	}

	descMgr->CommitDescriptorUpdates();
}

void VansVegetationSystem::WriteTreeDrawDescriptors(TreeDrawConfigGPU& cfg)
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->BeginDescriptorUpdate();

	descMgr->WriteBufferDescriptor(
		cfg.drawDescSet, VEG_TREE_DRAW_BINDING_INSTANCES,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_TreeInstanceBuffer.GetNativeBuffer(), 0, m_TreeInstanceBuffer.GetBufferSize() }});
	descMgr->WriteBufferDescriptor(
		cfg.drawDescSet, VEG_TREE_DRAW_BINDING_VISIBLE_INDICES,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_TreeVisibleIndexBuffer.GetNativeBuffer(), 0, m_TreeVisibleIndexBuffer.GetBufferSize() }});

	descMgr->CommitDescriptorUpdates();
}

// ============================================================================
// DispatchCullPass  - P0: GPU frustum + distance culling
//
// 1.  - visibleCount 重置 - 0
// 2. Dispatch GrassCull.comp  - 每线程判断一个实例是否可 - 
//    每个可见实例 atomicAdd(visibleCount, subBladeCount)，结果即 - indirect instanceCount
// 3. Barrier: compute  - transfer
// 4. CopyBuffer: visibleCount  - 每个 config  - indirect draw buffer instanceCount 字段
// 5. Barrier: transfer  - draw indirect + vertex shader read
// ============================================================================
bool VansVegetationSystem::DispatchCullPass(
	VansVKCommandBuffer& computeCmd,
	float cullDistance,
	bool sameQueueGraphicsConsumer)
{
	if (m_InstanceCount == 0) return false;
	if (!m_CullShader || m_CullDescSets.empty())
	{
		VANS_LOG_WARN("[VegetationSystem] CullPass skipped: cull shader or descriptor sets not ready.");
		return false;
	}
	if (m_InstanceCount == 0)
		return false;

	// 在命令流中清零，避免 CPU 提前覆写仍被上一帧 GPU 使用的计数器。
	computeCmd.FillBuffer(m_VisibleCountBuffer.GetNativeBuffer(), 0, m_VisibleCountBuffer.GetBufferSize(), 0);

	VkMemoryBarrier transferToCompute = {};
	transferToCompute.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	transferToCompute.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	transferToCompute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	computeCmd.PipelineBarrier(
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		{ transferToCompute });

	// ── Fill push constants ─────────────────────────────────────────
	GrassCullPushConstants cullPC = {};
	cullPC.cullDistance      = m_CullEnabled ? cullDistance : -1.0f;
	cullPC.grassHeight       = m_BladeHeight;
	cullPC.instanceCount     = m_InstanceCount;
	cullPC.scatterRadiusMax  = m_SubBladeScatterRadiusMax;
	// 每个可见实例 atomicAdd 此值， - visibleCount = 可见实例 - × subBladeCount
	cullPC.subBladeCount     = m_SubBladeCount;
	// Hi-Z 遮挡剔除参数
	cullPC.hizSampleBias     = m_HiZSampleBias;
	cullPC.hizMipCount       = static_cast<int>(m_HiZMipCount);
	cullPC.hizEnabled        = (m_HiZEnabled && m_HiZView != VK_NULL_HANDLE) ? 1 : 0;

	// ── Ensure pipeline + dispatch ──────────────────────────────────
	computeCmd.EnsureComputeShader(*m_CullShader, { m_GlobalDescSetLayout, m_CullLayout });

	uint32_t cullGroupsX = (m_InstanceCount + 63) / 64;
	computeCmd.DispatchCompute(*m_CullShader, cullGroupsX, 1, 1,
		{ m_GlobalDescSet, m_CullDescSets[0] }, &cullPC, sizeof(cullPC));

	{
		// ── Barrier: compute write  - transfer read (CopyBuffer source) + VS read ─
		VkMemoryBarrier computeToTransfer = {};
		computeToTransfer.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		computeToTransfer.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		computeToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
		computeCmd.PipelineBarrier(
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
				(sameQueueGraphicsConsumer ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT : 0u),
			{ computeToTransfer });

		// ── CopyBuffer: visibleCount  - indirect buffer instanceCount (offset 4) ─
		// VkDrawIndexedIndirectCommand.instanceCount 位于结构体偏 - 4 字节 - 
		for (auto& cfg : m_RenderConfigsGPU)
		{
			computeCmd.CopyBuffer(
				m_VisibleCountBuffer.GetNativeBuffer(),
				cfg.indirectDrawBuffer.GetNativeBuffer(),
				0,                                       // src offset = visibleCount
				offsetof(VkDrawIndexedIndirectCommand, instanceCount), // dst offset = 4
				sizeof(uint32_t));
			computeCmd.CopyBuffer(m_VisibleCountBuffer.GetNativeBuffer(),
				cfg.shadowIndirectDrawBuffer.GetNativeBuffer(), sizeof(uint32_t),
				offsetof(VkDrawIndexedIndirectCommand, instanceCount), sizeof(uint32_t));
		}

		// ── Barrier: transfer write  - indirect command read ─────────
		VkMemoryBarrier transferToIndirect = {};
		transferToIndirect.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		transferToIndirect.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		transferToIndirect.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		computeCmd.PipelineBarrier(
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
			{ transferToIndirect });
	}
	return true;
}

void VansVegetationSystem::DispatchTreeCullPass(
	VansVKCommandBuffer& computeCmd,
	bool sameQueueGraphicsConsumer)
{
	if (!m_TreeEnabled || !m_TreeCullShader || m_TreeCullDescSets.empty())
		return;
	computeCmd.FillBuffer(m_TreeVisibleCountsBuffer.GetNativeBuffer(), 0,
		m_TreeVisibleCountsBuffer.GetBufferSize(), 0);

	VkMemoryBarrier transferToCompute = {};
	transferToCompute.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	transferToCompute.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	transferToCompute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	computeCmd.PipelineBarrier(
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		{ transferToCompute });

	TreeCullPushConstants pc = {};
	pc.cullDistance = m_CullEnabled ? m_CullDistance : -1.0f;
	pc.instanceCount = static_cast<uint32_t>(m_TreeInstancesCPU.size());
	pc.speciesCount = static_cast<uint32_t>(m_TreeSpeciesInfosCPU.size());
	pc.hizEnabled = (m_HiZEnabled && m_HiZView != VK_NULL_HANDLE) ? 1u : 0u;
	pc.hizSampleBias = m_HiZSampleBias;
	pc.hizMipCount = static_cast<int>(m_HiZMipCount);
	computeCmd.EnsureComputeShader(*m_TreeCullShader, { m_GlobalDescSetLayout, m_TreeCullLayout });
	uint32_t groupsX = (pc.instanceCount + 63u) / 64u;
	computeCmd.DispatchCompute(*m_TreeCullShader, groupsX, 1, 1,
		{ m_GlobalDescSet, m_TreeCullDescSets[0] }, &pc, sizeof(pc));

	VkMemoryBarrier computeToTransfer = {};
	computeToTransfer.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	computeToTransfer.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	computeToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
	computeCmd.PipelineBarrier(
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT |
			(sameQueueGraphicsConsumer ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT : 0u),
		{ computeToTransfer });

	for (auto& cfg : m_TreeDrawConfigsGPU)
	{
		computeCmd.CopyBuffer(
			m_TreeVisibleCountsBuffer.GetNativeBuffer(),
			cfg.indirectDrawBuffer.GetNativeBuffer(),
			sizeof(uint32_t) * cfg.visibilityGroupIndex,
			offsetof(VkDrawIndexedIndirectCommand, instanceCount),
			sizeof(uint32_t));
		computeCmd.CopyBuffer(
			m_TreeVisibleCountsBuffer.GetNativeBuffer(), cfg.shadowIndirectDrawBuffer.GetNativeBuffer(),
			sizeof(uint32_t) * (m_TreeSpeciesInfosCPU.size() + cfg.visibilityGroupIndex),
			offsetof(VkDrawIndexedIndirectCommand, instanceCount), sizeof(uint32_t));
	}

	VkMemoryBarrier transferToIndirect = {};
	transferToIndirect.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	transferToIndirect.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	transferToIndirect.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
	computeCmd.PipelineBarrier(
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
		{ transferToIndirect });
}

// ============================================================================
// Update  - dispatches bone sim compute pass (skinning now in vertex shader)
// ============================================================================
void VansVegetationSystem::Update(VansVKCommandBuffer& computeCmd, float deltaTime, float time,
                                   const glm::vec2& windDirection, float windStrength,
                                   float windFrequency, float windSpeed, float windBendMult,
                                   float stiffness, float damping,
                                   float softness, float lodFullDist, float lodFadeDist,
								   bool useCullVisibilityMask,
								   bool sameQueueGraphicsConsumer)
{
	// 树木独立批次没有草骨骼资源，不参与草的模拟。
	if (m_InstanceCount == 0)
		return;
	if (!m_BoneSimShader || m_BoneSimDescSets.empty())
	{
		VANS_LOG_WARN("[VegetationSystem] Update skipped: shaders or descriptor sets not ready.");
		return;
	}
	// ── Pass 1: Bone Simulation ─────────────────────────────────────
	GrassSimPushConstants simPC = {};
	simPC.deltaTime     = deltaTime;
	simPC.time          = time;
	simPC.windStrength  = windStrength;
	simPC.windFrequency = windFrequency;
	simPC.windSpeed     = windSpeed;
	simPC.windBendMult  = windBendMult;
	simPC.windDirX      = windDirection.x;
	simPC.windDirY      = windDirection.y;
	simPC.stiffness     = stiffness;
	simPC.damping       = damping;
	simPC.softness      = softness;
	simPC.lodFullDist        = lodFullDist;
	simPC.lodFadeDist        = lodFadeDist;
	simPC.subBladeCount      = static_cast<int>(m_SubBladeCount);
	simPC.grassHeight        = m_BladeHeight;
	simPC.cullVisibilityEnabled = useCullVisibilityMask ? 1u : 0u;
	simPC.boneCount          = m_BoneCountPerInstance;

	computeCmd.EnsureComputeShader(*m_BoneSimShader, { m_GlobalDescSetLayout, m_BoneSimLayout });

	uint32_t simGroupsX = (m_InstanceCount + 63) / 64;
	computeCmd.DispatchCompute(*m_BoneSimShader, simGroupsX, 1, 1,
		{ m_GlobalDescSet, m_BoneSimDescSets[0] }, &simPC, sizeof(simPC));

	if (sameQueueGraphicsConsumer)
	{
		// 单队列路径在同一个 command buffer 内直接衔接 vertex consumer。
		VkMemoryBarrier simToDrawBarrier = {};
		simToDrawBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		simToDrawBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		simToDrawBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		computeCmd.PipelineBarrier(
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
			{ simToDrawBarrier });
	}
}

// ============================================================================
// Draw  - issues one indirect indexed draw call per render config
// ============================================================================
void VansVegetationSystem::Draw(VansVKCommandBuffer& graphicsCmd,
                                 GlobalStateData& globalState,
                                 const std::vector<VkDescriptorSetLayout>& baseDescSetLayouts,
                                 const std::vector<VkDescriptorSet>& baseDescSets,
                                 int pushConstantTransformIndex)
{
	if (m_RenderConfigsGPU.empty()) return;

	for (auto& cfg : m_RenderConfigsGPU)
	{
		if (cfg.assignedInstanceCount == 0) continue;
		auto* passShader = cfg.material->GetPassShader(VansPass::GBUFFER);
		if (!passShader) continue;
		auto& shader = *passShader;

		// ── Pipeline creation ───────────────────────────────────────
		// Temporarily swap vertex input state so EnsureGraphicsShader
		// creates / caches the correct pipeline for this mesh's format.
		auto* savedBindings   = globalState.vertexInputBindingDescriptions;
		auto* savedAttributes = globalState.vertexInputAttributeDescriptions;

		globalState.vertexInputBindingDescriptions   = &cfg.mesh->m_VertexInputBindingDescriptions;
		globalState.vertexInputAttributeDescriptions = &cfg.mesh->m_VertexInputAttributeDescriptions;

		// Build full descriptor set layout + set arrays: base sets + draw set (Set 3)
		std::vector<VkDescriptorSetLayout> layouts = baseDescSetLayouts;
		layouts.push_back(m_VegDrawLayout);
		std::vector<VkDescriptorSet> sets = baseDescSets;
		sets.push_back(cfg.drawDescSet);

		// If material provides texture descriptor (Set 4), append it
		auto* grassMat = dynamic_cast<VansGrassMaterial*>(cfg.material);
		if (grassMat)
		{
			if (grassMat->m_GrassOwnedLayout != VK_NULL_HANDLE && !grassMat->m_GrassOwnedDescSets.empty())
			{
				layouts.push_back(grassMat->m_GrassOwnedLayout);
				sets.push_back(grassMat->m_GrassOwnedDescSets[0]);
			}
		}

		graphicsCmd.EnsureGraphicsShader(shader, globalState, layouts);

		// Restore vertex input state
		globalState.vertexInputBindingDescriptions   = savedBindings;
		globalState.vertexInputAttributeDescriptions = savedAttributes;

		graphicsCmd.BindGraphicsPipeline(*shader.GetGraphicsPipeline());
		graphicsCmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, shader, 0, sets, {});

		// Push constants
		if (shader.GetPushConstantSize() > 0)
		{
			GrassDrawPushConstants pc = {};
			pc.materialIndex    = cfg.materialIndex;
			pc.objectIndex      = pushConstantTransformIndex;
			pc.vertexFeatureMask = 0u;
			pc.boneCount        = m_BoneCountPerInstance;
			pc.subBladeCount    = m_SubBladeCount;
			pc.grassHeight      = m_BladeHeight;
			// P6a: 传 - terrain 参数 - VS 用于子叶片地形采 - 
			// P1: 子叶片距 - LOD 阈 - 
			pc.lodMidDist           = m_SubBladeLodMidDist;
			pc.lodFarDist           = m_SubBladeLodFarDist;
			pc.aoStrength           = grassMat ? grassMat->m_GrassParams.aoStrength : 1.0f;
			pc.rootAOIntensity      = grassMat ? grassMat->m_GrassParams.rootAOIntensity : 0.35f;
			pc.rootAOHeight         = grassMat ? grassMat->m_GrassParams.rootAOHeight : 0.35f;
			graphicsCmd.UpdatePushConstants(*shader.GetGraphicsPipeline(),
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, shader.GetPushConstantSize(), &pc);
		}

		// Bind mesh vertex + index buffers (uniform for all configs)
		graphicsCmd.BindMesh(*cfg.mesh, 0, globalState);

		// Issue indirect draw
		graphicsCmd.DrawIndexedIndirect(
			cfg.indirectDrawBuffer.GetNativeBuffer(), 0, 1,
			sizeof(VkDrawIndexedIndirectCommand));
	}
}

void VansVegetationSystem::DrawGrassCascadeShadow(VansVKCommandBuffer& graphicsCmd,
    GlobalStateData& globalState,
    const std::vector<VkDescriptorSetLayout>& baseDescSetLayouts,
    const std::vector<VkDescriptorSet>& baseDescSets, int cascadeIndex)
{
    auto* shader = m_GrassShadowShader;
    if (!m_CastShadows || !shader || m_RenderConfigsGPU.empty()) return;
    for (const auto& part : m_RenderConfigsGPU)
    {
        auto* material = static_cast<VansGrassMaterial*>(part.material);
        if (!part.assignedInstanceCount || !material || material->m_GrassOwnedDescSets.empty()) continue;
        auto passState = globalState;
        passState.vertexInputBindingDescriptions = &part.mesh->m_VertexInputBindingDescriptions;
        passState.vertexInputAttributeDescriptions = &part.mesh->m_VertexInputAttributeDescriptions;
        auto layouts = baseDescSetLayouts;
        auto sets = baseDescSets;
        layouts.push_back(m_VegDrawLayout); layouts.push_back(material->m_GrassOwnedLayout);
        sets.push_back(part.drawDescSet); sets.push_back(material->m_GrassOwnedDescSets[0]);
        graphicsCmd.EnsureGraphicsShader(*shader, passState, layouts);
        graphicsCmd.BindGraphicsPipeline(*shader->GetGraphicsPipeline());
        graphicsCmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *shader, 0, sets, {});
        const GrassShadowPushConstants pc{m_BoneCountPerInstance, m_SubBladeCount, cascadeIndex, m_InstanceCount};
        graphicsCmd.UpdatePushConstants(*shader->GetGraphicsPipeline(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        graphicsCmd.BindMesh(*part.mesh, 0, passState);
        graphicsCmd.DrawIndexedIndirect(part.shadowIndirectDrawBuffer.GetNativeBuffer(), 0, 1, sizeof(VkDrawIndexedIndirectCommand));
    }
}

void VansVegetationSystem::DrawTrees(VansVKCommandBuffer& graphicsCmd,
                                      GlobalStateData& globalState,
                                      const std::vector<VkDescriptorSetLayout>& baseDescSetLayouts,
                                      const std::vector<VkDescriptorSet>& baseDescSets,
                                      int pushConstantTransformIndex)
{
	if (!m_TreeEnabled || !m_TreeGBufferShader || m_TreeDrawConfigsGPU.empty())
		return;

	for (auto& cfg : m_TreeDrawConfigsGPU)
	{
		if (!cfg.mesh || !cfg.material || cfg.instanceCapacity == 0)
			continue;

		const int materialIndex = ResolveTreeMaterialIndex(cfg.material);
		if (materialIndex < 0)
		{
			VANS_LOG_WARN("[VegetationSystem] Tree draw skipped: material '"
				<< cfg.material->m_AssetName << "' has no GPU PBR material index.");
			continue;
		}

		auto* savedBindings = globalState.vertexInputBindingDescriptions;
		auto* savedAttributes = globalState.vertexInputAttributeDescriptions;
		globalState.vertexInputBindingDescriptions = &cfg.mesh->m_VertexInputBindingDescriptions;
		globalState.vertexInputAttributeDescriptions = &cfg.mesh->m_VertexInputAttributeDescriptions;

		std::vector<VkDescriptorSetLayout> layouts = baseDescSetLayouts;
		layouts.push_back(m_TreeDrawLayout);
		std::vector<VkDescriptorSet> sets = baseDescSets;
		sets.push_back(cfg.drawDescSet);

		graphicsCmd.EnsureGraphicsShader(*m_TreeGBufferShader, globalState, layouts);

		globalState.vertexInputBindingDescriptions = savedBindings;
		globalState.vertexInputAttributeDescriptions = savedAttributes;

		graphicsCmd.BindGraphicsPipeline(*m_TreeGBufferShader->GetGraphicsPipeline());
		graphicsCmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_TreeGBufferShader, 0, sets, {});

		TreeDrawPushConstants pc = {};
		pc.materialIndex = materialIndex;
		pc.objectIndex = pushConstantTransformIndex;
		pc.visibleOffset = cfg.visibleOffset;
		pc.alphaTestEnabled = cfg.partType == TreePartType::Leaves ? 1u : 0u;
		graphicsCmd.UpdatePushConstants(*m_TreeGBufferShader->GetGraphicsPipeline(),
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, m_TreeGBufferShader->GetPushConstantSize(), &pc);

		graphicsCmd.BindMesh(*cfg.mesh, 0, globalState);
		graphicsCmd.DrawIndexedIndirect(
			cfg.indirectDrawBuffer.GetNativeBuffer(), 0, 1,
			sizeof(VkDrawIndexedIndirectCommand));
	}
}

void VansVegetationSystem::DrawTreeCascadeShadow(VansVKCommandBuffer& graphicsCmd,
                                                 GlobalStateData& globalState,
                                                 const std::vector<VkDescriptorSetLayout>& baseDescSetLayouts,
                                                 const std::vector<VkDescriptorSet>& baseDescSets,
                                                 int pushConstantTransformIndex)
{
	if (!m_CastShadows || !m_TreeEnabled || !m_TreeShadowShader || m_TreeDrawConfigsGPU.empty())
		return;

	for (auto& cfg : m_TreeDrawConfigsGPU)
	{
		if (!cfg.mesh || !cfg.material || cfg.instanceCapacity == 0)
			continue;

		const int materialIndex = ResolveTreeMaterialIndex(cfg.material);
		if (materialIndex < 0)
		{
			VANS_LOG_WARN("[VegetationSystem] Tree cascade shadow skipped: material '"
				<< cfg.material->m_AssetName << "' has no GPU PBR material index.");
			continue;
		}

		auto* savedBindings = globalState.vertexInputBindingDescriptions;
		auto* savedAttributes = globalState.vertexInputAttributeDescriptions;
		globalState.vertexInputBindingDescriptions = &cfg.mesh->m_VertexInputBindingDescriptions;
		globalState.vertexInputAttributeDescriptions = &cfg.mesh->m_VertexInputAttributeDescriptions;

		std::vector<VkDescriptorSetLayout> layouts = baseDescSetLayouts;
		layouts.push_back(m_TreeDrawLayout);
		std::vector<VkDescriptorSet> sets = baseDescSets;
		sets.push_back(cfg.drawDescSet);

		graphicsCmd.EnsureGraphicsShader(*m_TreeShadowShader, globalState, layouts);

		globalState.vertexInputBindingDescriptions = savedBindings;
		globalState.vertexInputAttributeDescriptions = savedAttributes;

		graphicsCmd.BindGraphicsPipeline(*m_TreeShadowShader->GetGraphicsPipeline());
		graphicsCmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_TreeShadowShader, 0, sets, {});

		TreeShadowPushConstants pc = {};
		pc.materialIndex = materialIndex;
		pc.objectIndex = pushConstantTransformIndex;
		pc.visibleOffset = cfg.visibleOffset + static_cast<uint32_t>(m_TreeInstancesCPU.size());
		pc.cascadeIndex = globalState.cascadeIndex;
		pc.alphaTestEnabled = cfg.partType == TreePartType::Leaves ? 1u : 0u;
		graphicsCmd.UpdatePushConstants(*m_TreeShadowShader->GetGraphicsPipeline(),
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, m_TreeShadowShader->GetPushConstantSize(), &pc);

		graphicsCmd.BindMesh(*cfg.mesh, 0, globalState);
		graphicsCmd.DrawIndexedIndirect(cfg.shadowIndirectDrawBuffer.GetNativeBuffer(), 0, 1, sizeof(VkDrawIndexedIndirectCommand));
	}
}

// ============================================================================
// Cleanup
// ============================================================================
void VansVegetationSystem::Cleanup(VkDevice device)
{
	auto* descriptors = VansVKDescriptorManager::GetInstance();
	descriptors->DestroyDescriptorSet(m_BoneSimDescSets);
	descriptors->DestroyDescriptorSet(m_CullDescSets);
	descriptors->DestroyDescriptorSet(m_TreeCullDescSets);
	std::vector<VkDescriptorSet> ownedDrawSets;
	for (auto& part : m_RenderConfigsGPU) ownedDrawSets.push_back(part.drawDescSet);
	for (auto& part : m_TreeDrawConfigsGPU) ownedDrawSets.push_back(part.drawDescSet);
	descriptors->DestroyDescriptorSet(ownedDrawSets);
	for (auto* layout : { &m_BoneSimLayout, &m_CullLayout, &m_VegDrawLayout, &m_TreeDrawLayout, &m_TreeCullLayout })
		descriptors->ReleaseDescriptorSetLayout(*layout);
	m_InstanceBuffer.DestroyVulkanBuffer(device);
	m_BoneBuffer.DestroyVulkanBuffer(device);
	m_BoneMatrixBuffer.DestroyVulkanBuffer(device);
	if (m_TemplateMesh)
	{
		delete m_TemplateMesh;
		m_TemplateMesh = nullptr;
	}
	m_LodFactorsBuffer.DestroyVulkanBuffer(device);
	m_ScatterOffsetUBO.DestroyVulkanBuffer(device);

	// P0: Cull buffers
	m_VisibilityBuffer.DestroyVulkanBuffer(device);
	m_VisibleCountBuffer.DestroyVulkanBuffer(device);
	m_VisibleIndexBuffer.DestroyVulkanBuffer(device);

	// Per-config buffers
	for (auto& cfg : m_RenderConfigsGPU)
	{
		cfg.boneWeightBuffer.DestroyVulkanBuffer(device);
		cfg.indirectDrawBuffer.DestroyVulkanBuffer(device);
		cfg.shadowIndirectDrawBuffer.DestroyVulkanBuffer(device);
	}
	m_RenderConfigsGPU.clear();

	for (auto& cfg : m_TreeDrawConfigsGPU) {
		cfg.indirectDrawBuffer.DestroyVulkanBuffer(device);
		cfg.shadowIndirectDrawBuffer.DestroyVulkanBuffer(device);
	}
	m_TreeDrawConfigsGPU.clear();
	m_TreeInstanceBuffer.DestroyVulkanBuffer(device);
	m_TreeVisibleCountsBuffer.DestroyVulkanBuffer(device);
	m_TreeVisibleIndexBuffer.DestroyVulkanBuffer(device);
	m_TreeSpeciesInfoBuffer.DestroyVulkanBuffer(device);
	m_TreeInstancesCPU.clear();
	m_TreeSpeciesInfosCPU.clear();

	// Shader programs are owned by VansShaderManager and survive scene/system
	// resource rebuilds. Only release this subsystem's non-owning references.
	m_BoneSimShader = nullptr;
	m_CullShader = nullptr;
	m_GrassShadowShader = nullptr;
	m_TreeGBufferShader = nullptr;
	m_TreeShadowShader = nullptr;
	m_TreeCullShader = nullptr;
}

// ============================================================================
// BuildRenderConfigs  - partition instances across configs, create GPU resources
// ============================================================================
void VansVegetationSystem::BuildRenderConfigs()
{
	m_RenderConfigsGPU.reserve(m_RenderConfigs.size());
	for (const auto& part : m_RenderConfigs)
	{
		GrassRenderConfigGPU cfg;
		cfg.mesh = part.proceduralBlade ? m_TemplateMesh : part.mesh;
		cfg.material = part.material;
		if (!cfg.mesh || !cfg.material || cfg.mesh->GetIndexCount() == 0 ||
			cfg.material->m_MaterialType != VansMaterialType::VAN_GRASS)
			throw std::invalid_argument("Grass part requires drawable geometry and a compatible grass material.");
		cfg.assignedInstanceCount = m_InstanceCount;
		cfg.materialIndex = 0;
		GenerateBoneWeights(cfg, cfg.mesh);
		VkDrawIndexedIndirectCommand command = {};
		command.indexCount = cfg.mesh->GetIndexCount();
		// 第一次 cull 之前保持零实例，避免读取尚未写入的压缩列表。
		cfg.indirectDrawBuffer.CreatVulkanBuffer(m_Device, sizeof(command), VK_FORMAT_R32_UINT,
			VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		cfg.indirectDrawBuffer.SetBufferData(&command, 0, sizeof(command));
		cfg.shadowIndirectDrawBuffer.CreatVulkanBuffer(m_Device, sizeof(command), VK_FORMAT_R32_UINT,
			VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		cfg.shadowIndirectDrawBuffer.SetBufferData(&command, 0, sizeof(command));
		std::vector<VkDescriptorSet> sets;
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_VegDrawLayout },
			sets, VansDescriptorLifetimeRole::ScenePersistent);
		cfg.drawDescSet = sets.at(0);
		WriteDrawDescriptors(cfg);
		m_RenderConfigsGPU.push_back(std::move(cfg));
	}
}

void VansVegetationSystem::BuildTreeResources()
{
	const uint32_t count = static_cast<uint32_t>(m_TreeInstancesCPU.size());
	if (count == 0 || m_TreeParts.empty()) return;
	m_TreeSpeciesInfosCPU = { TreeSpeciesCullInfo{0, count, {0, 0}} };
	const auto upload = [&](VansVKBuffer& buffer, const void* data, size_t bytes, VkBufferUsageFlags usage) {
		if (bytes > static_cast<size_t>(std::numeric_limits<int>::max()))
			throw std::invalid_argument("Tree batch exceeds the upload capacity.");
		buffer.CreatVulkanBuffer(m_Device, bytes, VK_FORMAT_R32_UINT, usage,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		buffer.SetBufferData(data, 0, static_cast<int>(bytes));
	};
	upload(m_TreeInstanceBuffer, m_TreeInstancesCPU.data(), count * sizeof(TreeInstanceGPU),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	const uint32_t visibleCount = m_CullEnabled ? 0u : count;
	const uint32_t visibleCounts[2] = { visibleCount, visibleCount };
	upload(m_TreeVisibleCountsBuffer, visibleCounts, sizeof(visibleCounts),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	std::vector<uint32_t> indices(static_cast<size_t>(count) * 2);
	std::iota(indices.begin(), indices.begin() + count, 0u);
	std::iota(indices.begin() + count, indices.end(), 0u);
	upload(m_TreeVisibleIndexBuffer, indices.data(), indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	upload(m_TreeSpeciesInfoBuffer, m_TreeSpeciesInfosCPU.data(), sizeof(TreeSpeciesCullInfo), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	for (const auto& part : m_TreeParts)
	{
		// 场景内容构建早于材质 GPU 表准备；此处只校验材质类型，绘制时读取已分配的索引。
		if (!part.mesh || part.mesh->GetIndexCount() == 0 || !SupportsTreeMaterial(part.material))
			throw std::invalid_argument("Tree part requires drawable geometry and a compatible PBR material.");
		TreeDrawConfigGPU cfg;
		cfg.mesh = part.mesh;
		cfg.material = part.material;
		cfg.materialIndex = ResolveTreeMaterialIndex(part.material);
		cfg.partType = part.type;
		cfg.instanceCapacity = count;
		VkDrawIndexedIndirectCommand command = {};
		command.indexCount = part.mesh->GetIndexCount();
		command.instanceCount = visibleCount;
		upload(cfg.indirectDrawBuffer, &command, sizeof(command),
			VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
		upload(cfg.shadowIndirectDrawBuffer, &command, sizeof(command),
			VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
		std::vector<VkDescriptorSet> sets;
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_TreeDrawLayout },
			sets, VansDescriptorLifetimeRole::ScenePersistent);
		cfg.drawDescSet = sets.at(0);
		WriteTreeDrawDescriptors(cfg);
		m_TreeDrawConfigsGPU.push_back(std::move(cfg));
	}
	m_TreeEnabled = true;
	WriteTreeCullDescriptors();
}

// ============================================================================
// GenerateBoneWeights  - for external meshes: map vertex Y  - dual-bone weights
//
// Each vertex gets: vec4(boneIdx0, boneIdx1, weight0, weight1)
// Y is normalised to [0..1] over the mesh AABB, then mapped to bone segments.
// ============================================================================
void VansVegetationSystem::GenerateBoneWeights(GrassRenderConfigGPU& cfg, VansMesh* mesh)
{
	const auto& rawPos = mesh->GetMeshRawPositionData();
	uint32_t vertCount = mesh->GetMeshVertexCount();

	// 程序化草叶保存紧密排列的 xyz（步长 3）；导入 Mesh 将 position 和 normal
	// 保存为两个对齐的 vec4（步长 8），同时兼容旧的 vec4 position 表示（步长 4）。
	size_t positionStride = 0;
	if (vertCount > 0)
	{
		const size_t vertexCount = static_cast<size_t>(vertCount);
		if (rawPos.size() >= vertexCount * 8)
			positionStride = 8;
		else if (rawPos.size() >= vertexCount * 4)
			positionStride = 4;
		else if (rawPos.size() >= vertexCount * 3)
			positionStride = 3;
	}

	if (positionStride == 0)
	{
		// Fallback: generate identity weights (root bone only)
		std::vector<glm::vec4> weights(std::max(vertCount, 1u), glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
		VkDeviceSize sz = sizeof(glm::vec4) * weights.size();
		cfg.boneWeightBuffer.CreatVulkanBuffer(m_Device, sz, VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		cfg.boneWeightBuffer.SetBufferData(weights.data(), 0, static_cast<int>(sz));
		return;
	}

	// Find Y min/max
	float yMin = FLT_MAX, yMax = -FLT_MAX;
	for (uint32_t i = 0; i < vertCount; ++i)
	{
		float y = rawPos[static_cast<size_t>(i) * positionStride + 1];
		yMin = std::min(yMin, y);
		yMax = std::max(yMax, y);
	}
	float yRange = (yMax - yMin);
	if (yRange < 1e-6f) yRange = 1.0f;

	uint32_t segments = m_BoneCountPerInstance - 1;
	std::vector<glm::vec4> weights(vertCount);

	for (uint32_t i = 0; i < vertCount; ++i)
	{
		float y = rawPos[static_cast<size_t>(i) * positionStride + 1];
		float t = (y - yMin) / yRange; // normalised [0,1]
		t = glm::clamp(t, 0.0f, 1.0f);

		float boneF   = t * static_cast<float>(segments);
		uint32_t bone0 = static_cast<uint32_t>(floorf(boneF));
		if (bone0 >= segments) bone0 = segments - 1;
		uint32_t bone1 = bone0 + 1;
		if (bone1 >= m_BoneCountPerInstance) bone1 = m_BoneCountPerInstance - 1;

		float frac = boneF - static_cast<float>(bone0);
		weights[i] = glm::vec4(
			static_cast<float>(bone0),
			static_cast<float>(bone1),
			1.0f - frac,
			frac);
	}

	VkDeviceSize sz = sizeof(glm::vec4) * vertCount;
	cfg.boneWeightBuffer.CreatVulkanBuffer(m_Device, sz, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	cfg.boneWeightBuffer.SetBufferData(weights.data(), 0, static_cast<int>(sz));
}

// ============================================================================
// SetTerrainHeightmap  - connects terrain height data for ground placement
// ============================================================================
void VansVegetationSystem::SetHiZDepth(VkImageView imageView, VkSampler sampler,
                                        uint32_t mipCount, float sampleBias)
{
	if (m_HiZView == imageView && m_HiZSampler == sampler && m_HiZMipCount == mipCount && m_HiZSampleBias == sampleBias)
		return;
	m_HiZView        = imageView;
	m_HiZSampler     = sampler;
	m_HiZMipCount    = mipCount;
	m_HiZSampleBias  = sampleBias;
	m_HiZEnabled     = (imageView != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE && mipCount > 0);

	if (m_TreeEnabled) WriteTreeCullDescriptors();

	// 更新该批次自己的 Hi-Z 绑定。
	if (m_HiZEnabled && !m_CullDescSets.empty())
	{
		WriteCullDescriptors();
	}

	VANS_LOG("[VegetationSystem] Hi-Z depth cull " << (m_HiZEnabled ? "enabled" : "disabled")
		<< " (mips=" << mipCount << ", bias=" << sampleBias << ")");
}
