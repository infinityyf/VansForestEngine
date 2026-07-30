#pragma once

//#include "VulkanCore/VansMesh.h"
#include "../VansNode.h"
#include "VansMaterial.h"
#include "VansRenderBounds.h"
#include "../ScriptCore/VansTransform.h"
#include "BRDFData/VansLight.h"
#include <cstdint>
#include <vector>
#include <queue>

namespace VansGraphics
{
	enum  RenderNodeType
	{
		NONE_NODE = 0,
		OPAQUE_NODE = 1 << 0,
		TRANSPARENT_NODE = 1 << 1,
		POSTPROCESS_NODE = 1 << 2,
		SKY_BOX_NODE = 1 << 3,
		DEFERRED_NODE = 1 << 4,
		SCREEN_SPACE_NODE = 1 << 5,
		TERRAIN_NODE = 1 << 6,
		VEGETATION_NODE = 1 << 7,
		DECAL_NODE = 1 << 8,    // OBB 贴花节点，叠写 GBuffer
		PARTICLE_NODE = 1 << 9, // 粒子实例化 Billboard 节点
		WATER_NODE = 1 << 10,   // 水面节点，独立 Water GBuffer Pass
		FORWARD_OPAQUE_AFTER_DEFERRED_NODE = 1 << 11,
		HAIR_NODE = 1 << 12,
	};

	struct alignas(16) ModelDataStruct
	{
		glm::mat4x4 ModelMatrix;
		glm::mat4x4 NormalMatrix;
		glm::vec4 Postion;
		glm::vec4 Scale;
		glm::mat4x4 PrevModelMatrix;
	};

	class VansCamera;
	class VansAnimationNode;
	class VansRenderNode : public VansNode
	{
		public:

			VansRenderNode(VkDevice& device, RenderNodeType type);

			virtual ~VansRenderNode();

			std::string m_NodeName;

			// If non-empty, this node was auto-generated as part of a multi-mesh group.
			// The hierarchy window uses this to group children under a parent tree node.
			std::string m_ParentGroupName;

			VansMesh* m_Mesh = nullptr;

			// Original model asset and serialized submesh identity.
			// m_Mesh may point at the current runtime slice implementation
			// (sourceMesh->m_SubMeshes[m_SubmeshIndex]).
			VansMesh* m_SourceMesh = nullptr;
			uint32_t m_SubmeshIndex = UINT32_MAX;
			std::string m_EntityGuid;
			std::string m_ParentEntityGuid;

			VansMaterial* m_Material = nullptr;

			// Per-instance GI ray-tracing participation. Transparent/transmission
			// materials are always disabled even when the serialized mode is "auto".
			bool m_RayTracingEnabled = true;

			// ── Animation support ───────────────────────────────────────────────
			// True when this node's mesh has a skeleton (bones), regardless of whether
			// any animation clips are currently playing.  Controls whether the real
			// bone matrix + bone weight buffers are bound in Set 3 (descriptor setup time).
			bool m_HasSkeletonBone = false;
			// True when the animation system is actively driving this node (clips playing).
			// Pushed to the shader each draw call as animationEnabled push constant so the
			// vertex shader knows whether to run the skinning math.
			bool m_AnimationEnabled = false;
			// Back-reference to the owning VansAnimationNode (null for static nodes).
			VansAnimationNode* m_AnimOwner = nullptr;
			// Index of this submesh within the animation node's per-submesh buffer arrays.
			// Used to look up the correct bone ID and weight buffers.
			uint32_t m_AnimSubmeshIndex = 0;
			// Pointers to this submesh's individual bone ID and weight GPU buffers.
			// Set by ExpandMultiMeshToRenderNodes; null for static / whole-mesh nodes.
			VansVKBuffer* m_AnimBoneIDBuffer     = nullptr;
			VansVKBuffer* m_AnimBoneWeightBuffer  = nullptr;


		//GPU 数据
		ModelDataStruct m_ModelData;
		VansRenderBounds m_WorldBounds;
		bool m_HasWorldBounds = false;

		// 共享 Transform SSBO 中的槽位索引。默认 -1 表示"未分配槽位"：
		// PrepareInstanceTransformData 仅为 Opaque/Transparent/Decal 节点分配真实索引，
		// 其余节点（SkyBox/Deferred/Terrain/Water/Vegetation 等）保持 -1，
		// UpdateModelData 的 `m_TransfromIndex >= 0` 守卫会跳过 SSBO 写入，
		// 避免未分配节点（如 Water）误写槽位 0 覆盖第一个不透明物体（如 Gun）的 Transform。
		int m_TransfromIndex = -1;

		// ID-based Data Access
		uint32_t m_TransformID;
		VkDevice m_Device = VK_NULL_HANDLE;

		// When false, this node shares another node's transform and must NOT free it.
		bool m_OwnsTransform = true;

		// Make this node share another node's transform (does not allocate/free).
		void ShareTransform(uint32_t sharedID)
		{
			if (m_OwnsTransform)
				VansTransformStore::FreeTransform(m_TransformID);
			m_TransformID = sharedID;
			m_OwnsTransform = false;
		}

		RenderNodeType GetNodeType() const { return m_NodeType; }
		void SetNodeType(RenderNodeType type) { m_NodeType = type; }

	protected:

		RenderNodeType m_NodeType;

		//描述符相关
		VkDescriptorSetLayout modelBufferLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> modelBufferDescriptorSets;

		//sampler imgae 描述符
		VkDescriptorSetLayout textureResourceLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> textureResourceDescriptorSets;

		VkDescriptorSetLayout frameBufferInputLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> frameBufferInputDescriptorSets;

		void DestroyDescriptorSets();


		std::vector<VkDescriptorSetLayout> m_UsedDescSetLayouts;

		std::vector<VkDescriptorSet> m_UsedDescSets;

		bool m_DescriptorsetsDirty;

		bool m_DescriptorsetsSetDone;

		bool CheckRenderNodeState();

		bool ValidateDescriptorBindings(
			const char* passName,
			const std::vector<VkDescriptorSetLayout>& layouts,
			const std::vector<VkDescriptorSet>& sets) const;

		// Helper function to compute model matrix from transform
		void ComputeModelDataFromTransform();
		void UpdateWorldBoundsFromModelData();

	public:
		void virtual CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) {};

		void virtual UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) {};

		virtual void UpdateDescriptorSets(VansMaterialManager& materialManager) {}

		virtual void RefreshAnimationDescriptorSet() {}

		virtual void MarkAnimationDescriptorDirty() {}

		void MarkDescriptorSetsDirty() { m_DescriptorsetsDirty = true; }

		void RecreateDescriptorSets(
			VansCamera* camera,
			VansLightManager& lightManager,
			VansMaterialManager& materialManager);

		static std::uint64_t GetDescriptorValidationFailureCount();
		static void ResetDescriptorValidationFailureCount();

		void OverridePassDescriptorSet(uint32_t setIndex, VkDescriptorSetLayout layout, VkDescriptorSet descriptorSet)
		{
			if (setIndex < m_UsedDescSetLayouts.size() && setIndex < m_UsedDescSets.size())
			{
				m_UsedDescSetLayouts[setIndex] = layout;
				m_UsedDescSets[setIndex] = descriptorSet;
			}
		}

		// New function for updating model data from logic code
		void UpdateModelData();
		void UpdateWorldBoundsFromTransform();
		bool HasWorldBounds() const { return m_HasWorldBounds && m_WorldBounds.IsValid(); }
		const VansRenderBounds& GetWorldBounds() const { return m_WorldBounds; }

		void BeforeDrawCall();

		virtual void Draw(VansVKCommandBuffer& cmd, GlobalStateData& global_state);

		bool PreparePipelineForDraw(VkDevice& device, GlobalStateData global_state);
		bool PreparePipelineForShader(
			VkDevice& device,
			GlobalStateData global_state,
			VansGraphicsShader* shader,
			const std::vector<VkDescriptorSetLayout>& layouts,
			const std::vector<VkDescriptorSet>& sets);



		// Draw with cascade shadow push constants: { materialIndex, transformIndex, cascadeIndex }
		void DrawCascadeShadowWithPassShader(VansVKCommandBuffer& cmd, GlobalStateData& global_state,
		                                     VansGraphicsShader* passShader,
		                                     const std::vector<VkDescriptorSet>& descSets,
		                                     const std::vector<VkDescriptorSetLayout>& descSetLayouts);

		void DrawPunctualShadowWithPassShader(VansVKCommandBuffer& cmd, GlobalStateData& global_state,
		                                      VansGraphicsShader* passShader,
		                                      const std::vector<VkDescriptorSet>& descSets,
		                                      const std::vector<VkDescriptorSetLayout>& descSetLayouts,
		                                      int lightIndex, int shadowFaceIndex);

		//void DrawWithMaterial(VansMaterial* material ,VansVKCommandBuffer& cmd, GlobalStateData& global_state);

		void SetName(const std::string& name)
		{
			m_NodeName = name;
		}

		void SetTransformData(glm::vec3 postion = glm::vec3(0, 0, 0), glm::vec3 rotation = glm::vec3(0, 0, 0), glm::vec3 scale = glm::vec3(1, 1, 1))
		{
			VansTransform& t = VansTransformStore::GetTransform(m_TransformID);
			t.m_Position = postion;
			t.m_Rotation = rotation;
			t.m_Scale = scale;
		}

		glm::vec3 GetTransformPosition()
		{
			return VansTransformStore::GetTransform(m_TransformID).m_Position;
		}

		glm::vec3 GetTransformRotation()
		{
			return VansTransformStore::GetTransform(m_TransformID).m_Rotation;
		}

		glm::vec3 GetTransformScale()
		{
			return VansTransformStore::GetTransform(m_TransformID).m_Scale;
		}

		glm::mat4x4 GetTransformMatrix()
		{
			return VansTransformStore::GetTransform(m_TransformID).GetModelMatrix();
		}
	};

	//更新材质参数,更新全局数据
	//1. 预计算环境漫反射
	//2. 高光lut
	//3. 大气

	class VansCommonRenderNode : public VansRenderNode
	{
	public:
		bool m_SupportShadow;
		uint32_t m_ShadowCasterMask;

		VansCommonRenderNode(VkDevice& device, RenderNodeType type)
			: VansRenderNode(device, type), m_SupportShadow(false), m_ShadowCasterMask(0xffffffffu) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;
		
		void UpdateDescriptorSets(VansMaterialManager& materialManager) override;

		void RefreshAnimationDescriptorSet() override;

		void MarkAnimationDescriptorDirty() override;

		void SyncMaterialToGPU(VansMaterial* mat, VansMaterialManager& materialManager);

		// ── Per-pass descriptor set arrays ─────────────────────────────────
		// Shadow pass: { Global, EmptyPass, Object } (3 sets — no animation, no material textures)
		std::vector<VkDescriptorSet>       m_ShadowDescSets;
		std::vector<VkDescriptorSetLayout> m_ShadowDescSetLayouts;
	};

	// ── Transparent render node ───────────────────────────────────────────────────
	// Each transparent material owns its own descriptor set and layout.
	// Uses objectIndex push constant to read model data from the Object
	// transform SSBO. No materialIndex — each material is self-contained.
	class VansTransparentRenderNode : public VansRenderNode
	{
	public:
		VansTransparentRenderNode(VkDevice& device, RenderNodeType type)
			: VansRenderNode(device, type) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;

		void UpdateDescriptorSets(VansMaterialManager& materialManager) override;

		void Draw(VansVKCommandBuffer& cmd, GlobalStateData& global_state) override;
	};

	class VansSkyBoxRenderNode : public VansRenderNode
	{
	public:

		VansSkyBoxRenderNode(VkDevice& device, RenderNodeType type) : VansRenderNode(device, type) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;

		void UpdateDescriptorSets(VansMaterialManager& materialManager) override;
	};

	class VansPostProcessRenderNode : public VansRenderNode
	{
	public:

		VansPostProcessRenderNode(VkDevice& device, RenderNodeType type) : VansRenderNode(device, type) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;

		void UpdateDescriptorSets(VansMaterialManager& materialManager) override;
	};

	class VansDeferredRenderNode : public VansRenderNode
	{
	public:

		VansDeferredRenderNode(VkDevice& device, RenderNodeType type) : VansRenderNode(device, type) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;

		void UpdateDescriptorSets(VansMaterialManager& materialManager) override;
	};

	class VansScreenSpaceRenderNode : public VansRenderNode
	{
	public:

		VansScreenSpaceRenderNode(VkDevice& device, RenderNodeType type) : VansRenderNode(device, type) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;

		void UpdateDescriptorSets(VansMaterialManager& materialManager) override;
	};

	// ── Vegetation render node — GPU-driven grass (indirect draw) ──────────────
	class VansVegetationSystem;
	class VansVegetationRenderNode : public VansRenderNode
	{
	private:
		VansVegetationSystem* m_VegetationSystem = nullptr;

	public:
		VansVegetationRenderNode(VkDevice& device, RenderNodeType type)
			: VansRenderNode(device, type) {}

		void SetVegetationSystem(VansVegetationSystem* system) { m_VegetationSystem = system; }
		VansVegetationSystem* GetVegetationSystem() const { return m_VegetationSystem; }

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;

		void UpdateDescriptorSets(VansMaterialManager& materialManager) override;

		void Draw(VansVKCommandBuffer& cmd, GlobalStateData& global_state) override;
		void DrawShadow(VansVKCommandBuffer& cmd, GlobalStateData& global_state);
		void DrawPunctualShadow(VansVKCommandBuffer& cmd, GlobalStateData& global_state, int lightIndex, int shadowFaceIndex);
	};

	// ── Water render node — flat grid plane at waterLevel, driven by VansWaterConfig ──
	class VansWaterSystem;
	struct VansWaterConfig;
	class VansWaterRenderNode : public VansRenderNode
	{
	public:
		VansWaterRenderNode(VkDevice& device, RenderNodeType type)
			: VansRenderNode(device, type) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;

		void UpdateDescriptorSets(VansMaterialManager& materialManager) override;

		void Draw(VansVKCommandBuffer& cmd, GlobalStateData& global_state) override;
	};

	class VansTerrain;
	struct TerrainConfig;
	class VansTerrainRenderNode : public VansRenderNode
	{
	private:

		VansTerrain* m_Terrain = nullptr;

	public:

		VansTerrainRenderNode(VansVKDevice* device, const TerrainConfig& config, RenderNodeType type);

		~VansTerrainRenderNode() override;

		VansTerrain* GetTerrain() const { return m_Terrain; }

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;

		void UpdateDescriptorSets(VansMaterialManager& materialManager) override;

		void Draw(VansVKCommandBuffer& cmd, GlobalStateData& global_state);

		void DrawShadow(VansVKCommandBuffer& cmd, GlobalStateData& global_state);

		void DrawMotionVector(VansVKCommandBuffer& cmd, GlobalStateData& global_state);
	};

	// ── Decal render node — OBB decal, overwrites GBuffer Normal/GBuffer0/GBuffer1 ──
	class VansDecalRenderNode : public VansRenderNode
	{
	public:
		VansDecalRenderNode(VkDevice& device) : VansRenderNode(device, DECAL_NODE) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;

		void UpdateDescriptorSets(VansMaterialManager& materialManager) override;
	};

}
