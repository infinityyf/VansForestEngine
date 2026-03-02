#pragma once

#include "VulkanCore/VansShader.h"
#include "VulkanCore/VansTexture.h"
#include "VulkanCore/VansVKBuffer.h"
#include "VansRuntimeRenderTextureManager.h"
#include "VulkanCore/VansVKDescriptorManager.h"
#include "BRDFData/VansPBR.h"
#include "BRDFData/VansLight.h"
#include "VansAsset.h"
#include <vector>
#include <unordered_map>
#include <string>

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
		static constexpr const char* RT_SSAO_RESULT = "Runtime.SSAO.Result";
		static constexpr const char* RT_SSGI_RESULT = "Runtime.SSGI.Result";
		static constexpr const char* RT_SSGI_TEMPORAL_A = "Runtime.SSGI.TemporalA";
		static constexpr const char* RT_SSGI_TEMPORAL_B = "Runtime.SSGI.TemporalB";
		static constexpr const char* RT_HZB_RESULT = "Runtime.HZB.Result";
		static constexpr const char* RT_SSR_HIT_INFO = "Runtime.SSR.HitInfo";
		static constexpr const char* RT_SSR_RAY_PDF = "Runtime.SSR.RayPDF";
		static constexpr const char* RT_SSR_RESULT = "Runtime.SSR.Result";
		static constexpr const char* RT_SSRAA_RESULT_A = "Runtime.SSR.AA.ResultA";
		static constexpr const char* RT_SSRAA_RESULT_B = "Runtime.SSR.AA.ResultB";
		static constexpr const char* RT_SSRAA_RESULT = "Runtime.SSR.AA.Result";
		static constexpr const char* RT_SSGI_FILTER_RESULT = "Runtime.SSGI.FilterResult";
		static constexpr const char* RT_SSAO_FILTER_RESULT = "Runtime.SSAO.FilterResult";
		static constexpr const char* RT_SH_R_RESULT = "Runtime.RayTracing.SH.R";
		static constexpr const char* RT_SH_G_RESULT = "Runtime.RayTracing.SH.G";
		static constexpr const char* RT_SH_B_RESULT = "Runtime.RayTracing.SH.B";
		static constexpr const char* RT_VOLUMETRIC_FOG_RESULT = "Runtime.VolumetricFog.Result";

		bool RegisterRuntimeRenderTexture(const std::string& name, VansTexture* texture, bool replaceExisting = true);

		VansTexture* GetRuntimeRenderTexture(const std::string& name) const;

		bool HasRuntimeRenderTexture(const std::string& name) const;

		bool RemoveRuntimeRenderTexture(const std::string& name);

		void ClearRuntimeRenderTextures();

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

		uint32_t     m_SSGITemporalFrame = 0;

		VansMaterialManager();

		~VansMaterialManager();

	public:

		VansComputeShader* m_SSGIShader;

		// SSGI temporal accumulation shader & descriptors
		VansComputeShader* m_SSGITemporalShader;
		VkDescriptorSetLayout m_SSGITemporalSetLayout;
		std::vector<VkDescriptorSet> m_SSGITemporalDescriptorSets; // [0]=frameA, [1]=frameB
		VansVKBuffer m_SSGITemporalCBBuffer;

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

		VansRuntimeRenderTextureManager m_RuntimeRenderTextureManager;

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