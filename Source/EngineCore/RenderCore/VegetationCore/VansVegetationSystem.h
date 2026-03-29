#pragma once

#include "../VulkanCore/VansVKBuffer.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace VansGraphics
{
	// ========================================================================
	// GPU-driven vegetation rendering system.
	//
	// Shared across all vegetation types (grass, brush, tree, etc.).
	// - Owns all GPU buffers for instanced vegetation
	// - Dispatches bone simulation + vertex skinning compute passes
	// - Issues indirect draw calls into the GBuffer
	// ========================================================================

	// Per-instance data uploaded once at init
	struct GrassInstance
	{
		glm::vec3 position;     // world XZ, Y = ground
		float     scale;        // random in [0.8, 1.2]
		float     rotation;     // random in [0, 2π]
		uint32_t  padding[3];   // align to 32 bytes
	};

	// Per-bone simulation state (GPU only after init)
	struct GrassBone
	{
		glm::vec4 position;     // world-space joint position
		glm::vec4 velocity;     // for Verlet integration (prevPosition)
		glm::vec4 restOffset;   // rest-pose offset from parent
	};

	// Push constants for bone simulation compute
	struct GrassSimPushConstants
	{
		float deltaTime;
		float time;
		float windStrength;
		float windFrequency;
		float windSpeed;       // animation rate of wind pattern (scrolling speed of noise)
		float windBendMult;    // overall bend amplification multiplier
		float windDirX;
		float windDirY;
		float stiffness;
		float damping;
		float softness;        // 0.0 = rigid (hard), 1.0 = fully soft
		float terrainSize;
		float terrainMaxHeight;
		float terrainHeightOffset;
		int   terrainEnabled;
		// LOD distances (camera position comes from global CameraData UBO, not push constant)
		float lodFullDist;
		float lodFadeDist;
		int   subBladeCount;   // number of sub-blades per main instance (matches m_SubBladeCount)
		float grassHeight;     // blade height in world units (controls segment length)
	};

	// Push constants for skinning compute
	struct GrassSkinningPushConstants
	{
		uint32_t instanceCount;   // main instance count (bone chains)
		uint32_t vertexCount;
		uint32_t boneCount;
		float    grassHeight;
		uint32_t subBladeCount;   // sub-blades per instance; skinning emits instanceCount*subBladeCount blades
	};

	// Template vertex for the grass blade mesh
	struct GrassVertex
	{
		glm::vec4 position;     // xyz = position, w = 0
		glm::vec4 normal;       // xyz = normal,   w = 0
		glm::vec2 uv;
		glm::vec2 padding;
	};

	class VansVegetationSystem
	{
	public:
		VansVegetationSystem() = default;
		~VansVegetationSystem();

		// ── Initialisation ──────────────────────────────────────────────
		void Init(VkDevice device, uint32_t instanceCount = 2000000,
		          uint32_t boneCountPerInstance = 6);

		// ── Per-frame update: dispatches compute passes ────────────────
		void Update(VansVKCommandBuffer& computeCmd, float deltaTime, float time,
		            const glm::vec2& windDirection = glm::vec2(1.0f, 0.0f),
		            float windStrength = 4.0f, float windFrequency = 0.5f,
		            float windSpeed = 1.5f, float windBendMult = 5.0f,
		            float stiffness = 15.0f, float damping = 0.92f,
		            float softness = 0.2f,
		            float lodFullDist = 15.0f, float lodFadeDist = 20.0f);

		// ── Draw: issues indirect indexed draw ─────────────────────────
		void Draw(VansVKCommandBuffer& graphicsCmd, VansGraphicsShader& shader,
		          GlobalStateData& globalState,
		          const std::vector<VkDescriptorSetLayout>& descSetLayouts,
		          const std::vector<VkDescriptorSet>& descSets,
		          int pushConstantMaterialIndex, int pushConstantTransformIndex);

		// ── Cleanup ────────────────────────────────────────────────────
		void Cleanup(VkDevice device);

		// ── Terrain Integration ────────────────────────────────────────
		// Call after Init() and before first Update() to enable terrain height sampling.
		// The heightmap image must already be in SHADER_READ_ONLY_OPTIMAL layout.
		void SetTerrainHeightmap(VkImageView imageView, VkSampler sampler,
		                         float terrainSize, float maxHeight, float heightOffset);

		// ── Blade height — must be set before Init() ──────────────────────
		void SetBladeHeight(float h) { m_BladeHeight = h; }

		// ── Sub-blade tuft config — must be set before Init() ────────────────
		// count: how many blades share each bone chain (higher = denser tufts).
		// radiusMin/Max: XZ scatter range of sub-blades around the main root (world units).
		void SetSubBladeParams(uint32_t count, float radiusMin, float radiusMax)
		{
			m_SubBladeCount            = count;
			m_SubBladeScatterRadiusMin = radiusMin;
			m_SubBladeScatterRadiusMax = radiusMax;
		}
		// ── Runtime simulation parameters (updated from JSON at load time) ────────────
		// Call after Init() to override the per-frame defaults used by RecordVegetationCompute.
		void SetSimParams(glm::vec2 windDir, float windStrength, float windFrequency,
		                  float windSpeed, float windBendMult,
		                  float stiffness, float damping, float softness,
		                  float lodFullDist, float lodFadeDist)
		{
			m_SimWindDirection = windDir;
			m_SimWindStrength  = windStrength;
			m_SimWindFrequency = windFrequency;
			m_SimWindSpeed     = windSpeed;
			m_SimWindBendMult  = windBendMult;
			m_SimStiffness     = stiffness;
			m_SimDamping       = damping;
			m_SimSoftness      = softness;
			m_SimLodFullDist   = lodFullDist;
			m_SimLodFadeDist   = lodFadeDist;
		}

		// Accessors for stored sim params (used by RecordVegetationCompute)
		const glm::vec2& GetWindDirection()  const { return m_SimWindDirection; }
		float GetWindStrength()   const { return m_SimWindStrength; }
		float GetWindFrequency()  const { return m_SimWindFrequency; }
		float GetWindSpeed()      const { return m_SimWindSpeed; }
		float GetWindBendMult()   const { return m_SimWindBendMult; }
		float GetStiffness()      const { return m_SimStiffness; }
		float GetDamping()        const { return m_SimDamping; }
		float GetSoftness()       const { return m_SimSoftness; }
		float GetLodFullDist()    const { return m_SimLodFullDist; }
		float GetLodFadeDist()    const { return m_SimLodFadeDist; }
		// ── Initial lean direction (must be set before Init()) ─────────────
		// dir: XZ world-space wind direction (Y ignored).
		// deviationDeg: max per-blade random angular deviation from dir (e.g. 35 degrees).
		void SetInitWindDirection(glm::vec2 dir, float deviationDeg = 35.0f)
		{
			m_InitWindDir       = glm::length(dir) > 0.0001f ? glm::normalize(dir) : glm::vec2(1.0f, 0.0f);
			m_InitLeanDeviation = glm::radians(deviationDeg);
		}

		// ── Global camera descriptor set (set=0 in bone sim compute) ────
		// Call once after the scene's global descriptor set is ready (from the render node).
		void SetGlobalDescriptorSet(VkDescriptorSetLayout layout, VkDescriptorSet set)
		{
			m_GlobalDescSetLayout = layout;
			m_GlobalDescSet       = set;
		}

		// ── Accessors ──────────────────────────────────────────────────
		VansVKBuffer& GetInstanceBuffer()         { return m_InstanceBuffer; }
		VansVKBuffer& GetSkinnedVertexBuffer()    { return m_SkinnedVertexBuffer; }
		VansVKBuffer& GetSkinnedNormalBuffer()    { return m_SkinnedNormalBuffer; }
		VansVKBuffer& GetIndirectDrawBuffer()     { return m_IndirectDrawBuffer; }

		uint32_t GetInstanceCount() const         { return m_InstanceCount; }
		uint32_t GetBoneCountPerInstance() const  { return m_BoneCountPerInstance; }
		uint32_t GetVertexCount() const           { return m_VertexCount; }
		uint32_t GetIndexCount() const            { return m_IndexCount; }

		// Descriptor sets for the compute passes
		VkDescriptorSetLayout m_BoneSimLayout      = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_BoneSimDescSets;

		VkDescriptorSetLayout m_SkinningLayout     = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_SkinningDescSets;

		// Descriptor set for draw pass (Set 3: skinned SSBOs)
		VkDescriptorSetLayout m_VegDrawLayout      = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_VegDrawDescSets;

		VkDescriptorSetLayout GetVegDrawLayout() const { return m_VegDrawLayout; }
		VkDescriptorSet GetVegDrawDescSet() const { return m_VegDrawDescSets.empty() ? VK_NULL_HANDLE : m_VegDrawDescSets[0]; }

	private:
		// ── Init helpers ────────────────────────────────────────────────
		void CreateTemplateMesh(VkDevice device);
		void CreateInstanceBuffer(VkDevice device);
		void CreateBoneBuffer(VkDevice device);
		void CreateBoneMatrixBuffer(VkDevice device);
		void CreateSkinnedBuffers(VkDevice device);
		void CreateIndirectDrawBuffer(VkDevice device);
		void CreateLodFactorsBuffer(VkDevice device);
		void CreateSubBladeRootsBuffer(VkDevice device);  // sub-blade root positions (scatter around each main instance)
		void CreateDescriptorSets();
		void LoadComputeShaders(VkDevice device);
		void WriteBoneSimDescriptors();
		void WriteSkinningDescriptors();
		void WriteDrawDescriptors();

		// ── Configuration ───────────────────────────────────────────────
		uint32_t m_InstanceCount        = 0;
		uint32_t m_BoneCountPerInstance = 0;
		uint32_t m_VertexCount          = 0;
		uint32_t m_IndexCount           = 0;
		float    m_BladeHeight          = 0.5f;
		float    m_BladeWidthRoot              = 0.04f;
		uint32_t m_SubBladeCount               = 10;     // extra blades per simulated bone chain
		float    m_SubBladeScatterRadiusMin    = 0.15f;  // min XZ scatter radius for sub-blade tuft
		float    m_SubBladeScatterRadiusMax    = 0.45f;  // max XZ scatter radius for sub-blade tuft
		glm::vec2 m_InitWindDir        = glm::vec2(1.0f, 0.0f); // XZ wind dir for initial lean bias
		float     m_InitLeanDeviation  = glm::radians(35.0f);   // ± this many radians from wind dir
		// ── Per-frame simulation parameters (set via SetSimParams, read from JSON) ─────
		glm::vec2 m_SimWindDirection  = glm::vec2(1.0f, 0.0f);
		float     m_SimWindStrength   = 4.0f;   // strong default
		float     m_SimWindFrequency  = 0.5f;
		float     m_SimWindSpeed      = 1.5f;   // noise scroll rate
		float     m_SimWindBendMult   = 5.0f;   // bend amplification (was baked as 3.5)
		float     m_SimStiffness      = 15.0f;
		float     m_SimDamping        = 0.92f;
		float     m_SimSoftness       = 0.2f;
		float     m_SimLodFullDist    = 15.0f;
		float     m_SimLodFadeDist    = 20.0f;		// ── GPU Buffers ──────────────────────────────────────────────────
		VansVKBuffer m_InstanceBuffer;
		VansVKBuffer m_BoneBuffer;
		VansVKBuffer m_BoneMatrixBuffer;
		VansVKBuffer m_SkinnedVertexBuffer;
		VansVKBuffer m_SkinnedNormalBuffer;
		VansVKBuffer m_TemplateMeshBuffer;     // vertex data as SSBO for compute
		VansVKBuffer m_TemplateIndexBuffer;    // index buffer for draw
		VansVKBuffer m_TemplateVertexBuffer;   // vertex buffer for draw
		VansVKBuffer m_IndirectDrawBuffer;		VansVKBuffer m_LodFactorsBuffer;       // per-instance LOD factor (float)
		VansVKBuffer m_SubBladeRootsBuffer;    // sub-blade root positions — vec4 per (instance*subBladeCount)
		// ── Compute Shaders ──────────────────────────────────────────────
		VansComputeShader* m_BoneSimShader   = nullptr;
		VansComputeShader* m_SkinningShader  = nullptr;

		// ── Global descriptor set (camera UBO, set=0 in bone sim compute) ────────
		VkDescriptorSetLayout m_GlobalDescSetLayout = VK_NULL_HANDLE;
		VkDescriptorSet       m_GlobalDescSet       = VK_NULL_HANDLE;

		// ── Terrain heightmap (optional) ─────────────────────────────────
		VkImageView m_TerrainHeightmapView    = VK_NULL_HANDLE;
		VkSampler   m_TerrainHeightmapSampler = VK_NULL_HANDLE;
		float       m_TerrainSize             = 1024.0f;
		float       m_TerrainMaxHeight        = 500.0f;
		float       m_TerrainHeightOffset     = -23.0f;
		bool        m_TerrainEnabled          = false;

		VkDevice m_Device = VK_NULL_HANDLE;
	};
}
