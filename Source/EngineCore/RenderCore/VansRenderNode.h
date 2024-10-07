#pragma once

//#include "VulkanCore/VansMesh.h"
#include "VansMaterial.h"
#include "VansTransform.h"
#include "BRDFData/VansLight.h"

namespace VansGraphics
{
	enum  RenderNodeType
	{
		NONE_NODE = 0,
		OPAQUE_NODE = 1<<0,
		TRANSPARENT_NODE = 1 <<1,
		POSTPROCESS_NODE = 1 <<2,
		SKY_BOX_NODE = 1 <<3,
	};
	class VansRenderNode
	{
	public:

		VansRenderNode(RenderNodeType typee);

		~VansRenderNode();

		std::string m_NodeName;

		VansMesh* m_Mesh;

		VansMaterial* m_Material;

		//transform数据
		VansTransform m_Transform;

	private:

		RenderNodeType m_NodeType;

		//描述符相关
		VkDescriptorSetLayout cameraBufferLayout;
		std::vector<VkDescriptorSet> cameraBufferDescriptorSets;

		//sampler imgae 描述符
		VkDescriptorSetLayout textureResourceLayout;
		std::vector<VkDescriptorSet> textureResourceDescriptorSets;

		//描述符相关
		VkDescriptorSetLayout frameBufferInputLayout;
		std::vector<VkDescriptorSet> frameBufferInputDescriptorSets;

		void DestroyDescriptorSets();


		std::vector<VkDescriptorSetLayout> m_UsedDescSetLayouts;

		std::vector<VkDescriptorSet> m_UsedDescSets;
	public:
		void CreateDescriptorSets();

		void RegistLightDescriptor(VansLightManager& lightManager);

		void RegistMaterialDescriptor(VansMaterialManager& materialManager);

		void UpdateDescriptorSets(VansVKDevice* device, VansMaterialManager& materialManager);

		void Draw(VansVKCommandBuffer& cmd, GlobalStateData& global_state);

		void SetName(const std::string& name)
		{
			m_NodeName = name;
		}
	};
}