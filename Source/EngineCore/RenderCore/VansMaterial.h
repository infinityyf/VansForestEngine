#pragma once

#include "VulkanCore/VansShader.h"
#include "VulkanCore/VansTexture.h"
#include "VulkanCore/VansVKBuffer.h"
#include "VulkanCore/VansVKDescriptorManager.h"
#include "BRDFData/VansPBR.h"
#include "BRDFData/VansLight.h"
#include "VansAsset.h"
#include <vector>

using namespace VansGraphics;
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

	struct alignas(16) VansMaterialPushConstant
	{
		int		materialIndex; //用于索引全局gpu资源  : pbr参数，objectcb， texture bindless
		int		transfromIndex; //用于索引model matrix
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

		VkDescriptorSetLayout m_SSRAASetLayout;
		std::vector<VkDescriptorSet> m_SSRAADescriptorSets;

		VkDescriptorSetLayout m_BilateralFilterSetLayout;
		std::vector<VkDescriptorSet> m_BilateralFilterDescriptorSets;

		VkDescriptorSetLayout m_VolumetricFogSetLayout;
		std::vector<VkDescriptorSet> m_VolumetricFogDescriptorSets;


		//全局pbr参数buffer，不每个pbr材质自己持有
		VansVKBuffer m_GlobalPBRDataBuffer;
		std::vector<VansMaterial*> m_GlobalPBRMaterial;
		std::vector<VansBasePBRParam> m_GlobalPBRParamData;
		VkDescriptorSetLayout m_GlobalPBRDataSetLayout;
		std::vector<VkDescriptorSet> m_GlobalPBRDataDescriptorSets;

		//全局pbr贴图的bindless descriptor set, 不每个材质自己持有
		std::vector<VansVKImage*> m_GlobalPBRTextures;
		VkDescriptorSetLayout m_GlobalPBRTexSetLayout;
		std::vector<VkDescriptorSet> m_GlobalPBRTexDescriptorSets;

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

		VansTexture* m_SSRAAResultA;
		VansTexture* m_SSRAAResultB;
		VansTexture* m_SSRAAResult;

		VansTexture* m_SSGIFilterResult;
		VansTexture* m_SSAOFilterResult;

		VansTexture* m_SHRResult;

		VansTexture* m_SHGResult;

		VansTexture* m_SHBResult;

		VansTexture* m_VolumetricFogResult;

		VansMaterialManager();

	public:

		VansComputeShader* m_SSGIShader;

		VansVKBuffer m_SSGICBBuffer;

		VansVKBuffer m_SkySHResultBuffer;

		uint32_t m_HIZMipCount;

		VansComputeShader* m_HZBShader;

		VansComputeShader* m_SSRTraceShader;

		VansComputeShader* m_SSRResolveShader;

		VansComputeShader* m_SSRTemporalAAShader;

		struct BilateralFilterPushConst
		{
			float sigmaSpace;
			float sigmaDepth;
			int radius;
			float depthThreshold;
		};

		BilateralFilterPushConst m_BilateralFilterPushConstant;

		VansComputeShader* m_BilateralFilterShader;

		VansComputeShader* m_VolumetrcFogShader;

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
		////pbr数据data buffer
		//VansVKBuffer m_BasePBRDataBuffer;

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

		//push constant
		VansMaterialPushConstant m_MaterialPushConstant;

		////定义GPU数据
		//void CreatePBRMaterialDataBuffer(VkDevice& logic_device);

		//VansVKBuffer& GetPBRDataBuffer() { return m_BasePBRDataBuffer; }

		//void UpdatePBRUniformData();

		void UpdateAtmosphereMaterialData(VansMaterialManager& materialManager, VansLightManager& lightManager);
	};
}