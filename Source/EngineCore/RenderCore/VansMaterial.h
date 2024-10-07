#pragma once

#include "VulkanCore/VansShader.h"
#include "VulkanCore/VansTexture.h"
#include "VulkanCore/VansVKBuffer.h"
#include "VulkanCore/VansVKDescriptorManager.h"
#include "BRDFData/VansPBR.h"
#include "VansAsset.h"
#include <vector>

using namespace VansVulkan;
namespace VansGraphics
{
	enum VansMaterialType
	{
		VAN_PBR = 0,
		VAN_COAT = 1,
		VAN_TRANSPARENT = 2,
		VAN_POST_PROCESS = 3,
		VAN_SKY_BOX = 4,
	};


	class VansMaterialManager
	{
		friend class VansMaterial;
		friend class VansRenderNode;
		//用于存储材质数据buffer的描述符
		//实际buffer由material持有
	private:

		void InitMaterialDataDescriptors();

	public:
		//PBR参数
		VkDescriptorSetLayout m_MaterialPBRBaseDataLayout;
		std::vector<VkDescriptorSet> m_MaterialPBRBaseDataDescriptorSets;

		//PBR预卷积
		VkDescriptorSetLayout m_PreDiffuseConvSetLayout;
		std::vector<VkDescriptorSet> m_PreDiffuseConvtDescriptorSets;

		//保存全局的一些texture数据
		VansTexture* m_PreConvDiffuse;

		VansMaterialManager();
	};

	class VansMaterial : public VansAsset
	{
		friend class VansScene;

	private:
		//pbr数据data buffer
		VansVKBuffer m_BasePBRDataBuffer;

		void UpdatePBRMaterialData(VansMaterialManager& materialManager);

	public:
		VansMaterialType m_MaterialType;

		std::vector<VansTexture*> m_Texture;
		//shader
		VansGraphicsShader* m_Shader;

		//材质参数
		VansBasePBRParam m_BasePBRParam;

		//定义GPU数据
		void CreatePBRMaterialDataBuffer(VkDevice& logic_device);

		void UpdateMaterialData(VansMaterialManager& materialManager);
	};


}