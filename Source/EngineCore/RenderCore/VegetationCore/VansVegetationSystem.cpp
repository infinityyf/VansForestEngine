#include "VansVegetationSystem.h"
#include "../VulkanCore/VansMesh.h"
#include "../VansMaterial.h"
#include "../../Util/VansLog.h"
#include "../../Configration/VansConfigration.h"
#include <random>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <functional>

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
	CreateLodFactorsBuffer(device);
	CreateSubBladeRootsBuffer(device);
	LoadComputeShaders(device);
	CreateDescriptorSets();

	VANS_LOG("VansVegetationSystem initialized: " << m_InstanceCount << " instances, "
		<< m_BoneCountPerInstance << " bones/instance, " << m_VertexCount << " verts/blade");
}

// ============================================================================
// CreateTemplateMesh — grass blade quad-strip
//
// With default 6 bones (5 segments), we subdivide each segment into
// SUB_DIVS rows so that bone weight interpolation is smooth everywhere.
// Extra rows near the tip ensure no visible normal jump.
//
//        Tip
//         /\
//        /  \         ← tip triangle
//      ──────         ← sub-row N  (t close to 1.0)
//      |      |
//      ──────         ...intermediate sub-rows...
//      |      |
//      v0────v1       ← sub-row 0 (root, t = 0)
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
		left.normal   = glm::vec3(0.0f, 0.0f, 1.0f);
		left.uv       = glm::vec2(0.0f, t);
		vertices.push_back(left);
		rawPositions.push_back(-halfW); rawPositions.push_back(y); rawPositions.push_back(0.0f);

		GrassVertex right = {};
		right.position = glm::vec3(halfW, y, 0.0f);
		right.normal   = glm::vec3(0.0f, 0.0f, 1.0f);
		right.uv       = glm::vec2(1.0f, t);
		vertices.push_back(right);
		rawPositions.push_back(halfW); rawPositions.push_back(y); rawPositions.push_back(0.0f);
	}

	// Tip vertex
	GrassVertex tip = {};
	tip.position = glm::vec3(0.0f, h, 0.0f);
	tip.normal   = glm::vec3(0.0f, 0.0f, 1.0f);
	tip.uv       = glm::vec2(0.5f, 1.0f);
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

	// Vertex input descriptions matching GrassVertex and Grass.vert:
	//   loc 0: vec3 position  (offset 0)
	//   loc 1: vec3 normal    (offset 12)
	//   loc 2: vec2 uv        (offset 24)
	std::vector<VkVertexInputBindingDescription> bindings = {
		{ 0, static_cast<uint32_t>(sizeof(GrassVertex)), VK_VERTEX_INPUT_RATE_VERTEX }
	};
	std::vector<VkVertexInputAttributeDescription> attribs = {
		{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(GrassVertex, position)) },
		{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(GrassVertex, normal)) },
		{ 2, 0, VK_FORMAT_R32G32_SFLOAT,    static_cast<uint32_t>(offsetof(GrassVertex, uv)) },
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

// ============================================================================
// CreateInstanceBuffer — random grass positions in [-10, 10] XZ
// ============================================================================
void VansVegetationSystem::CreateInstanceBuffer(VkDevice device)
{
	std::vector<GrassInstance> instances(m_InstanceCount);
	std::mt19937 rng(42);
	std::uniform_real_distribution<float> posDist(-100.0f, 100.0f);
	std::uniform_real_distribution<float> scaleDist(0.4f, 1.5f);
	std::uniform_real_distribution<float> rotDist(0.0f, 6.28318530718f);

	for (uint32_t i = 0; i < m_InstanceCount; ++i)
	{
		// Explicit sequential draws — C++ does not guarantee evaluation order of
		// function arguments, so glm::vec3(posDist(rng),0,posDist(rng)) produces
		// unspecified X/Z mapping.  Sequential statements give a defined draw order
		// that must match CreateBoneBuffer and CreateSubBladeRootsBuffer exactly.
		float px               = posDist(rng);
		float pz               = posDist(rng);
		instances[i].position  = glm::vec3(px, 0.0f, pz);
		instances[i].scale     = scaleDist(rng);
		instances[i].rotation  = rotDist(rng);
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

	float baseSegLength = m_BladeHeight / static_cast<float>(m_BoneCountPerInstance - 1);

	// Maximum tilt angle at the tip (radians). ~45° gives a natural relaxed arc.
	const float maxTiltRad = glm::radians(45.0f);

	// Read back instance data for root positions
	// (we just generated it in-line, so regenerate with same seed)
	std::mt19937 rng(42);
	std::uniform_real_distribution<float> posDist(-100.0f, 100.0f);
	std::uniform_real_distribution<float> scaleDist(0.4f, 1.5f);
	std::uniform_real_distribution<float> rotDist(0.0f, 6.28318530718f);
	// Per-blade lean deviation: random angle within ±m_InitLeanDeviation around wind direction
	// NOTE: do NOT add an extra rng draw here — it would break sync with CreateInstanceBuffer
	//  (both functions use the same seed-42 sequence: posX, posZ, scale, rot per instance).
	//  Instead we remap rot ∈ [0,2π] → lean deviation ∈ [-initLeanDeviation, +initLeanDeviation].
	const float windAngle = atan2f(m_InitWindDir.y, m_InitWindDir.x);
	const float twoPi     = 6.28318530718f;

	for (uint32_t i = 0; i < m_InstanceCount; ++i)
	{
		// Explicit sequential draws — must match CreateInstanceBuffer and
		// CreateSubBladeRootsBuffer exactly (draw1 → X, draw2 → Z).
		float bpx = posDist(rng);
		float bpz = posDist(rng);
		glm::vec3 pos(bpx, 0.0f, bpz);
		float scale = scaleDist(rng);
		float rot   = rotDist(rng);   // same draw as CreateInstanceBuffer — RNG stays in sync

		// Map rot uniformly over [0,2π] → deviation ∈ [-m_InitLeanDeviation, +m_InitLeanDeviation].
		// (rot/twoPi) ∈ [0,1), remapped to [-1,+1] then scaled by the deviation limit.
		float deviation = (rot / twoPi * 2.0f - 1.0f) * m_InitLeanDeviation;
		float leanAngle = windAngle + deviation;
		glm::vec3 leanDir = glm::normalize(glm::vec3(cosf(leanAngle), 0.0f, sinf(leanAngle)));

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
					glm::vec3(0.0f, 1.0f, 0.0f) * cosf(glm::radians(5.0f)) +
					leanDir * sinf(glm::radians(5.0f)));
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
					glm::vec3(0.0f, 1.0f, 0.0f) * cosf(tiltAngle) +
					leanDir * sinf(tiltAngle));

				bones[idx].restOffset = glm::vec4(restDir * segLength, 0.0f);

				// Place bone along the pre-bent arc
				accumPos += restDir * segLength;
				bones[idx].position  = glm::vec4(accumPos, 1.0f);
				bones[idx].velocity  = glm::vec4(accumPos, 0.0f); // prevPos = pos
			}
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

	// Pre-fill with identity matrices so that the first rendered frame (before the
	// first compute dispatch) shows blades in rest-pose instead of at position (0,0,0).
	std::vector<glm::mat4> identities(totalMatrices, glm::mat4(1.0f));

	m_BoneMatrixBuffer.CreatVulkanBuffer(device, bufferSize, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	m_BoneMatrixBuffer.SetBufferData(identities.data(), 0, static_cast<int>(bufferSize));
}

// (CreateSkinnedBuffers and CreateIndirectDrawBuffer removed — skinning
//  moved to vertex shader; indirect draw buffers are per-config now.)

// ============================================================================
// CreateLodFactorsBuffer — one float per instance, written by bone sim
// ============================================================================
void VansVegetationSystem::CreateLodFactorsBuffer(VkDevice device)
{
	VkDeviceSize bufferSize = sizeof(float) * m_InstanceCount;
	m_LodFactorsBuffer.CreatVulkanBuffer(device, bufferSize, VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

// ============================================================================
// CreateSubBladeRootsBuffer
//
// Each main instance (bone chain) owns m_SubBladeCount root positions that
// form a small visual tuft.  Sub-blade 0 is the main root (same XZ as the
// instance position).  Sub-blades 1..N-1 are scattered within a short radius.
// Y is initialised to 0 here; the bone-sim compute shader writes the correct
// terrain-snapped Y value every frame.
// ============================================================================
void VansVegetationSystem::CreateSubBladeRootsBuffer(VkDevice device)
{
	const uint32_t total = m_InstanceCount * m_SubBladeCount;
	std::vector<glm::vec4> roots(total, glm::vec4(0.0f));

	// Reproduce the exact same instance XZ positions as CreateInstanceBuffer()
	// and CreateBoneBuffer() so all three buffers stay in sync.
	std::mt19937 rng(42);
	std::uniform_real_distribution<float> posDist(-100.0f, 100.0f);
	std::uniform_real_distribution<float> scaleDist(0.4f, 1.5f);
	std::uniform_real_distribution<float> rotDist(0.0f, 6.28318530718f);

	// Separate RNG for sub-blade scatter so it doesn't affect the main sequence.
	std::mt19937 rngTuft(137);
	std::uniform_real_distribution<float> radiusDist(m_SubBladeScatterRadiusMin, m_SubBladeScatterRadiusMax);
	std::uniform_real_distribution<float> angleDist(0.0f, 6.28318530718f);

	for (uint32_t i = 0; i < m_InstanceCount; ++i)
	{
		float px = posDist(rng);
		float pz = posDist(rng);
		(void)scaleDist(rng);  // consume to stay in sync
		(void)rotDist(rng);

		// Sub-blade 0 = main blade root (terrain Y corrected by bone sim)
		roots[i * m_SubBladeCount + 0] = glm::vec4(px, 0.0f, pz, 1.0f);

		// Sub-blades 1..N-1 = nearby positions forming a visual tuft
		for (uint32_t s = 1; s < m_SubBladeCount; ++s)
		{
			float r   = radiusDist(rngTuft);
			float ang = angleDist(rngTuft);
			roots[i * m_SubBladeCount + s] = glm::vec4(
				px + r * cosf(ang),
				0.0f,
				pz + r * sinf(ang),
				1.0f);
		}
	}

	VkDeviceSize bufferSize = sizeof(glm::vec4) * total;
	// HOST_VISIBLE so CPU can upload initial XZ; GPU reads/writes Y each frame.
	m_SubBladeRootsBuffer.CreatVulkanBuffer(device, bufferSize, VK_FORMAT_R32G32B32A32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_SubBladeRootsBuffer.SetBufferData(roots.data(), 0, static_cast<int>(bufferSize));

	VANS_LOG("[VegetationSystem] Sub-blade roots buffer: " << m_InstanceCount
		<< " instances \xc3\x97 " << m_SubBladeCount << " sub-blades = " << total << " roots");
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
		VansDescriptorSetLayoutFactory::CreateAndAllocate_VegetationDraw(m_VegDrawLayout, unused);
		// We only need the layout handle; per-config sets are allocated individually.
	}

	WriteBoneSimDescriptors();
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

	// Terrain heightmap (binding 3) — always write a valid descriptor
	if (m_TerrainEnabled && m_TerrainHeightmapView != VK_NULL_HANDLE && m_TerrainHeightmapSampler != VK_NULL_HANDLE)
	{
		descMgr->m_ImageDescInfos.push_back({
			m_BoneSimDescSets[0], VEG_SIM_BINDING_TERRAIN_HEIGHTMAP, 0,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ m_TerrainHeightmapSampler, m_TerrainHeightmapView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }}
			});
	}

	// LOD factors buffer (binding 4)
	descMgr->m_BufferDescInfos.push_back({
		m_BoneSimDescSets[0], VEG_SIM_BINDING_LOD_FACTORS, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_LodFactorsBuffer.GetNativeBuffer(), 0, m_LodFactorsBuffer.GetBufferSize() }}
		});

	// Sub-blade roots buffer (binding 5) — bone sim writes terrain-snapped Y each frame
	descMgr->m_BufferDescInfos.push_back({
		m_BoneSimDescSets[0], VEG_SIM_BINDING_SUB_BLADE_ROOTS, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_SubBladeRootsBuffer.GetNativeBuffer(), 0, m_SubBladeRootsBuffer.GetBufferSize() }}
		});

	descMgr->UpdateDescriptorSets();
}

// (WriteSkinningDescriptors removed — skinning moved to vertex shader)

void VansVegetationSystem::WriteDrawDescriptors(GrassRenderConfigGPU& cfg)
{
	auto* descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->ResetState();

	// Binding 0: Bone matrices (compute output, VS reads)
	descMgr->m_BufferDescInfos.push_back({
		cfg.drawDescSet, VEG_DRAW_BINDING_BONE_MATRICES, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_BoneMatrixBuffer.GetNativeBuffer(), 0, m_BoneMatrixBuffer.GetBufferSize() }}
	});

	// Binding 1: Bone weights (static, per-vertex)
	descMgr->m_BufferDescInfos.push_back({
		cfg.drawDescSet, VEG_DRAW_BINDING_BONE_WEIGHTS, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ cfg.boneWeightBuffer.GetNativeBuffer(), 0, cfg.boneWeightBuffer.GetBufferSize() }}
	});

	// Binding 2: Instance remap (uint indices into global instance/bone arrays)
	descMgr->m_BufferDescInfos.push_back({
		cfg.drawDescSet, VEG_DRAW_BINDING_INSTANCE_REMAP, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ cfg.instanceRemapBuffer.GetNativeBuffer(), 0, cfg.instanceRemapBuffer.GetBufferSize() }}
	});

	// Binding 3: Sub-blade roots
	descMgr->m_BufferDescInfos.push_back({
		cfg.drawDescSet, VEG_DRAW_BINDING_SUB_BLADE_ROOTS, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_SubBladeRootsBuffer.GetNativeBuffer(), 0, m_SubBladeRootsBuffer.GetBufferSize() }}
	});

	// Binding 4: LOD factors
	descMgr->m_BufferDescInfos.push_back({
		cfg.drawDescSet, VEG_DRAW_BINDING_LOD_FACTORS, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_LodFactorsBuffer.GetNativeBuffer(), 0, m_LodFactorsBuffer.GetBufferSize() }}
	});

	// Binding 5: Instance data (positions, rotations, etc.)
	descMgr->m_BufferDescInfos.push_back({
		cfg.drawDescSet, VEG_DRAW_BINDING_INSTANCE_DATA, 0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{ m_InstanceBuffer.GetNativeBuffer(), 0, m_InstanceBuffer.GetBufferSize() }}
	});

	descMgr->UpdateDescriptorSets();
}

// ============================================================================
// Update — dispatches bone sim compute pass (skinning now in vertex shader)
// ============================================================================
void VansVegetationSystem::Update(VansVKCommandBuffer& computeCmd, float deltaTime, float time,
                                   const glm::vec2& windDirection, float windStrength,
                                   float windFrequency, float windSpeed, float windBendMult,
                                   float stiffness, float damping,
                                   float softness, float lodFullDist, float lodFadeDist)
{
	if (!m_BoneSimShader || m_BoneSimDescSets.empty())
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
	simPC.windSpeed     = windSpeed;
	simPC.windBendMult  = windBendMult;
	simPC.windDirX      = windDirection.x;
	simPC.windDirY      = windDirection.y;
	simPC.stiffness     = stiffness;
	simPC.damping       = damping;
	simPC.softness      = softness;
	simPC.terrainSize        = m_TerrainSize;
	simPC.terrainMaxHeight   = m_TerrainMaxHeight;
	simPC.terrainHeightOffset = m_TerrainHeightOffset;
	simPC.terrainEnabled     = m_TerrainEnabled ? 1 : 0;
	simPC.lodFullDist        = lodFullDist;
	simPC.lodFadeDist        = lodFadeDist;
	simPC.subBladeCount      = static_cast<int>(m_SubBladeCount);
	simPC.grassHeight        = m_BladeHeight;
	simPC.boneCount          = m_BoneCountPerInstance;

	m_BoneSimShader->SetPushConstantData(&simPC);

	computeCmd.EnsureComputeShader(*m_BoneSimShader, { m_GlobalDescSetLayout, m_BoneSimLayout });

	uint32_t simGroupsX = (m_InstanceCount + 63) / 64;
	computeCmd.DispatchCompute(*m_BoneSimShader, simGroupsX, 1, 1, { m_GlobalDescSet, m_BoneSimDescSets[0] });

	// ── Barrier: bone sim compute write → vertex shader read ────────
	VkMemoryBarrier simToDrawBarrier = {};
	simToDrawBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	simToDrawBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	simToDrawBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	computeCmd.PipelineBarrier(
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
		{ simToDrawBarrier });
}

// ============================================================================
// Draw — issues one indirect indexed draw call per render config
// ============================================================================
void VansVegetationSystem::Draw(VansVKCommandBuffer& graphicsCmd, VansGraphicsShader& shader,
                                 GlobalStateData& globalState,
                                 const std::vector<VkDescriptorSetLayout>& baseDescSetLayouts,
                                 const std::vector<VkDescriptorSet>& baseDescSets,
                                 int pushConstantTransformIndex)
{
	if (m_RenderConfigsGPU.empty()) return;

	for (auto& cfg : m_RenderConfigsGPU)
	{
		if (cfg.assignedInstanceCount == 0) continue;

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
		if (cfg.material)
		{
			auto* grassMat = dynamic_cast<VansGrassMaterial*>(cfg.material);
			if (grassMat && grassMat->m_GrassOwnedLayout != VK_NULL_HANDLE && !grassMat->m_GrassOwnedDescSets.empty())
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
			pc.animationEnabled = 1;
			pc.boneCount        = m_BoneCountPerInstance;
			pc.subBladeCount    = m_SubBladeCount;
			pc.grassHeight      = m_BladeHeight;
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

// ============================================================================
// Cleanup
// ============================================================================
void VansVegetationSystem::Cleanup(VkDevice device)
{
	m_InstanceBuffer.DestroyVulkanBuffer(device);
	m_BoneBuffer.DestroyVulkanBuffer(device);
	m_BoneMatrixBuffer.DestroyVulkanBuffer(device);
	if (m_TemplateMesh)
	{
		delete m_TemplateMesh;
		m_TemplateMesh = nullptr;
	}
	m_LodFactorsBuffer.DestroyVulkanBuffer(device);
	m_SubBladeRootsBuffer.DestroyVulkanBuffer(device);

	// Per-config buffers
	for (auto& cfg : m_RenderConfigsGPU)
	{
		cfg.boneWeightBuffer.DestroyVulkanBuffer(device);
		cfg.instanceRemapBuffer.DestroyVulkanBuffer(device);
		cfg.indirectDrawBuffer.DestroyVulkanBuffer(device);
	}
	m_RenderConfigsGPU.clear();

	if (m_BoneSimShader)
	{
		delete m_BoneSimShader;
		m_BoneSimShader = nullptr;
	}
}

// ============================================================================
// BuildRenderConfigs — partition instances across configs, create GPU resources
// ============================================================================
void VansVegetationSystem::BuildRenderConfigs(
	std::function<VansMesh*(const std::string&)> meshLookup,
	std::function<VansMaterial*(const std::string&)> materialLookup)
{
	// If no render configs were set, create a single default (procedural blade mesh)
	if (m_RenderConfigs.empty())
	{
		GrassRenderConfig defaultCfg;
		defaultCfg.meshName     = "";
		defaultCfg.materialName = "";
		defaultCfg.percent      = 1.0f;
		m_RenderConfigs.push_back(defaultCfg);
	}

	// Normalise percentages so they sum to 1.0
	float totalPercent = 0.0f;
	for (auto& rc : m_RenderConfigs) totalPercent += rc.percent;
	if (totalPercent > 0.0f)
		for (auto& rc : m_RenderConfigs) rc.percent /= totalPercent;

	// Shuffle instance indices for random distribution among configs
	std::vector<uint32_t> instanceIndices(m_InstanceCount);
	std::iota(instanceIndices.begin(), instanceIndices.end(), 0u);
	std::mt19937 rng(1234); // deterministic shuffle
	std::shuffle(instanceIndices.begin(), instanceIndices.end(), rng);

	// Partition indices according to percent
	uint32_t assignedSoFar = 0;
	m_RenderConfigsGPU.resize(m_RenderConfigs.size());

	for (size_t i = 0; i < m_RenderConfigs.size(); ++i)
	{
		auto& rc  = m_RenderConfigs[i];
		auto& cfg = m_RenderConfigsGPU[i];

		uint32_t count = (i == m_RenderConfigs.size() - 1)
			? (m_InstanceCount - assignedSoFar)                      // last config gets remainder
			: static_cast<uint32_t>(rc.percent * m_InstanceCount);
		if (assignedSoFar + count > m_InstanceCount)
			count = m_InstanceCount - assignedSoFar;

		cfg.assignedInstanceCount = count;

		// ── Resolve mesh ────────────────────────────────────────────
		// External mesh from asset, or fall back to the procedural template blade.
		VansMesh* mesh = nullptr;
		if (!rc.meshName.empty() && meshLookup)
			mesh = meshLookup(rc.meshName);

		cfg.mesh = mesh ? mesh : m_TemplateMesh;

		// ── Resolve material ────────────────────────────────────────
		if (!rc.materialName.empty() && materialLookup)
		{
			cfg.material = materialLookup(rc.materialName);
			// Grass materials don't have m_MaterialIndex (that's PBR-only).
			// cfg.materialIndex stays at its default (the config index).
			if (cfg.material)
				cfg.materialIndex = static_cast<int>(i);
		}

		// ── Instance remap buffer ───────────────────────────────────
		{
			std::vector<uint32_t> remap(count);
			for (uint32_t k = 0; k < count; ++k)
				remap[k] = instanceIndices[assignedSoFar + k];

			VkDeviceSize sz = sizeof(uint32_t) * std::max(count, 1u);
			cfg.instanceRemapBuffer.CreatVulkanBuffer(m_Device, sz, VK_FORMAT_R32_UINT,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			if (count > 0)
				cfg.instanceRemapBuffer.SetBufferData(remap.data(), 0, static_cast<int>(sizeof(uint32_t) * count));
		}

		// ── Bone weight buffer ──────────────────────────────────────
		GenerateBoneWeights(cfg, cfg.mesh);

		// ── Indirect draw buffer ────────────────────────────────────
		{
			VkDrawIndexedIndirectCommand cmd = {};
			cmd.indexCount    = cfg.mesh->GetIndexCount();
			cmd.instanceCount = count * m_SubBladeCount;
			cmd.firstIndex    = 0;
			cmd.vertexOffset  = 0;
			cmd.firstInstance = 0;

			VkDeviceSize sz = sizeof(VkDrawIndexedIndirectCommand);
			cfg.indirectDrawBuffer.CreatVulkanBuffer(m_Device, sz, VK_FORMAT_R32_UINT,
				VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			cfg.indirectDrawBuffer.SetBufferData(&cmd, 0, static_cast<int>(sz));
		}

		// ── Allocate per-config descriptor set (Set 3) ──────────────
		{
			std::vector<VkDescriptorSet> sets;
			VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_VegDrawLayout }, sets);
			cfg.drawDescSet = sets.empty() ? VK_NULL_HANDLE : sets[0];
		}

		// ── Write descriptor set ────────────────────────────────────
		WriteDrawDescriptors(cfg);

		assignedSoFar += count;

		VANS_LOG("[VegetationSystem] RenderConfig[" << i << "]: "
			<< (mesh ? rc.meshName : std::string("procedural"))
			<< ", material=" << rc.materialName
			<< ", instances=" << count
			<< " (" << (rc.percent * 100.0f) << "%)");
	}

	VANS_LOG("[VegetationSystem] BuildRenderConfigs done: " << m_RenderConfigsGPU.size()
		<< " configs, " << assignedSoFar << "/" << m_InstanceCount << " instances assigned.");
}

// ============================================================================
// GenerateBoneWeights — for external meshes: map vertex Y → dual-bone weights
//
// Each vertex gets: vec4(boneIdx0, boneIdx1, weight0, weight1)
// Y is normalised to [0..1] over the mesh AABB, then mapped to bone segments.
// ============================================================================
void VansVegetationSystem::GenerateBoneWeights(GrassRenderConfigGPU& cfg, VansMesh* mesh)
{
	const auto& rawPos = mesh->GetMeshRawPositionData(); // flat float array: x,y,z,x,y,z,...
	uint32_t vertCount = mesh->GetMeshVertexCount();
	if (rawPos.empty() || vertCount == 0)
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
	for (uint32_t i = 0; i < vertCount && (i * 3 + 1) < rawPos.size(); ++i)
	{
		float y = rawPos[i * 3 + 1];
		yMin = std::min(yMin, y);
		yMax = std::max(yMax, y);
	}
	float yRange = (yMax - yMin);
	if (yRange < 1e-6f) yRange = 1.0f;

	uint32_t segments = m_BoneCountPerInstance - 1;
	std::vector<glm::vec4> weights(vertCount);

	for (uint32_t i = 0; i < vertCount && (i * 3 + 1) < rawPos.size(); ++i)
	{
		float y = rawPos[i * 3 + 1];
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
// SetTerrainHeightmap — connects terrain height data for ground placement
// ============================================================================
void VansVegetationSystem::SetTerrainHeightmap(VkImageView imageView, VkSampler sampler,
                                                float terrainSize, float maxHeight, float heightOffset)
{
	m_TerrainHeightmapView    = imageView;
	m_TerrainHeightmapSampler = sampler;
	m_TerrainSize             = terrainSize;
	m_TerrainMaxHeight        = maxHeight;
	m_TerrainHeightOffset     = heightOffset;
	m_TerrainEnabled          = (imageView != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE);

	// Re-write bone sim descriptors to include the terrain heightmap
	if (m_TerrainEnabled && !m_BoneSimDescSets.empty())
	{
		WriteBoneSimDescriptors();
	}

	VANS_LOG("[VegetationSystem] Terrain heightmap " << (m_TerrainEnabled ? "enabled" : "disabled")
		<< " (size=" << terrainSize << ", maxH=" << maxHeight << ", offset=" << heightOffset << ")");
}
