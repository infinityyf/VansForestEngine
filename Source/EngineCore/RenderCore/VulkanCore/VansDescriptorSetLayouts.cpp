#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansDescriptorSetLayouts.h"
#include <cassert>

namespace VansGraphics
{

// ============================================================
// Helper to create a layout from a vector of bindings
// ============================================================
static VkDescriptorSetLayout CreateLayoutFromBindings(
	VkDevice device,
	const std::vector<VkDescriptorSetLayoutBinding>& bindings,
	const VkDescriptorSetLayoutBindingFlagsCreateInfo* pFlagsInfo = nullptr,
	VkDescriptorSetLayoutCreateFlags flags = 0)
{
	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.pNext = pFlagsInfo;
	layoutInfo.flags = flags;
	layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	layoutInfo.pBindings = bindings.data();

	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	VkResult result = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout);
	assert(result == VK_SUCCESS);
	return layout;
}

// ============================================================
// Set 0: Global Layout (universal across all pipelines)
// ============================================================
VkDescriptorSetLayout VansDescriptorSetLayoutFactory::CreateGlobalLayout(
	VkDevice device, uint32_t maxBindlessTextures)
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
		// binding 50: Bindless PBR textures (fixed max count, no variable descriptor)
		{GLOBAL_BINDING_BINDLESS_TEXTURES, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		 maxBindlessTextures, BINDLESS_TEX_STAGES, nullptr},
	};

	// Use simple layout creation (no bindless flags needed since pool
	// does not support UPDATE_AFTER_BIND)
	return CreateLayoutFromBindings(device, bindings);
}

// ============================================================
// Set 2: Per-Object Layout
// ============================================================
VkDescriptorSetLayout VansDescriptorSetLayoutFactory::CreateObjectLayout(VkDevice device)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{OBJECT_BINDING_TRANSFORM_SSBO, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT, nullptr},
	};
	return CreateLayoutFromBindings(device, bindings);
}

// ============================================================
// Set 1: Per-Pass Layouts
// ============================================================

VkDescriptorSetLayout VansDescriptorSetLayoutFactory::CreatePassLayout_Empty(VkDevice device)
{
	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 0;
	layoutInfo.pBindings = nullptr;

	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout);
	return layout;
}

VkDescriptorSetLayout VansDescriptorSetLayoutFactory::CreatePassLayout_DeferredLighting(VkDevice device)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		// GBuffer subpass inputs (bindings 0-4)
		{0,  VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{1,  VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{2,  VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{3,  VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{4,  VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		// Screen-space effect results (bindings 5-13)
		{5,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // SSAO
		{6,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // SSGI
		{7,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // SSR
		{8,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // Shadow map
		{9,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // Punctual shadow
		{10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // SH R
		{11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // SH G
		{12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // SH B
		{13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // Fog
	};
	return CreateLayoutFromBindings(device, bindings);
}

VkDescriptorSetLayout VansDescriptorSetLayoutFactory::CreatePassLayout_SkyBox(VkDevice device)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // Atmosphere UBO
	};
	return CreateLayoutFromBindings(device, bindings);
}

VkDescriptorSetLayout VansDescriptorSetLayoutFactory::CreatePassLayout_ScreenSpace(VkDevice device)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // normal
		{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // gbuffer0
		{2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // gbuffer1
		{3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // gbuffer2
		{4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // depth
		{5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // SSAO output
	};
	return CreateLayoutFromBindings(device, bindings);
}

VkDescriptorSetLayout VansDescriptorSetLayoutFactory::CreatePassLayout_PostProcess(VkDevice device)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // Final color input
	};
	return CreateLayoutFromBindings(device, bindings);
}

VkDescriptorSetLayout VansDescriptorSetLayoutFactory::CreatePassLayout_Terrain(VkDevice device)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // Height map
		{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // Albedo map
	};
	return CreateLayoutFromBindings(device, bindings);
}

// ============================================================
// Compute Pass Layouts
// ============================================================

VkDescriptorSetLayout VansDescriptorSetLayoutFactory::CreatePassLayout_SSGI(VkDevice device)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{0,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // inputNormal
		{1,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // inputDepth
		{2,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // inputColor
		{3,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // inputPosition
		{4,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // inputSkydiffuse
		{5,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // resultR
		{6,  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // ssgiInfo
		{7,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // SHRCoeff
		{8,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // SHGCoeff
		{9,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // SHBCoeff
		{10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // hizDepth
	};
	return CreateLayoutFromBindings(device, bindings);
}

VkDescriptorSetLayout VansDescriptorSetLayoutFactory::CreatePassLayout_SSR_Trace(VkDevice device)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // inputNormal
		{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // inputRoughness
		{2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // inputPosition
		{3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // inputHIZ
		{4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // traceHit
		{5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // tracePDF
	};
	return CreateLayoutFromBindings(device, bindings);
}

VkDescriptorSetLayout VansDescriptorSetLayoutFactory::CreatePassLayout_SSR_Resolve(VkDevice device)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // inputColor
		{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // inputRoughness
		{2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // inputNormal
		{3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // inputPosition
		{4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // traceHit
		{5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // tracePDF
		{6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // resolveResult
	};
	return CreateLayoutFromBindings(device, bindings);
}

VkDescriptorSetLayout VansDescriptorSetLayoutFactory::CreatePassLayout_SSR_TemporalAA(VkDevice device)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // inputColor
		{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // inputPosition
		{2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // aaResultA
		{3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // aaResultB
		{4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // aaResult
	};
	return CreateLayoutFromBindings(device, bindings);
}

VkDescriptorSetLayout VansDescriptorSetLayoutFactory::CreatePassLayout_VolumetricFog(VkDevice device)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // inputPosition
		{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // mainLightShadowMap
		{2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // fogResult
	};
	return CreateLayoutFromBindings(device, bindings);
}

VkDescriptorSetLayout VansDescriptorSetLayoutFactory::CreatePassLayout_BilateralFilter(VkDevice device)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // inputColor
		{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // inputDepth
		{2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // result
	};
	return CreateLayoutFromBindings(device, bindings);
}

VkDescriptorSetLayout VansDescriptorSetLayoutFactory::CreatePassLayout_HIZ(VkDevice device)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // hizSource
		{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // hizResult
	};
	return CreateLayoutFromBindings(device, bindings);
}

VkDescriptorSetLayout VansDescriptorSetLayoutFactory::CreatePassLayout_GISHUpdate(VkDevice device)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // directLightResult
		{2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // resultRImage
		{3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // resultGImage
		{4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // resultBImage
	};
	return CreateLayoutFromBindings(device, bindings);
}

VkDescriptorSetLayout VansDescriptorSetLayoutFactory::CreatePassLayout_GIPointLight(VkDevice device)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{0,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // hitPositionBuffer
		{1,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // hitNormalBuffer
		{2,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // directLightResult
		{4,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // environmentMap
		{5,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // SHRCoeff
		{6,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // SHGCoeff
		{7,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // SHBCoeff
		{8,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // shadowMap
		{9,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // punctualShadowMap
		{10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // pbrDataBuffer
	};
	return CreateLayoutFromBindings(device, bindings);
}

VkDescriptorSetLayout VansDescriptorSetLayoutFactory::CreatePassLayout_RayTracing(VkDevice device)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		{0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr}, // TLAS
		{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              1, VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr}, // resultImage
		{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             1, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr}, // hitPositionBuffer
		{3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,        500,    VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr}, // vertexBuffers[]
		{4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,        500,    VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr}, // indexBuffers[]
		{5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr}, // instanceDataBuffer
		{6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             1, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr}, // hitNormalBuffer
		{7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr}, // instanceToTextureIndex
		{8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             1, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr}, // hitAlbedoRoughness
	};
	return CreateLayoutFromBindings(device, bindings);
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

} // namespace VansGraphics
