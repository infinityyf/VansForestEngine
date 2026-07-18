#pragma once

#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include "vulkan/vulkan.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace VansGraphics
{
	enum class VansPipelineProgramKind
	{
		Graphics,
		Compute,
		RayTracing
	};

	struct VansShaderStageFileDesc
	{
		VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
		std::string file;
		std::string entryPoint = "main";
	};

	struct VansGraphicsPipelineStateDesc
	{
		VkBool32 depthTest = VK_TRUE;
		VkBool32 depthWrite = VK_TRUE;
		VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
		VkBool32 depthBoundsTest = VK_FALSE;
		VkBool32 stencilTest = VK_FALSE;
		VkStencilOpState stencilFront = {};
		VkStencilOpState stencilBack = {};
		float minDepthBounds = 0.0f;
		float maxDepthBounds = 1.0f;
		VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
		VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
		VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		VkPrimitiveTopology primitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		VkBool32 primitiveRestart = VK_FALSE;
		uint32_t patchControlPoints = 1;
		VkBool32 depthClamp = VK_FALSE;
		VkBool32 rasterizerDiscard = VK_FALSE;
		VkBool32 depthBias = VK_FALSE;
		float depthBiasConstantFactor = 0.0f;
		float depthBiasClamp = 0.0f;
		float depthBiasSlopeFactor = 0.0f;
		float lineWidth = 1.0f;
		VkBool32 alphaToCoverage = VK_FALSE;
		VkBool32 alphaToOne = VK_FALSE;
		int colorAttachmentCount = -1;
		bool enableAlphaBlend = false;
		bool enableDecalBlend = false;
		bool enableAdditiveBlend = false;
		uint32_t additiveBlendAttachmentMask = 0;
		bool enablePremultipliedAlphaBlend = false;
	};

	struct VansPipelineProgramDesc
	{
		std::string name;
		std::string shaderPath;
		VansPipelineProgramKind kind = VansPipelineProgramKind::Graphics;
		std::vector<VansShaderStageFileDesc> stages;
		VansGraphicsPipelineStateDesc graphicsState;
		int pushConstantSize = 0;
		std::vector<std::string> materialPasses;

		void Clear()
		{
			name.clear();
			shaderPath.clear();
			kind = VansPipelineProgramKind::Graphics;
			stages.clear();
			graphicsState = {};
			pushConstantSize = 0;
			materialPasses.clear();
		}
	};

	struct VansPipelineRuntimeDesc
	{
		VkRenderPass renderPass = VK_NULL_HANDLE;
		uint32_t subpass = 0;
		uint32_t descriptorSetLayoutCount = 0;
		std::vector<uint64_t> descriptorSetLayoutKeys;
		uint32_t pushConstantSize = 0;
		uint32_t vertexBindingCount = 0;
		uint32_t vertexAttributeCount = 0;
		VkSampleCountFlagBits rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		VkBool32 sampleShadingEnable = VK_FALSE;
	};

	struct VansPipelineDescriptorKey
	{
		std::string text;
		uint64_t hash = 0;

		bool IsValid() const { return hash != 0 && !text.empty(); }
	};

	class VansPipelineDescriptorBuilder
	{
	public:
		static std::vector<VansShaderStageFileDesc> BuildStageFiles(
			const std::string& shaderPath,
			VansPipelineProgramKind kind,
			const std::map<VkShaderStageFlagBits, std::string>& explicitStageFiles);

		static VansPipelineDescriptorKey BuildPipelineKey(
			const VansPipelineProgramDesc& programDesc,
			const VansPipelineRuntimeDesc& runtimeDesc);

		static VansPipelineRuntimeDesc BuildRuntimeDesc(
			const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
			uint32_t pushConstantSize = 0);

		static VansPipelineRuntimeDesc BuildRuntimeDesc(
			VkRenderPass renderPass,
			uint32_t subpass,
			const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
			uint32_t pushConstantSize,
			uint32_t vertexBindingCount,
			uint32_t vertexAttributeCount,
			VkSampleCountFlagBits rasterizationSamples,
			VkBool32 sampleShadingEnable);

		static std::vector<uint64_t> BuildDescriptorSetLayoutKeys(
			const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts);

		static const char* ToString(VansPipelineProgramKind kind);
	};
}
