#pragma once

#include "../VulkanCore/VansVKBuffer.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>
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
	// Layout matches GLSL std430 exactly:
	//   vec3 'position' has base alignment 16 but only occupies 12 bytes;
	//   the following float 'scale' is placed at the next multiple of 4 → offset 12.
	//   No padding is inserted between position and scale in std430.
	struct GrassInstance
	{
		glm::vec3 position;     // world XZ, Y = ground  (bytes  0-11)
		float     scale;        // random in [0.8, 1.2]   (bytes 12-15)
		float     rotation;     // random in [0, 2π]        (bytes 16-19)
		uint32_t  padding[3];   // align to 32 bytes      (bytes 20-31)
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
		uint32_t boneCount;    // bones per instance — must match m_BoneCountPerInstance
	};

	// Push constants for VS-skinning draw (grass vertex shader)
	struct GrassDrawPushConstants
	{
		int   materialIndex;
		int   objectIndex;
		int   animationEnabled;
		uint32_t boneCount;
		uint32_t subBladeCount;
		float    grassHeight;
	};

	// Lightweight vertex used by the procedural grass blade template mesh.
	// Matches the shader attribute layout: vec3 pos (loc 0), vec3 nrm (loc 1), vec2 uv (loc 2).
	struct GrassVertex
	{
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 uv;
	};

	// ========================================================================
	// Render config — one entry from JSON "renderConfigs" array.
	// Each config maps to one draw call with a specific mesh + material.
	// ========================================================================
	struct GrassRenderConfig
	{
		std::string meshName;       // empty = use procedural blade
		std::string materialName;
		float       percent = 1.0f; // fraction of total instances [0,1]
	};

	class VansMesh;
	class VansMaterial;

	// Per-config GPU resources
	struct GrassRenderConfigGPU
	{
		// Mesh — always valid; points to the system's template blade or an external mesh.
		VansMesh* mesh = nullptr;

		// Static bone-weight SSBO — one vec4 per vertex: (boneIdx0, boneIdx1, w0, w1)
		VansVKBuffer  boneWeightBuffer;

		// Instance remap buffer — uint[] maps [0, assignedCount) → global bone chain index
		VansVKBuffer  instanceRemapBuffer;
		uint32_t      assignedInstanceCount = 0;

		// Indirect draw command
		VansVKBuffer  indirectDrawBuffer;

		// Descriptor set for draw pass (Set 3)
		VkDescriptorSet drawDescSet = VK_NULL_HANDLE;

		// Material info (resolved at load)
		int materialIndex = -1;
		VansMaterial* material = nullptr;
	};

	class VansVegetationSystem
	{
	public:
		VansVegetationSystem() = default;
		~VansVegetationSystem();

		// ── Initialisation ──────────────────────────────────────────────
		void Init(VkDevice device, uint32_t instanceCount = 2000000,
		          uint32_t boneCountPerInstance = 6);

		// ── Render configs — call before Init() if JSON has renderConfigs ──
		void SetRenderConfigs(const std::vector<GrassRenderConfig>& configs) { m_RenderConfigs = configs; }

		// ── Build per-config GPU resources (call after Init and after meshes & materials are loaded) ──
		// meshLookup / materialLookup: callables that resolve name → pointer.
		// For procedural-blade configs (empty meshName), system's own buffers are used.
		void BuildRenderConfigs(
			std::function<VansMesh*(const std::string&)> meshLookup,
			std::function<VansMaterial*(const std::string&)> materialLookup);

		// ── Per-frame update: dispatches bone sim compute pass ─────────
		void Update(VansVKCommandBuffer& computeCmd, float deltaTime, float time,
		            const glm::vec2& windDirection = glm::vec2(1.0f, 0.0f),
		            float windStrength = 4.0f, float windFrequency = 0.5f,
		            float windSpeed = 1.5f, float windBendMult = 5.0f,
		            float stiffness = 15.0f, float damping = 0.92f,
		            float softness = 0.2f,
		            float lodFullDist = 15.0f, float lodFadeDist = 20.0f);

		// ── Draw: issues one indirect indexed draw per render config ───
		void Draw(VansVKCommandBuffer& graphicsCmd, VansGraphicsShader& shader,
		          GlobalStateData& globalState,
		          const std::vector<VkDescriptorSetLayout>& baseDescSetLayouts,
		          const std::vector<VkDescriptorSet>& baseDescSets,
		          int pushConstantTransformIndex);

		// ── Cleanup ────────────────────────────────────────────────────
		void Cleanup(VkDevice device);

		// ── Terrain Integration ────────────────────────────────────────
		void SetTerrainHeightmap(VkImageView imageView, VkSampler sampler,
		                         float terrainSize, float maxHeight, float heightOffset);

		// ── Blade height — must be set before Init() ──────────────────────
		void SetBladeHeight(float h) { m_BladeHeight = h; }

		// ── Sub-blade tuft config — must be set before Init() ────────────────
		void SetSubBladeParams(uint32_t count, float radiusMin, float radiusMax)
		{
			m_SubBladeCount            = count;
			m_SubBladeScatterRadiusMin = radiusMin;
			m_SubBladeScatterRadiusMax = radiusMax;
		}

		// ── Runtime simulation parameters (updated from JSON at load time) ─────
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

		// ── Initial lean direction (must be set before Init()) ─────────
		void SetInitWindDirection(glm::vec2 dir, float deviationDeg = 35.0f)
		{
			m_InitWindDir       = glm::length(dir) > 0.0001f ? glm::normalize(dir) : glm::vec2(1.0f, 0.0f);
			m_InitLeanDeviation = glm::radians(deviationDeg);
		}

		// ── Global camera descriptor set (set=0 in bone sim compute) ────
		void SetGlobalDescriptorSet(VkDescriptorSetLayout layout, VkDescriptorSet set)
		{
			m_GlobalDescSetLayout = layout;
			m_GlobalDescSet       = set;
		}

		// ── Accessors ──────────────────────────────────────────────────
		VansVKBuffer& GetInstanceBuffer()         { return m_InstanceBuffer; }
		uint32_t GetInstanceCount() const         { return m_InstanceCount; }
		uint32_t GetBoneCountPerInstance() const  { return m_BoneCountPerInstance; }
		uint32_t GetVertexCount() const           { return m_VertexCount; }
		uint32_t GetIndexCount() const            { return m_IndexCount; }
		uint32_t GetSubBladeCount() const         { return m_SubBladeCount; }
		float    GetBladeHeight() const           { return m_BladeHeight; }

		// ── Per-config GPU data (for render node to iterate) ──────────
		const std::vector<GrassRenderConfigGPU>& GetRenderConfigsGPU() const { return m_RenderConfigsGPU; }

		// Bone sim descriptor sets (used by render node)
		VkDescriptorSetLayout m_BoneSimLayout      = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_BoneSimDescSets;

		// Descriptor set layout for draw pass (Set 3: per-config)
		VkDescriptorSetLayout m_VegDrawLayout      = VK_NULL_HANDLE;

		VkDescriptorSetLayout GetVegDrawLayout() const { return m_VegDrawLayout; }

	private:
		// ── Init helpers ────────────────────────────────────────────────
		void CreateTemplateMesh(VkDevice device);
		void CreateInstanceBuffer(VkDevice device);
		void CreateBoneBuffer(VkDevice device);
		void CreateBoneMatrixBuffer(VkDevice device);
		void CreateLodFactorsBuffer(VkDevice device);
		void CreateSubBladeRootsBuffer(VkDevice device);
		void CreateDescriptorSets();
		void LoadComputeShaders(VkDevice device);
		void WriteBoneSimDescriptors();

		// ── Per-config helpers ──────────────────────────────────────────
		void GenerateBoneWeights(GrassRenderConfigGPU& cfg, VansMesh* mesh);
		void WriteDrawDescriptors(GrassRenderConfigGPU& cfg);

		// ── Configuration ───────────────────────────────────────────────
		uint32_t m_InstanceCount        = 0;
		uint32_t m_BoneCountPerInstance = 0;
		uint32_t m_VertexCount          = 0;     // procedural blade vertex count
		uint32_t m_IndexCount           = 0;     // procedural blade index count
		float    m_BladeHeight          = 0.5f;
		float    m_BladeWidthRoot       = 0.04f;
		uint32_t m_SubBladeCount        = 10;
		float    m_SubBladeScatterRadiusMin = 0.3f;
		float    m_SubBladeScatterRadiusMax = 1.0f;
		glm::vec2 m_InitWindDir         = glm::vec2(1.0f, 0.0f);
		float     m_InitLeanDeviation   = glm::radians(35.0f);

		// ── Per-frame simulation parameters ─────────────────────────────
		glm::vec2 m_SimWindDirection  = glm::vec2(1.0f, 0.0f);
		float     m_SimWindStrength   = 4.0f;
		float     m_SimWindFrequency  = 0.5f;
		float     m_SimWindSpeed      = 1.5f;
		float     m_SimWindBendMult   = 5.0f;
		float     m_SimStiffness      = 15.0f;
		float     m_SimDamping        = 0.92f;
		float     m_SimSoftness       = 0.2f;
		float     m_SimLodFullDist    = 15.0f;
		float     m_SimLodFadeDist    = 20.0f;

		// ── Render configs ──────────────────────────────────────────────
		std::vector<GrassRenderConfig>    m_RenderConfigs;
		std::vector<GrassRenderConfigGPU> m_RenderConfigsGPU;

		// ── GPU Buffers ─────────────────────────────────────────────────
		VansVKBuffer m_InstanceBuffer;
		VansVKBuffer m_BoneBuffer;
		VansVKBuffer m_BoneMatrixBuffer;
		VansVKBuffer m_LodFactorsBuffer;
		VansVKBuffer m_SubBladeRootsBuffer;

		// ── Procedural template blade mesh (owned by this system) ───────
		VansMesh* m_TemplateMesh = nullptr;

		// ── Compute Shaders ─────────────────────────────────────────────
		VansComputeShader* m_BoneSimShader = nullptr;

		// ── Global descriptor set (camera UBO, set=0 in bone sim compute) ────
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
