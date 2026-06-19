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
		GLOBAL_BINDING_REFLECTION_PROBE_SPECULAR = 13, // Prefiltered samplerCubeArray
		GLOBAL_BINDING_REFLECTION_PROBE_BUFFER   = 14, // Reflection probe metadata SSBO
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
		DEFERRED_BINDING_RECT_LIGHT_EMISSIVE = 15, // 面光源发光贴图数组 (sampler2DArray)
		DEFERRED_BINDING_IES_PROFILES        = 16, // IES profile 纹理数组 (sampler2DArray, R16F)
	};

	// --- SkyBox Pass ---
	enum SkyBoxPassBinding : uint32_t
	{
		SKYBOX_BINDING_ATMOSPHERE_UBO = 0,
		SKYBOX_BINDING_FOG            = 1,
		SKYBOX_BINDING_CLOUD          = 2,   // 1/4 分辨率体积云结果（COMBINED_IMAGE_SAMPLER）
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

	// --- Post-Process Pass（Final Composite，Subpass 1）---
	enum PostProcessPassBinding : uint32_t
	{
		POSTPROCESS_BINDING_COLOR_INPUT  = 0,   // INPUT_ATTACHMENT：SceneColorHDR（subpassLoad）
		POSTPROCESS_BINDING_BLOOM_RESULT = 1,   // COMBINED_IMAGE_SAMPLER：Bloom 合成结果
		POSTPROCESS_BINDING_EXPOSURE_VAL = 2,   // COMBINED_IMAGE_SAMPLER：1x1 当前曝光值（R16F）
		POSTPROCESS_BINDING_PP_PARAMS    = 3,   // UNIFORM_BUFFER：VansPostProcessParamsGPU UBO
	};

	// --- Exposure Luminance Compute Pass ---
	enum ExposureLuminancePassBinding : uint32_t
	{
		EXPOSURE_LUM_BINDING_SRC_COLOR  = 0,   // COMBINED_IMAGE_SAMPLER：SceneColorHDR 输入
		EXPOSURE_LUM_BINDING_LUM_OUT    = 1,   // STORAGE_IMAGE：64x64 亮度输出（R16F）
	};

	// --- Exposure Adapt Compute Pass ---
	enum ExposureAdaptPassBinding : uint32_t
	{
		EXPOSURE_ADAPT_BINDING_LUM_IN   = 0,   // COMBINED_IMAGE_SAMPLER：64x64 亮度输入
		EXPOSURE_ADAPT_BINDING_EXP_OUT  = 1,   // STORAGE_IMAGE：1x1 当前曝光输出（R16F）
		EXPOSURE_ADAPT_BINDING_PARAMS   = 2,   // UNIFORM_BUFFER：自适应曝光参数 UBO
	};

	// --- Bloom Prefilter Compute Pass ---
	enum BloomPrefilterPassBinding : uint32_t
	{
		BLOOM_PREFILTER_BINDING_SRC     = 0,   // COMBINED_IMAGE_SAMPLER：SceneColorHDR 输入
		BLOOM_PREFILTER_BINDING_DST     = 1,   // STORAGE_IMAGE：半分辨率预滤输出（RGBA16F）
		BLOOM_PREFILTER_BINDING_PARAMS  = 2,   // UNIFORM_BUFFER：Bloom 参数（threshold/knee）UBO
	};

	// --- Bloom Downsample Compute Pass（每级共用同一 Layout）---
	enum BloomDownsamplePassBinding : uint32_t
	{
		BLOOM_DOWNSAMPLE_BINDING_SRC    = 0,   // COMBINED_IMAGE_SAMPLER：上一级输入
		BLOOM_DOWNSAMPLE_BINDING_DST    = 1,   // STORAGE_IMAGE：下一级输出（RGBA16F）
	};

	// --- Bloom Upsample Compute Pass（每级共用同一 Layout）---
	enum BloomUpsamplePassBinding : uint32_t
	{
		BLOOM_UPSAMPLE_BINDING_SRC_LO   = 0,   // COMBINED_IMAGE_SAMPLER：较低分辨率输入
		BLOOM_UPSAMPLE_BINDING_SRC_HI   = 1,   // COMBINED_IMAGE_SAMPLER：较高分辨率输入（加法混合）
		BLOOM_UPSAMPLE_BINDING_DST      = 2,   // STORAGE_IMAGE：输出（RGBA16F）
		BLOOM_UPSAMPLE_BINDING_PARAMS   = 3,   // UNIFORM_BUFFER：Bloom scatter 权重 UBO
	};

	// --- Terrain Pass ---
	enum TerrainPassBinding : uint32_t
	{
		TERRAIN_BINDING_HEIGHT_MAP          = 0,
		TERRAIN_BINDING_SPLATMAP_0          = 1,
		TERRAIN_BINDING_SPLATMAP_1          = 2,
		TERRAIN_BINDING_ALBEDO_ARRAY        = 3,  // descriptorCount = 8
		TERRAIN_BINDING_NORMAL_ARRAY        = 4,  // descriptorCount = 8
		TERRAIN_BINDING_ROUGHNESS_ARRAY     = 5,  // descriptorCount = 8
		TERRAIN_BINDING_PARAMS_UBO          = 6,
		TERRAIN_BINDING_TESSELLATION_PARAMS = 7,  // TessellationParams UBO (read by TCS)
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

	// --- Cloud Ray March Compute Pass ---
	enum CloudRayMarchPassBinding : uint32_t
	{
		CLOUD_MARCH_BINDING_RESULT = 0,   // cloudResult (STORAGE_IMAGE, rgba16f, 1/4 分辨率)
		CLOUD_MARCH_BINDING_PARAMS = 1,   // CloudParams UBO (UNIFORM_BUFFER)
		CLOUD_MARCH_BINDING_MAIN_NOISE = 2,   // cloudMainNoise  (sampler3D, 128^3 RGBA Perlin-Worley)
		CLOUD_MARCH_BINDING_DETAIL_NOISE = 3, // cloudDetailNoise(sampler3D, 32^3 RGBA detail)
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

	// --- Decal Pass (Set 1) ---
	// 贴花节点 Pass-level 描述符集：GBuffer2（世界坐标重建）
	enum DecalPassBinding : uint32_t
	{
		DECAL_PASS_BINDING_GBUFFER2 = 0,  // GBuffer2 采样器，用于重建世界坐标
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

	// --- Water GBuffer Pass（Set 1）---
	// 对应 water_prepass.vert/.frag 的 set=1 绑定
	enum WaterGBufferPassBinding : uint32_t
	{
		WATER_GBUF_BINDING_PARAMS        = 0,   // WaterGBufferParams UBO（lodRanges / meshDim 等）
		WATER_GBUF_BINDING_DISPLACEMENT  = 1,   // Compute 输出的水面位移贴图 Texture2DArray（Vertex 采样）
		WATER_GBUF_BINDING_WAVE_SSBO     = 2,   // GerstnerWaveGPU SSBO（W-04）
		WATER_GBUF_BINDING_NORMAL_MAP    = 3,   // 水面法线贴图（W-08）
	};

	// --- Water Wave Compute Pass（Set 0）---
	// 对应 water_wave_spectrum.comp 的 set=0 绑定
	enum WaterWaveComputeBinding : uint32_t
	{
		WATER_WAVE_BINDING_PARAMS        = 0,   // WaterGBufferParams UBO（时间 / 波参数）
		WATER_WAVE_BINDING_DISPLACEMENT  = 1,   // RGBA16F Storage ImageArray，输出 xyz 位移 + w 坡度（W-01）
		WATER_WAVE_BINDING_WAVE_SSBO     = 2,   // GerstnerWaveGPU SSBO 输入（W-04）
	};

	// --- Water Composite Pass（Set 1）---
	// 对应 water_composite.frag 的 set=1 绑定
	enum WaterCompositePassBinding : uint32_t
	{
		WATER_COMP_BINDING_GBUF_NORMAL   = 0,   // WaterGBuf_Normal  (sampler2D)
		WATER_COMP_BINDING_GBUF_DEPTH    = 1,   // WaterGBuf_LinearDepth (sampler2D)
		WATER_COMP_BINDING_PARAMS        = 2,   // WaterCompositeParams UBO
		WATER_COMP_BINDING_SCENE_GBUF2   = 3,   // 主场景 GBuffer2（worldPos.xyz + linearDepth.w）
		WATER_COMP_BINDING_REFLECTION    = 4,   // WaterSSR/反射结果
		WATER_COMP_BINDING_REFRACTION    = 5,   // 折射颜色结果
		WATER_COMP_BINDING_CAUSTICS      = 6,   // 焦散结果
		WATER_COMP_BINDING_FOAM_TEXTURE  = 7,   // W-15: 泡沫纹理
		WATER_COMP_BINDING_THICKNESS     = 8,   // W-16: 厚度图
		WATER_COMP_BINDING_SSS_SCATTER  = 9,   // W-16: SSS 散射输出
	};

	// --- Water Effects Compute Pass（Set 0）---
	// 对应 water_effects.comp 的 set=0 绑定
	enum WaterEffectsComputeBinding : uint32_t
	{
		WATER_EFFECT_BINDING_GBUF_NORMAL    = 0,
		WATER_EFFECT_BINDING_GBUF_DEPTH     = 1,
		WATER_EFFECT_BINDING_SCENE_GBUF2    = 2,
		WATER_EFFECT_BINDING_SCENE_COLOR    = 3,
		WATER_EFFECT_BINDING_PARAMS         = 4,
		WATER_EFFECT_BINDING_REFLECTION_OUT = 5,
		WATER_EFFECT_BINDING_REFRACTION_OUT = 6,
		WATER_EFFECT_BINDING_CAUSTICS_OUT   = 7,
	};

	// --- Water SSR Compute（Set 0）- W-12 ---
	enum WaterSSRComputeBinding : uint32_t
	{
		WATER_SSR_BINDING_GBUF_NORMAL  = 0,   // WaterGBuf Normal (sampler2D)
		WATER_SSR_BINDING_GBUF_DEPTH   = 1,   // WaterGBuf LinearDepth (sampler2D)
		WATER_SSR_BINDING_SCENE_HZB    = 2,   // 场景 HZB mip chain (sampler2D)
		WATER_SSR_BINDING_SCENE_GBUF2  = 3,   // 场景 GBuffer2 (sampler2D)
		WATER_SSR_BINDING_SCENE_COLOR  = 4,   // 场景 SceneColor (sampler2D)
		WATER_SSR_BINDING_PARAMS       = 5,   // WaterSSRParams UBO
		WATER_SSR_BINDING_REFLECTION_OUT = 6, // Reflection RGBA16F (storage image)
	};

	// --- Water Refraction Compute（Set 0）- W-13 ---
	enum WaterRefractionComputeBinding : uint32_t
	{
		WATER_REFRACTION_BINDING_GBUF_NORMAL  = 0,
		WATER_REFRACTION_BINDING_GBUF_DEPTH   = 1,
		WATER_REFRACTION_BINDING_SCENE_GBUF2  = 2,
		WATER_REFRACTION_BINDING_SCENE_COLOR  = 3,
		WATER_REFRACTION_BINDING_PARAMS       = 4,
		WATER_REFRACTION_BINDING_REFRACTION_OUT = 5,
	};

	// --- Water Caustics Compute（Set 0）- W-14 ---
	enum WaterCausticsComputeBinding : uint32_t
	{
		WATER_CAUSTICS_BINDING_GBUF_NORMAL  = 0,
		WATER_CAUSTICS_BINDING_GBUF_DEPTH   = 1,
		WATER_CAUSTICS_BINDING_SCENE_GBUF2  = 2,
		WATER_CAUSTICS_BINDING_PARAMS       = 3,
		WATER_CAUSTICS_BINDING_CAUSTICS_OUT = 4,
	};

	// --- Water Thickness Compute（Set 0）- W-16 ---
	enum WaterThicknessComputeBinding : uint32_t
	{
		WATER_THICKNESS_BINDING_GBUF_DEPTH  = 0,
		WATER_THICKNESS_BINDING_SCENE_GBUF2 = 1,
		WATER_THICKNESS_BINDING_PARAMS      = 2,
		WATER_THICKNESS_BINDING_THICKNESS_OUT = 3,
	};

	// --- Water SSS Scatter Compute（Set 0）- W-16 Phase 2 ---
	enum WaterSSSScatterComputeBinding : uint32_t
	{
		WATER_SSS_SCATTER_BINDING_GBUF_NORMAL   = 0,
		WATER_SSS_SCATTER_BINDING_GBUF_DEPTH    = 1,
		WATER_SSS_SCATTER_BINDING_THICKNESS_MAP = 2,
		WATER_SSS_SCATTER_BINDING_SCENE_GBUF2   = 3,
		WATER_SSS_SCATTER_BINDING_PARAMS        = 4,
		WATER_SSS_SCATTER_BINDING_SCATTER_OUT   = 5,
	};

	// --- Water Detail Normal Compute（Set 0）- N-01 ---
	enum WaterDetailNormalBinding : uint32_t
	{
		WATER_DETAIL_BINDING_PARAMS       = 0,   // WaterGBufferParams UBO
		WATER_DETAIL_BINDING_OUTPUT       = 1,   // Detail normal storage image array
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
		VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
		VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
		VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;

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
		static void CreateAndAllocate_CloudRayMarch(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
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
		// 贴花 Pass Set 1：仅绑定 GBuffer2 用于世界坐标重建
		static void CreateAndAllocate_DecalPass(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);

		// --- 水面 Pass Layouts ---
		// Water GBuffer Pass Set 1：WaterGBufferParams UBO（CDLOD LOD 参数）
		static void CreateAndAllocate_WaterGBuffer(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		// Water Wave Compute Set 0：WaterGBufferParams UBO + 位移 Storage Image
		static void CreateAndAllocate_WaterWaveCompute(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		// Water Composite Pass Set 1：WaterGBuf_Normal + WaterGBuf_LinearDepth + Params UBO
		static void CreateAndAllocate_WaterComposite(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		// Water Effects Compute Set 0：WaterGBuf / SceneColor 输入 + 反射折射焦散输出
		static void CreateAndAllocate_WaterEffectsCompute(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		// Phase 2 独立 CS Layouts
		static void CreateAndAllocate_WaterSSRCompute(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_WaterRefractionCompute(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_WaterCausticsCompute(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_WaterThicknessCompute(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		// W-16 Phase 2: Water SSS Scatter Compute (water_sss_scatter.comp)
		static void CreateAndAllocate_WaterSSSScatterCompute(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		// N-01: Water Detail Normal Compute (water_detail_normal.comp)
		static void CreateAndAllocate_WaterDetailNormalCompute(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);

		// --- 后处理 Compute Pass Layouts ---
		static void CreateAndAllocate_ExposureLuminance(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_ExposureAdapt(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_BloomPrefilter(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_BloomDownsample(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
		static void CreateAndAllocate_BloomUpsample(VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount = 1);
	};
}
