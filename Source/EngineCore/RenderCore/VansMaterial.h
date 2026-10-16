#pragma once
#include "AmbientOcclusionCore/VansGTAO.h"
#include "../MaterialCore/VansHairMaterialParameters.h"
#include "../MaterialCore/VansClothMaterialParameters.h"
#include "../MaterialCore/VansTreeLeafMaterialParameters.h"
#include "VulkanCore/VansVKGIReceiverVisibility.h"
#include "VulkanCore/VansVKAmbientSkyCache.h"
#include "VulkanCore/VansVKSSGI.h"
#include "SkyLightingCore/VansSkyLighting.h"
#include "GICore/VansGIVoxelMaterial.h"

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
#include "VansRenderRuntimeConfig.h"

#include <vector>
#include <array>

#include <unordered_map>

#include <string>

#include <cstddef>

#include <cstdint>

#include <memory>
#include <mutex>

#include <variant>



using namespace VansGraphics;

namespace VansGraphics

{



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

		static constexpr const char* FORWARD_OPAQUE_PRE_ATMOSPHERE = "forwardOpaquePreAtmosphere";

		static constexpr const char* FORWARD_TRANSPARENT = "transparent";   // forward transparent


		static constexpr const char* DEFERRED         = "deferred";         // deferred lighting resolve

		static constexpr const char* POST_PROCESS     = "postProcess";      // post-processing


		static constexpr const char* HAIR_VISIBILITY  = "hairVisibility";


		static constexpr const char* HAIR_LIGHTING    = "hairLighting";
		static constexpr const char* HAIR_LIGHTING_SKY = "hairLightingSky";

		static constexpr const char* HAIR_COMPOSITE   = "hairComposite";

		// future

		static constexpr const char* VELOCITY         = "velocity";

		static constexpr const char* PRE_DEPTH        = "preDepth";



		static constexpr const char* DECAL_MODIFIER    = "decalModifier";
		static constexpr const char* ROAD_DECAL_MODIFIER = "roadDecalModifier";
		static constexpr const char* SPLINE_DECAL_MODIFIER = "splineDecalModifier";

	}

	class VansPBRMaterial;

	class VansTransparentMaterial;

	class VansSkinMaterial;

	class VansClothMaterial;
	struct VansClothGPUParam;
    struct VansHairParamsGPU;

	class VansHairMaterial;

	class VansSubsurfaceMaterial;

	class VansPostProcessMaterial;

	class VansDeferredMaterial;


	class VansEmissiveMaterial;

	class VansDecalMaterial;

	class VansMaterial;
	struct VansRenderMaterialFrameData;



	enum VansMaterialType

	{

		VAN_PBR = 0,

		VAN_COAT = 1,

		VAN_TRANSPARENT = 2,

		VAN_POST_PROCESS = 3,

		VAN_DEFERRED = 5,


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

	struct alignas(16) VansTreeLeafParamsGPU
	{
		VansTreeLeafParamsGPU(const Vans::VansTreeLeafMaterialParameters& value = {})
			: subsurfaceColorAndStrength(value.subsurfaceColor[0], value.subsurfaceColor[1], value.subsurfaceColor[2], value.transmission),
			  scattering(value.wrap, value.scatterRoughness, value.specularScale, value.alphaClip) {}
		Vans::VansTreeLeafMaterialParameters Parameters() const
		{
			return {{subsurfaceColorAndStrength.x, subsurfaceColorAndStrength.y, subsurfaceColorAndStrength.z},
				subsurfaceColorAndStrength.w, scattering.x, scattering.y, scattering.z, scattering.w};
		}
		glm::vec4 subsurfaceColorAndStrength;
		glm::vec4 scattering;
		glm::vec4 reserved = glm::vec4(0.0f);
	};

	static_assert(sizeof(VansTreeLeafParamsGPU) == 48, "Tree leaf parameter layout must match GLSL");

	struct alignas(16) VansSkinGPUParam
	{
		glm::vec4 scatterColorAmount = glm::vec4(1.0f, 0.34f, 0.22f, 0.65f);
		glm::vec4 roughnessNormalSpecular = glm::vec4(0.62f, 0.35f, 1.0f, 1.0f);
		glm::vec4 lobeIOR = glm::vec4(0.75f, 1.75f, 1.4f, 0.72f);
		// x = diffusion radius, y = thinness, z = optical depth, w = ambient scatter.
		glm::vec4 profileControls = glm::vec4(1.0f, 1.0f, 1.0f, 0.35f);
		// rgb = per-channel diffusion radius scale, w = boundary color bleed.
		glm::vec4 profileShape = glm::vec4(1.0f);
		// x = profile LUT layer, -1 = legacy 2D LUT fallback; y = authored thinness map weight.
		glm::vec4 profileLUT = glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f);
		// x = debug view mode, yzw reserved.
		glm::vec4 debugControls = glm::vec4(0.0f);
	};

	static_assert(sizeof(VansSkinGPUParam) == 112, "Skin parameter layout must match GLSL");

	constexpr int VANS_SKIN_PROFILE_LUT_SIZE = 256;
	constexpr int VANS_BUILTIN_SKIN_PROFILE_LUT_LAYER_COUNT = 4;
	constexpr int VANS_FIRST_DYNAMIC_SKIN_PROFILE_LUT_LAYER = VANS_BUILTIN_SKIN_PROFILE_LUT_LAYER_COUNT;
	constexpr int VANS_SKIN_PROFILE_LUT_LAYER_COUNT = 16;

	using VansSkinProfileLUTFingerprint = std::array<int32_t, 10>;

	bool IsDynamicSkinProfileLUTLayer(int layer);
	VansSkinProfileLUTFingerprint BuildSkinProfileLUTFingerprint(const VansSkinGPUParam& profile);
	std::string NormalizeSkinProfilePresetName(std::string profileName);
	bool ResolveSkinProfilePresetPayload(
		const std::string& profileName,
		VansBasePBRParam& legacy,
		VansSkinGPUParam& params);
	const char* GetBuiltInSkinProfileLUTLayerName(int layer);
	bool GenerateSkinProfileLUTPixels(
		const VansSkinGPUParam& profile,
		int width,
		int height,
		std::vector<uint8_t>& outPixels);
	bool GenerateBuiltInSkinProfileLUTLayer(
		int layer,
		int width,
		int height,
		std::vector<uint8_t>& outPixels);





	class VansMaterialManager

	{

		friend class VansMaterial;

		friend class VansRenderNode;
		friend class VansVKDevice;




	private:
		struct RuntimeMaterialInstance
		{
			std::unique_ptr<VansMaterial> material;
			int poolIndex = -1;
			bool customPayload = false;
		};

		std::unordered_map<std::string, RuntimeMaterialInstance> m_RuntimeMaterialInstances;
		std::vector<int> m_FreeRuntimePBRIndices;
		std::vector<int> m_FreeRuntimeCustomIndices;
		bool m_GlobalBindlessDescriptorsDirty = false;
		uint64_t m_ClothLightingRevision = 0;
        std::shared_ptr<VansGIVoxelMaterialInputs> m_GIVoxelMaterials;

		struct SkinProfileLUTCacheEntry
		{
			VansSkinProfileLUTFingerprint fingerprint{};
			int layer = -1;
			int refCount = 0;
		};

		struct PendingSkinProfileLUTUpload
		{
			VansSkinProfileLUTFingerprint fingerprint{};
			int layer = -1;
			std::vector<uint8_t> pixels;
		};

		std::vector<SkinProfileLUTCacheEntry> m_SkinProfileDynamicLUTCache;
		std::vector<PendingSkinProfileLUTUpload> m_PendingSkinProfileLUTUploads;
		std::unordered_map<int, int> m_SkinMaterialDynamicLUTLayers;



		void InitMaterialDataDescriptors();
		void ReleaseSkinProfileLUTLayerForMaterial(int materialIndex);



	public:

		static constexpr const char* RT_GTAO_RAW = "Runtime.GTAO.Raw";
        static constexpr const char* RT_GTAO_DEPTH = "Runtime.GTAO.Depth";
        static constexpr const char* RT_GTAO_EDGES = "Runtime.GTAO.Edges";

		// Luminance first/second moments are ping-ponged with the temporal GI
		// history. They provide an explicit variance estimate for rejection and
		// clamping instead of deriving it only from the current 3x3 neighborhood.
		// RGBA32F：xyz 世界坐标；w 为 SSGISurface.glsl 定义的法线/材质整数数值。

		static constexpr const char* RT_HZB_RESULT = "Runtime.HZB.Result";

		static constexpr const char* RT_HZB_OCCLUSION_RESULT = "Runtime.HZB.OcclusionResult";

		static constexpr const char* RT_SCREEN_SPACE_SHADOW_RESULT = "Runtime.ScreenSpaceShadow.Result";
		static constexpr const char* RT_CASCADE_SHADOW_MIN_MAX = "Runtime.CascadeShadow.MinMax";

		static constexpr const char* RT_PUNCTUAL_SHADOW_DEBUG_PREVIEW = "Runtime.PunctualShadow.DebugPreview";

		static constexpr const char* RT_SSR_HIT_INFO = "Runtime.SSR.HitInfo";

		static constexpr const char* RT_SSR_RAY_PDF = "Runtime.SSR.RayPDF";

		static constexpr const char* RT_SSR_RESULT = "Runtime.SSR.Result";

		static constexpr const char* RT_SSRAA_RESULT_A = "Runtime.SSR.AA.ResultA";

		static constexpr const char* RT_SSRAA_RESULT_B = "Runtime.SSR.AA.ResultB";

		static constexpr const char* RT_SSRAA_RESULT = "Runtime.SSR.AA.Result";

		static constexpr const char* RT_GTAO_RESULT = "Runtime.GTAO.Result";
        VkDescriptorSetLayout m_GTAODepthSetLayout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_GTAODepthDescriptorSets;
        VkDescriptorSetLayout m_GTAOMainSetLayout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_GTAOMainDescriptorSets;
        VkDescriptorSetLayout m_GTAODenoiseSetLayout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_GTAODenoiseDescriptorSets;
        VansComputeShader* m_GTAODepthShader = nullptr;
        VansComputeShader* m_GTAOMainShader = nullptr;
        VansComputeShader* m_GTAODenoiseShader = nullptr;
        VansGTAOParameters m_GTAOParameters;


		static constexpr const char* RT_RECT_LIGHT_EMISSIVE = "Runtime.RectLight.EmissiveArray";











		static constexpr const char* RT_EXPOSURE_LUMINANCE   = "Runtime.PostProcess.Exposure.Luminance";
		static constexpr const char* RT_EXPOSURE_CURRENT     = "Runtime.PostProcess.Exposure.Current";
		static constexpr const char* RT_UPSCALER_EXPOSURE    = "Runtime.Upscaler.ExposureMultiplier";




		static constexpr const char* RT_BLOOM_PREFILTER     = "Runtime.PostProcess.Bloom.Prefilter";

		static constexpr const char* RT_BLOOM_MIP0          = "Runtime.PostProcess.Bloom.Mip0";         // 1/2 鍒嗚鲸鐜?

		static constexpr const char* RT_BLOOM_MIP1          = "Runtime.PostProcess.Bloom.Mip1";         // 1/4 鍒嗚鲸鐜?

		static constexpr const char* RT_BLOOM_MIP2          = "Runtime.PostProcess.Bloom.Mip2";         // 1/8 鍒嗚鲸鐜?

		static constexpr const char* RT_BLOOM_MIP3          = "Runtime.PostProcess.Bloom.Mip3";         // 1/16 鍒嗚鲸鐜?

		static constexpr const char* RT_BLOOM_UP_MIP0       = "Runtime.PostProcess.Bloom.Upsample.Mip0"; // 1/2 resolved bloom
		static constexpr const char* RT_BLOOM_UP_MIP1       = "Runtime.PostProcess.Bloom.Upsample.Mip1"; // 1/4 resolved bloom
		static constexpr const char* RT_BLOOM_UP_MIP2       = "Runtime.PostProcess.Bloom.Upsample.Mip2"; // 1/8 resolved bloom

		static constexpr const char* RT_BLOOM_BASE          = "Runtime.PostProcess.Bloom.Base";
		static constexpr const char* RT_BLOOM_RESULT        = "Runtime.PostProcess.Bloom.Result";
		static constexpr const char* RT_DOF_RESULT          = "Runtime.PostProcess.DOF.Result";



		bool RegisterRuntimeRenderTexture(const std::string& name, VansTexture* texture, bool replaceExisting = true);



		VansTexture* GetRuntimeRenderTexture(const std::string& name) const;



		bool HasRuntimeRenderTexture(const std::string& name) const;



		bool RemoveRuntimeRenderTexture(const std::string& name);





		bool UnregisterRuntimeRenderTexture(const std::string& name);



		void ClearRuntimeRenderTextures();

		// Releases only render-size-dependent transient resources. Scene materials,
		// fixed lookup textures, and global bindless state remain valid.
		void ClearResolutionDependentRenderData(VkDevice device);

		// Releases the ambient-sky cache resources without disturbing other
		// resolution-dependent GI and post-process resources.








		void ClearScenePBRData(VkDevice device);

		// Main-only CPU staging; GPU buffers are uploaded from the published frame on RT.
		bool FlushMaterialPayload(VansMaterial& material);
        void RegisterGIVoxelMaterial(const VansPBRMaterial& material);
        void UnregisterGIVoxelMaterial(const VansPBRMaterial& material);
        // 只在 Main 或 Main 已暂停的场景/维护事务中取作者快照；普通 RT 帧使用发布的 frame data。
        std::shared_ptr<const VansGIVoxelMaterialInputs> GetGIVoxelMaterialInputs() const {return m_GIVoxelMaterials;}
		VansRenderMaterialFrameData CaptureRenderMaterialFrameData(
			const std::vector<VansMaterial*>& activeMaterials);
		bool UploadRenderMaterialFrameData(
			const VansRenderMaterialFrameData& frameData,
			std::uint32_t slot);
		void ResetSkinProfileLUTCache();
		bool ResolveSkinProfileLUTForMaterial(
			VansSkinMaterial& skin,
			VansSkinGPUParam& payload,
			VansVKCommandBuffer* immediateCommandBuffer = nullptr);
		bool RecordPendingSkinProfileLUTUploads(VansVKCommandBuffer& commandBuffer);

		bool ApplyMaterialParameter(

			VansMaterial& material,

			const std::string& parameterPath,

			const VansMaterialParameterValue& value);

		void InitializeRuntimeMaterialPools(
			std::size_t pbrInstanceCount,
			std::size_t customInstanceCount,
			VansVKImage* fallbackTexture);
		VansMaterial* AcquireRuntimeMaterialInstance(
			const std::string& instanceKey,
			const VansMaterial& source,
			VkDescriptorSet sceneGlobalDescriptorSet = VK_NULL_HANDLE);
		bool ReleaseRuntimeMaterialInstance(const std::string& instanceKey);
		void ClearRuntimeMaterialInstances();
		std::size_t RuntimeMaterialInstanceCount() const { return m_RuntimeMaterialInstances.size(); }

		bool ReplaceGlobalBindlessTexture(
			std::size_t textureIndex,
			VansTexture* texture,
			VkDescriptorSet sceneGlobalDescriptorSet = VK_NULL_HANDLE);

		bool RewriteGlobalBindlessTextureDescriptors(
			VkDescriptorSet sceneGlobalDescriptorSet = VK_NULL_HANDLE);

		VansScreenSpacePunctualShadowSettings GetScreenSpacePunctualShadowSettings() const;

		void ApplyScreenSpacePunctualShadowSettings(const VansScreenSpacePunctualShadowSettings& settings);

		void SetScreenSpaceShadowExtent(uint32_t width, uint32_t height);






		//HIZ

		std::vector<VkDescriptorSetLayout> m_HZBTexSetLayouts;

		std::vector<VkDescriptorSet> m_HZBDescriptorSets;

		std::vector<VkDescriptorSetLayout> m_OcclusionHZBTexSetLayouts;

		std::vector<VkDescriptorSet> m_OcclusionHZBDescriptorSets;



		//SSR

		VkDescriptorSetLayout m_SSRTraceSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_SSRTraceDescriptorSets;
		VansVKBuffer m_SSRRayListBuffer;
		VansVKBuffer m_SSRTraceControlBuffer;



		VkDescriptorSetLayout m_ScreenSpaceShadowSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_ScreenSpaceShadowDescriptorSets;
		VkDescriptorSetLayout m_CascadeShadowMinMaxSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_CascadeShadowMinMaxDescriptorSets;
		uint32_t m_CascadeShadowMinMaxMipCount = 0;

		VansVKBuffer m_ScreenSpaceShadowParamsCBBuffer;

		ScreenSpaceShadowParamsGPU m_ScreenSpaceShadowParams;

		VkDescriptorSetLayout m_MainCameraHiZCullSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_MainCameraHiZCullDescriptorSets;



		VkDescriptorSetLayout m_SSRResolveSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_SSRResolveDescriptorSets;



		VkDescriptorSetLayout m_SSRAASetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_SSRAADescriptorSets;







		struct FrameMaterialBuffers
		{
			VansVKBuffer pbr;
			VansVKBuffer cloth;
			VansVKBuffer hair;
			VansVKBuffer treeLeaf;
			VansVKBuffer skin;
			VansVKBuffer custom;
		};
		std::array<FrameMaterialBuffers, VansCommandRecordingConfig::MaximumFramesInFlight> m_FrameMaterialBuffers;

		std::vector<VansPBRMaterial*> m_GlobalPBRMaterial;

		std::vector<VansBasePBRParam> m_GlobalPBRParamData;
		std::vector<VansClothGPUParam> m_GlobalClothParamData;
        std::vector<VansHairParamsGPU> m_GlobalHairParamData;
		std::vector<VansTreeLeafParamsGPU> m_GlobalTreeLeafParamData;
		std::vector<VansSkinGPUParam> m_GlobalSkinParamData;

		std::vector<VansCustomMaterialPayload> m_GlobalCustomMaterialParamData;

		std::vector<VansVKImage*> m_GlobalPBRTextures;
		// The texture table is scene-stable, but runtime material instances and
		// live texture edits may replace entries while RT publishes descriptors.
		// This narrow mutex protects that rare ownership handoff only; per-frame
		// material payloads continue through immutable frame snapshots.
		mutable std::mutex m_GlobalPBRTexturesMutex;

		VkDescriptorSetLayout m_GlobalPBRTexSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_GlobalPBRTexDescriptorSets;







        VansSkyLighting m_SkyLighting;




		VansTexture* m_BRDFIntegralLUT = nullptr;



		VansTexture* m_SkinBSDFLUT = nullptr;



		VansTexture* m_SkinProfileLUTArray = nullptr;



		VansTexture* m_ClothBRDFLUT = nullptr;
		VansTexture* m_ClothLightingLUT = nullptr;
        VansTexture* m_GrassEnergyLUT = nullptr;






		// LTC LUTs (Linearly Transformed Cosines, area-light BRDF)

		// 64x64, RGBA16F, runtime-uploaded from embedded float arrays (LTCData.h).

		VansTexture* m_LTC1 = nullptr;



		VansTexture* m_LTC2 = nullptr;







		VansTexture* m_RectLightEmissiveArray = nullptr;



		VansMaterialManager();



		~VansMaterialManager();



	public:
		VansVKSSGI m_SSGI;
		VansVKGIReceiverVisibility m_GIReceiverVisibility;



		VansVKAmbientSkyCache m_AmbientSkyCache;





		uint32_t m_HIZMipCount;



		VansComputeShader* m_HZBShader;

		VansComputeShader* m_OcclusionHZBShader = nullptr;





		VansComputeShader* m_HIZSeedShader;

		VansComputeShader* m_OcclusionHIZSeedShader = nullptr;

		VkDescriptorSetLayout m_HIZSeedSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_HIZSeedDescriptorSets;

		VkDescriptorSetLayout m_OcclusionHIZSeedSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_OcclusionHIZSeedDescriptorSets;



		VansComputeShader* m_SSRClassifyShader = nullptr;
		VansComputeShader* m_SSRPrepareIndirectShader = nullptr;
		VansComputeShader* m_SSRTraceShader = nullptr;



		VansComputeShader* m_ScreenSpaceShadowShader = nullptr;
		VansComputeShader* m_CascadeShadowMinMaxSeedShader = nullptr;
		VansComputeShader* m_CascadeShadowMinMaxReduceShader = nullptr;

		VansComputeShader* m_MainCameraHiZCullShader = nullptr;



		VansComputeShader* m_SSRResolveShader;



		VansComputeShader* m_SSRTemporalAAShader;













		// ---- TileLight Build Pass ----

		struct TileLightFrameBuffers
		{
			VansVKBuffer header;
			VansVKBuffer indices;
		};
		std::array<TileLightFrameBuffers, VansCommandRecordingConfig::MaximumFramesInFlight> m_TileLightFrames;

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

		VansComputeShader* m_BloomShapeShader = nullptr;

		VkDescriptorSetLayout          m_BloomShapeSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet>   m_BloomShapeDescriptorSets;

		VansComputeShader* m_DepthOfFieldShader = nullptr;

		VkDescriptorSetLayout          m_DepthOfFieldSetLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet>   m_DepthOfFieldDescriptorSets;



		std::array<VansVKBuffer, VansCommandRecordingConfig::MaximumFramesInFlight> m_PostProcessParamsCBBuffer;
		std::array<VansVKBuffer, VansCommandRecordingConfig::MaximumFramesInFlight> m_ExposureAdaptParamsCBBuffer;

		VansVKBuffer m_BloomParamsCBBuffer;
		VansVKBuffer m_BloomShapeParamsCBBuffer;
		std::array<VansVKBuffer, VansCommandRecordingConfig::MaximumFramesInFlight> m_DepthOfFieldParamsCBBuffer;





		VansPostProcessProfile m_PostProcessProfile;



	public:




		

	public:



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

		int GetGlobalMaterialIndex() const { return m_MaterialIndex; }
		void SetGlobalMaterialIndex(int index) { m_MaterialIndex = index; }



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
		bool m_AlphaTestEnabled = false;
		float m_AlphaCutoff = 0.5f;
		VansTreeLeafParamsGPU m_TreeLeafParams;



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





        // 空字符串表示该材质不使用视频纹理。




		std::string m_VideoName;

	};



	// ============================================================

	// VansDecalMaterial ??? VansMaterial (type 15)









	// ============================================================

    // 通用材质 payload：values[0]=albedo/opacity，values[1]=roughness/三种属性权重，
    // values[2].x=sortPriority；textureIndices=颜色/法线/粗糙度/三通道覆盖遮罩。
    // 样条贴花还使用 values[3].yzw 设置两侧、首尾渐隐与 Noise 强度，
    // values[4].x 设置 Noise 世界频率；可选第 5 纹理槽写入 values[5].x。
    class VansDecalMaterial : public VansMaterial
    {
    public:
        VansDecalMaterial()
        {
            m_MaterialType = VAN_DECAL;
            m_CustomMaterialPayload.values[0] = glm::vec4(1.0f);
            m_CustomMaterialPayload.values[1] = glm::vec4(0.5f, 1.0f, 1.0f, 1.0f);
        }
    };

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

	// VansSkinMaterial - dedicated Skin shading (type 9)

	// ============================================================

	class VansSkinMaterial : public VansMaterial

	{

	public:

		~VansSkinMaterial() override;



		VansTexture* m_BaseColorTexture = nullptr;

		VansTexture* m_NormalTexture    = nullptr;
		VansTexture* m_RoughnessTexture = nullptr;
		VansTexture* m_CavityTexture    = nullptr;
		VansTexture* m_ScatterMaskTexture = nullptr;
		VansTexture* m_ThicknessTexture = nullptr;

		// Legacy fields are still filled for compatibility with existing assets.
		// BuildGPUParam translates them into the typed Skin payload consumed by shaders.
		VansBasePBRParam m_BasePBRParam;
		VansSkinGPUParam m_SkinParams;
		std::string m_SkinProfileName = "neutral";
		std::string m_SkinProfileAssetGuid;
		std::vector<std::string> m_SkinProfileInheritedFields;
		bool m_UseExplicitSkinProfileLUTLayer = false;



		VkDescriptorSetLayout          m_SkinOwnedLayout  = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet>   m_SkinOwnedDescSets;



		void BuildSkinTextureDescriptors();
		bool ApplySkinProfilePreset(const std::string& profileName);
		VansSkinGPUParam BuildGPUParam() const;

	};

	// ============================================================


	// ============================================================

    struct alignas(16) VansClothGPUParam
    {
        glm::vec4 sheenColorWeight{1.0f, 1.0f, 1.0f, 0.2f};
        glm::vec4 transmittanceRoughness{0.0f, 0.0f, 0.0f, 0.6f};
        glm::vec4 surface{0.25f, 0.0f, 0.0f, 0.5f}; // 表面反射、各向异性、旋转弧度、裁剪阈值
        glm::uvec4 flags{0u, 1u, 0u, 0u}; // Mask、双面、方向图存在、保留零值
    };
    static_assert(sizeof(VansClothGPUParam) == 64, "Cloth std430 payload ABI");

    class VansClothMaterial : public VansMaterial
    {
    public:
        ~VansClothMaterial() override;
        Vans::VansClothMaterialParameters m_Params;
        VansTexture* m_BaseColorTexture = nullptr;
        VansTexture* m_NormalTexture = nullptr;
        VansTexture* m_RoughnessTexture = nullptr;
        VansTexture* m_AoTexture = nullptr;
        VansTexture* m_SheenMaskTexture = nullptr;
        VansTexture* m_FiberDirectionTexture = nullptr;
        VansTexture* m_OpacityTexture = nullptr;
        VkDescriptorSetLayout m_ClothOwnedLayout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_ClothOwnedDescSets;
        void BuildClothTextureDescriptors();
        bool RefreshClothTextureDescriptors();
        VansClothGPUParam BuildGPUParam() const;
        VansBasePBRParam BuildBasePBRParam() const;
    };

	// ============================================================


	// 发片纹理：basecolor、alpha、normal、roughness、AO、flow；参数经帧快照上传。

	// ============================================================

    struct alignas(16) VansHairParamsGPU
    {
        glm::vec4 scattering; // 纵向/方位粗糙度、毛鳞片倾角（弧度）、法线强度
        glm::vec4 coverage;   // 覆盖阈值、缩放、flow 强度、投影开关
        glm::vec4 occlusion;  // AO 强度；其余分量为 ABI 填充
    };
    static_assert(sizeof(VansHairParamsGPU) == 48, "Hair material GPU ABI");

	class VansHairMaterial : public VansMaterial

	{

	public:

		~VansHairMaterial() override;



		VansTexture* m_AlbedoTexture      = nullptr;

		VansTexture* m_AlphaTexture       = nullptr;

		VansTexture* m_NormalTexture      = nullptr;

		VansTexture* m_RoughnessTexture   = nullptr;

		VansTexture* m_AOTexture          = nullptr;

		VansTexture* m_FlowTexture        = nullptr;



		Vans::VansHairMaterialParameters m_Params;
        VansHairParamsGPU BuildGPUParams() const;





		VkDescriptorSetLayout          m_HairOwnedLayout  = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet>   m_HairOwnedDescSets;



		void BuildHairDescriptors(VansMaterialManager& materialManager);

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



		VkDescriptorSetLayout          m_SubsurfaceOwnedLayout  = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet>   m_SubsurfaceOwnedDescSets;



		void BuildSubsurfaceTextureDescriptors();

	};



	// ============================================================


	// ============================================================

	class VansPostProcessMaterial : public VansMaterial {};

	class VansDeferredMaterial    : public VansMaterial {};




	// ============================================================


	// Textures: albedo, normal, roughness, translucency, AO

	// Grass 材质参数通过绘制常量上传。

	// Masked GBuffer 与可选的级联阴影共享几何。

	// ============================================================

	struct GrassParams
    {
        float aoStrength = 1.0f;           // 叶片微结构 AO；不承担草丛体遮蔽。
        float normalStrength = 1.0f;
        float transmissionStrength = 0.5f;
        float indirectDiffuseStrength = 1.0f; // 草专用环境漫反射/透射倍率，不改变直接光或镜面反射。
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

