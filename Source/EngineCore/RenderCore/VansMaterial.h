#pragma once

#include "VulkanCore/VansShader.h"
#include "VulkanCore/VansTexture.h"
#include "VulkanCore/VansVKBuffer.h"
#include "VulkanCore/VansVKDescriptorManager.h"
#include "BRDFData/VansPBR.h"
#include "BRDFData/VansLight.h"
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
		VAN_DEFERRED = 5,
		VAN_SCREEN_SPACE_AO = 6,
		VAN_SCREEN_SPACE_REFLECTION = 7,
		VAN_SHAODW = 8,
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
		VkDescriptorSetLayout m_MaterialAtmosphereDataLayout;
		std::vector<VkDescriptorSet> m_MaterialAtmosphereDataDescriptorSets;

		//PBR预卷积
		VkDescriptorSetLayout m_BRDFInterationTexSetLayout;
		std::vector<VkDescriptorSet> m_BRDFInterationTextDescriptorSets;

		//SSGI
		VkDescriptorSetLayout m_SSGITexSetLayout;
		std::vector<VkDescriptorSet> m_SSGIDescriptorSets;

		//HIZ
		std::vector<VkDescriptorSetLayout> m_HZBTexSetLayouts;
		std::vector<VkDescriptorSet> m_HZBDescriptorSets;

		//SSR
		VkDescriptorSetLayout m_SSRTraceSetLayout;
		std::vector<VkDescriptorSet> m_SSRTraceDescriptorSets;

		VkDescriptorSetLayout m_SSRResolveSetLayout;
		std::vector<VkDescriptorSet> m_SSRResolveDescriptorSets;

		VkDescriptorSetLayout m_BilateralFilterSetLayout;
		std::vector<VkDescriptorSet> m_BilateralFilterDescriptorSets;

		//保存全局的一些texture数据
		VansTexture* m_PreConvDiffuse;

		VansTexture* m_PreConvSpecular;

		VansTexture* m_BRDFIntegralLUT;

		VansTexture* m_SSAOResult;

		VansTexture* m_SSGIResult;

		VansTexture* m_HZBResult;

		VansTexture* m_SSRHitInfo;

		VansTexture* m_SSRRayPDF;

		VansTexture* m_SSRResult;

		VansTexture* m_SSGIFilterResult;

		VansTexture* m_SHRResult;

		VansTexture* m_SHGResult;

		VansTexture* m_SHBResult;

		VansMaterialManager();

	public:

		VansComputeShader* m_SSGIShader;

		VansVKBuffer m_SSGICBBuffer;

		VansVKBuffer m_SkySHResultBuffer;

		uint32_t m_HIZMipCount;

		VansComputeShader* m_HZBShader;

		VansComputeShader* m_SSRTraceShader;

		VansComputeShader* m_SSRResolveShader;

		struct BilateralFilterPushConst
		{
			float sigmaSpace;
			float sigmaDepth;
			int radius;
			float depthThreshold;
		};

		BilateralFilterPushConst m_BilateralFilterPushConstant;

		VansComputeShader* m_BilateralFilterShader;

	public:

		VansVKBuffer m_AtmospherePBRDataBuffer;

		
	public:

		//梭有材质公用
		void UpdatePBRLutDescriptorSets();

		void UpdateAtmosphereDescriptorSets();

	};

	class VansMaterial : public VansAsset
	{
		friend class VansScene;

	private:
		//pbr数据data buffer
		VansVKBuffer m_BasePBRDataBuffer;

	public:
		VansMaterialType m_MaterialType;

		//pbr
		VansTexture* m_BaseColorTexture;
		VansTexture* m_NormalTexture;
		VansTexture* m_MetalTexture;
		VansTexture* m_RoughnessTexture;
		VansTexture* m_AoTexture;

		VansBasePBRParam m_BasePBRParam;

		//大气材质参数
		VansAtmospherePBRParam m_AtmospherePBRParam;

		//shader
		VansGraphicsShader* m_Shader;

		//定义GPU数据
		void CreatePBRMaterialDataBuffer(VkDevice& logic_device);

		VansVKBuffer& GetPBRDataBuffer() { return m_BasePBRDataBuffer; }

		void UpdatePBRUniformData();

		void UpdateAtmosphereMaterialData(VansMaterialManager& materialManager, VansLightManager& lightManager);
	};
}