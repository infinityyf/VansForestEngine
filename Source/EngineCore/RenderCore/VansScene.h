#pragma once
#include "VansRenderNode.h"
#include "VansCamera.h"
#include "BRDFData/VansLight.h"
#include "../PhysicsCore/VansPhysicsNode.h"
#include "../PhysicsCore/VansPhysicsVehicle.h"
#include "VulkanCore/VansDescriptorSetLayouts.h"
#include <vector>
#include <map>
#include <set>

#include <nlohmann/json.hpp>
using json = nlohmann::json;
namespace VansGraphics
{
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

		// Object descriptor set (Set 2): Transform SSBO - reuses m_GlobalTransformDataSetLayout
		// but with the new object layout for the new convention
		VkDescriptorSetLayout m_ObjectDescriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorSet m_ObjectDescriptorSet = VK_NULL_HANDLE;

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
		VansRenderNode* FindRenderNodeByName(const std::string& name);

		void AddTerrainNode(VansVKDevice* device);

		void AddDeferredNode(VkDevice& device);

		void AddScreenSpaceFeatureNode(VkDevice& device);

		void UnLoadScene();

		void UpdateSceneData();

		// Update per-node GPU data once per frame before command buffer recording.
		void UpdateRenderNodesDataBeforeRecord();

		void UpdatePhysicsTransforms();

	private:

		void UpdateTransformRenderData();

	private:

		void ImportDefaultTextures(const std::string& path, const std::string& name, VansVKDevice* vkDevice, bool isSRGB);

		void LoadSceneResource(json& sceneData);

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
