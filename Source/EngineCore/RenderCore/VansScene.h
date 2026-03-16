#pragma once
#include "VansRenderNode.h"
#include "VansShaderRegistry.h"
#include "VansCamera.h"
#include "BRDFData/VansLight.h"
#include "../PhysicsCore/VansPhysicsNode.h"
#include "../PhysicsCore/VansPhysicsVehicle.h"
#include "VulkanCore/VansDescriptorSetLayouts.h"
#include "../AnimationCore/VansAnimationNode.h"
#include <vector>
#include <map>
#include <set>
#include <unordered_map>

#include <nlohmann/json.hpp>
using json = nlohmann::json;
namespace VansGraphics
{
	// A logical group representing a multi-mesh parent and all its auto-expanded child render nodes.
	struct MultiMeshGroup
	{
		std::string parentName;
		glm::vec3 position = glm::vec3(0);
		glm::vec3 rotation = glm::vec3(0);
		glm::vec3 scale    = glm::vec3(1);
		uint32_t sharedTransformID = 0;            // transform owned by the first child
		std::vector<VansRenderNode*> childNodes;   // opaque + transparent children
		std::vector<VansRenderNode*> shadowNodes;  // shadow children
	};
	//struct TLASInstanceData
	//{
	//	glm::mat4x4 m_MVMatrix;
	//	int m_BufferIndex;
	//};

	class VansScene
	{
	public:
		void CreateNodeDescriptorSets();
	private:

		VansCamera* m_Camera;

		VansLightManager m_LightManager;

		VansMaterialManager m_MaterialManager;

		VansAsset* GetMeshAsset(const std::string& name);

		VansAsset* GetShaderAsset(const std::string& name);

		VansAsset* GetTextureAsset(const std::string& name);

		VansAsset* GetMaterialAsset(const std::string& name);

		void RegistRenderNode(VansRenderNode* renderNode, RenderNodeType type);


	public:

		//记录所有资产
		std::vector<VansAsset*> m_Meshes;

		std::vector<VansAsset*> m_Textures;

		std::vector<VansAsset*> m_Shaders;

		//记录运行时数据
		std::vector<VansAsset*> m_Materials;

	public:
		VansRenderNode* m_SkyBoxNode;

		VansRenderNode* m_DeferredNode;

		std::vector<VansRenderNode*> m_OpaqueRenderNodes;

		VansRenderNode* m_TerrainRenderNode;

		std::vector<VansRenderNode*> m_TransParentRenderNodes;

		std::vector<VansRenderNode*> m_PostProcessRenderNodes;

		std::vector<VansRenderNode*> m_ScreenSpaceRenderNodes;

		std::vector<VansRenderNode*> m_ShadowRenderNodes;

		std::vector<VansRenderNode*> m_PunctualShadowRenderNodes;

		// Multi-mesh parent groups for hierarchical display in the editor.
		// Key = parent group name, Value = group info with child node pointers.
		std::unordered_map<std::string, MultiMeshGroup> m_MultiMeshGroups;

		// ── Skeletal animation nodes ────────────────────────────────────────────────
		// Created automatically in ExpandMultiMeshToRenderNodes().
		std::vector<VansAnimationNode*> m_AnimationNodes;

		// Shared dummy buffers for non-animated render nodes (64 bytes, device-local-ish).
		// Bound at Object descriptor set bindings 1 & 2 instead of real bone data.
		VansVKBuffer m_DummyBoneIDBuffer;
		VansVKBuffer m_DummyBoneBuffer;
		VansVKBuffer m_DummyWeightBuffer;

		// Physics nodes
		std::vector<VansEngine::VansPhysicsNode*> m_PhysicsNodes;

		// Vehicle
		VansEngine::VansPhysicsVehicle* m_Vehicle = nullptr;
		// Initialize the vehicle in the scene from JSON-specified parameters.
		// Render node name bindings are stored on the vehicle itself.
		void InitVehicle(VansEngine::VansPhysicsSystem* physicsSystem, const glm::vec3& position,
			const std::string& bodyRenderNodeName, const std::vector<std::string>& tireRenderNodeNames);

	public:

		VansVKBuffer m_InstanceTransformDataBuffer;
		std::vector<ModelDataStruct> m_InstanceTransformData;
		VkDescriptorSetLayout m_GlobalTransformDataSetLayout;
		std::vector<VkDescriptorSet> m_GlobalTransformDataDescriptorSets;

		// ===== New: Reorganized descriptor set system =====
		// Global descriptor set (Set 0): Camera + Lights + Materials + IBL + Bindless
		VkDescriptorSetLayout m_GlobalDescriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorSet m_GlobalDescriptorSet = VK_NULL_HANDLE;

		// Object descriptor set (Set 2): Transform SSBO — shared by all geometry nodes
		VkDescriptorSetLayout m_ObjectDescriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorSet m_ObjectDescriptorSet = VK_NULL_HANDLE;

		// Animation descriptor set (Set 3): Bone Matrices SSBO + Bone Weight SSBO
		// Per-node for animated VansCommonRenderNodes; static nodes bind this shared dummy set.
		VkDescriptorSetLayout m_AnimationDescriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorSet m_AnimationDescriptorSet = VK_NULL_HANDLE;  // shared dummy for static nodes

		// Empty pass layout (Set 1) for passes that have no per-pass resources
		VkDescriptorSetLayout m_EmptyPassLayout = VK_NULL_HANDLE;
		VkDescriptorSet m_EmptyPassDescriptorSet = VK_NULL_HANDLE;

		// Creates the global Set 0 descriptor set and writes all global resources into it
		void CreateGlobalDescriptorSet(VkDevice device);
		void UpdateGlobalDescriptorSet();

	public:
		//editor
		VansRenderNode* m_SelectedNode;

	public:
		bool LoadScene(const char* path);

		void LoadLights(VkDevice& device, json& light_node);

		void LoadRenderNodes(VkDevice& device, json& render_node);

		void LoadPhysicsNodes(json& physics_node);

		// Find a render node by name across all render node lists
		VansRenderNode* FindRenderNodeByName(const std::string& name) const;

		void AddTerrainNode(VansVKDevice* device, json& terrainData);

		void AddDeferredNode(VkDevice& device);

		void AddScreenSpaceFeatureNode(VkDevice& device);

		void UnLoadScene();

		void UpdateSceneData();

		// Per-frame skeletal animation CPU update + GPU bone matrix upload.
		void UpdateAnimations(float deltaTime);

		// Update per-node GPU data once per frame before command buffer recording.
		void UpdateRenderNodesDataBeforeRecord();

		void UpdatePhysicsTransforms();

	private:

		void UpdateTransformRenderData();

	private:

		void ImportDefaultTextures(const std::string& path, const std::string& name, VansVKDevice* vkDevice, bool isSRGB);

		void LoadSceneResource(json& sceneData);

		// ── Multi-mesh auto-splitting ─────────────────────────────────────────
		// Given a multi-mesh asset, automatically creates one render node per
		// submesh with auto-generated materials + textures based on FBX data.
		// parentTransform carries the transform from the JSON render node entry.
		// supportShadow controls whether shadow nodes are also created.
		void ExpandMultiMeshToRenderNodes(
			VkDevice& device,
			VansMesh* multiMesh,
			const std::string& parentName,
			const glm::vec3& position,
			const glm::vec3& rotation,
			const glm::vec3& scale,
			bool supportShadow);

		// Helper: loads or reuses a texture by its absolute file path.
		// Returns the existing VansTexture* if one with the same name was already loaded.
		VansTexture* LoadOrGetTexture(const std::string& absPath, bool isSRGB);

		// Helper: returns the default shader for a given material type.
		// Loads the shader on first use and caches it in the scene shader list.
		VansGraphicsShader* GetOrCreateDefaultShader(VansMaterialType matType, VkDevice& device);

		// Helper: creates and registers one shader from a registry entry.
		// No-op if the shader is already loaded. Used by LoadSceneResource.
		void LoadShaderFromEntry(const VansShaderRegistryEntry& entry,
			                     const std::string& pathPrefix, VkDevice& device);

		// Loads all scene meshes from the JSON 'mesh' array.
		void LoadMeshesFromJson(const json& meshData, const std::string& pathPrefix,
			                    VkDevice& device, VansVKDevice* vkDevice);

		// Loads all shaders from the C++ shader registry.
		void LoadShadersFromRegistry(const std::string& pathPrefix, VkDevice& device);

		// Loads all scene textures from the JSON 'texture' array and imports engine defaults.
		void LoadTexturesFromJson(const json& textureData, const std::string& pathPrefix,
			                      VansVKDevice* vkDevice);

	public:

		void BuildRayTracingAS(VansVKDevice* vans_device, VansVKCommandBuffer* vans_commandBuffer);

		void ReleaseASTempBuffer(VansVKDevice* vans_device);

	public:

		void DrawShadowNodes();

		void DrawPointShadow(int lightIndex);

		void DrawSpotShadow(int pointCount, int lightIndex);

		void DrawSkyBoxNode();

		void DrawOpaqueNodes();

		void DrawTerrainNode(bool shadowPass = false);

		void DrawTransParentNodes();

		void DrawPostProcessNodes();

		void DrawScreenSpaceFeatureNode();

		void DeferredShading();

	public:

		void InjectCamera(VansCamera* camera) { m_Camera = camera; }

		VansMaterialManager* GetMaterialManager() { return &m_MaterialManager; }

		VansLightManager* GetLightManager() { return &m_LightManager; }

		VansCamera* GetCamera() { return m_Camera; }

		VkAccelerationStructureKHR& GetTopAS() { return m_TopLevelAS; }

		std::vector<VansVKBuffer>& GetBLASVertexBuffers() { return m_BLASVertexData; }

		std::vector<VansVKBuffer>& GetBLASIndexBuffers() { return m_BLASIndexData; }

		std::vector<uint32_t>& GetTLASInstanceData() { return m_TLASInstaneData; }

		std::vector<VansVKImage>& GetTLASInstanceTextures() { return m_TlasInstanceTextures; }

		std::vector<uint32_t>& GetTLASInstanceTextureIndex() { return m_TlasInstanceTextureIndex; }

	private:

		//光线追踪加速结构
		VkAccelerationStructureKHR m_TopLevelAS;

		VansVKBuffer m_TopLevelASBuffer;

		VansVKBuffer m_InstancesBuffer;

		VansVKBuffer m_TLASScratchBuffer;

		std::vector<VkAccelerationStructureInstanceKHR> m_TlasInstancesInfos;

		// Collection of geometries for the acceleration structure.
		std::vector<VkAccelerationStructureGeometryKHR> m_AsGeometry;

		// Build range information corresponding to each geometry.
		std::vector<VkAccelerationStructureBuildRangeInfoKHR> m_AsBuildRangeInfo;

		std::vector<VansVKBuffer> m_BLASVertexData;

		std::vector<VansVKBuffer> m_BLASIndexData;

		std::vector<uint32_t> m_TLASInstaneData;

		//bindless 贴图的索引
		//每个instance都有一个索引
		std::vector<uint32_t> m_TlasInstanceTextureIndex;
		//global的贴图资源，会绑定到bindless descriptor set
		std::vector<VansVKImage> m_TlasInstanceTextures;
		std::map<std::string,int> m_TlasInstanceMaterialToIndex;
	};
}

extern VansGraphics::VansScene* m_Scene;

class VansAssetsFileWatcher;
extern VansAssetsFileWatcher* m_SceneFileWatcher;
