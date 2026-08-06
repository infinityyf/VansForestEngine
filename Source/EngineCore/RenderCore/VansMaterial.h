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

#include <cstddef>

#include <cstdint>

#include <variant>



using namespace VansGraphics;

namespace VansGraphics

{

	struct VansFogSettings

	{

		float fogDensity = 0.002f;

		float heightFalloff = 0.08f;

		float sunScatterScale = 0.3f;

		float ambientScale = 0.5f;

		float fogMinHeight = -100.0f;

		float skyFogDistance = 10000.0f;

	};



	struct VansFogVolumeSettings

	{

		float density = 0.05f;

		float anisotropy = 0.6f;

		float scatterScale = 1.0f;

		float ambientScale = 0.05f;

		float volumeNear = 2.0f;

		float volumeFar = 200.0f;

		float slicePower = 2.0f;

		float padding = 0.0f;

		float fogBoxMin[4] = { -50.0f, -50.0f, -50.0f, 0.0f };

		float fogBoxMax[4] = { 50.0f, 50.0f, 50.0f, 0.0f };

	};



	struct alignas(16) SSGIParamsGPU

	{

		glm::vec4 screenSize;

		glm::vec4 giVolumeMin;

		glm::vec4 giVolumeSizeAndBias;

		glm::vec4 traceParams; // x=max trace distance, y=fade start ratio, z=probe volume fade distance, w=reserved

	};

	static_assert(sizeof(SSGIParamsGPU) == 64, "SSGI parameter layout must match GLSL");

	struct alignas(16) SSGITemporalParamsGPU

	{

		glm::vec4 screenSize;

		glm::vec4 frameParams; // x = temporal frame index, yzw = reserved

	};

	static_assert(sizeof(SSGITemporalParamsGPU) == 32, "SSGI temporal parameter layout must match GLSL");



	struct alignas(16) ScreenSpaceShadowParamsGPU

	{

		glm::vec4 screenSize = glm::vec4(0.0f);

		glm::vec4 punctualRayParams = glm::vec4(12.0f, 0.10f, 0.020f, 64.0f); // x=max distance, y=thickness, z=normal bias, w=max steps

		glm::vec4 directionalRayParams = glm::vec4(1.75f, 0.080f, 0.020f, 48.0f); // x=max distance, y=thickness, z=normal bias, w=max steps

		glm::vec4 fadeParams = glm::vec4(32.0f, 70.0f, 1.0f, 0.95f); // x=edge fade pixels, y=directional depth fade, z=directional strength, w=punctual strength

	};

	static_assert(sizeof(ScreenSpaceShadowParamsGPU) == 64, "Screen-space shadow parameter layout must match GLSL");

	struct VansScreenSpacePunctualShadowSettings
	{
		float maxTraceDistance = 12.0f;
		float thickness = 0.10f;
		float normalBias = 0.020f;
		uint32_t maxSteps = 64;
		float strength = 0.95f;
	};



	// ============================================================

	// Well-known render-pass name constants.

	// Each engine render pass queries the material for its unique pass name.

	// ============================================================

	namespace VansPass

	{

		static constexpr const char* GBUFFER          = "gbuffer";          // opaque GBuffer fill

		static constexpr const char* SHADOW           = "shadow";           // 级联阴影图

		static constexpr const char* PUNCTUAL_SHADOW  = "punctualShadow";   // 点光/聚光阴影

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



		static constexpr const char* DECAL_GBUFFER    = "decalGBuffer";

	}

	class VansPBRMaterial;

	class VansTransparentMaterial;

	class VansSkyBoxMaterial;

	class VansSkinMaterial;

	class VansClothMaterial;
	struct VansClothGPUParam;

	class VansHairMaterial;

	class VansSubsurfaceMaterial;

	class VansPostProcessMaterial;

	class VansDeferredMaterial;

	class VansSSAOMaterial;

	class VansEmissiveMaterial;

	class VansDecalMaterial;

	class VansMaterial;



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

		VAN_EMISSIVE = 14,

		VAN_DECAL    = 15,

		VAN_WATER    = 16,

		VAN_CUSTOM_SHADER = 17,

		VAN_PBR_TRANSMISSION = 18,

		// Opaque PBR surface with a texture-masked emissive region.  This is
		// distinct from VAN_EMISSIVE: non-emitting texels still receive normal
		// PBR lighting, shadows, GI and reflection-probe specular.
		VAN_PBR_EMISSIVE = 19,

	};



	// Lightweight push-constant payload built at draw time.

	// Each field maps to a global GPU resource index.

	struct alignas(16) VansDrawPushConstant

	{

		int materialIndex;    // index into global PBR param SSBO / bindless textures

		int transformIndex;   // index into per-object transform SSBO

		std::uint32_t vertexFeatureMask; // mesh/render-item vertex deformation flags

		int passUser0;        // reserved for pass-specific data; keeps ABI at 16 bytes

	};

	static_assert(sizeof(VansDrawPushConstant) == 16, "VansDrawPushConstant must stay 16 bytes");



	static constexpr int VANS_CUSTOM_MATERIAL_VEC4_COUNT = 8;

	static constexpr int VANS_CUSTOM_MATERIAL_TEXTURE_COUNT = 5;

	using VansMaterialParameterValue = std::variant<
		std::monostate,
		bool,
		std::int32_t,
		std::uint32_t,
		float,
		glm::vec2,
		glm::vec3,
		glm::vec4,
		std::string>;



	struct alignas(16) VansCustomMaterialPayload

	{

		glm::vec4 values[VANS_CUSTOM_MATERIAL_VEC4_COUNT] = {};

		glm::ivec4 textureIndices = glm::ivec4(-1);


	};

	static_assert(sizeof(VansCustomMaterialPayload) == 144, "Custom material payload layout must match GLSL");





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

		friend class VansRenderNode;




	private:



		void InitMaterialDataDescriptors();



	public:

		static constexpr const char* RT_SSAO_RESULT = "Runtime.SSAO.Result";

		static constexpr const char* RT_SSGI_RESULT = "Runtime.SSGI.Result";

		static constexpr const char* RT_SSGI_TEMPORAL_A = "Runtime.SSGI.TemporalA";

		static constexpr const char* RT_SSGI_TEMPORAL_B = "Runtime.SSGI.TemporalB";

		static constexpr const char* RT_HZB_RESULT = "Runtime.HZB.Result";

		static constexpr const char* RT_HZB_OCCLUSION_RESULT = "Runtime.HZB.OcclusionResult";

		static constexpr const char* RT_SCREEN_SPACE_SHADOW_RESULT = "Runtime.ScreenSpaceShadow.Result";

		static constexpr const char* RT_PUNCTUAL_SHADOW_DEBUG_PREVIEW = "Runtime.PunctualShadow.DebugPreview";

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

		static constexpr const char* RT_GI_VISIBILITY_ATLAS = "Runtime.RayTracing.GI.VisibilityAtlas";

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



		static constexpr const char* RT_CLOUD_BUFFER         = "Runtime.Cloud.Buffer";

		static constexpr const char* RT_CLOUD_MAIN_NOISE     = "Runtime.Cloud.MainNoise3D";

		static constexpr const char* RT_CLOUD_DETAIL_NOISE   = "Runtime.Cloud.DetailNoise3D";
		static constexpr const char* RT_EXPOSURE_LUMINANCE   = "Runtime.PostProcess.Exposure.Luminance";
		static constexpr const char* RT_EXPOSURE_CURRENT     = "Runtime.PostProcess.Exposure.Current";




		static constexpr const char* RT_BLOOM_PREFILTER     = "Runtime.PostProcess.Bloom.Prefilter";

		static constexpr const char* RT_BLOOM_MIP0          = "Runtime.PostProcess.Bloom.Mip0";         // 1/2 鍒嗚鲸鐜?

		static constexpr const char* RT_BLOOM_MIP1          = "Runtime.PostProcess.Bloom.Mip1";         // 1/4 鍒嗚鲸鐜?

		static constexpr const char* RT_BLOOM_MIP2          = "Runtime.PostProcess.Bloom.Mip2";         // 1/8 鍒嗚鲸鐜?

		static constexpr const char* RT_BLOOM_MIP3          = "Runtime.PostProcess.Bloom.Mip3";         // 1/16 鍒嗚鲸鐜?

		static constexpr const char* RT_BLOOM_RESULT        = "Runtime.PostProcess.Bloom.Result";



		bool RegisterRuntimeRenderTexture(const std::string& name, VansTexture* texture, bool replaceExisting = true);



		VansTexture* GetRuntimeRenderTexture(const std::string& name) const;



		bool HasRuntimeRenderTexture(const std::string& name) const;



		bool RemoveRuntimeRenderTexture(const std::string& name);





		bool UnregisterRuntimeRenderTexture(const std::string& name);



		void ClearRuntimeRenderTextures();








		void ClearScenePBRData(VkDevice device);

		bool FlushMaterialPayload(VansMaterial& material);

		bool ApplyMaterialParameter(

			VansMaterial& material,

			const std::string& parameterPath,

			const VansMaterialParameterValue& value);

		bool ReplaceGlobalBindlessTexture(
			std::size_t textureIndex,
			VansTexture* texture,
			VkDescriptorSet sceneGlobalDescriptorSet = VK_NULL_HANDLE);

		bool RewriteGlobalBindlessTextureDescriptors(
			VkDescriptorSet sceneGlobalDescriptorSet = VK_NULL_HANDLE);

		const VansFogSettings& GetFogSettings() const { return m_FogSettings; }

		const VansFogVolumeSettings& GetFogVolumeSettings() const { return m_FogVolumeSettings; }

		void ApplyFogSettings(const VansFogSettings& settings);

		void ApplyFogVolumeSettings(const VansFogVolumeSettings& settings);

		VansScreenSpacePunctualShadowSettings GetScreenSpacePunctualShadowSettings() const;

		void ApplyScreenSpacePunctualShadowSettings(const VansScreenSpacePunctualShadowSettings& settings);

		void SetScreenSpaceShadowExtent(uint32_t width, uint32_t height);



		VkDescriptorSetLayout m_MaterialAtmosphereDataLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_MaterialAtmosphereDataDescriptorSets;




		VkDescriptorSetLayout m_BRDFInterationTexSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_BRDFInterationTextDescriptorSets;



		//SSGI

		VkDescriptorSetLayout m_SSGITexSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_SSGIDescriptorSets;



		//HIZ

		std::vector<VkDescriptorSetLayout> m_HZBTexSetLayouts;

		std::vector<VkDescriptorSet> m_HZBDescriptorSets;

		std::vector<VkDescriptorSetLayout> m_OcclusionHZBTexSetLayouts;

		std::vector<VkDescriptorSet> m_OcclusionHZBDescriptorSets;



		//SSR

		VkDescriptorSetLayout m_SSRTraceSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_SSRTraceDescriptorSets;



		VkDescriptorSetLayout m_ScreenSpaceShadowSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_ScreenSpaceShadowDescriptorSets;

		VansVKBuffer m_ScreenSpaceShadowParamsCBBuffer;

		ScreenSpaceShadowParamsGPU m_ScreenSpaceShadowParams;

		VkDescriptorSetLayout m_MainCameraHiZCullSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_MainCameraHiZCullDescriptorSets;



		VkDescriptorSetLayout m_SSRResolveSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_SSRResolveDescriptorSets;



		VkDescriptorSetLayout m_SSRAASetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_SSRAADescriptorSets;



		VkDescriptorSetLayout m_BilateralFilterSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_BilateralFilterDescriptorSets;



		VkDescriptorSetLayout m_VolumetricFogSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_VolumetricFogDescriptorSets;

		VansVKBuffer m_FogParamsCBBuffer;

		VansFogSettings m_FogSettings;



		// --- Voxel Fog (Light Injection + Ray March) ---

		VkDescriptorSetLayout m_FogLightInjectionSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_FogLightInjectionDescriptorSets;



		VkDescriptorSetLayout m_FogRayMarchSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_FogRayMarchDescriptorSets;



		VansVKBuffer m_FogVolumeParamsCBBuffer;   // FogVolumeParams UBO (density, anisotropy, scatter, ambient)

		VansFogVolumeSettings m_FogVolumeSettings;

		uint32_t     m_FogTemporalFrame = 0;       // ping-pong frame index for fog injection

		bool         m_FogHistoryValid = false;    // invalidated on creation/scene change







		VansVKBuffer m_GlobalPBRDataBuffer;
		VansVKBuffer m_GlobalClothDataBuffer;

		VansVKBuffer m_GlobalCustomMaterialDataBuffer;

		std::vector<VansPBRMaterial*> m_GlobalPBRMaterial;

		std::vector<VansBasePBRParam> m_GlobalPBRParamData;
		std::vector<VansClothGPUParam> m_GlobalClothParamData;

		std::vector<VansCustomMaterialPayload> m_GlobalCustomMaterialParamData;

		VkDescriptorSetLayout m_GlobalPBRDataSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_GlobalPBRDataDescriptorSets;





		std::vector<VansVKImage*> m_GlobalPBRTextures;

		VkDescriptorSetLayout m_GlobalPBRTexSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_GlobalPBRTexDescriptorSets;







		VkDescriptorSet m_VideoBindlessDescriptorSet = VK_NULL_HANDLE;




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

		VansComputeShader* m_OcclusionHZBShader = nullptr;





		VansComputeShader* m_HIZSeedShader;

		VansComputeShader* m_OcclusionHIZSeedShader = nullptr;

		VkDescriptorSetLayout m_HIZSeedSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_HIZSeedDescriptorSets;

		VkDescriptorSetLayout m_OcclusionHIZSeedSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_OcclusionHIZSeedDescriptorSets;



		VansComputeShader* m_SSRTraceShader;



		VansComputeShader* m_ScreenSpaceShadowShader = nullptr;

		VansComputeShader* m_MainCameraHiZCullShader = nullptr;



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

		VansComputeShader*             m_CloudRayMarchShader         = nullptr;

		VkDescriptorSetLayout          m_CloudRayMarchSetLayout       = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet>   m_CloudRayMarchDescriptorSets;

		VansVKBuffer                   m_CloudParamsCBBuffer;   // CloudParams UBO

		VansCloudParamsGPU            m_CloudParams;



		// ---- TileLight Build Pass ----

		VansVKBuffer m_TileLightHeaderBuffer;

		VansVKBuffer m_TileLightIndexBuffer;

		VkDescriptorSetLayout          m_TileLightBuildSetLayout      = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet>   m_TileLightBuildDescriptorSets;

		VansVKBuffer m_TileLightBuildParamsCBBuffer;

		VansComputeShader* m_TileLightBuildShader = nullptr;

		uint32_t m_TileLightGridX = 0;

		uint32_t m_TileLightGridY = 0;

		// ---- Punctual Shadow diagnostic resolve ----
		VkDescriptorSetLayout m_PunctualShadowDebugSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_PunctualShadowDebugDescriptorSets;
		VansComputeShader* m_PunctualShadowDebugShader = nullptr;

		VansComputeShader* m_ExposureLuminanceShader = nullptr;
		VkDescriptorSetLayout m_ExposureLuminanceSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_ExposureLuminanceDescriptorSets;

		VansComputeShader* m_ExposureAdaptShader = nullptr;
		VkDescriptorSetLayout m_ExposureAdaptSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_ExposureAdaptDescriptorSets;




		VansComputeShader* m_BloomPrefilterShader   = nullptr;

		VkDescriptorSetLayout          m_BloomPrefilterSetLayout         = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet>   m_BloomPrefilterDescriptorSets;



		VansComputeShader* m_BloomDownsampleShader  = nullptr;



		VkDescriptorSetLayout          m_BloomDownsampleSetLayout        = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet>   m_BloomDownsampleDescriptorSets;



		VansComputeShader* m_BloomUpsampleShader    = nullptr;



		VkDescriptorSetLayout          m_BloomUpsampleSetLayout          = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet>   m_BloomUpsampleDescriptorSets;



		VansVKBuffer m_PostProcessParamsCBBuffer;
		VansVKBuffer m_ExposureAdaptParamsCBBuffer;

		VansVKBuffer m_BloomParamsCBBuffer;





		VansPostProcessProfile m_PostProcessProfile;



	public:



		VansVKBuffer m_AtmospherePBRDataBuffer;



		

	public:



		// ?? PBR LUT ????
		void UpdatePBRLutDescriptorSets();



		void UpdateAtmosphereDescriptorSets();



		void UploadCloudParamsToGPU();



		VansRuntimeRenderTextureManager m_RuntimeRenderTextureManager;



	};



	// ============================================================


	// No texture or parameter data lives here.

	// ============================================================

	class VansMaterial : public VansAsset

	{

		friend class VansScene;



	public:

		VansMaterialType    m_MaterialType;




		std::unordered_map<std::string, VansGraphicsShader*> m_PassShaders;

		std::unordered_map<std::string, std::string> m_PassShaderOverrides;

		// Custom materials are scheduled from the graphics shader state instead of
		// the serialized render-node type. Depth-writing shaders join the forward
		// opaque queue; non-depth-writing shaders join the transparent queue.
		bool m_CustomShaderDepthWrite = true;

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









	// ============================================================

	class VansEmissiveMaterial : public VansMaterial

	{

	public:



		VansBasePBRParam m_BasePBRParam;







		// VAN_EMISSIVE uses only m_EmissiveTexture.  VAN_PBR_EMISSIVE uses the
		// complete five-slot payload below (AO is implicitly one because the
		// fifth bindless slot is occupied by the emission mask).
		VansTexture* m_BaseColorTexture = nullptr;
		VansTexture* m_NormalTexture = nullptr;
		VansTexture* m_MetalTexture = nullptr;
		VansTexture* m_RoughnessTexture = nullptr;
		VansTexture* m_EmissiveTexture = nullptr;





		int m_MaterialIndex = -1;



        // 空字符串表示该材质不使用视频纹理。




		std::string m_VideoName;

	};



	// ============================================================

	// VansDecalMaterial ??? VansMaterial (type 15)









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





		int m_MaterialIndex = -1;

	};



	// ============================================================


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



	class VansTransmissionMaterial : public VansMaterial

	{

	public:

		VansTexture* m_BaseColorTexture = nullptr;

		VansTexture* m_NormalTexture = nullptr;

		VansTexture* m_RoughnessTexture = nullptr;

		VansTexture* m_ThicknessTexture = nullptr;

		VansTexture* m_ReflectionTexture = nullptr;

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

		// Skin reuses the global MaterialPayload:
		// albedo = subsurface tint, roughness = specular roughness,
		// metallic = normal strength, ao = SSS amount, padding = specular scale.
		VansBasePBRParam m_BasePBRParam;



		VkDescriptorSetLayout          m_SkinOwnedLayout  = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet>   m_SkinOwnedDescSets;



		void BuildSkinTextureDescriptors();

	};

	// ============================================================


	// ============================================================

	enum class VansClothModel : uint32_t
	{
		Fuzz = 0,
		Silk = 1,
		Thin = 2,
	};

	static constexpr uint32_t VANS_CLOTH_FLAG_ALBEDO_SHEEN_TINT = 1u << 0;

	struct alignas(16) VansClothGPUParam
	{
		glm::vec4 sheenColorWeight = glm::vec4(1.0f, 1.0f, 1.0f, 0.5f);
		glm::vec4 transmissionColorStrength = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
		glm::vec4 controls = glm::vec4(
			static_cast<float>(VansClothModel::Fuzz), 0.0f, 1.0f,
			static_cast<float>(VANS_CLOTH_FLAG_ALBEDO_SHEEN_TINT));
	};

	static_assert(sizeof(VansClothGPUParam) == 48, "VansClothGPUParam must match the GLSL std430 payload");

	class VansClothMaterial : public VansMaterial

	{

	public:

		~VansClothMaterial() override;



		VansTexture* m_BaseColorTexture  = nullptr;

		VansTexture* m_NormalTexture     = nullptr;

		VansTexture* m_RoughnessTexture  = nullptr;

		VansTexture* m_AoTexture         = nullptr;



		float        m_SheenRoughness    = 0.5f;
		VansClothModel m_ClothModel      = VansClothModel::Fuzz;
		glm::vec3    m_SheenColor        = glm::vec3(1.0f);
		float        m_SheenStrength     = 0.5f;
		float        m_Anisotropy        = 0.0f;
		glm::vec3    m_TransmissionColor = glm::vec3(1.0f);
		float        m_Translucency      = 0.35f;
		float        m_Thickness         = 1.0f;
		uint32_t     m_ClothFlags        = VANS_CLOTH_FLAG_ALBEDO_SHEEN_TINT;

		VansBasePBRParam m_BasePBRParam{ glm::vec3(1.0f), 0.5f, 0.35f, 1.0f, 0.5f };



		VkDescriptorSetLayout          m_ClothOwnedLayout   = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet>   m_ClothOwnedDescSets;



		void BuildClothTextureDescriptors();
		VansClothGPUParam BuildGPUParam() const;

	};

	// ============================================================


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



		// Legacy asset key: "subsurfacePower".  The value is now the maximum
		// Burley diffusion distance in millimetres; keeping the member/key avoids
		// invalidating existing material assets.
		float        m_SubsurfacePower   = 12.0f;

		float        m_Thickness         = 5.0f;      // thickness-map scale (mm)

		// RGB relative scattering distances.  The largest channel travels
		// m_SubsurfacePower millimetres; shorter channels are absorbed sooner.
		glm::vec3    m_SubsurfaceColor   = glm::vec3(1.0f, 0.35f, 0.2f);

		float        m_SubsurfaceAmount  = 1.0f;

		float        m_CurvatureInfluence = 0.35f;

		float        m_IOR = 1.4f;



		VansBasePBRParam m_BasePBRParam;

		int              m_MaterialIndex = -1;



		VkDescriptorSetLayout          m_SubsurfaceOwnedLayout  = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet>   m_SubsurfaceOwnedDescSets;



		void BuildSubsurfaceTextureDescriptors();

	};



	// ============================================================


	// ============================================================

	class VansPostProcessMaterial : public VansMaterial {};

	class VansDeferredMaterial    : public VansMaterial {};

	class VansSSAOMaterial        : public VansMaterial {};



	// ============================================================


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

