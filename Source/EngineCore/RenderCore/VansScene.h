#pragma once
#include "VansRenderNode.h"
#include "VansCamera.h"
#include "BRDFData/VansLight.h"
#include <vector>

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

		std::vector<VansRenderNode*> m_TransParentRenderNodes;

		std::vector<VansRenderNode*> m_PostProcessRenderNodes;

		std::vector<VansRenderNode*> m_ScreenSpaceRenderNodes;

		std::vector<VansRenderNode*> m_ShadowRenderNodes;

		std::vector<VansRenderNode*> m_PunctualShadowRenderNodes;

	public:
		//editor
		VansRenderNode* m_SelectedNode;

	public:
		bool LoadScene(const char* path);

		void LoadLights(VkDevice& device, json& light_node);

		void LoadRenderNodes(VkDevice& device, json& render_node);

		void AddDeferredNode(VkDevice& device);

		void AddScreenSpaceFeatureNode(VkDevice& device);

		void UnLoadScene();

		void UpdateSceneData();

	private:

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
	};
}

extern VansGraphics::VansScene* m_Scene;

class VansAssetsFileWatcher;
extern VansAssetsFileWatcher* m_SceneFileWatcher;
