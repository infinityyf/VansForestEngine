#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <vector>

namespace VansGraphics
{
	// ====================================================================
	// Descriptor Set Index Convention
	// ====================================================================
	enum DescriptorSetIndex : uint32_t
	{
		DS_GLOBAL       = 0,  // Per-frame global data (Camera, Lights, Materials, IBL, Bindless)
		DS_PASS         = 1,  // Per-pass data (varies per render pass / compute dispatch)
		DS_OBJECT       = 2,  // Per-object data (Transform SSBO, geometry passes only)
		DS_COUNT
	};

	// ====================================================================
	// Set 0 (Global) Binding Indices
	// ====================================================================
	enum GlobalBinding : uint32_t
	{
		GLOBAL_BINDING_CAMERA_UBO               = 0,
		GLOBAL_BINDING_LIGHTS_UBO               = 1,
		GLOBAL_BINDING_MATERIAL_SSBO            = 2,
		GLOBAL_BINDING_BRDF_LUT                 = 3,
		GLOBAL_BINDING_PRECONV_DIFFUSE          = 4,
		GLOBAL_BINDING_PRECONV_SPECULAR         = 5,
		GLOBAL_BINDING_SH_COEFFICIENTS          = 6,
		GLOBAL_BINDING_BINDLESS_TEXTURES        = 50,  // Variable count
	};

	// ====================================================================
	// Set 2 (Per-Object) Binding Indices
	// ====================================================================
	enum ObjectBinding : uint32_t
	{
		OBJECT_BINDING_TRANSFORM_SSBO   = 0,
	};

	// ====================================================================
	// Constants
	// ====================================================================
	static constexpr uint32_t MAX_BINDLESS_TEXTURES = 500;

	// ====================================================================
	// Set 1 Per-Pass Binding Indices
	// Each enum mirrors the bindings in CreatePassLayout_* factory methods.
	// ====================================================================

	// --- Deferred Lighting Pass ---
	enum DeferredLightingPassBinding : uint32_t
	{
		DEFERRED_BINDING_GBUFFER_0        = 0,
		DEFERRED_BINDING_GBUFFER_1        = 1,
		DEFERRED_BINDING_GBUFFER_2        = 2,
		DEFERRED_BINDING_GBUFFER_3        = 3,
		DEFERRED_BINDING_DEPTH            = 4,
		DEFERRED_BINDING_SSAO             = 5,
		DEFERRED_BINDING_SSGI             = 6,
		DEFERRED_BINDING_SSR              = 7,
		DEFERRED_BINDING_SHADOW_MAP       = 8,
		DEFERRED_BINDING_PUNCTUAL_SHADOW  = 9,
		DEFERRED_BINDING_SH_R             = 10,
		DEFERRED_BINDING_SH_G             = 11,
		DEFERRED_BINDING_SH_B             = 12,
		DEFERRED_BINDING_FOG              = 13,
	};

	// --- SkyBox Pass ---
	enum SkyBoxPassBinding : uint32_t
	{
		SKYBOX_BINDING_ATMOSPHERE_UBO = 0,
	};

	// --- Screen-Space Pass (SSAO etc.) ---
	enum ScreenSpacePassBinding : uint32_t
	{
		SCREEN_BINDING_NORMAL        = 0,
		SCREEN_BINDING_GBUFFER0      = 1,
		SCREEN_BINDING_GBUFFER1      = 2,
		SCREEN_BINDING_GBUFFER2      = 3,
		SCREEN_BINDING_DEPTH         = 4,
		SCREEN_BINDING_SSAO_OUTPUT   = 5,
	};

	// --- Post-Process Pass ---
	enum PostProcessPassBinding : uint32_t
	{
		POSTPROCESS_BINDING_COLOR_INPUT = 0,
	};

	// --- Terrain Pass ---
	enum TerrainPassBinding : uint32_t
	{
		TERRAIN_BINDING_HEIGHT_MAP  = 0,
		TERRAIN_BINDING_ALBEDO_MAP = 1,
	};

	// --- SSGI Compute Pass ---
	enum SSGIPassBinding : uint32_t
	{
		SSGI_BINDING_NORMAL       = 0,
		SSGI_BINDING_DEPTH        = 1,
		SSGI_BINDING_COLOR        = 2,
		SSGI_BINDING_POSITION     = 3,
		SSGI_BINDING_SKY_DIFFUSE  = 4,
		SSGI_BINDING_RESULT       = 5,
		SSGI_BINDING_INFO_UBO     = 6,
		SSGI_BINDING_SH_R         = 7,
		SSGI_BINDING_SH_G         = 8,
		SSGI_BINDING_SH_B         = 9,
		SSGI_BINDING_HIZ_DEPTH    = 10,
	};

	// --- SSGI Temporal Accumulation Pass ---
	enum SSGITemporalPassBinding : uint32_t
	{
		SSGI_TEMPORAL_BINDING_DEPTH          = 0,
		SSGI_TEMPORAL_BINDING_MOTION_VECTOR  = 1,
		SSGI_TEMPORAL_BINDING_HISTORY_GI     = 2,
		SSGI_TEMPORAL_BINDING_CURRENT_GI     = 3,
		SSGI_TEMPORAL_BINDING_ACCUMULATED_GI = 4,
		SSGI_TEMPORAL_BINDING_INFO_UBO       = 5,
	};

	// --- SSR Trace Compute Pass ---
	enum SSRTracePassBinding : uint32_t
	{
		SSR_TRACE_BINDING_NORMAL     = 0,
		SSR_TRACE_BINDING_ROUGHNESS  = 1,
		SSR_TRACE_BINDING_POSITION   = 2,
		SSR_TRACE_BINDING_HIZ        = 3,
		SSR_TRACE_BINDING_HIT        = 4,
		SSR_TRACE_BINDING_PDF        = 5,
	};

	// --- SSR Resolve Compute Pass ---
	enum SSRResolvePassBinding : uint32_t
	{
		SSR_RESOLVE_BINDING_COLOR      = 0,
		SSR_RESOLVE_BINDING_ROUGHNESS  = 1,
		SSR_RESOLVE_BINDING_NORMAL     = 2,
		SSR_RESOLVE_BINDING_POSITION   = 3,
		SSR_RESOLVE_BINDING_HIT        = 4,
		SSR_RESOLVE_BINDING_PDF        = 5,
		SSR_RESOLVE_BINDING_RESULT     = 6,
	};

	// --- SSR Temporal-AA Compute Pass ---
	enum SSRTemporalAAPassBinding : uint32_t
	{
		SSR_TAA_BINDING_COLOR     = 0,
		SSR_TAA_BINDING_POSITION  = 1,
		SSR_TAA_BINDING_RESULT_A  = 2,
		SSR_TAA_BINDING_RESULT_B  = 3,
		SSR_TAA_BINDING_RESULT    = 4,
	};

	// --- Volumetric Fog Compute Pass ---
	enum VolumetricFogPassBinding : uint32_t
	{
		FOG_BINDING_POSITION    = 0,
		FOG_BINDING_SHADOW_MAP  = 1,
		FOG_BINDING_RESULT      = 2,
	};

	// --- Bilateral Filter Compute Pass ---
	enum BilateralFilterPassBinding : uint32_t
	{
		BILATERAL_BINDING_COLOR   = 0,
		BILATERAL_BINDING_DEPTH   = 1,
		BILATERAL_BINDING_RESULT  = 2,
	};

	// --- HIZ Compute Pass ---
	enum HIZPassBinding : uint32_t
	{
		HIZ_BINDING_SOURCE = 0,
		HIZ_BINDING_RESULT = 1,
	};

	// --- GI SH Update Compute Pass ---
	enum GISHUpdatePassBinding : uint32_t
	{
		GISH_BINDING_DIRECT_LIGHT  = 0,
		GISH_BINDING_RESULT_R      = 2,
		GISH_BINDING_RESULT_G      = 3,
		GISH_BINDING_RESULT_B      = 4,
	};

	// --- GI Point Light Compute Pass ---
	enum GIPointLightPassBinding : uint32_t
	{
		GIPL_BINDING_HIT_POSITION     = 0,
		GIPL_BINDING_HIT_NORMAL       = 1,
		GIPL_BINDING_DIRECT_LIGHT     = 2,
		GIPL_BINDING_ENVIRONMENT_MAP  = 4,
		GIPL_BINDING_SH_R             = 5,
		GIPL_BINDING_SH_G             = 6,
		GIPL_BINDING_SH_B             = 7,
		GIPL_BINDING_SHADOW_MAP       = 8,
		GIPL_BINDING_PUNCTUAL_SHADOW  = 9,
		GIPL_BINDING_PBR_DATA         = 10,
	};

	// --- Ray Tracing Pass ---
	enum RayTracingPassBinding : uint32_t
	{
		RT_BINDING_TLAS                  = 0,
		RT_BINDING_RESULT_IMAGE          = 1,
		RT_BINDING_HIT_POSITION          = 2,
		RT_BINDING_VERTEX_BUFFERS        = 3,
		RT_BINDING_INDEX_BUFFERS         = 4,
		RT_BINDING_INSTANCE_DATA         = 5,
		RT_BINDING_HIT_NORMAL            = 6,
		RT_BINDING_INSTANCE_TEX_INDEX    = 7,
		RT_BINDING_HIT_ALBEDO_ROUGHNESS  = 8,
	};

	// ====================================================================
	// Generic Pass Binding Slots (transitional)
	// For legacy code that creates one-off descriptor set layouts outside
	// the factory. Prefer per-pass binding enums for new / migrated code.
	// ====================================================================
	namespace PassBinding
	{
		// Sampled textures (COMBINED_IMAGE_SAMPLER)
		static constexpr uint32_t TEXTURE_0  = 0;
		static constexpr uint32_t TEXTURE_1  = 1;
		static constexpr uint32_t TEXTURE_2  = 2;
		static constexpr uint32_t TEXTURE_3  = 3;
		static constexpr uint32_t TEXTURE_4  = 4;
		static constexpr uint32_t TEXTURE_5  = 5;
		static constexpr uint32_t TEXTURE_6  = 6;
		static constexpr uint32_t TEXTURE_7  = 7;
		static constexpr uint32_t TEXTURE_8  = 8;
		static constexpr uint32_t TEXTURE_9  = 9;
		static constexpr uint32_t TEXTURE_10 = 10;
		static constexpr uint32_t TEXTURE_11 = 11;

		// Storage images (UAV)
		static constexpr uint32_t UAV_IMAGE   = 0;
		static constexpr uint32_t UAV_IMAGE_0 = 1;
		static constexpr uint32_t UAV_IMAGE_1 = 2;
		static constexpr uint32_t UAV_IMAGE_2 = 3;
		static constexpr uint32_t UAV_IMAGE_3 = 4;
		static constexpr uint32_t UAV_IMAGE_4 = 5;
		static constexpr uint32_t UAV_IMAGE_5 = 6;

		// Uniform buffers (constant buffers)
		static constexpr uint32_t CBUFFER_0 = 0;
		static constexpr uint32_t CBUFFER_1 = 1;
		static constexpr uint32_t CBUFFER_2 = 2;
		static constexpr uint32_t CBUFFER_3 = 3;
		static constexpr uint32_t CBUFFER_4 = 4;
		static constexpr uint32_t CBUFFER_5 = 5;
		static constexpr uint32_t CBUFFER_6 = 6;

		// Storage buffers
		static constexpr uint32_t BUFFER_0  = 0;
		static constexpr uint32_t BUFFER_1  = 1;
		static constexpr uint32_t BUFFER_2  = 2;
		static constexpr uint32_t BUFFER_3  = 3;
		static constexpr uint32_t BUFFER_4  = 4;
		static constexpr uint32_t BUFFER_5  = 5;
		static constexpr uint32_t BUFFER_6  = 6;
		static constexpr uint32_t BUFFER_7  = 7;
		static constexpr uint32_t BUFFER_8  = 8;
		static constexpr uint32_t BUFFER_9  = 9;
		static constexpr uint32_t BUFFER_10 = 10;
		static constexpr uint32_t BUFFER_11 = 11;

		// Input attachments
		static constexpr uint32_t INPUT_ATTACHMENT_0 = 0;
		static constexpr uint32_t INPUT_ATTACHMENT_1 = 1;
		static constexpr uint32_t INPUT_ATTACHMENT_2 = 2;
		static constexpr uint32_t INPUT_ATTACHMENT_3 = 3;
		static constexpr uint32_t INPUT_ATTACHMENT_4 = 4;

		// Acceleration structures (ray tracing)
		static constexpr uint32_t TLAS_0 = 0;
	}

	// ====================================================================
	// Universal stage flags for Set 0 bindings
	// We use broad stage flags for global descriptors to prevent "shader cannot read data"
	// issues when a new shader stage decides to access a global UBO/SSBO.
	static constexpr VkShaderStageFlags GLOBAL_STAGES =
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
		VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR | 
		VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

	static constexpr VkShaderStageFlags CAMERA_STAGES = GLOBAL_STAGES;
	static constexpr VkShaderStageFlags LIGHTS_STAGES = GLOBAL_STAGES;
	static constexpr VkShaderStageFlags MATERIAL_STAGES = GLOBAL_STAGES;
	static constexpr VkShaderStageFlags IBL_STAGES = GLOBAL_STAGES;
	static constexpr VkShaderStageFlags BINDLESS_TEX_STAGES = GLOBAL_STAGES;

	// ====================================================================
	// Layout Factory
	// ====================================================================
	class VansDescriptorSetLayoutFactory
	{
	public:
		// ==============================================
		// Set 0: Global (universal - same for all pipelines)
		// ==============================================
		static VkDescriptorSetLayout CreateGlobalLayout(
			VkDevice device,
			uint32_t maxBindlessTextures = MAX_BINDLESS_TEXTURES);

		// ==============================================
		// Set 1: Per-Pass (one creator per pass type)
		// ==============================================
		static VkDescriptorSetLayout CreatePassLayout_Empty(VkDevice device);
		static VkDescriptorSetLayout CreatePassLayout_DeferredLighting(VkDevice device);
		static VkDescriptorSetLayout CreatePassLayout_SkyBox(VkDevice device);
		static VkDescriptorSetLayout CreatePassLayout_ScreenSpace(VkDevice device);
		static VkDescriptorSetLayout CreatePassLayout_PostProcess(VkDevice device);
		static VkDescriptorSetLayout CreatePassLayout_Terrain(VkDevice device);

		// Compute pass layouts
		static VkDescriptorSetLayout CreatePassLayout_SSGI(VkDevice device);
		static VkDescriptorSetLayout CreatePassLayout_SSR_Trace(VkDevice device);
		static VkDescriptorSetLayout CreatePassLayout_SSR_Resolve(VkDevice device);
		static VkDescriptorSetLayout CreatePassLayout_SSR_TemporalAA(VkDevice device);
		static VkDescriptorSetLayout CreatePassLayout_VolumetricFog(VkDevice device);
		static VkDescriptorSetLayout CreatePassLayout_BilateralFilter(VkDevice device);
		static VkDescriptorSetLayout CreatePassLayout_HIZ(VkDevice device);
		static VkDescriptorSetLayout CreatePassLayout_GISHUpdate(VkDevice device);
		static VkDescriptorSetLayout CreatePassLayout_GIPointLight(VkDevice device);

		// Ray tracing pass layout
		static VkDescriptorSetLayout CreatePassLayout_RayTracing(VkDevice device);

		// ==============================================
		// Set 2: Per-Object (geometry passes only)
		// ==============================================
		static VkDescriptorSetLayout CreateObjectLayout(VkDevice device);

		// ==============================================
		// Convenience: build pipeline layout arrays
		// ==============================================

		// For graphics geometry passes (3 sets: Global + Pass + Object)
		static std::array<VkDescriptorSetLayout, DS_COUNT> BuildGraphicsGeometryLayouts(
			VkDescriptorSetLayout globalLayout,
			VkDescriptorSetLayout passLayout,
			VkDescriptorSetLayout objectLayout);

		// For fullscreen passes (2 sets: Global + Pass, no Object)
		static std::vector<VkDescriptorSetLayout> BuildFullscreenPassLayouts(
			VkDescriptorSetLayout globalLayout,
			VkDescriptorSetLayout passLayout);

		// For compute dispatches (2 sets: Global + Pass)
		static std::vector<VkDescriptorSetLayout> BuildComputeLayouts(
			VkDescriptorSetLayout globalLayout,
			VkDescriptorSetLayout passLayout);

		// For ray tracing (2 sets: Global + Pass)
		static std::vector<VkDescriptorSetLayout> BuildRayTracingLayouts(
			VkDescriptorSetLayout globalLayout,
			VkDescriptorSetLayout passLayout);

		// Cleanup
		static void DestroyLayout(VkDevice device, VkDescriptorSetLayout& layout);
	};
}
