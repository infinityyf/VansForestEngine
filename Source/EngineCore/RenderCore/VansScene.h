#pragma once
#include "VansRenderNode.h"
#include "BRDFData/VansLight.h"
#include <vector>

#include <nlohmann/json.hpp>
using json = nlohmann::json;
namespace VansGraphics
{
	class VansScene
	{
	private:

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

		std::vector<VansRenderNode*> m_OpaqueRenderNodes;

		std::vector<VansRenderNode*> m_TransParentRenderNodes;

		std::vector<VansRenderNode*> m_PostProcessRenderNodes;

		VansRenderNode* m_SelectedNode;

	public:
		bool LoadScene(const char* path);

		void LoadLights(VkDevice& device, json& light_node);

		void LoadRenderNodes(VkDevice& device, json& render_node);

		void UnLoadScene();

		void UpdateSceneData();

		void DrawSkyBoxNode();

		void DrawOpaqueNodes();

		void DrawTransParentNodes();

		void DrawPostProcessNodes();

		VansMaterialManager* GetMaterialManager() { return &m_MaterialManager; }
	};
}

extern VansGraphics::VansScene* m_Scene;
