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
#include <algorithm>
#include <functional>

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

	class VansMesh;
	class VansMaterial;

	// PCG 已确定完整变换；GPU 只模拟和绘制，不再生成或重新分配位置。
	struct GrassInstance
	{
		glm::mat4 modelMatrix = glm::mat4(1.0f);
		float boundsRadius = 0;
		uint32_t randomSeed = 0;
		uint32_t padding[2] = {};
	};
	static_assert(sizeof(GrassInstance) == 80, "Grass instance must match GLSL std430");
	static_assert(offsetof(GrassInstance, boundsRadius) == 64 && offsetof(GrassInstance, randomSeed) == 68,
		"Grass instance field offsets must match GLSL");

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




		// LOD distances (camera position comes from global CameraData UBO, not push constant)
		float lodFullDist;
		float lodFadeDist;
		int   subBladeCount;   // number of sub-blades per main instance (matches m_SubBladeCount)
		float grassHeight;     // blade height in world units (controls segment length)
		uint32_t cullVisibilityEnabled; // Grass cull 已产生本帧可见性掩码时为 1
		uint32_t boneCount;    // bones per instance — must match m_BoneCountPerInstance
	};

	// Push constants for VS-skinning draw (grass vertex shader)
	struct GrassDrawPushConstants
	{
		int   materialIndex;
		int   objectIndex;
		uint32_t vertexFeatureMask;
		uint32_t boneCount;
		uint32_t subBladeCount;
		float    grassHeight;
		// P6a: terrain params for VS heightmap sampling




		// P1: 子叶片距离 LOD — 远距离减少子叶片数以降低 VS/FS 开销
		float    lodMidDist;       // 中距离阈值，超过后子叶片数减半
		float    lodFarDist;       // 远距离阈值，超过后子叶片数降至最少
		// 材质 AO 与基于骨骼高度的根部微遮蔽参数（FS 使用）
		float    aoStrength;
		float    rootAOIntensity;
		float    rootAOHeight;
	};
	static_assert(sizeof(GrassDrawPushConstants) == 44, "Grass draw push constants must match GLSL");

	// P0: Push constants for GPU cull compute pass
	struct GrassCullPushConstants
	{
		float    cullDistance;         // 超出此距离的实例被剪除
		float    grassHeight;          // 草叶高度
		uint32_t instanceCount;        // 总实例数
		float    scatterRadiusMax;     // 子叶片最大散布半径—用于包围球扩展




		uint32_t subBladeCount;        // 子叶片数，用于 atomicAdd 直接计算 indirect instanceCount
		// Hi-Z 遮挡剔除参数。输入为上一帧 max-depth occlusion HZB，shader 使用历史相机矩阵重投影。
		float    hizSampleBias;        // 线性深度偏差，单位为米；只有超过该偏差才判为遮挡
		int      hizMipCount;          // Hi-Z mip 层数
		int      hizEnabled;           // 是否启用 Hi-Z 剪除
	};

	// 程序化草叶模板使用的轻量顶点，语义顺序与标准导入 Mesh 保持一致：
	// vec3 position (loc 0)、vec2 uv (loc 1)、vec3 normal (loc 2)。
	struct GrassVertex
	{
		glm::vec3 position;
		glm::vec2 uv;
		glm::vec3 normal;
	};

	// 每个 GPU 批次对应一个分布项的一种模型变体；所有部件共享同一实例列表。
	struct GrassRenderConfig
	{
		VansMesh* mesh = nullptr;
		VansMaterial* material = nullptr;
		bool proceduralBlade = false;
	};

	enum class TreePartType : uint32_t { Trunk = 0, Leaves = 1, Custom = 2 };
	struct TreePartConfig
	{
		VansMesh* mesh = nullptr;
		VansMaterial* material = nullptr;
		TreePartType type = TreePartType::Custom;
	};

	struct TreeInstanceGPU
	{
		glm::mat4 modelMatrix = glm::mat4(1.0f);
		glm::vec4 boundsSphere = glm::vec4(0.0f); // xyz center, w radius
		uint32_t speciesIndex = 0;
		uint32_t regionIndex = 0; // visibility group index for GPU cull output
		uint32_t randomSeed = 0;
		uint32_t flags = 0;
	};

	struct TreeSpeciesCullInfo
	{
		uint32_t visibleOffset = 0;
		uint32_t maxCount = 0;
		uint32_t padding[2] = { 0, 0 };
	};

	struct TreeCullPushConstants
	{
		float cullDistance;
		uint32_t instanceCount;
		uint32_t speciesCount;
		uint32_t hizEnabled;
		float hizSampleBias;
		int hizMipCount;
		float padding0;
		float padding1;
	};

	struct GrassShadowPushConstants
	{
		uint32_t boneCount;
		uint32_t subBladeCount;
		int32_t shadowIndex;
		uint32_t visibleOffset = 0;
	};
	static_assert(sizeof(GrassShadowPushConstants) == 16);

	struct TreeDrawPushConstants
	{
		int materialIndex;
		int objectIndex;
		uint32_t visibleOffset;
		uint32_t alphaTestEnabled;
	};

	struct TreeShadowPushConstants
	{
		int materialIndex;
		int objectIndex;
		uint32_t visibleOffset;
		int cascadeIndex;
		uint32_t alphaTestEnabled;
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
		uint32_t      assignedInstanceCount = 0;

		// Indirect draw command
		VansVKBuffer  indirectDrawBuffer;
		VansVKBuffer  shadowIndirectDrawBuffer;

		// Descriptor set for draw pass (Set 3)
		VkDescriptorSet drawDescSet = VK_NULL_HANDLE;

		// Material info (resolved at load)
		int materialIndex = -1;
		VansMaterial* material = nullptr;
	};

	struct TreeDrawConfigGPU
	{
		VansMesh* mesh = nullptr;
		VansMaterial* material = nullptr;
		int materialIndex = -1;
		uint32_t speciesIndex = 0;
		uint32_t partIndex = 0;
		TreePartType partType = TreePartType::Custom;
		uint32_t visibilityGroupIndex = 0;
		int32_t submeshIndex = -1;
		uint32_t visibleOffset = 0;
		uint32_t instanceCapacity = 0;
		VansVKBuffer indirectDrawBuffer;
		VansVKBuffer shadowIndirectDrawBuffer;
		VkDescriptorSet drawDescSet = VK_NULL_HANDLE;
	};

	class VansVegetationSystem
	{
	public:
		VansVegetationSystem() = default;
		~VansVegetationSystem();

		// 显式实例输入；零实例不会创建默认草叶或默认树。
		void Init(VkDevice device, std::vector<GrassInstance> grass,
			std::vector<TreeInstanceGPU> trees, uint32_t boneCountPerInstance);
		void SetRenderConfigs(const std::vector<GrassRenderConfig>& configs) { m_RenderConfigs = configs; }
		void SetTreeParts(const std::vector<TreePartConfig>& parts) { m_TreeParts = parts; }
		void BuildRenderConfigs();
		void BuildTreeResources();
		void SetRenderOptions(bool culling, float distance, bool castShadows)
		{
			m_CullEnabled = culling;
			m_CullDistance = distance;
			m_CastShadows = castShadows;
		}
		bool CastsShadows() const { return m_CastShadows; }

		// ── Per-frame update: dispatches bone sim compute pass ─────────
		void Update(VansVKCommandBuffer& computeCmd, float deltaTime, float time,
		            const glm::vec2& windDirection = glm::vec2(1.0f, 0.0f),
		            float windStrength = 4.0f, float windFrequency = 0.5f,
		            float windSpeed = 1.5f, float windBendMult = 5.0f,
		            float stiffness = 15.0f, float damping = 0.92f,
		            float softness = 0.2f,
		            float lodFullDist = 15.0f, float lodFadeDist = 20.0f,
		            bool useCullVisibilityMask = false,
		            bool sameQueueGraphicsConsumer = true);

		// ── P0: GPU frustum + distance cull — dispatch before Draw() ────
		bool DispatchCullPass(VansVKCommandBuffer& computeCmd, float cullDistance,
			bool sameQueueGraphicsConsumer = true);
		void DispatchTreeCullPass(VansVKCommandBuffer& computeCmd,
			bool sameQueueGraphicsConsumer = true);

		// ── Draw: issues one indirect indexed draw per render config ───
		void Draw(VansVKCommandBuffer& graphicsCmd,
		          GlobalStateData& globalState,
		          const std::vector<VkDescriptorSetLayout>& baseDescSetLayouts,
		          const std::vector<VkDescriptorSet>& baseDescSets,
		          int pushConstantTransformIndex);
		void DrawGrassCascadeShadow(VansVKCommandBuffer& graphicsCmd, GlobalStateData& globalState,
			const std::vector<VkDescriptorSetLayout>& baseDescSetLayouts,
			const std::vector<VkDescriptorSet>& baseDescSets, int cascadeIndex);
		void DrawTrees(VansVKCommandBuffer& graphicsCmd,
		               GlobalStateData& globalState,
		               const std::vector<VkDescriptorSetLayout>& baseDescSetLayouts,
		               const std::vector<VkDescriptorSet>& baseDescSets,
		               int pushConstantTransformIndex);
		void DrawTreeCascadeShadow(VansVKCommandBuffer& graphicsCmd,
		                            GlobalStateData& globalState,
		                            const std::vector<VkDescriptorSetLayout>& baseDescSetLayouts,
		                            const std::vector<VkDescriptorSet>& baseDescSets,
		                            int pushConstantTransformIndex);

		// ── Cleanup ────────────────────────────────────────────────────
		void Cleanup(VkDevice device);

		// ── Hi-Z 遮挡剔除 (HZB 必须在 cull descriptor 写入前就绪) ─────
		void SetHiZDepth(VkImageView imageView, VkSampler sampler, uint32_t mipCount,
		                float sampleBias = 0.2f);

		// ── Blade height — must be set before Init() ──────────────────────
		void SetBladeHeight(float h) { m_BladeHeight = h; }
		void SetBladeWidth(float w) { m_BladeWidthRoot = w; }

		// ── Sub-blade tuft config — must be set before Init() ────────────────
		void SetSubBladeParams(uint32_t count, float radiusMin, float radiusMax)
		{
			m_SubBladeCount            = count;
			m_SubBladeScatterRadiusMin = radiusMin;
			m_SubBladeScatterRadiusMax = radiusMax;
		}
		void SetRestShape(float tipDegrees,float rootDegrees,uint32_t scatterSeed)
		{
			m_RestTipBendDegrees=tipDegrees;m_RestRootBendDegrees=rootDegrees;m_SubBladeScatterSeed=scatterSeed;
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

		// ── P0: Visibility data accessors ──────────────────────────────
		VansVKBuffer& GetVisibilityBuffer()       { return m_VisibilityBuffer; }
		VansVKBuffer& GetVisibleCountBuffer()     { return m_VisibleCountBuffer; }
		VansVKBuffer& GetVisibleIndexBuffer()     { return m_VisibleIndexBuffer; }

		// ── Cull distance (set before or after Init) ─────────────────────
		void  SetCullDistance(float d) { m_CullDistance = d; }
		float GetCullDistance() const  { return m_CullDistance; }

		// ── P1: 子叶片距离 LOD 控制 ──────────────────────────────────
		void SetSubBladeLodDistances(float midDist, float farDist) { m_SubBladeLodMidDist = midDist; m_SubBladeLodFarDist = farDist; }
		float GetSubBladeLodMidDist() const { return m_SubBladeLodMidDist; }
		float GetSubBladeLodFarDist() const { return m_SubBladeLodFarDist; }

		// ── Per-config GPU data (for render node to iterate) ──────────
		const std::vector<GrassRenderConfigGPU>& GetRenderConfigsGPU() const { return m_RenderConfigsGPU; }
		const std::vector<TreeDrawConfigGPU>& GetTreeDrawConfigsGPU() const { return m_TreeDrawConfigsGPU; }
		bool HasTrees() const { return m_TreeEnabled && !m_TreeInstancesCPU.empty() && !m_TreeDrawConfigsGPU.empty(); }

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
		void CreateScatterOffsetUBO(VkDevice device);
		void CreateCullBuffers(VkDevice device);
		void CreateDescriptorSets();
		void LoadComputeShaders(VkDevice device);
		void WriteBoneSimDescriptors();
		void WriteCullDescriptors();

		// ── Per-config helpers ──────────────────────────────────────────
		void GenerateBoneWeights(GrassRenderConfigGPU& cfg, VansMesh* mesh);
		void WriteDrawDescriptors(GrassRenderConfigGPU& cfg);
		void CreateTreeDescriptorSets();
		void WriteTreeCullDescriptors();
		void WriteTreeDrawDescriptors(TreeDrawConfigGPU& cfg);
		void LoadTreeShaders(VkDevice device);

		// ── Configuration ───────────────────────────────────────────────
		uint32_t m_InstanceCount        = 0;
		uint32_t m_BoneCountPerInstance = 0;
		uint32_t m_VertexCount          = 0;     // procedural blade vertex count
		uint32_t m_IndexCount           = 0;     // procedural blade index count
		float    m_BladeHeight          = 0.0f;
		float    m_BladeWidthRoot       = 0.0f;
		uint32_t m_SubBladeCount        = 1;
		uint32_t m_SubBladeScatterSeed = 0;
		float m_RestTipBendDegrees = 0;
		float m_RestRootBendDegrees = 0;
		float    m_SubBladeScatterRadiusMin = 0.0f;
		float    m_SubBladeScatterRadiusMax = 0.0f;
		glm::vec2 m_InitWindDir         = glm::vec2(1.0f, 0.0f);
		float     m_InitLeanDeviation   = 0.0f;
		float     m_CullDistance        = 0.0f;  // P0: 最大绘制距离，超出则剔除
		float     m_SubBladeLodMidDist  = 0.0f;   // P1: 中距离子叶片 LOD 阈值
		float     m_SubBladeLodFarDist  = 0.0f;   // P1: 远距离子叶片 LOD 阈值
		std::vector<GrassInstance> m_GrassInstancesCPU;

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

		std::vector<TreePartConfig> m_TreeParts;
		bool m_CullEnabled = false;
		bool m_CastShadows = false;
		bool m_TreeEnabled = false;
		std::vector<TreeInstanceGPU> m_TreeInstancesCPU;
		std::vector<TreeSpeciesCullInfo> m_TreeSpeciesInfosCPU;
		std::vector<TreeDrawConfigGPU> m_TreeDrawConfigsGPU;
		VansVKBuffer m_TreeInstanceBuffer;
		VansVKBuffer m_TreeVisibleCountsBuffer;
		VansVKBuffer m_TreeVisibleIndexBuffer;
		VansVKBuffer m_TreeSpeciesInfoBuffer;
		VkDescriptorSetLayout m_TreeDrawLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_TreeCullLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_TreeCullDescSets;
		VansGraphicsShader* m_GrassShadowShader = nullptr;
		VansGraphicsShader* m_TreeGBufferShader = nullptr;
		VansGraphicsShader* m_TreeShadowShader = nullptr;
		VansComputeShader* m_TreeCullShader = nullptr;

		// ── GPU Buffers ─────────────────────────────────────────────────
		VansVKBuffer m_InstanceBuffer;
		VansVKBuffer m_BoneBuffer;
		VansVKBuffer m_BoneMatrixBuffer;
		VansVKBuffer m_LodFactorsBuffer;
		// P6a 优化: 用共享散布偏移 UBO 替换每实例×每子叶片的 SubBladeRoots 大 SSBO
		// 仅 m_SubBladeCount 个 vec4 (XZ 偏移)，节省 ~320 MB 显存
		VansVKBuffer m_ScatterOffsetUBO;

		// ── Procedural template blade mesh (owned by this system) ───────
		VansMesh* m_TemplateMesh = nullptr;

		// ── Compute Shaders ─────────────────────────────────────────────
		VansComputeShader* m_BoneSimShader = nullptr;
		VansComputeShader* m_CullShader    = nullptr;

		// ── P0: GPU Cull Buffers ───────────────────────────────────────
		VansVKBuffer m_VisibilityBuffer;      // uint per instance: 1=visible 0=culled
		VansVKBuffer m_VisibleCountBuffer;    // single uint: atomic counter
		VansVKBuffer m_VisibleIndexBuffer;    // compact list of visible instance indices
		VkDescriptorSetLayout m_CullLayout        = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_CullDescSets;

		// ── Global descriptor set (camera UBO, set=0 in bone sim compute) ────
		VkDescriptorSetLayout m_GlobalDescSetLayout = VK_NULL_HANDLE;
		VkDescriptorSet       m_GlobalDescSet       = VK_NULL_HANDLE;

		// ── Hi-Z depth pyramid (optional, 上一帧 max-depth occlusion HZB) ──
		VkImageView m_HiZView        = VK_NULL_HANDLE;
		VkSampler   m_HiZSampler     = VK_NULL_HANDLE;
		uint32_t    m_HiZMipCount    = 0;
		float       m_HiZSampleBias  = 0.2f;   // 单位：米（HIZ 已改为线性深度，原 NDC 偏置 0.005 已不适用）
		bool        m_HiZEnabled     = false;
		VkDevice m_Device = VK_NULL_HANDLE;
	};
}
