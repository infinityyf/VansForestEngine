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
		DS_OBJECT       = 2,  // Per-object data (Transform SSBO, shared by all geometry nodes)
		DS_ANIMATION    = 3,  // Per-node animation data (Bone matrices + bone weight SSBOs)
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
		GLOBAL_BINDING_SKIN_BSDF_LUT            = 7,
		GLOBAL_BINDING_CLOTH_BRDF_LUT           = 8,
		GLOBAL_BINDING_TILE_LIGHT_GRID          = 9,   // TileLight Header SSBO (readonly)
		GLOBAL_BINDING_TILE_LIGHT_INDICES       = 10,  // TileLight Index SSBO  (readonly)
		GLOBAL_BINDING_LTC1_LUT                 = 11,  // Area-light LTC matrix LUT (RGBA16F 64x64)
		GLOBAL_BINDING_LTC2_LUT                 = 12,  // Area-light LTC amplitude/Fresnel LUT (RGBA16F 64x64)
		GLOBAL_BINDING_BINDLESS_TEXTURES        = 50,  // Variable count
	};

	// ====================================================================
	// Set 2 (Per-Object) Binding Indices
	// ====================================================================
	enum ObjectBinding : uint32_t
	{
		OBJECT_BINDING_TRANSFORM_SSBO   = 0,   // Transform matrix SSBO (shared by all nodes)
	};

	// ====================================================================
	// Set 3 (Per-Node Animation) Binding Indices
	// Only used by VansCommonRenderNode for deferred opaque geometry.
	// Animated nodes get real bone/weight buffers; static nodes get shared dummies.
	// ====================================================================
	enum AnimationBinding : uint32_t
	{
		ANIMATION_BINDING_BONEID_SSBO      = 0,   // Per-vertex bone IDs SSBO (ivec4 per vertex, per-submesh)
		ANIMATION_BINDING_BONE_SSBO        = 1,   // Bone matrices SSBO (mat4[MAX_BONES])
		ANIMATION_BINDING_BONEWEIGHT_SSBO  = 2,   // Per-vertex bone weights SSBO (vec4 per vertex, per-submesh)
	};

	// ====================================================================
	// Set 4 (Per-Node Skin Texture) Binding Indices
	// Only used by VansCommonRenderNode when the material type is VAN_SKIN.
	// Each skin node owns its descriptor set with dedicated albedo + normal textures.
	// ====================================================================
	enum SkinTextureBinding : uint32_t
	{
		SKIN_TEXTURE_BINDING_ALBEDO  = 0,   // Skin albedo texture (COMBINED_IMAGE_SAMPLER)
		SKIN_TEXTURE_BINDING_NORMAL  = 1,   // Skin normal texture (COMBINED_IMAGE_SAMPLER)
	};

	// ====================================================================
	// Set 4 (Per-Node Cloth Texture) Binding Indices
	// Only used by VansCommonRenderNode when the material type is VAN_CLOTH.
	// Each cloth node owns its descriptor set with dedicated albedo + normal textures.
	// ====================================================================
	enum ClothTextureBinding : uint32_t
	{
		CLOTH_TEXTURE_BINDING_ALBEDO     = 0,   // Cloth albedo texture (COMBINED_IMAGE_SAMPLER)
		CLOTH_TEXTURE_BINDING_NORMAL     = 1,   // Cloth normal texture (COMBINED_IMAGE_SAMPLER)
		CLOTH_TEXTURE_BINDING_ROUGHNESS  = 2,   // Cloth roughness texture (COMBINED_IMAGE_SAMPLER)
		CLOTH_TEXTURE_BINDING_AO         = 3,   // Cloth ambient occlusion texture (COMBINED_IMAGE_SAMPLER)
	};

	// ====================================================================
	// Set 4 (Per-Node Hair Texture) Binding Indices
	// Only used by VansCommonRenderNode when the material type is VAN_HAIR.
	// Each hair node owns its descriptor set with dedicated hair textures.
	// ====================================================================
	enum HairTextureBinding : uint32_t
	{
		HAIR_TEXTURE_BINDING_ALBEDO_ALPHA = 0,  // Hair albedo+alpha texture (COMBINED_IMAGE_SAMPLER)
		HAIR_TEXTURE_BINDING_NORMAL       = 1,  // Hair normal texture (COMBINED_IMAGE_SAMPLER)
		HAIR_TEXTURE_BINDING_ROUGHNESS    = 2,  // Hair roughness texture (COMBINED_IMAGE_SAMPLER)
		HAIR_TEXTURE_BINDING_AO           = 3,  // Hair ambient occlusion texture (COMBINED_IMAGE_SAMPLER)
		HAIR_TEXTURE_BINDING_SHIFT        = 4,  // Hair strand shift texture (COMBINED_IMAGE_SAMPLER)
		HAIR_TEXTURE_BINDING_ALPHA        = 5,  // Hair dedicated alpha mask texture (COMBINED_IMAGE_SAMPLER)
		HAIR_TEXTURE_BINDING_FLOW         = 6,  // Hair flow map texture (COMBINED_IMAGE_SAMPLER) — bends tangent/normal
	};

	// ====================================================================
	// Set 4 (Per-Node Subsurface Texture) Binding Indices
	// Only used by VansCommonRenderNode when the material type is VAN_SUBSURFACE.
	// Each subsurface node owns its descriptor set with dedicated textures.
	// ====================================================================
	enum SubsurfaceTextureBinding : uint32_t
	{
		SUBSURFACE_TEXTURE_BINDING_ALBEDO    = 0,  // Base color texture (COMBINED_IMAGE_SAMPLER)
		SUBSURFACE_TEXTURE_BINDING_NORMAL    = 1,  // Normal map texture (COMBINED_IMAGE_SAMPLER)
		SUBSURFACE_TEXTURE_BINDING_THICKNESS = 2,  // Thickness map texture (COMBINED_IMAGE_SAMPLER)
		SUBSURFACE_TEXTURE_BINDING_ROUGHNESS = 3,  // Roughness texture (COMBINED_IMAGE_SAMPLER)
	};

	// ====================================================================
	// Set 4 (Per-Node Grass Texture) Binding Indices
	// Only used by VansVegetationRenderNode when the material type is VAN_GRASS.
	// Each grass node owns its descriptor set with dedicated vegetation textures.
	// ====================================================================
	enum GrassTextureBinding : uint32_t
	{
		GRASS_TEXTURE_BINDING_ALBEDO        = 0,  // Grass albedo texture (COMBINED_IMAGE_SAMPLER)
		GRASS_TEXTURE_BINDING_NORMAL        = 1,  // Grass normal texture (COMBINED_IMAGE_SAMPLER)
		GRASS_TEXTURE_BINDING_ROUGHNESS     = 2,  // Grass roughness texture (COMBINED_IMAGE_SAMPLER)
		GRASS_TEXTURE_BINDING_TRANSLUCENCY  = 3,  // Grass translucency/thickness mask (COMBINED_IMAGE_SAMPLER)
		GRASS_TEXTURE_BINDING_AO            = 4,  // Grass ambient occlusion (COMBINED_IMAGE_SAMPLER)
	};

	// ====================================================================
	// Vegetation Compute — Bone Simulation Pass Binding Indices
	// ====================================================================
	enum VegetationBoneSimBinding : uint32_t
	{
		VEG_SIM_BINDING_INSTANCE_DATA    = 0,  // SSBO (read) — per-instance position/scale/cosR/sinR
		VEG_SIM_BINDING_BONE_DATA        = 1,  // SSBO (read/write) — bone positions + velocities
		VEG_SIM_BINDING_BONE_MATRICES    = 2,  // SSBO (write) — output bone mat4 matrices
		VEG_SIM_BINDING_TERRAIN_HEIGHTMAP = 3, // COMBINED_IMAGE_SAMPLER — terrain heightmap for ground placement
		VEG_SIM_BINDING_LOD_FACTORS      = 4,  // SSBO (write) — per-instance LOD factor (1=full sim, 0=skip)
		VEG_SIM_BINDING_SCATTER_OFFSETS  = 5,  // UBO (read) — shared sub-blade scatter offsets (P6a)
	};

	// ====================================================================
	// Vegetation Draw — Per-Config SSBO Data (Set 3) Binding Indices
	// Vertex shader performs bone skinning using bone matrices + weight buffer.
	// ====================================================================
	enum VegetationDrawBinding : uint32_t
	{
		VEG_DRAW_BINDING_BONE_MATRICES    = 0,  // SSBO (read) — bone mat4 from bone sim
		VEG_DRAW_BINDING_BONE_WEIGHTS     = 1,  // SSBO (read) — per-vertex vec4(boneIdx0, boneIdx1, w0, w1)
		VEG_DRAW_BINDING_INSTANCE_REMAP   = 2,  // SSBO (read) — uint[] maps draw instance → global bone chain
		VEG_DRAW_BINDING_SCATTER_OFFSETS  = 3,  // UBO (read) — shared sub-blade scatter offsets (P6a)
		VEG_DRAW_BINDING_LOD_FACTORS      = 4,  // SSBO (read) — per-instance LOD factor
		VEG_DRAW_BINDING_INSTANCE_DATA    = 5,  // SSBO (read) — per-instance position/scale/cosR/sinR
		VEG_DRAW_BINDING_TERRAIN_HEIGHTMAP = 6, // COMBINED_IMAGE_SAMPLER — terrain heightmap for VS sub-blade Y
		VEG_DRAW_BINDING_VISIBILITY_FLAGS  = 7, // SSBO (read) — per-instance visibility from GPU cull (P0)
	};

	// ====================================================================
	// Vegetation Compute — GPU Cull Pass Binding Indices (P0)
	// ====================================================================
	enum VegetationCullBinding : uint32_t
	{
		VEG_CULL_BINDING_INSTANCE_DATA    = 0,  // SSBO (read) — per-instance position/scale
		VEG_CULL_BINDING_VISIBILITY       = 1,  // SSBO (write) — uint per instance: 1=visible, 0=culled
		VEG_CULL_BINDING_VISIBLE_COUNT    = 2,  // SSBO (read/write) — atomic counter
		VEG_CULL_BINDING_VISIBLE_INDICES  = 3,  // SSBO (write) — compact list of visible instance indices
		VEG_CULL_BINDING_TERRAIN_HEIGHTMAP = 4, // COMBINED_IMAGE_SAMPLER — terrain heightmap for correct Y
		VEG_CULL_BINDING_HIZ              = 5,  // COMBINED_IMAGE_SAMPLER — Hi-Z depth pyramid for occlusion culling
	};

	// ====================================================================
	// Constants
	// ====================================================================
	static constexpr uint32_t MAX_BINDLESS_TEXTURES = 2048;

	// ====================================================================
	// Set 1 Per-Pass Binding Indices
	// Each enum mirrors the bindings in CreateAndAllocate_* factory methods.
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
		SKYBOX_BINDING_FOG            = 1,
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
		TERRAIN_BINDING_HEIGHT_MAP       = 0,
		TERRAIN_BINDING_SPLATMAP_0       = 1,
		TERRAIN_BINDING_SPLATMAP_1       = 2,
		TERRAIN_BINDING_ALBEDO_ARRAY     = 3,  // descriptorCount = 8
		TERRAIN_BINDING_NORMAL_ARRAY     = 4,  // descriptorCount = 8
		TERRAIN_BINDING_ROUGHNESS_ARRAY  = 5,  // descriptorCount = 8
		TERRAIN_BINDING_PARAMS_UBO       = 6,
	};

	static constexpr uint32_t TERRAIN_MAX_LAYERS = 8;

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
		SSR_TAA_BINDING_COLOR          = 0,
		SSR_TAA_BINDING_POSITION       = 1,
		SSR_TAA_BINDING_RESULT_A       = 2,
		SSR_TAA_BINDING_RESULT_B       = 3,
		SSR_TAA_BINDING_RESULT         = 4,
		SSR_TAA_BINDING_MOTION_VECTOR  = 5,
	};

	// --- Volumetric Fog Compose Compute Pass ---
	enum VolumetricFogPassBinding : uint32_t
	{
		FOG_BINDING_POSITION      = 0,   // inputPosition    (COMBINED_IMAGE_SAMPLER)
		FOG_BINDING_RESULT        = 1,   // fogResult        (STORAGE_IMAGE)
		FOG_BINDING_PARAMS        = 2,   // FogParams UBO    (UNIFORM_BUFFER)
		FOG_BINDING_VOXEL_VOLUME  = 3,   // voxelFogVolume   (COMBINED_IMAGE_SAMPLER, 3D)
		FOG_BINDING_VOLUME_PARAMS = 4,   // FogVolumeParams UBO (UNIFORM_BUFFER)
	};

	// --- Fog Light Injection Compute Pass ---
	enum FogLightInjectionPassBinding : uint32_t
	{
		FOG_INJECT_BINDING_VOXEL_GRID       = 0,   // i_VoxelGrid       (STORAGE_IMAGE, 3D, rgba16f)
		FOG_INJECT_BINDING_SHADOW_MAP       = 1,   // fogShadowMap      (COMBINED_IMAGE_SAMPLER)
		FOG_INJECT_BINDING_PARAMS           = 2,   // FogVolumeParams UBO
		FOG_INJECT_BINDING_HISTORY          = 3,   // s_History         (COMBINED_IMAGE_SAMPLER, 3D)
		FOG_INJECT_BINDING_PUNCTUAL_SHADOW  = 4,   // punctualShadowMap (COMBINED_IMAGE_SAMPLER)
	};

	// --- TileLight Build Compute Pass (Set 1, write access) ---
	enum TileLightBuildPassBinding : uint32_t
	{
		TILE_BUILD_BINDING_GRID    = 0,  // TileLightHeader SSBO (write)
		TILE_BUILD_BINDING_INDICES = 1,  // TileLight Index SSBO  (write)
		TILE_BUILD_BINDING_PARAMS  = 2,  // TileLightBuildParams UBO (read)
	};

	// --- Fog Ray March Accumulate Compute Pass ---
	enum FogRayMarchPassBinding : uint32_t
	{
		FOG_MARCH_BINDING_INPUT_VOXEL    = 0,   // s_VoxelGrid       (COMBINED_IMAGE_SAMPLER, 3D)
		FOG_MARCH_BINDING_RESULT         = 1,   // i_RayMarchResult  (STORAGE_IMAGE, 3D, rgba16f)
		FOG_MARCH_BINDING_VOLUME_PARAMS  = 2,   // FogVolumeParams UBO (UNIFORM_BUFFER)
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

	// --- HIZ Seed Compute Pass (GBuffer linear depth → HIZ mip 0) ---
	enum HIZSeedPassBinding : uint32_t
	{
		HIZ_SEED_BINDING_POSITION = 0,   // GBuffer position (COMBINED_IMAGE_SAMPLER)
		HIZ_SEED_BINDING_HIZ_MIP0 = 1,   // HIZ mip 0 输出 (STORAGE_IMAGE, r32f)
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

		// ==============================================
		// Set 1: Per-Pass (one creator per pass type)
		// All pass layouts are now created via CreateAndAllocate_* methods below.
		// ==============================================

		// ==============================================
		// Set 2: Per-Object (geometry passes only)
		// ==============================================

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

		// ==============================================
		// Combined Layout Creation + Set Allocation
		// Uses VansVKDescriptorManager internally.
		// ==============================================
		static void CreateAndAllocate_Global(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t maxBindlessTextures = MAX_BINDLESS_TEXTURES, uint32_t setCount = 1);
		static void CreateAndAllocate_Object(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_Animation(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_PostProcess(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_DeferredLighting(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_SkyBox(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_ScreenSpace(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_Empty(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_Terrain(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_SSGI(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_SSGITemporal(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 2);
		static void CreateAndAllocate_SSR_Trace(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_SSR_Resolve(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_SSR_TemporalAA(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_VolumetricFog(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_FogLightInjection(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_FogRayMarch(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_BilateralFilter(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 3);
		static void CreateAndAllocate_HIZ(std::vector<VkDescriptorSetLayout>& outLayouts, std::vector<VkDescriptorSet>& outSets, uint32_t mipCount);
		static void CreateAndAllocate_HIZSeed(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_GISHUpdate(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_GIPointLight(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_RayTracing(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_SkinTexture(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_ClothTexture(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_HairTexture(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_SubsurfaceTexture(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_GrassTexture(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_VegetationBoneSim(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_VegetationDraw(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_VegetationCull(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_TileLightBuild(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
	};
}
