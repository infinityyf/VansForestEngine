#pragma once
#include "VansRenderNode.h"
#include "VansCamera.h"
#include "BRDFData/VansLight.h"
#include <vector>

#include <nlohmann/json.hpp>
using json = nlohmann::json;
namespace VansGraphics
{
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

	private:

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

		void DrawSkyBoxNode();

		void DrawOpaqueNodes();

		void DrawTransParentNodes();

		void DrawPostProcessNodes();

		void DrawScreenSpaceFeatureNode();

		void DeferredShading();

		void InjectCamera(VansCamera* camera) { m_Camera = camera; }

		VansMaterialManager* GetMaterialManager() { return &m_MaterialManager; }

		VansLightManager* GetLightManager() { return &m_LightManager; }

		VansCamera* GetCamera() { return m_Camera; }

	};
}

extern VansGraphics::VansScene* m_Scene;
