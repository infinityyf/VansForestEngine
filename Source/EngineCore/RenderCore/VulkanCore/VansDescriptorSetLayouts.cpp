#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansDescriptorSetLayouts.h"
#include "VansVKDescriptorManager.h"
#include <cassert>

namespace VansGraphics
{

// ============================================================
// Helper: create layout + allocate N descriptor sets
// ============================================================
static void CreateLayoutAndAllocateSets(
	const std::vector<VkDescriptorSetLayoutBinding>& bindings,
	VkDescriptorSetLayout& outLayout,
	std::vector<VkDescriptorSet>& outSets,
	uint32_t setCount)
{
	VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout(bindings, outLayout);
	std::vector<VkDescriptorSetLayout> layouts(setCount, outLayout);
	VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet(layouts, outSets);
}

// ============================================================
// Set 0: Global Layout (universal across all pipelines)
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_Global(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets,
	uint32_t maxBindlessTextures, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		// binding 0: Camera UBO
		{GLOBAL_BINDING_CAMERA_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
		 CAMERA_STAGES, nullptr},
		// binding 1: Lights SSBO (changed from UBO to SSBO to support MAX_POINT_LIGHTS=64)
		{GLOBAL_BINDING_LIGHTS_UBO, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 LIGHTS_STAGES, nullptr},
		// binding 2: Material SSBO
		{GLOBAL_BINDING_MATERIAL_SSBO, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 MATERIAL_STAGES, nullptr},
		// binding 3: BRDF LUT
		{GLOBAL_BINDING_BRDF_LUT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 IBL_STAGES, nullptr},
		// binding 4: Pre-convolved diffuse environment
		{GLOBAL_BINDING_PRECONV_DIFFUSE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 IBL_STAGES, nullptr},
		// binding 5: Pre-convolved specular environment
		{GLOBAL_BINDING_PRECONV_SPECULAR, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 IBL_STAGES, nullptr},
		// binding 6: SH coefficients buffer
		{GLOBAL_BINDING_SH_COEFFICIENTS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 IBL_STAGES, nullptr},
		// binding 7: Skin pre-integrated BSDF LUT
		{GLOBAL_BINDING_SKIN_BSDF_LUT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 IBL_STAGES, nullptr},
		// binding 9: TileLight Header SSBO (readonly in shaders)
		{GLOBAL_BINDING_TILE_LIGHT_GRID,    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 GLOBAL_STAGES, nullptr},
		// binding 10: TileLight Index SSBO (readonly in shaders)
		{GLOBAL_BINDING_TILE_LIGHT_INDICES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 GLOBAL_STAGES, nullptr},
		// binding 11: Area-light LTC matrix LUT (RGBA16F 64x64)
		{GLOBAL_BINDING_LTC1_LUT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 IBL_STAGES, nullptr},
		// binding 12: Area-light LTC amplitude/Fresnel LUT (RGBA16F 64x64)
		{GLOBAL_BINDING_LTC2_LUT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 IBL_STAGES, nullptr},
		// bindings 13/14: local reflection probes
		{GLOBAL_BINDING_REFLECTION_PROBE_SPECULAR, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 IBL_STAGES | VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{GLOBAL_BINDING_REFLECTION_PROBE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 IBL_STAGES | VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		// binding 50: Bindless PBR textures (fixed max count, no variable descriptor)
		{GLOBAL_BINDING_BINDLESS_TEXTURES, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		 maxBindlessTextures, BINDLESS_TEX_STAGES, nullptr},
	};

	// 为每个 binding 设置标志位：仅 bindless 纹理数组（最后一项）需要 UPDATE_AFTER_BIND，
	// 以允许在 GPU 执行期间（如视频源切换时）通过 vkUpdateDescriptorSets 更新该槽位。
	std::vector<VkDescriptorBindingFlags> bindingFlags(bindings.size(), 0);
	bindingFlags.back() = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

	VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayoutWithFlags(
		bindings, bindingFlags,
		VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
		outLayout);

	std::vector<VkDescriptorSetLayout> layouts(setCount, outLayout);
	VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet(layouts, outSets);
}

// ============================================================
// Set 2: Per-Object Layout (1 binding: Transform SSBO only)
// Shared by all geometry nodes (opaque, transparent, shadow, terrain).
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_Object(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		// binding 0: Instance Transform SSBO (all nodes index into this)
		// Fragment stage 也需要访问（贴花 pass 在 fragment shader 中读取 ModelMatrix 做 OBB 测试）
		{OBJECT_BINDING_TRANSFORM_SSBO, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// ============================================================
// Set 3: Per-Node Animation Layout (3 bindings: Bone IDs + Bone Matrices + Bone Weights)
// Used exclusively by VansCommonRenderNode (deferred opaque geometry).
// Animated nodes get real buffers; static nodes bind scene-shared dummy buffers.
// Each submesh gets its own bone ID and weight buffers (no offset needed).
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_Animation(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		// binding 0: Per-vertex Bone IDs SSBO (ivec4 per vertex, per-submesh)
		{ANIMATION_BINDING_BONEID_SSBO,     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT, nullptr},
		// binding 1: Bone Matrices SSBO (mat4[MAX_BONES])
		{ANIMATION_BINDING_BONE_SSBO,       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT, nullptr},
		// binding 2: Per-vertex Bone Weights SSBO (vec4 per vertex, per-submesh)
		{ANIMATION_BINDING_BONEWEIGHT_SSBO, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}


// ============================================================
// Convenience layout builders
// ============================================================

std::array<VkDescriptorSetLayout, DS_COUNT>
VansDescriptorSetLayoutFactory::BuildGraphicsGeometryLayouts(
	VkDescriptorSetLayout globalLayout,
	VkDescriptorSetLayout passLayout,
	VkDescriptorSetLayout objectLayout)
{
	return { globalLayout, passLayout, objectLayout };
}

std::vector<VkDescriptorSetLayout>
VansDescriptorSetLayoutFactory::BuildFullscreenPassLayouts(
	VkDescriptorSetLayout globalLayout,
	VkDescriptorSetLayout passLayout)
{
	return { globalLayout, passLayout };
}

std::vector<VkDescriptorSetLayout>
VansDescriptorSetLayoutFactory::BuildComputeLayouts(
	VkDescriptorSetLayout globalLayout,
	VkDescriptorSetLayout passLayout)
{
	return { globalLayout, passLayout };
}

std::vector<VkDescriptorSetLayout>
VansDescriptorSetLayoutFactory::BuildRayTracingLayouts(
	VkDescriptorSetLayout globalLayout,
	VkDescriptorSetLayout passLayout)
{
	return { globalLayout, passLayout };
}

void VansDescriptorSetLayoutFactory::DestroyLayout(VkDevice device, VkDescriptorSetLayout& layout)
{
	if (layout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(device, layout, nullptr);
		layout = VK_NULL_HANDLE;
	}
}

// ============================================================
// Combined Layout + Set Allocation Methods
// ============================================================

void VansDescriptorSetLayoutFactory::CreateAndAllocate_PostProcess(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		// binding 0：SceneColorHDR 输入（subpassLoad，tile-local）
		{POSTPROCESS_BINDING_COLOR_INPUT,  VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,      1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 1：Bloom 合成结果（Compute 前序输出）
		{POSTPROCESS_BINDING_BLOOM_RESULT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 2：1x1 当前曝光值（R16F，auto-exposure 输出）
		{POSTPROCESS_BINDING_EXPOSURE_VAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 3：后处理参数 UBO（ToneMapping / ColorGrading / Vignette / FilmGrain 等）
		{POSTPROCESS_BINDING_PP_PARAMS,    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// ============================================================
// Exposure Luminance Compute：SceneColorHDR → 64x64 亮度缩图
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_ExposureLuminance(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{EXPOSURE_LUM_BINDING_SRC_COLOR, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{EXPOSURE_LUM_BINDING_LUM_OUT,   VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// ============================================================
// Exposure Adapt Compute：64x64 亮度 → 1x1 曝光值自适应收敛
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_ExposureAdapt(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{EXPOSURE_ADAPT_BINDING_LUM_IN,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{EXPOSURE_ADAPT_BINDING_EXP_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{EXPOSURE_ADAPT_BINDING_PARAMS,  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// ============================================================
// Bloom Prefilter Compute：SceneColorHDR → 半分辨率预滤
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_BloomPrefilter(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{BLOOM_PREFILTER_BINDING_SRC,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{BLOOM_PREFILTER_BINDING_DST,    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{BLOOM_PREFILTER_BINDING_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// ============================================================
// Bloom Downsample Compute（每级共用同一 Layout）：SRC → DST 下采样
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_BloomDownsample(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{BLOOM_DOWNSAMPLE_BINDING_SRC, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{BLOOM_DOWNSAMPLE_BINDING_DST, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// ============================================================
// Bloom Upsample Compute（每级共用同一 Layout）：SRC_LO + SRC_HI → DST
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_BloomUpsample(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{BLOOM_UPSAMPLE_BINDING_SRC_LO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{BLOOM_UPSAMPLE_BINDING_SRC_HI, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{BLOOM_UPSAMPLE_BINDING_DST,    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{BLOOM_UPSAMPLE_BINDING_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}


void VansDescriptorSetLayoutFactory::CreateAndAllocate_DeferredLighting(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		// GBuffer 在独立 pass 中生成，Deferred 通过采样器读取。
		{0,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{1,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{2,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{3,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{4,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// Screen-space effect results (bindings 5-13)
		{5,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{6,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{7,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{8,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{9,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{14, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// 面光源发光贴图数组 (binding 15, sampler2DArray)
		{15, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// IES profile 纹理数组 (binding 16, sampler2DArray, R16F)
		{16, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_SkyBox(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{SKYBOX_BINDING_ATMOSPHERE_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{SKYBOX_BINDING_FOG, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// 1/4 分辨率体积云结果（RGB=内散射，A=透射率）
		{SKYBOX_BINDING_CLOUD, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_ScreenSpace(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{SCREEN_BINDING_NORMAL,      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{SCREEN_BINDING_GBUFFER0,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{SCREEN_BINDING_GBUFFER1,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{SCREEN_BINDING_GBUFFER2,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{SCREEN_BINDING_DEPTH,       VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{SCREEN_BINDING_SSAO_OUTPUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_SSGI(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{SSGI_BINDING_NORMAL,      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSGI_BINDING_DEPTH,       VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSGI_BINDING_COLOR,       VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSGI_BINDING_POSITION,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSGI_BINDING_SKY_DIFFUSE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSGI_BINDING_RESULT,      VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSGI_BINDING_INFO_UBO,    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSGI_BINDING_SH_R,        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSGI_BINDING_SH_G,        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSGI_BINDING_SH_B,        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSGI_BINDING_HIZ_DEPTH,   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_ScreenSpaceShadow(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{SSS_BINDING_NORMAL,   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSS_BINDING_GBUFFER2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSS_BINDING_HIZ,      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSS_BINDING_RESULT,   VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSS_BINDING_PARAMS,   VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_SSGITemporal(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{SSGI_TEMPORAL_BINDING_DEPTH,          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSGI_TEMPORAL_BINDING_MOTION_VECTOR,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSGI_TEMPORAL_BINDING_HISTORY_GI,     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSGI_TEMPORAL_BINDING_CURRENT_GI,     VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSGI_TEMPORAL_BINDING_ACCUMULATED_GI, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSGI_TEMPORAL_BINDING_INFO_UBO,       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_SSR_Trace(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{SSR_TRACE_BINDING_NORMAL,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_TRACE_BINDING_ROUGHNESS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_TRACE_BINDING_POSITION,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_TRACE_BINDING_HIZ,       VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_TRACE_BINDING_HIT,       VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_TRACE_BINDING_PDF,       VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_SSR_Resolve(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{SSR_RESOLVE_BINDING_COLOR,     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_RESOLVE_BINDING_ROUGHNESS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_RESOLVE_BINDING_NORMAL,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_RESOLVE_BINDING_POSITION,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_RESOLVE_BINDING_HIT,       VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_RESOLVE_BINDING_PDF,       VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_RESOLVE_BINDING_RESULT,    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_SSR_TemporalAA(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{SSR_TAA_BINDING_COLOR,         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_TAA_BINDING_POSITION,      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_TAA_BINDING_RESULT_A,      VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_TAA_BINDING_RESULT_B,      VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_TAA_BINDING_RESULT,        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_TAA_BINDING_MOTION_VECTOR, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_VolumetricFog(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{FOG_BINDING_POSITION,      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{FOG_BINDING_RESULT,        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{FOG_BINDING_PARAMS,        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{FOG_BINDING_VOXEL_VOLUME,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{FOG_BINDING_VOLUME_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_FogLightInjection(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{FOG_INJECT_BINDING_VOXEL_GRID,       VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{FOG_INJECT_BINDING_SHADOW_MAP,       VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{FOG_INJECT_BINDING_PARAMS,           VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{FOG_INJECT_BINDING_HISTORY,          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{FOG_INJECT_BINDING_PUNCTUAL_SHADOW,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_FogRayMarch(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{FOG_MARCH_BINDING_INPUT_VOXEL,   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{FOG_MARCH_BINDING_RESULT,         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{FOG_MARCH_BINDING_VOLUME_PARAMS,  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_CloudRayMarch(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		// binding 0: 云层结果输出（1/4 分辨率，RGBA16F，RGB=内散射，A=透射率）
		{CLOUD_MARCH_BINDING_RESULT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		// binding 1: CloudParams UBO（云层参数：行星半径、云底/顶高度、密度等）
		{CLOUD_MARCH_BINDING_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		// binding 2/3: 预计算 3D 噪声纹理（主形状 + 细节侵蚀）
		{CLOUD_MARCH_BINDING_MAIN_NOISE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{CLOUD_MARCH_BINDING_DETAIL_NOISE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_BilateralFilter(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{BILATERAL_BINDING_COLOR,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{BILATERAL_BINDING_DEPTH,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{BILATERAL_BINDING_RESULT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_HIZ(
	std::vector<VkDescriptorSetLayout>& outLayouts, std::vector<VkDescriptorSet>& outSets, uint32_t mipCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{HIZ_BINDING_SOURCE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{HIZ_BINDING_RESULT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout(bindings, layout);
	outLayouts.resize(mipCount);
	for (uint32_t i = 0; i < mipCount; ++i)
	{
		outLayouts[i] = layout;
	}
	VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet(outLayouts, outSets);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_HIZSeed(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	// binding 0: GBuffer position 输入（采样器）
	// binding 1: HIZ mip 0 存储图输出（r32f 线性深度）
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{HIZ_SEED_BINDING_POSITION, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{HIZ_SEED_BINDING_HIZ_MIP0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_Empty(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings;
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_Terrain(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		// binding 0: heightMap — VS, TCS (patch center), TES (displacement), FS (normal)
		{TERRAIN_BINDING_HEIGHT_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
		 VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{TERRAIN_BINDING_SPLATMAP_0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{TERRAIN_BINDING_SPLATMAP_1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{TERRAIN_BINDING_ALBEDO_ARRAY, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, TERRAIN_MAX_LAYERS,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{TERRAIN_BINDING_NORMAL_ARRAY, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, TERRAIN_MAX_LAYERS,
		 VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{TERRAIN_BINDING_ROUGHNESS_ARRAY, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, TERRAIN_MAX_LAYERS,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 6: terrainParams UBO — VS, TES (heightfieldParams), FS
		{TERRAIN_BINDING_PARAMS_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT |
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 7: TessellationParams UBO — TCS + TES（TES 需要 tessDistance 做距离衰减）
		{TERRAIN_BINDING_TESSELLATION_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
		 VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, nullptr},

		// ── binding 8: NoiseDetailParams UBO — TES + FS ──
		{TERRAIN_BINDING_NOISE_DETAIL_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
		 VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_GISHUpdate(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{GISH_BINDING_DIRECT_LIGHT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{GISH_BINDING_RESULT_R,     VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{GISH_BINDING_RESULT_G,     VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{GISH_BINDING_RESULT_B,     VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_GIPointLight(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{GIPL_BINDING_HIT_POSITION,    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{GIPL_BINDING_HIT_NORMAL,      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{GIPL_BINDING_DIRECT_LIGHT,    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{GIPL_BINDING_ENVIRONMENT_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{GIPL_BINDING_SH_R,            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{GIPL_BINDING_SH_G,            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{GIPL_BINDING_SH_B,            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{GIPL_BINDING_SHADOW_MAP,      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{GIPL_BINDING_PUNCTUAL_SHADOW, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{GIPL_BINDING_PBR_DATA,        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_RayTracing(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{RT_BINDING_TLAS,                  VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr},
		{RT_BINDING_RESULT_IMAGE,          VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              1, VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr},
		{RT_BINDING_HIT_POSITION,          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             1, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr},
		{RT_BINDING_VERTEX_BUFFERS,        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,        500,    VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr},
		{RT_BINDING_INDEX_BUFFERS,         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,        500,    VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr},
		{RT_BINDING_INSTANCE_DATA,         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr},
		{RT_BINDING_HIT_NORMAL,            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             1, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr},
		{RT_BINDING_INSTANCE_TEX_INDEX,    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr},
		{RT_BINDING_HIT_ALBEDO_ROUGHNESS,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             1, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// ============================================================
// Set 4: Per-Node Skin Texture Layout (2 bindings: albedo + normal)
// Each skin render node owns its own descriptor set with dedicated textures.
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_SkinTexture(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		// binding 0: Skin albedo texture
		{SKIN_TEXTURE_BINDING_ALBEDO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 1: Skin normal texture
		{SKIN_TEXTURE_BINDING_NORMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// ============================================================
// Set 4: Per-Node Cloth Texture Layout (4 bindings: albedo + normal + roughness + ao)
// Each cloth render node owns its own descriptor set with dedicated textures.
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_ClothTexture(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		// binding 0: Cloth albedo texture
		{CLOTH_TEXTURE_BINDING_ALBEDO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 1: Cloth normal texture
		{CLOTH_TEXTURE_BINDING_NORMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 2: Cloth roughness texture
		{CLOTH_TEXTURE_BINDING_ROUGHNESS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 3: Cloth ambient occlusion texture
		{CLOTH_TEXTURE_BINDING_AO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// ============================================================
// Set 4: Per-Node Hair Texture Layout (7 bindings: albedo+alpha, normal, roughness, ao, shift, alpha, flow)
// Each hair render node owns its own descriptor set with dedicated textures.
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_HairTexture(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		// binding 0: Hair albedo + alpha texture
		{HAIR_TEXTURE_BINDING_ALBEDO_ALPHA, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 1: Hair normal texture
		{HAIR_TEXTURE_BINDING_NORMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 2: Hair roughness texture
		{HAIR_TEXTURE_BINDING_ROUGHNESS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 3: Hair ambient occlusion texture
		{HAIR_TEXTURE_BINDING_AO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 4: Hair strand shift texture
		{HAIR_TEXTURE_BINDING_SHIFT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 5: Hair dedicated alpha mask texture
		{HAIR_TEXTURE_BINDING_ALPHA, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 6: Hair flow map texture (RG = tangent-space flow direction)
		{HAIR_TEXTURE_BINDING_FLOW, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// ============================================================
// Set 4: Per-Node Subsurface Texture Layout (3 bindings: albedo + normal + thickness)
// Each subsurface render node owns its own descriptor set with dedicated textures.
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_SubsurfaceTexture(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		// binding 0: Subsurface albedo texture
		{SUBSURFACE_TEXTURE_BINDING_ALBEDO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 1: Subsurface normal texture
		{SUBSURFACE_TEXTURE_BINDING_NORMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 2: Subsurface thickness texture
		{SUBSURFACE_TEXTURE_BINDING_THICKNESS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 3: Subsurface roughness texture
		{SUBSURFACE_TEXTURE_BINDING_ROUGHNESS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// ============================================================
// Set 4: Per-Node Grass Texture Layout (5 bindings: albedo + normal + roughness + translucency + ao)
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_GrassTexture(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{GRASS_TEXTURE_BINDING_ALBEDO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{GRASS_TEXTURE_BINDING_NORMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{GRASS_TEXTURE_BINDING_ROUGHNESS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{GRASS_TEXTURE_BINDING_TRANSLUCENCY, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{GRASS_TEXTURE_BINDING_AO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// ============================================================
// Vegetation Compute — Bone Simulation Descriptor Layout
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_VegetationBoneSim(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{VEG_SIM_BINDING_INSTANCE_DATA, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{VEG_SIM_BINDING_BONE_DATA, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{VEG_SIM_BINDING_BONE_MATRICES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{VEG_SIM_BINDING_TERRAIN_HEIGHTMAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{VEG_SIM_BINDING_LOD_FACTORS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		// P6a: 共享散布偏移改为 UBO
		{VEG_SIM_BINDING_SCATTER_OFFSETS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
		 VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// ============================================================
// Vegetation Draw — Per-Config VS-Skinning SSBO Data (Set 3) Descriptor Layout
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_VegetationDraw(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{VEG_DRAW_BINDING_BONE_MATRICES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT, nullptr},
		{VEG_DRAW_BINDING_BONE_WEIGHTS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT, nullptr},
		{VEG_DRAW_BINDING_INSTANCE_REMAP, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT, nullptr},
		// P6a: 共享散布偏移改为 UBO
		{VEG_DRAW_BINDING_SCATTER_OFFSETS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT, nullptr},
		{VEG_DRAW_BINDING_LOD_FACTORS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT, nullptr},
		{VEG_DRAW_BINDING_INSTANCE_DATA, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT, nullptr},
		// P6a: terrain heightmap for VS sub-blade Y sampling
		{VEG_DRAW_BINDING_TERRAIN_HEIGHTMAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT, nullptr},
		// P0: per-instance visibility flags from GPU cull
		{VEG_DRAW_BINDING_VISIBILITY_FLAGS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// ============================================================
// Vegetation Compute — GPU Cull Pass Descriptor Layout (P0)
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_VegetationCull(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{VEG_CULL_BINDING_INSTANCE_DATA, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{VEG_CULL_BINDING_VISIBILITY, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{VEG_CULL_BINDING_VISIBLE_COUNT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{VEG_CULL_BINDING_VISIBLE_INDICES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{VEG_CULL_BINDING_TERRAIN_HEIGHTMAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{VEG_CULL_BINDING_HIZ, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_TileLightBuild(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		// binding 0: TileLightHeader SSBO (write)
		{TILE_BUILD_BINDING_GRID,    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		// binding 1: TileLight Index SSBO (write)
		{TILE_BUILD_BINDING_INDICES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		// binding 2: TileLightBuildParams UBO (per-dispatch tile grid dimensions)
		{TILE_BUILD_BINDING_PARAMS,  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_DecalPass(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	// 贴花 Pass Set 1：GBuffer2（COMBINED_IMAGE_SAMPLER），供 fragment shader 重建世界坐标
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{DECAL_PASS_BINDING_GBUFFER2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// ============================================================
// Water GBuffer Pass Set 1
// water_prepass.vert/.frag：CDLOD LOD 参数 UBO（lodRanges / meshDim / oceanBaseScale）
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterGBuffer(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		// binding 0: WaterGBufferParams UBO（vert + frag 均可读）
		{WATER_GBUF_BINDING_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 1: Wave displacement Texture2DArray（W-01）
		{WATER_GBUF_BINDING_DISPLACEMENT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT, nullptr},
		// binding 2: GerstnerWaveGPU SSBO（W-04）
		{WATER_GBUF_BINDING_WAVE_SSBO, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT, nullptr},
		// binding 3: Water normal map（W-08）
		{WATER_GBUF_BINDING_NORMAL_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// ============================================================
// Water Wave Compute Set 0
// water_wave_spectrum.comp：读取 SSBO 波分量 + 参数 UBO，输出 displacement storage image array
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterWaveCompute(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{WATER_WAVE_BINDING_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
		 VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_WAVE_BINDING_DISPLACEMENT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
		 VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		// binding 2: GerstnerWaveGPU SSBO 输入（W-04）
		{WATER_WAVE_BINDING_WAVE_SSBO, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// ============================================================
// Water Composite Pass Set 1
// water_composite.frag：WaterGBuf_Normal + WaterGBuf_LinearDepth + Params UBO
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterComposite(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		// binding 0: WaterGBuf_Normal（COMBINED_IMAGE_SAMPLER，RG16F oct 法线）
		{WATER_COMP_BINDING_GBUF_NORMAL,   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 1: WaterGBuf_LinearDepth（COMBINED_IMAGE_SAMPLER，R32F 线性深度）
		{WATER_COMP_BINDING_GBUF_DEPTH,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 2: WaterCompositeParams UBO（水色 / Fresnel 指数 / waterLevel）
		{WATER_COMP_BINDING_PARAMS,        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 3: 主场景 GBuffer2（worldPos.xyz + linearDepth.w）用于场景遮挡测试
		{WATER_COMP_BINDING_SCENE_GBUF2,   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 4-6: Pre-Water Compute 输出的反射 / 折射 / 焦散结果
		{WATER_COMP_BINDING_REFLECTION,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{WATER_COMP_BINDING_REFRACTION,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{WATER_COMP_BINDING_CAUSTICS,      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 7: W-15 泡沫纹理
		{WATER_COMP_BINDING_FOAM_TEXTURE,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 8: W-16 厚度图
		{WATER_COMP_BINDING_THICKNESS,     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// binding 9: W-16 Phase 2 SSS 散射输出
		{WATER_COMP_BINDING_SSS_SCATTER,   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// ============================================================
// Water Effects Compute Set 0
// water_effects.comp：WaterGBuf + SceneColor → Reflection/Refraction/Caustics
// ============================================================
void VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterEffectsCompute(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{WATER_EFFECT_BINDING_GBUF_NORMAL,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_EFFECT_BINDING_GBUF_DEPTH,     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_EFFECT_BINDING_SCENE_GBUF2,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_EFFECT_BINDING_SCENE_COLOR,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_EFFECT_BINDING_PARAMS,         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_EFFECT_BINDING_REFLECTION_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_EFFECT_BINDING_REFRACTION_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_EFFECT_BINDING_CAUSTICS_OUT,   VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// ============================================================
// Phase 2 独立 CS Layouts
// ============================================================

// W-12: Water SSR Compute (HZB Ray March)
void VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterSSRCompute(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{WATER_SSR_BINDING_GBUF_NORMAL,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_SSR_BINDING_GBUF_DEPTH,   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_SSR_BINDING_SCENE_HZB,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_SSR_BINDING_SCENE_GBUF2,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_SSR_BINDING_SCENE_COLOR,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_SSR_BINDING_PARAMS,       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_SSR_BINDING_REFLECTION_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,        1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// W-13: Water Refraction Compute (Snell + depth validation)
void VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterRefractionCompute(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{WATER_REFRACTION_BINDING_GBUF_NORMAL,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_REFRACTION_BINDING_GBUF_DEPTH,   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_REFRACTION_BINDING_SCENE_GBUF2,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_REFRACTION_BINDING_SCENE_COLOR,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_REFRACTION_BINDING_PARAMS,       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_REFRACTION_BINDING_REFRACTION_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,        1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// W-14: Water Caustics Compute (sun-direction driven)
void VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterCausticsCompute(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{WATER_CAUSTICS_BINDING_GBUF_NORMAL,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_CAUSTICS_BINDING_GBUF_DEPTH,   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_CAUSTICS_BINDING_SCENE_GBUF2,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_CAUSTICS_BINDING_PARAMS,       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_CAUSTICS_BINDING_CAUSTICS_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// W-16: Water Thickness Compute
void VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterThicknessCompute(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{WATER_THICKNESS_BINDING_GBUF_DEPTH,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_THICKNESS_BINDING_SCENE_GBUF2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_THICKNESS_BINDING_PARAMS,      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_THICKNESS_BINDING_THICKNESS_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,        1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// W-16 Phase 2: Water SSS Scatter Compute (water_sss_scatter.comp)
void VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterSSSScatterCompute(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{WATER_SSS_SCATTER_BINDING_GBUF_NORMAL,   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_SSS_SCATTER_BINDING_GBUF_DEPTH,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_SSS_SCATTER_BINDING_THICKNESS_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_SSS_SCATTER_BINDING_SCENE_GBUF2,   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_SSS_SCATTER_BINDING_PARAMS,        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_SSS_SCATTER_BINDING_SCATTER_OUT,   VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

// N-01: Water Detail Normal Compute (water_detail_normal.comp)
void VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterDetailNormalCompute(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{WATER_DETAIL_BINDING_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
		 VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{WATER_DETAIL_BINDING_OUTPUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
		 VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

} // namespace VansGraphics
