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
	struct alignas(16) SSGIParamsGPU
	{
		glm::vec4 screenSize;
		glm::vec4 giVolumeMin;
		glm::vec4 giVolumeSizeAndBias;
		glm::vec4 traceParams; // x=max trace distance, y=fade start ratio
	};
	static_assert(sizeof(SSGIParamsGPU) == 64, "SSGI parameter layout must match GLSL");

	struct alignas(16) ScreenSpaceShadowParamsGPU
	{
		glm::vec4 screenSize;
		glm::vec4 halfSize;
		glm::vec4 rayParams;
		glm::vec4 fadeParams;
	};
	static_assert(sizeof(ScreenSpaceShadowParamsGPU) == 64, "Screen-space shadow parameter layout must match GLSL");

	// ============================================================
	// Well-known render-pass name constants.
	// Each engine render pass queries the material for its unique pass name.
	// ============================================================
	namespace VansPass
	{
		static constexpr const char* GBUFFER          = "gbuffer";          // opaque GBuffer fill
		static constexpr const char* SHADOW           = "shadow";           // cascade shadow map
		static constexpr const char* PUNCTUAL_SHADOW  = "punctualShadow";   // point/spot shadow
		static constexpr const char* FORWARD_OPAQUE_AFTER_DEFERRED = "forwardOpaqueAfterDeferred";
		static constexpr const char* FORWARD_TRANSPARENT = "transparent";   // forward transparent
		static constexpr const char* SKY_BOX          = "skybox";           // sky box
		static constexpr const char* DEFERRED         = "deferred";         // deferred lighting resolve
		static constexpr const char* POST_PROCESS     = "postProcess";      // post-processing
		static constexpr const char* SCREEN_SPACE     = "screenSpace";      // SSAO / SSR etc.
		static constexpr const char* HAIR_VISIBILITY  = "hairVisibility";
		static constexpr const char* HAIR_SHADOW      = "hairShadow";
		static constexpr const char* HAIR_LIGHTING    = "hairLighting";
		static constexpr const char* HAIR_COMPOSITE   = "hairComposite";
		// future
		static constexpr const char* VELOCITY         = "velocity";
		static constexpr const char* PRE_DEPTH        = "preDepth";
		// 璐磋姳闃舵锛氳鍐?Normal / GBuffer0 / GBuffer1
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
		VAN_EMISSIVE = 14,   // 鑷彂鍏夋潗璐細albedo 脳 intensity 鐩撮€氾紝鏃?BRDF
		VAN_DECAL    = 15,   // 璐磋姳鏉愯川锛氬彔鍐欏埌 GBuffer Normal/GBuffer0/GBuffer1
		VAN_WATER    = 16,   // 姘撮潰鏉愯川锛氱嫭绔?Water GBuffer + Composite 绠＄嚎
		VAN_CUSTOM_SHADER = 17,
	};

	// Lightweight push-constant payload built at draw time.
	// Each field maps to a global GPU resource index.
	struct alignas(16) VansDrawPushConstant
	{
		int materialIndex;    // index into global PBR param SSBO / bindless textures
		int transformIndex;   // index into per-object transform SSBO
		int animationEnabled; // 1 = skinned, 0 = static
		int passUser0;        // reserved for pass-specific data; keeps ABI at 16 bytes
	};
	static_assert(sizeof(VansDrawPushConstant) == 16, "VansDrawPushConstant must stay 16 bytes");

	static constexpr int VANS_CUSTOM_MATERIAL_VEC4_COUNT = 8;
	static constexpr int VANS_CUSTOM_MATERIAL_TEXTURE_COUNT = 4;

	struct alignas(16) VansCustomMaterialPayload
	{
		glm::vec4 values[VANS_CUSTOM_MATERIAL_VEC4_COUNT] = {};
		glm::ivec4 textureIndices = glm::ivec4(-1);
	};
	static_assert(sizeof(VansCustomMaterialPayload) == 144, "Custom material payload layout must match GLSL");

	// 浣撶Н浜?GPU 鍙傛暟锛屼笌 CloudRayMarch.comp 鐨?CloudParamsUBO 淇濇寔浜岃繘鍒朵竴鑷淬€?
	struct alignas(16) VansCloudParamsGPU
	{
		float planetRadius         = 6340000.0f;
		float seaLevel             = 200.0f;
		float cloudMinHeight       = 1070.0f;     // Cloud base height (m)
		float cloudMaxHeight       = 7410.0f;     // Cloud top height = base + thickness (m)

		float density              = 0.025f;
		float coverage             = 0.350f;
		float sunBrightness        = 0.380f;
		float phaseG               = 0.365f;

		float mainTileMeters       = 43300.0f;
		float detailTileMeters     = 2200.0f;
		float mainHeightScale      = 0.260f;
		float detailHeightScale    = 3.070f;

		float thresholdLowCoverage = 0.115f;      // Overcast threshold
		float thresholdHighCoverage = 0.720f;     // Clear threshold
		float densityRemapLow      = 0.425f;
		float densityRemapHigh     = 0.915f;

		float mainErosionStrength  = 1.160f;
		float detailErosionStrength = 1.340f;
		float edgeErosionStrength  = 0.500f;
		float verticalShapePower   = 1.420f;

		float detailErosionLow     = 0.280f;
		float detailErosionHigh    = 0.810f;
		float detailEdgeStrength   = 0.270f;
		float shadowDensityScale   = 0.870f;
	};

	class VansMaterialManager
	{
		friend class VansMaterial;
		friend class VansMaterialLiveEditService;
		friend class VansRenderNode;
		//鐢ㄤ簬瀛樺偍鏉愯川鏁版嵁buffer鐨勬弿杩扮
		//瀹為檯buffer鐢眒aterial鎸佹湁
	private:

		void InitMaterialDataDescriptors();

	public:
		static constexpr const char* RT_SSAO_RESULT = "Runtime.SSAO.Result";
		static constexpr const char* RT_SSGI_RESULT = "Runtime.SSGI.Result";
		static constexpr const char* RT_SSGI_TEMPORAL_A = "Runtime.SSGI.TemporalA";
		static constexpr const char* RT_SSGI_TEMPORAL_B = "Runtime.SSGI.TemporalB";
		static constexpr const char* RT_HZB_RESULT = "Runtime.HZB.Result";
		static constexpr const char* RT_SCREEN_SPACE_SHADOW_RESULT = "Runtime.ScreenSpaceShadow.Result";
		static constexpr const char* RT_SCREEN_SPACE_SHADOW_FILTER_RESULT = "Runtime.ScreenSpaceShadow.FilterResult";
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
		static constexpr const char* RT_RECT_LIGHT_EMISSIVE = "Runtime.RectLight.EmissiveArray";
		static constexpr const char* RT_FOG_VOXEL_INJECTION = "Runtime.Fog.VoxelInjection";
		static constexpr const char* RT_FOG_VOXEL_INJECTION_HISTORY = "Runtime.Fog.VoxelInjectionHistory";
		static constexpr const char* RT_FOG_VOXEL_RAYMARCH  = "Runtime.Fog.VoxelRayMarch";
		static constexpr const char* RT_HAIR_VIS0           = "Runtime.Hair.Vis0";
		static constexpr const char* RT_HAIR_VIS1           = "Runtime.Hair.Vis1";
		static constexpr const char* RT_HAIR_VIS2           = "Runtime.Hair.Vis2";
		static constexpr const char* RT_HAIR_VIS3           = "Runtime.Hair.Vis3";
		static constexpr const char* RT_HAIR_DEPTH          = "Runtime.Hair.Depth";
		static constexpr const char* RT_HAIR_COVERAGE       = "Runtime.Hair.Coverage";
		static constexpr const char* RT_HAIR_COLOR          = "Runtime.Hair.Color";
		static constexpr const char* RT_HAIR_DEEP_OPACITY   = "Runtime.Hair.DeepOpacity";
		// 1/4 鍒嗚鲸鐜囦綋绉簯缁撴灉锛圧GB=鍐呮暎灏勶紝A=閫忓皠鐜囷級
		static constexpr const char* RT_CLOUD_BUFFER         = "Runtime.Cloud.Buffer";
		static constexpr const char* RT_CLOUD_MAIN_NOISE     = "Runtime.Cloud.MainNoise3D";
		static constexpr const char* RT_CLOUD_DETAIL_NOISE   = "Runtime.Cloud.DetailNoise3D";

		// --- 鍚庡鐞?RuntimeRT ---
		static constexpr const char* RT_EXPOSURE_LUMINANCE  = "Runtime.PostProcess.Exposure.Luminance"; // 64x64 浜害缂╁浘锛圧16F锛?
		static constexpr const char* RT_EXPOSURE_CURRENT    = "Runtime.PostProcess.Exposure.Current";   // 1x1 褰撳墠鏇濆厜鍊硷紙R16F锛?
		static constexpr const char* RT_BLOOM_PREFILTER     = "Runtime.PostProcess.Bloom.Prefilter";    // 鍗婂垎杈ㄧ巼棰勬护杈撳嚭锛圧GBA16F锛?
		static constexpr const char* RT_BLOOM_MIP0          = "Runtime.PostProcess.Bloom.Mip0";         // 1/2 鍒嗚鲸鐜?
		static constexpr const char* RT_BLOOM_MIP1          = "Runtime.PostProcess.Bloom.Mip1";         // 1/4 鍒嗚鲸鐜?
		static constexpr const char* RT_BLOOM_MIP2          = "Runtime.PostProcess.Bloom.Mip2";         // 1/8 鍒嗚鲸鐜?
		static constexpr const char* RT_BLOOM_MIP3          = "Runtime.PostProcess.Bloom.Mip3";         // 1/16 鍒嗚鲸鐜?
		static constexpr const char* RT_BLOOM_RESULT        = "Runtime.PostProcess.Bloom.Result";       // Upsample 鏈€缁堝悎鎴愶紙RGBA16F锛?

		bool RegisterRuntimeRenderTexture(const std::string& name, VansTexture* texture, bool replaceExisting = true);

		VansTexture* GetRuntimeRenderTexture(const std::string& name) const;

		bool HasRuntimeRenderTexture(const std::string& name) const;

		bool RemoveRuntimeRenderTexture(const std::string& name);

		// 浠呬粠娉ㄥ唽琛ㄧЩ闄ゆ潯鐩紝涓?delete 绾圭悊锛堟墍鏈夋潈褰掑閮ㄧ郴缁燂紝濡?VansRayTracing锛?
		bool UnregisterRuntimeRenderTexture(const std::string& name);

		void ClearRuntimeRenderTextures();

		// 鈹€鈹€ 鍦烘櫙鍒囨崲鏃舵竻绌哄満鏅骇 PBR 鏁版嵁 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
		// 娓呴櫎 GlobalPBR 鏁扮粍鍜屽搴?GPU buffer / descriptor銆?
		// 涓嶉噴鏀?PreConv/LUT 绛夐」鐩骇璧勬簮銆?
		void ClearScenePBRData(VkDevice device);

		VkDescriptorSetLayout m_MaterialAtmosphereDataLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_MaterialAtmosphereDataDescriptorSets;

		//PBR棰勫嵎绉?
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

		VkDescriptorSetLayout m_ScreenSpaceShadowSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_ScreenSpaceShadowDescriptorSets;
		VansVKBuffer m_ScreenSpaceShadowParamsCBBuffer;

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


		//鍏ㄥ眬pbr鍙傛暟buffer锛屼笉姣忎釜pbr鏉愯川鑷繁鎸佹湁
		VansVKBuffer m_GlobalPBRDataBuffer;
		VansVKBuffer m_GlobalCustomMaterialDataBuffer;
		std::vector<VansPBRMaterial*> m_GlobalPBRMaterial;
		std::vector<VansBasePBRParam> m_GlobalPBRParamData;
		std::vector<VansCustomMaterialPayload> m_GlobalCustomMaterialParamData;
		VkDescriptorSetLayout m_GlobalPBRDataSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_GlobalPBRDataDescriptorSets;

		//鍏ㄥ眬pbr璐村浘鐨刡indless descriptor set, 涓嶆瘡涓潗璐ㄨ嚜宸辨寔鏈?
		std::vector<VansVKImage*> m_GlobalPBRTextures;
		VkDescriptorSetLayout m_GlobalPBRTexSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_GlobalPBRTexDescriptorSets;

		// 鍦烘櫙 Global Descriptor Set锛圫et 0锛夌殑蹇嵎寮曠敤锛岀敤浜庤棰戝垏鎹㈡椂鐩存帴鏇存柊 Bindless 妲姐€?
		// 鐢?LoadSceneForRendering 鍦?CreateGlobalDescriptorSet 涔嬪悗鍐欏叆銆?
		VkDescriptorSet m_VideoBindlessDescriptorSet = VK_NULL_HANDLE;

		//淇濆瓨鍏ㄥ眬鐨勪竴浜泃exture鏁版嵁
		VansTexture* m_PreConvDiffuse = nullptr;

		VansTexture* m_PreConvSpecular = nullptr;

		VansTexture* m_BRDFIntegralLUT = nullptr;

		VansTexture* m_SkinBSDFLUT = nullptr;

		VansTexture* m_ClothBRDFLUT = nullptr;

		VansTexture* m_MoonAlbedoTexture = nullptr;

		// LTC LUTs (Linearly Transformed Cosines, area-light BRDF)
		// 64x64, RGBA16F, runtime-uploaded from embedded float arrays (LTCData.h).
		VansTexture* m_LTC1 = nullptr;

		VansTexture* m_LTC2 = nullptr;

		// 闈㈠厜婧愬彂鍏夎创鍥炬暟缁勶細256脳256脳32锛孷K_IMAGE_TYPE_2D + layerCount=32锛宻ampler2DArray銆?
		// 鐢?VansVKRenderDataPreparation 鍒涘缓锛岀敱 VansSceneLoader 鎸夐渶濉厖鍚勫眰銆?
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

		// HIZ Seed锛氬皢 GBuffer 绾挎€ф繁搴﹀啓鍏?mip 0
		VansComputeShader* m_HIZSeedShader;
		VkDescriptorSetLayout m_HIZSeedSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_HIZSeedDescriptorSets;

		VansComputeShader* m_SSRTraceShader;

		VansComputeShader* m_ScreenSpaceShadowShader = nullptr;

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

		// --- 浣撶Н浜?Cloud Ray March Pass ---
		VansComputeShader*             m_CloudRayMarchShader         = nullptr;
		VkDescriptorSetLayout          m_CloudRayMarchSetLayout       = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_CloudRayMarchDescriptorSets;
		VansVKBuffer                   m_CloudParamsCBBuffer;   // CloudParams UBO
		VansCloudParamsGPU            m_CloudParams;           // CPU 渚т綋绉簯鍙傛暟鏉冨▉鏉ユ簮

		// ---- TileLight Build Pass ----
		VansVKBuffer m_TileLightHeaderBuffer;
		VansVKBuffer m_TileLightIndexBuffer;
		VkDescriptorSetLayout          m_TileLightBuildSetLayout      = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_TileLightBuildDescriptorSets;
		VansVKBuffer m_TileLightBuildParamsCBBuffer;
		VansComputeShader* m_TileLightBuildShader = nullptr;
		uint32_t m_TileLightGridX = 0;
		uint32_t m_TileLightGridY = 0;

		// --- 鍚庡鐞?Compute Shaders & Descriptors ---
		// Exposure锛氫袱姝ワ紙浜害鐩存柟鍥剧缉鍥?+ 鑷€傚簲鏇濆厜鏀舵暃锛?
		VansComputeShader* m_ExposureLuminanceShader = nullptr;
		VkDescriptorSetLayout          m_ExposureLuminanceSetLayout      = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_ExposureLuminanceDescriptorSets;

		VansComputeShader* m_ExposureAdaptShader    = nullptr;
		VkDescriptorSetLayout          m_ExposureAdaptSetLayout          = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_ExposureAdaptDescriptorSets;

		// Bloom锛氶婊?+ 4 绾?Downsample + 4 绾?Upsample
		VansComputeShader* m_BloomPrefilterShader   = nullptr;
		VkDescriptorSetLayout          m_BloomPrefilterSetLayout         = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_BloomPrefilterDescriptorSets;

		VansComputeShader* m_BloomDownsampleShader  = nullptr;
		// Downsample 姣忎釜 mip 绾х嫭绔?descriptor set锛歔mip0鈫抦ip1, mip1鈫抦ip2, mip2鈫抦ip3, mip3鈫抦ip4]
		VkDescriptorSetLayout          m_BloomDownsampleSetLayout        = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_BloomDownsampleDescriptorSets;  // 4 涓?set (4 绾?

		VansComputeShader* m_BloomUpsampleShader    = nullptr;
		// Upsample 姣忎釜 mip 绾х嫭绔?descriptor set锛歔mip3+mip4鈫抦ip3, mip2+mip3鈫抦ip2, mip1+mip2鈫抦ip1, mip0+mip1鈫抮esult]
		VkDescriptorSetLayout          m_BloomUpsampleSetLayout          = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_BloomUpsampleDescriptorSets;    // 4 涓?set (4 绾?

		VansVKBuffer m_PostProcessParamsCBBuffer;       // VansPostProcessParamsGPU UBO锛團inal Composite frag锛?
		VansVKBuffer m_ExposureAdaptParamsCBBuffer;     // VansExposureAdaptParamsGPU UBO锛圗xposure Adapt compute锛?
		VansVKBuffer m_BloomParamsCBBuffer;             // VansBloomParamsGPU UBO锛圔loom Prefilter + Upsample compute锛?

		// CPU 渚у悗澶勭悊鍙傛暟鏉冨▉鏉ユ簮锛孖nspector 鐩存帴璇诲啓姝ゅ璞?
		VansPostProcessProfile m_PostProcessProfile;

	public:

		VansVKBuffer m_AtmospherePBRDataBuffer;

		
	public:

		//姊湁鏉愯川鍏敤
		void UpdatePBRLutDescriptorSets();

		void UpdateAtmosphereDescriptorSets();

		void UploadCloudParamsToGPU();

		VansRuntimeRenderTextureManager m_RuntimeRenderTextureManager;

	};

	// ============================================================
	// Base Material 鈥?asset name (from VansAsset), type tag, shader
	// No texture or parameter data lives here.
	// ============================================================
	class VansMaterial : public VansAsset
	{
		friend class VansScene;

	public:
		VansMaterialType    m_MaterialType;

		// pass name 鈫?shader (populated at scene load from Material Pass Table)
		std::unordered_map<std::string, VansGraphicsShader*> m_PassShaders;
		std::unordered_map<std::string, std::string> m_PassShaderOverrides;
		int m_MaterialIndex = -1;

		// Lookup shader for a specific pass. Returns nullptr if this material
		// does not participate in that pass.
		VansGraphicsShader* GetPassShader(const std::string& passName) const;
		bool                HasPass(const std::string& passName) const;

		VansCustomMaterialPayload m_CustomMaterialPayload;
		std::unordered_map<std::string, int> m_CustomParameterSlots;
		std::unordered_map<std::string, int> m_CustomTextureSlots;
		std::unordered_map<std::string, VansTexture*> m_CustomTextures;

		virtual ~VansMaterial() = default;
	};

	// ============================================================
	// VansPBRMaterial - opaque PBR surface (type 0)
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
	// VansEmissiveMaterial 鈥?鑷彂鍏夎〃闈?(type 14)
	// 澶嶇敤 VansBasePBRParam 鐨?GPU SSBO 妲戒綅甯冨眬锛?
	//   m_BasePBRParam.m_albedo    鈫?鍙戝厜棰滆壊 (RGB)
	//   m_BasePBRParam.m_roughness 鈫?鍙戝厜寮哄害 (scalar, 鏀寔 HDR >1.0)
	// Deferred.frag 璇诲彇 color.rgb * roughness 鐩存帴杈撳嚭锛岃烦杩囧叏閮?BRDF/鍏夌収/闃村奖
	// ============================================================
	class VansEmissiveMaterial : public VansMaterial
	{
	public:
		// 鍙戝厜棰滆壊鍜屽己搴︼紝鏄犲皠鍒板叏灞€ PBR SSBO 鐨?albedo/roughness 妲?
		VansBasePBRParam m_BasePBRParam;

		// 鍙€夎嚜鍙戝厜绾圭悊锛堜娇鐢?Bindless slot 0锛屼笌 PBR albedo 瑙勫垯涓€鑷达級
		// 鏈缃椂缁戝畾 defaultAlbedo锛堢函鐧?1脳1锛夛紝涔樻硶涓€?
		VansTexture* m_EmissiveTexture = nullptr;

		// 鍏ㄥ眬 PBR SSBO 涓殑鏉愯川绱㈠紩锛岀敱 PreparePBRMaterialData 璧嬪€?
		int m_MaterialIndex = -1;

		// Video runtime name resolved from an asset reference.
		// LoadSceneObjects 涓敤浜庡湪瀵硅薄涓婅嚜鍔ㄥ垱寤?VansScriptVideoComponent銆?
		// 绌哄瓧绗︿覆琛ㄧず璇ユ潗璐ㄤ笉浣跨敤瑙嗛绾圭悊銆?
		std::string m_VideoName;
	};

	// ============================================================
	// VansDecalMaterial 鈥?璐磋姳鏉愯川 (type 15)
	// 澶嶇敤鍏ㄥ眬 PBR SSBO / Bindless 绾圭悊浣撶郴锛岀粨鏋勪笌 VansPBRMaterial 鐩稿悓
	// 璐磋姳闃舵璇存槑锛?
	//   閲囨牱 GBuffer2 閲嶅缓涓栫晫鍧愭爣锛屽垽鏂槸鍚﹀湪 OBB 鍐咃紝灏嗚创鑺?UV 鏄犲皠鍒?PBR 绾圭悊
	//   MRT 杈撳嚭锛氶檮浠?0=Normal锛岄檮浠?1=GBuffer0(albedo/alpha)锛岄檮浠?2=GBuffer1(metallic+AO)
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

		// 鍏ㄥ眬 PBR SSBO 涓殑鏉愯川绱㈠紩锛岀敱 PreparePBRMaterialData 璧嬪€?
		int m_MaterialIndex = -1;
	};

	// ============================================================
	// VansTransparentMaterial 鈥?multi-texture transparent pass (type 2)
	// ============================================================
	class VansTransparentMaterial : public VansMaterial
	{
	public:
		~VansTransparentMaterial() override;

		// Flat ordered texture list; binding index == position in vector
		std::vector<VansTexture*>                          m_TransparentTextures;
		// (slot label, asset name) - for debugging / tooling
		std::vector<std::pair<std::string, std::string>>   m_TransparentTextureMap;

		// Owned descriptor set layout / set for this transparent material
		VkDescriptorSetLayout          m_TransparentOwnedLayout  = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_TransparentOwnedDescSets;

		void BuildTransparentTextureDescriptors();
		void CreateTransparentDescriptorLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings = {});
	};

	// ============================================================
	// VansSkyBoxMaterial - sky / atmosphere (type 4)
	// ============================================================
	class VansSkyBoxMaterial : public VansMaterial
	{
	public:
		VansAtmospherePBRParam m_AtmospherePBRParam;
		float m_SunDiskAngularRadius = 0.00465f;
		float m_SunDiskFeather = 0.00075f;
		float m_SunDiskRadianceScale = 1.0f;
		float m_SunDiskOcclusionStrength = 6.0f;
		bool m_SunDiskEnabled = true;
		float m_MoonDiskAngularRadius = 0.00465f;
		float m_MoonDiskFeather = 0.00075f;
		float m_MoonDiskRadianceScale = 0.00008f;
		float m_MoonDiskOcclusionStrength = 6.0f;
		bool m_MoonDiskEnabled = true;

		void UpdateAtmosphereMaterialData(VansMaterialManager& materialManager, VansLightManager& lightManager);
	};

	// ============================================================
	// VansSkinMaterial - subsurface skin shading (type 9)
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
	// VansClothMaterial 鈥?cloth shading (type 10)
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
		VansBasePBRParam m_BasePBRParam{ glm::vec3(1.0f), 0.5f, 0.35f, 1.0f, 0.5f };

		VkDescriptorSetLayout          m_ClothOwnedLayout   = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_ClothOwnedDescSets;

		void BuildClothTextureDescriptors();
	};
	// ============================================================
	// VansHairMaterial 鈥?card-based hair shading (type 11)
	// Textures: albedo+alpha, normal, roughness, AO, strand shift
	// ============================================================
	struct alignas(16) VansHairParamsGPU
	{
		glm::vec4 absorption     = glm::vec4(0.35f, 0.22f, 0.12f, 1.0f);
		glm::vec4 roughnessScale = glm::vec4(1.0f, 0.55f, 2.0f, 0.35f);
		glm::vec4 shiftParams    = glm::vec4(1.0f, 1.0f, 1.5f, 0.25f);
		glm::vec4 coverageParams = glm::vec4(0.35f, 1.5f, 0.25f, 1.0f);
	};
	static_assert(sizeof(VansHairParamsGPU) == sizeof(glm::vec4) * 4, "VansHairParamsGPU layout must match GLSL");

	class VansHairMaterial : public VansMaterial
	{
	public:
		~VansHairMaterial() override;

		VansTexture* m_AlbedoTexture      = nullptr;
		VansTexture* m_AlphaTexture       = nullptr;
		VansTexture* m_NormalTexture      = nullptr;
		VansTexture* m_RoughnessTexture   = nullptr;
		VansTexture* m_AOTexture          = nullptr;
		VansTexture* m_ShiftTexture       = nullptr;
		VansTexture* m_FlowTexture        = nullptr;
		VansTexture* m_IDTexture          = nullptr;

		VansHairParamsGPU m_Params;
		VansVKBuffer m_ParamsBuffer;
		VkDevice m_ParamsDevice = VK_NULL_HANDLE;

		VkDescriptorSetLayout          m_HairOwnedLayout  = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_HairOwnedDescSets;

		void BuildHairDescriptors(VkDevice& device);
	};
	// ============================================================
	// VansSubsurfaceMaterial 鈥?subsurface scattering (type 12)
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
		float        m_SubsurfaceAmount  = 1.0f;
		float        m_CurvatureInfluence = 0.35f;

		VansBasePBRParam m_BasePBRParam;
		int              m_MaterialIndex = -1;

		VkDescriptorSetLayout          m_SubsurfaceOwnedLayout  = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet>   m_SubsurfaceOwnedDescSets;

		void BuildSubsurfaceTextureDescriptors();
	};

	// ============================================================
	// Pass-only materials 鈥?carry only the shader (inherited from base)
	// ============================================================
	class VansPostProcessMaterial : public VansMaterial {};
	class VansDeferredMaterial    : public VansMaterial {};
	class VansSSAOMaterial        : public VansMaterial {};

	// ============================================================
	// VansGrassMaterial 鈥?GPU-driven vegetation (type 13)
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
