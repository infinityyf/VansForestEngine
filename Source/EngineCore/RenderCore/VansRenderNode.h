#pragma once

//#include "VulkanCore/VansMesh.h"
#include "VansMaterial.h"
#include "../ScriptCore/VansTransform.h"
#include "BRDFData/VansLight.h"
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
	};

	struct alignas(16) ModelDataStruct
	{
		glm::mat4x4 ModelMatrix;
		glm::mat4x4 NormalMatrix;
		glm::vec4 Postion;
		glm::vec4 Scale;
	};

	class VansCamera;
	class VansAnimationNode;
	class VansRenderNode
	{
		public:

			VansRenderNode(VkDevice& device, RenderNodeType type);

			virtual ~VansRenderNode();

			std::string m_NodeName;

			// If non-empty, this node was auto-generated as part of a multi-mesh group.
			// The hierarchy window uses this to group children under a parent tree node.
			std::string m_ParentGroupName;

			VansMesh* m_Mesh;

			VansMaterial* m_Material;

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

		int m_TransfromIndex;

		// ID-based Data Access
		uint32_t m_TransformID;

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

	protected:

		RenderNodeType m_NodeType;

		//描述符相关
		VkDescriptorSetLayout modelBufferLayout;
		std::vector<VkDescriptorSet> modelBufferDescriptorSets;

		//sampler imgae 描述符
		VkDescriptorSetLayout textureResourceLayout;
		std::vector<VkDescriptorSet> textureResourceDescriptorSets;

		VkDescriptorSetLayout frameBufferInputLayout;
		std::vector<VkDescriptorSet> frameBufferInputDescriptorSets;

		void DestroyDescriptorSets();


		std::vector<VkDescriptorSetLayout> m_UsedDescSetLayouts;

		std::vector<VkDescriptorSet> m_UsedDescSets;

		bool m_DescriptorsetsDirty;

		bool m_DescriptorsetsSetDone;

		bool CheckRenderNodeState();

		// Helper function to compute model matrix from transform
		void ComputeModelDataFromTransform();

	public:
		void virtual CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) {};

		void virtual UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) {};

		void virtual UpdateDescripterSets(VansMaterialManager& materialManager) {}

		// New function for updating model data from logic code
		void UpdateModelData();

		void BeforeDrawCall();

		virtual void Draw(VansVKCommandBuffer& cmd, GlobalStateData& global_state);

		void DrawPunctualShadow(VansVKCommandBuffer& cmd, GlobalStateData& global_state, int lightIndex, int shadowIndex);

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

		VansCommonRenderNode(VkDevice& device, RenderNodeType type) : VansRenderNode(device, type), m_SupportShadow(false) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;
		
		void UpdateDescripterSets(VansMaterialManager& materialManager) override;

		void SyncMaterialToGPU(VansMaterial* mat, VansMaterialManager& materialManager);
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

		void UpdateDescripterSets(VansMaterialManager& materialManager) override;

		void Draw(VansVKCommandBuffer& cmd, GlobalStateData& global_state) override;
	};

	class VansSkyBoxRenderNode : public VansRenderNode
	{
	public:

		VansSkyBoxRenderNode(VkDevice& device, RenderNodeType type) : VansRenderNode(device, type) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;

		void UpdateDescripterSets(VansMaterialManager& materialManager) override;
	};

	class VansPostProcessRenderNode : public VansRenderNode
	{
	public:

		VansPostProcessRenderNode(VkDevice& device, RenderNodeType type) : VansRenderNode(device, type) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;

		void UpdateDescripterSets(VansMaterialManager& materialManager) override;
	};

	class VansDeferredRenderNode : public VansRenderNode
	{
	public:

		VansDeferredRenderNode(VkDevice& device, RenderNodeType type) : VansRenderNode(device, type) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;

		void UpdateDescripterSets(VansMaterialManager& materialManager) override;
	};

	class VansScreenSpaceRenderNode : public VansRenderNode
	{
	public:

		VansScreenSpaceRenderNode(VkDevice& device, RenderNodeType type) : VansRenderNode(device, type) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;

		void UpdateDescripterSets(VansMaterialManager& materialManager) override;
	};

	class VansShadowRenderNode : public VansRenderNode
	{
	public:

		VansShadowRenderNode(VkDevice& device) : VansRenderNode(device, NONE_NODE) {}

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;

		void UpdateDescripterSets(VansMaterialManager& materialManager) override;
	};

	class VansTerrain;
	struct TerrainConfig;
	class VansTerrainRenderNode : public VansRenderNode
	{
	private:

		VansTerrain* m_Terrain;

	public:

		VansTerrainRenderNode(VansVKDevice* device, const TerrainConfig& config, RenderNodeType type);

		void CreateDescriptorSets(VansCamera* camera, VansLightManager& lightManager, VansMaterialManager& materialManager) override;

		void UpdateRenderData(VansVKDevice* device, VansMaterialManager& materialManager, VansLightManager& lightManager, VansCamera* camera) override;

		void UpdateDescripterSets(VansMaterialManager& materialManager) override;

		void Draw(VansVKCommandBuffer& cmd, GlobalStateData& global_state);

		void DrawShadow(VansVKCommandBuffer& cmd, GlobalStateData& global_state);
	};

}