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
		// binding 1: Lights UBO
		{GLOBAL_BINDING_LIGHTS_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
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
		// binding 50: Bindless PBR textures (fixed max count, no variable descriptor)
		{GLOBAL_BINDING_BINDLESS_TEXTURES, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		 maxBindlessTextures, BINDLESS_TEX_STAGES, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
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
		{OBJECT_BINDING_TRANSFORM_SSBO, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT, nullptr},
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
		{POSTPROCESS_BINDING_COLOR_INPUT, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	};
	CreateLayoutAndAllocateSets(bindings, outLayout, outSets, setCount);
}

void VansDescriptorSetLayoutFactory::CreateAndAllocate_DeferredLighting(
	VkDescriptorSetLayout& outLayout, std::vector<VkDescriptorSet>& outSets, uint32_t setCount)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		// GBuffer subpass inputs (bindings 0-4)
		{0,  VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{1,  VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{2,  VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{3,  VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{4,  VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
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
		{SSR_TAA_BINDING_COLOR,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_TAA_BINDING_POSITION, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_TAA_BINDING_RESULT_A, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_TAA_BINDING_RESULT_B, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{SSR_TAA_BINDING_RESULT,   VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
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
		{FOG_INJECT_BINDING_VOXEL_GRID, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{FOG_INJECT_BINDING_SHADOW_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{FOG_INJECT_BINDING_PARAMS,     VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		{FOG_INJECT_BINDING_HISTORY,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
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
		{TERRAIN_BINDING_HEIGHT_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{TERRAIN_BINDING_SPLATMAP_0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{TERRAIN_BINDING_SPLATMAP_1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{TERRAIN_BINDING_ALBEDO_ARRAY, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, TERRAIN_MAX_LAYERS,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{TERRAIN_BINDING_NORMAL_ARRAY, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, TERRAIN_MAX_LAYERS,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{TERRAIN_BINDING_ROUGHNESS_ARRAY, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, TERRAIN_MAX_LAYERS,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{TERRAIN_BINDING_PARAMS_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
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

} // namespace VansGraphics
