#pragma once

#include "VulkanCore/VansShader.h"
#include "VulkanCore/VansTexture.h"
#include "VulkanCore/VansVKBuffer.h"
#include "VansRuntimeRenderTextureManager.h"
#include "VulkanCore/VansVKDescriptorManager.h"
#include "VulkanCore/VansDescriptorSetLayouts.h"
#include "BRDFData/VansPBR.h"
#include "BRDFData/VansLight.h"
#include "VansAsset.h"
#include "VansPostProcessProfile.h"
#include <vector>
#include <unordered_map>
#include <string>

using namespace VansGraphics;
namespace VansGraphics
{
	// ============================================================
	// Well-known render-pass name constants.
	// Each engine render pass queries the material for its unique pass name.
	// ============================================================
	namespace VansPass
	{
		static constexpr const char* GBUFFER          = "gbuffer";          // opaque GBuffer fill
		static constexpr const char* SHADOW           = "shadow";           // cascade shadow map
		static constexpr const char* PUNCTUAL_SHADOW  = "punctualShadow";   // point/spot shadow
		static constexpr const char* FORWARD_TRANSPARENT = "transparent";   // forward transparent
		static constexpr const char* SKY_BOX          = "skybox";           // sky box
		static constexpr const char* DEFERRED         = "deferred";         // deferred lighting resolve
		static constexpr const char* POST_PROCESS     = "postProcess";      // post-processing
		static constexpr const char* SCREEN_SPACE     = "screenSpace";      // SSAO / SSR etc.
		// future
		static constexpr const char* VELOCITY         = "velocity";
		static constexpr const char* PRE_DEPTH        = "preDepth";
		// 贴花阶段：袖写 Normal / GBuffer0 / GBuffer1
		static constexpr const char* DECAL_GBUFFER    = "decalGBuffer";
	}
	class VansPBRMaterial;
	class VansTransparentMaterial;
	class VansSkyBoxMaterial;
	class VansSkinMaterial;
	class VansClothMaterial;
	class VansHairMaterial;
	class VansSubsurfaceMaterial;
	class VansPostProcessMaterial;
	class VansDeferredMaterial;
	class VansSSAOMaterial;
	class VansEmissiveMaterial;
	class VansDecalMaterial;

	enum VansMaterialType
	{
		VAN_PBR = 0,
		VAN_COAT = 1,
		VAN_TRANSPARENT = 2,
		VAN_POST_PROCESS = 3,
		VAN_SKY_BOX = 4,
		VAN_DEFERRED = 5,
		VAN_SCREEN_SPACE_AO = 6,
		VAN_SKIN = 9,
		VAN_CLOTH = 10,
		VAN_HAIR = 11,
		VAN_SUBSURFACE = 12,
		VAN_GRASS    = 13,
		VAN_EMISSIVE = 14,   // 自发光材质：albedo × intensity 直通，无 BRDF
		VAN_DECAL    = 15,   // 贴花材质：叠写到 GBuffer Normal/GBuffer0/GBuffer1
	};

	// Lightweight push-constant payload built at draw time.
	// Each field maps to a global GPU resource index.
	struct alignas(16) VansDrawPushConstant
	{
		int materialIndex;    // index into global PBR param SSBO / bindless textures
		int transformIndex;   // index into per-object transform SSBO
		int animationEnabled; // 1 = skinned, 0 = static
	};

	// 体积云 GPU 参数，与 CloudRayMarch.comp 的 CloudParamsUBO 保持二进制一致。
	struct alignas(16) VansCloudParamsGPU
	{
		float planetRadius         = 6340000.0f;
		float seaLevel             = 200.0f;
		float cloudMinHeight       = 1200.0f;
		float cloudMaxHeight       = 7000.0f;

		float density              = 0.06f;
		float coverage             = 0.32f;
		float sunBrightness        = 1.5f;
		float phaseG               = 0.35f;

		float mainTileMeters       = 48000.0f;
		float detailTileMeters     = 5000.0f;
		float mainHeightScale      = 1.35f;
		float detailHeightScale    = 4.20f;

		float thresholdLowCoverage = 0.86f;
		float thresholdHighCoverage = 0.50f;
		float densityRemapLow      = 0.08f;
		float densityRemapHigh     = 0.62f;

		float mainErosionStrength  = 1.15f;
		float detailErosionStrength = 0.42f;
		float edgeErosionStrength  = 1.20f;
		float verticalShapePower   = 0.55f;

		float detailErosionLow     = 0.15f;
		float detailErosionHigh    = 0.76f;
		float detailEdgeStrength   = 1.00f;
		float shadowDensityScale   = 1.00f;
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
		static constexpr const char* RT_GI_VISIBILITY = "Runtime.RayTracing.GI.Visibility";
		static constexpr const char* RT_VOLUMETRIC_FOG_RESULT = "Runtime.VolumetricFog.Result";
		static constexpr const char* RT_RECT_LIGHT_EMISSIVE = "Runtime.RectLight.EmissiveArray";
		static constexpr const char* RT_FOG_VOXEL_INJECTION = "Runtime.Fog.VoxelInjection";
		static constexpr const char* RT_FOG_VOXEL_INJECTION_HISTORY = "Runtime.Fog.VoxelInjectionHistory";
		static constexpr const char* RT_FOG_VOXEL_RAYMARCH  = "Runtime.Fog.VoxelRayMarch";
		// 1/4 分辨率体积云结果（RGB=内散射，A=透射率）
		static constexpr const char* RT_CLOUD_BUFFER         = "Runtime.Cloud.Buffer";
		static constexpr const char* RT_CLOUD_MAIN_NOISE     = "Runtime.Cloud.MainNoise3D";
		static constexpr const char* RT_CLOUD_DETAIL_NOISE   = "Runtime.Cloud.DetailNoise3D";

		// --- 后处理 RuntimeRT ---
		static constexpr const char* RT_EXPOSURE_LUMINANCE  = "Runtime.PostProcess.Exposure.Luminance"; // 64x64 亮度缩图（R16F）
		static constexpr const char* RT_EXPOSURE_CURRENT    = "Runtime.PostProcess.Exposure.Current";   // 1x1 当前曝光值（R16F）
		static constexpr const char* RT_BLOOM_PREFILTER     = "Runtime.PostProcess.Bloom.Prefilter";    // 半分辨率预滤输出（RGBA16F）
		static constexpr const char* RT_BLOOM_MIP0          = "Runtime.PostProcess.Bloom.Mip0";         // 1/2 分辨率
		static constexpr const char* RT_BLOOM_MIP1          = "Runtime.PostProcess.Bloom.Mip1";         // 1/4 分辨率
		static constexpr const char* RT_BLOOM_MIP2          = "Runtime.PostProcess.Bloom.Mip2";         // 1/8 分辨率
		static constexpr const char* RT_BLOOM_MIP3          = "Runtime.PostProcess.Bloom.Mip3";         // 1/16 分辨率
		static constexpr const char* RT_BLOOM_RESULT        = "Runtime.PostProcess.Bloom.Result";       // Upsample 最终合成（RGBA16F）

		bool RegisterRuntimeRenderTexture(const std::string& name, VansTexture* texture, bool replaceExisting = true);

		VansTexture* GetRuntimeRenderTexture(const std::string& name) const;

		bool HasRuntimeRenderTexture(const std::string& name) const;

		bool RemoveRuntimeRenderTexture(const std::string& name);

		// 仅从注册表移除条目，不 delete 纹理（所有权归外部系统，如 VansRayTracing）
		bool UnregisterRuntimeRenderTexture(const std::string& name);

		void ClearRuntimeRenderTextures();

		// ── 场景切换时清空场景级 PBR 数据 ─────────────────────────────
		// 清除 GlobalPBR 数组和对应 GPU buffer / descriptor。
		// 不释放 PreConv/LUT 等项目级资源。
		void ClearScenePBRData(VkDevice device);

		VkDescriptorSetLayout m_MaterialAtmosphereDataLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_MaterialAtmosphereDataDescriptorSets;

		//PBR预卷积
		VkDescriptorSetLayout m_BRDFInterationTexSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_BRDFInterationTextDescriptorSets;

		//SSGI
		VkDescriptorSetLayout m_SSGITexSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_SSGIDescriptorSets;

		//HIZ
		std::vector<VkDescriptorSetLayout> m_HZBTexSetLayouts;
		std::vector<VkDescriptorSet> m_HZBDescriptorSets;

		//SSR
		VkDescriptorSetLayout m_SSRTraceSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_SSRTraceDescriptorSets;

		VkDescriptorSetLayout m_SSRResolveSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_SSRResolveDescriptorSets;

		VkDescriptorSetLayout m_SSRAASetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_SSRAADescriptorSets;

		VkDescriptorSetLayout m_BilateralFilterSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_BilateralFilterDescriptorSets;

		VkDescriptorSetLayout m_VolumetricFogSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_VolumetricFogDescriptorSets;
		VansVKBuffer m_FogParamsCBBuffer;

		// --- Voxel Fog (Light Injection + Ray March) ---
		VkDescriptorSetLayout m_FogLightInjectionSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_FogLightInjectionDescriptorSets;

		VkDescriptorSetLayout m_FogRayMarchSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_FogRayMarchDescriptorSets;

		VansVKBuffer m_FogVolumeParamsCBBuffer;   // FogVolumeParams UBO (density, anisotropy, scatter, ambient)
		uint32_t     m_FogTemporalFrame = 0;       // ping-pong frame index for fog injection


		//全局pbr参数buffer，不每个pbr材质自己持有
		VansVKBuffer m_GlobalPBRDataBuffer;
		std::vector<VansPBRMaterial*> m_GlobalPBRMaterial;
		std::vector<VansBasePBRParam> m_GlobalPBRParamData;
		VkDescriptorSetLayout m_GlobalPBRDataSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_GlobalPBRDataDescriptorSets;

		//全局pbr贴图的bindless descriptor set, 不每个材质自己持有
		std::vector<VansVKImage*> m_GlobalPBRTextures;
		VkDescriptorSetLayout m_GlobalPBRTexSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_GlobalPBRTexDescriptorSets;

		// 场景 Global Descriptor Set（Set 0）的快捷引用，用于视频切换时直接更新 Bindless 槽。
		// 由 LoadSceneForRendering 在 CreateGlobalDescriptorSet 之后写入。
		VkDescriptorSet m_VideoBindlessDescriptorSet = VK_NULL_HANDLE;

		//保存全局的一些texture数据
		VansTexture* m_PreConvDiffuse = nullptr;

		VansTexture* m_PreConvSpecular = nullptr;

		VansTexture* m_BRDFIntegralLUT = nullptr;

		VansTexture* m_SkinBSDFLUT = nullptr;

		VansTexture* m_ClothBRDFLUT = nullptr;

		// LTC LUTs (Linearly Transformed Cosines, area-light BRDF)
		// 64x64, RGBA16F, runtime-uploaded from embedded float arrays (LTCData.h).
		VansTexture* m_LTC1 = nullptr;

		VansTexture* m_LTC2 = nullptr;

		// 面光源发光贴图数组：256×256×32，VK_IMAGE_TYPE_2D + layerCount=32，sampler2DArray。
		// 由 VansVKRenderDataPreparation 创建，由 VansSceneLoader 按需填充各层。
		VansTexture* m_RectLightEmissiveArray = nullptr;

		uint32_t     m_SSGITemporalFrame = 0;

		VansMaterialManager();

		~VansMaterialManager();

	public:

		VansComputeShader* m_SSGIShader;

		// SSGI temporal accumulation shader & descriptors
		VansComputeShader* m_SSGITemporalShader;
		VkDescriptorSetLayout m_SSGITemporalSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_SSGITemporalDescriptorSets; // [0]=frameA, [1]=frameB
		VansVKBuffer m_SSGITemporalCBBuffer;

		VansVKBuffer m_SSGICBBuffer;

		VansVKBuffer m_SkySHResultBuffer;

		uint32_t m_HIZMipCount;

		VansComputeShader* m_HZBShader;

		// HIZ Seed：将 GBuffer 线性深度写入 mip 0
		VansComputeShader* m_HIZSeedShader;
		VkDescriptorSetLayout m_HIZSeedSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_HIZSeedDescriptorSets;

		VansComputeShader* m_SSRTraceShader;

		VansComputeShader* m_SSRResolveShader;

		VansComputeShader* m_SSRTemporalAAShader;

		struct BilateralFilterPushConst
		{
			float sigmaSpace;
			float sigmaDepth;
			int radius;
			float depthThreshold;
			int depthMode;
		};

		BilateralFilterPushConst m_BilateralFilterPushConstant;

		VansComputeShader* m_BilateralFilterShader;

		VansComputeShader* m_VolumetrcFogShader;

		VansComputeShader* m_FogLightInjectionShader;
		VansComputeShader* m_FogRayMarchShader;

		// --- 体积云 Cloud Ray March Pass ---
		VansComputeShader*             m_CloudRayMarchShader         = nullptr;
		VkDescriptorSetLayout          m_CloudRayMarchSetLayout       = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_CloudRayMarchDescriptorSets;
		VansVKBuffer                   m_CloudParamsCBBuffer;   // CloudParams UBO
		VansCloudParamsGPU            m_CloudParams;           // CPU 侧体积云参数权威来源

		// ---- TileLight Build Pass ----
		VansVKBuffer m_TileLightHeaderBuffer;
		VansVKBuffer m_TileLightIndexBuffer;
		VkDescriptorSetLayout          m_TileLightBuildSetLayout      = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_TileLightBuildDescriptorSets;
		VansVKBuffer m_TileLightBuildParamsCBBuffer;
		VansComputeShader* m_TileLightBuildShader = nullptr;
		uint32_t m_TileLightGridX = 0;
		uint32_t m_TileLightGridY = 0;

		// --- 后处理 Compute Shaders & Descriptors ---
		// Exposure：两步（亮度直方图缩图 + 自适应曝光收敛）
		VansComputeShader* m_ExposureLuminanceShader = nullptr;
		VkDescriptorSetLayout          m_ExposureLuminanceSetLayout      = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_ExposureLuminanceDescriptorSets;

		VansComputeShader* m_ExposureAdaptShader    = nullptr;
		VkDescriptorSetLayout          m_ExposureAdaptSetLayout          = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_ExposureAdaptDescriptorSets;

		// Bloom：预滤 + 4 级 Downsample + 4 级 Upsample
		VansComputeShader* m_BloomPrefilterShader   = nullptr;
		VkDescriptorSetLayout          m_BloomPrefilterSetLayout         = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_BloomPrefilterDescriptorSets;

		VansComputeShader* m_BloomDownsampleShader  = nullptr;
		// Downsample 每个 mip 级独立 descriptor set：[mip0→mip1, mip1→mip2, mip2→mip3, mip3→mip4]
		VkDescriptorSetLayout          m_BloomDownsampleSetLayout        = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_BloomDownsampleDescriptorSets;  // 4 个 set (4 级)

		VansComputeShader* m_BloomUpsampleShader    = nullptr;
		// Upsample 每个 mip 级独立 descriptor set：[mip3+mip4→mip3, mip2+mip3→mip2, mip1+mip2→mip1, mip0+mip1→result]
		VkDescriptorSetLayout          m_BloomUpsampleSetLayout          = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_BloomUpsampleDescriptorSets;    // 4 个 set (4 级)

		VansVKBuffer m_PostProcessParamsCBBuffer;       // VansPostProcessParamsGPU UBO（Final Composite frag）
		VansVKBuffer m_ExposureAdaptParamsCBBuffer;     // VansExposureAdaptParamsGPU UBO（Exposure Adapt compute）
		VansVKBuffer m_BloomParamsCBBuffer;             // VansBloomParamsGPU UBO（Bloom Prefilter + Upsample compute）

		// CPU 侧后处理参数权威来源，Inspector 直接读写此对象
		VansPostProcessProfile m_PostProcessProfile;

	public:

		VansVKBuffer m_AtmospherePBRDataBuffer;

		
	public:

		//梭有材质公用
		void UpdatePBRLutDescriptorSets();

		void UpdateAtmosphereDescriptorSets();

		void UploadCloudParamsToGPU();

		VansRuntimeRenderTextureManager m_RuntimeRenderTextureManager;

	};

	// ============================================================
	// Base Material — asset name (from VansAsset), type tag, shader
	// No texture or parameter data lives here.
	// ============================================================
	class VansMaterial : public VansAsset
	{
		friend class VansScene;

	public:
		VansMaterialType    m_MaterialType;

		// pass name → shader (populated at scene load from Material Pass Table)
		std::unordered_map<std::string, VansGraphicsShader*> m_PassShaders;

		// Lookup shader for a specific pass. Returns nullptr if this material
		// does not participate in that pass.
		VansGraphicsShader* GetPassShader(const std::string& passName) const;
		bool                HasPass(const std::string& passName) const;

		virtual ~VansMaterial() = default;
	};

	// ============================================================
	// VansPBRMaterial 鈥?opaque PBR surface (type 0)
	// ============================================================
	class VansPBRMaterial : public VansMaterial
	{
	public:
		VansTexture* m_BaseColorTexture  = nullptr;
		VansTexture* m_NormalTexture     = nullptr;
		VansTexture* m_MetalTexture      = nullptr;
		VansTexture* m_RoughnessTexture  = nullptr;
		VansTexture* m_AoTexture         = nullptr;

		VansBasePBRParam m_BasePBRParam;

		// Index into the global PBR param SSBO / bindless texture array.
		// Assigned during PreparePBRMaterialData; used by draw push-constant.
		int m_MaterialIndex = -1;
	};

	// ============================================================
	// VansEmissiveMaterial — 自发光表面 (type 14)
	// 复用 VansBasePBRParam 的 GPU SSBO 槽位布局：
	//   m_BasePBRParam.m_albedo    → 发光颜色 (RGB)
	//   m_BasePBRParam.m_roughness → 发光强度 (scalar, 支持 HDR >1.0)
	// Deferred.frag 读取 color.rgb * roughness 直接输出，跳过全部 BRDF/光照/阴影
	// ============================================================
	class VansEmissiveMaterial : public VansMaterial
	{
	public:
		// 发光颜色和强度，映射到全局 PBR SSBO 的 albedo/roughness 槽
		VansBasePBRParam m_BasePBRParam;

		// 可选自发光纹理（使用 Bindless slot 0，与 PBR albedo 规则一致）
		// 未设置时绑定 defaultAlbedo（纯白 1×1），乘法中性
		VansTexture* m_EmissiveTexture = nullptr;

		// 全局 PBR SSBO 中的材质索引，由 PreparePBRMaterialData 赋值
		int m_MaterialIndex = -1;

		// 视频资源名称（来自 resource.json 注册的名称）。
		// LoadSceneObjects 中用于在对象上自动创建 VansScriptVideoComponent。
		// 空字符串表示该材质不使用视频纹理。
		std::string m_VideoName;
	};

	// ============================================================
	// VansDecalMaterial — 贴花材质 (type 15)
	// 复用全局 PBR SSBO / Bindless 纹理体系，结构与 VansPBRMaterial 相同
	// 贴花阶段说明：
	//   采样 GBuffer2 重建世界坐标，判断是否在 OBB 内，将贴花 UV 映射到 PBR 纹理
	//   MRT 输出：附件 0=Normal，附件 1=GBuffer0(albedo/alpha)，附件 2=GBuffer1(metallic+AO)
	// ============================================================
	class VansDecalMaterial : public VansMaterial
	{
	public:
		VansTexture* m_BaseColorTexture  = nullptr;
		VansTexture* m_NormalTexture     = nullptr;
		VansTexture* m_MetalTexture      = nullptr;
		VansTexture* m_RoughnessTexture  = nullptr;
		VansTexture* m_AoTexture         = nullptr;

		VansBasePBRParam m_BasePBRParam;

		// 全局 PBR SSBO 中的材质索引，由 PreparePBRMaterialData 赋值
		int m_MaterialIndex = -1;
	};

	// ============================================================
	// VansTransparentMaterial — multi-texture transparent pass (type 2)
	// ============================================================
	class VansTransparentMaterial : public VansMaterial
	{
	public:
		~VansTransparentMaterial() override;

		// Flat ordered texture list; binding index == position in vector
		std::vector<VansTexture*>                          m_TransparentTextures;
		// (slot label, asset name) 鈥?for debugging / tooling
		std::vector<std::pair<std::string, std::string>>   m_TransparentTextureMap;

		// Owned descriptor set layout / set for this transparent material
		VkDescriptorSetLayout          m_TransparentOwnedLayout  = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_TransparentOwnedDescSets;

		void BuildTransparentTextureDescriptors();
		void CreateTransparentDescriptorLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings = {});
	};

	// ============================================================
	// VansSkyBoxMaterial 鈥?sky / atmosphere (type 4)
	// ============================================================
	class VansSkyBoxMaterial : public VansMaterial
	{
	public:
		VansAtmospherePBRParam m_AtmospherePBRParam;

		void UpdateAtmosphereMaterialData(VansMaterialManager& materialManager, VansLightManager& lightManager);
	};

	// ============================================================
	// VansSkinMaterial 鈥?subsurface skin shading (type 9)
	// ============================================================
	class VansSkinMaterial : public VansMaterial
	{
	public:
		~VansSkinMaterial() override;

		VansTexture* m_BaseColorTexture = nullptr;
		VansTexture* m_NormalTexture    = nullptr;

		VkDescriptorSetLayout          m_SkinOwnedLayout  = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_SkinOwnedDescSets;

		void BuildSkinTextureDescriptors();
	};
	// ============================================================
	// VansClothMaterial — cloth shading (type 10)
	// ============================================================
	class VansClothMaterial : public VansMaterial
	{
	public:
		~VansClothMaterial() override;

		VansTexture* m_BaseColorTexture  = nullptr;
		VansTexture* m_NormalTexture     = nullptr;
		VansTexture* m_RoughnessTexture  = nullptr;
		VansTexture* m_AoTexture         = nullptr;

		float        m_SheenRoughness    = 0.5f;   // 0 = silk, 1 = rough fabric

		VkDescriptorSetLayout          m_ClothOwnedLayout   = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_ClothOwnedDescSets;

		void BuildClothTextureDescriptors();
	};
	// ============================================================
	// VansHairMaterial — card-based hair shading (type 11)
	// Textures: albedo+alpha, normal, roughness, AO, strand shift
	// ============================================================
	class VansHairMaterial : public VansMaterial
	{
	public:
		~VansHairMaterial() override;

		VansTexture* m_AlbedoAlphaTexture = nullptr;  // .rgb = albedo, .a = alpha mask
		VansTexture* m_NormalTexture      = nullptr;  // tangent-space normal
		VansTexture* m_RoughnessTexture   = nullptr;  // .r = roughness
		VansTexture* m_AoTexture          = nullptr;  // .r = ambient occlusion
		VansTexture* m_ShiftTexture       = nullptr;  // .r = strand shift (0.5 = neutral)
		VansTexture* m_AlphaTexture       = nullptr;  // .r = dedicated alpha mask
		VansTexture* m_FlowTexture        = nullptr;  // .rg = tangent-space flow direction (0.5 = neutral)

		VkDescriptorSetLayout          m_HairOwnedLayout  = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_HairOwnedDescSets;

		void BuildHairTextureDescriptors();
	};
	// ============================================================
	// VansSubsurfaceMaterial — subsurface scattering (type 12)
	// Textures: albedo, normal, thickness map
	// Parameters: subsurfacePower, subsurfaceColor
	// ============================================================
	class VansSubsurfaceMaterial : public VansMaterial
	{
	public:
		~VansSubsurfaceMaterial() override;

		VansTexture* m_BaseColorTexture  = nullptr;
		VansTexture* m_NormalTexture     = nullptr;
		VansTexture* m_ThicknessTexture  = nullptr;  // .r = normalized thickness [0,1]
		VansTexture* m_RoughnessTexture  = nullptr;  // .r = perceptual roughness

		float        m_SubsurfacePower   = 12.234f;  // forward-scatter sharpness
		float        m_Thickness         = 0.5f;      // default constant thickness
		glm::vec3    m_SubsurfaceColor   = glm::vec3(1.0f, 0.2f, 0.1f); // scatter tint

		VkDescriptorSetLayout          m_SubsurfaceOwnedLayout  = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_SubsurfaceOwnedDescSets;

		void BuildSubsurfaceTextureDescriptors();
	};

	// ============================================================
	// Pass-only materials — carry only the shader (inherited from base)
	// ============================================================
	class VansPostProcessMaterial : public VansMaterial {};
	class VansDeferredMaterial    : public VansMaterial {};
	class VansSSAOMaterial        : public VansMaterial {};

	// ============================================================
	// VansGrassMaterial — GPU-driven vegetation (type 13)
	// Textures: albedo, normal, roughness, translucency, AO
	// Parameters: GrassParams struct uploaded to global PBR SSBO
	// Only participates in GBUFFER pass (no shadows)
	// ============================================================
	struct GrassParams
	{
		glm::vec4 baseColor       = glm::vec4(0.2f, 0.6f, 0.1f, 1.0f); // sRGB base tint
		float     roughness       = 0.6f;
		float     metallic        = 0.0f;
		float     translucency    = 0.5f;   // 0..1 scatter strength
		float     scatterWidth    = 0.5f;   // wrap lighting half-angle
		float     sssDistortion   = 0.2f;   // normal distortion for back-scatter
		float     sssAmbient      = 0.1f;   // ambient scatter floor
		float     sssPower        = 3.0f;   // exponent for view-dependent scatter
		float     aoStrength      = 1.0f;   // AO contribution
	};

	class VansGrassMaterial : public VansMaterial
	{
	public:
		~VansGrassMaterial() override;

		VansTexture* m_AlbedoTexture        = nullptr;
		VansTexture* m_NormalTexture        = nullptr;
		VansTexture* m_RoughnessTexture     = nullptr;
		VansTexture* m_TranslucencyTexture  = nullptr;
		VansTexture* m_AOTexture            = nullptr;

		GrassParams  m_GrassParams;

		VkDescriptorSetLayout          m_GrassOwnedLayout  = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_GrassOwnedDescSets;

		void BuildGrassTextureDescriptors();
	};
}
