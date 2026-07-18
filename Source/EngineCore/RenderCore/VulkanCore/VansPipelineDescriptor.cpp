#include "VansPipelineDescriptor.h"

#include <filesystem>
#include <sstream>
#include <utility>

namespace VansGraphics
{
	namespace
	{
		uint64_t HashFNV1a64(const std::string& text)
		{
			uint64_t hash = 14695981039346656037ull;
			for (unsigned char c : text)
			{
				hash ^= static_cast<uint64_t>(c);
				hash *= 1099511628211ull;
			}
			return hash == 0 ? 1 : hash;
		}

		uint64_t HandleToKey(VkRenderPass renderPass)
		{
			return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(renderPass));
		}

		uint64_t HandleToKey(VkDescriptorSetLayout descriptorSetLayout)
		{
			return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(descriptorSetLayout));
		}

		void AppendStencilState(std::ostringstream& stream, const char* name, const VkStencilOpState& state)
		{
			stream
				<< "|" << name << "Fail=" << static_cast<uint32_t>(state.failOp)
				<< "|" << name << "Pass=" << static_cast<uint32_t>(state.passOp)
				<< "|" << name << "DepthFail=" << static_cast<uint32_t>(state.depthFailOp)
				<< "|" << name << "Compare=" << static_cast<uint32_t>(state.compareOp)
				<< "|" << name << "CompareMask=" << state.compareMask
				<< "|" << name << "WriteMask=" << state.writeMask
				<< "|" << name << "Reference=" << state.reference;
		}
	}

	std::vector<VansShaderStageFileDesc> VansPipelineDescriptorBuilder::BuildStageFiles(
		const std::string& shaderPath,
		VansPipelineProgramKind kind,
		const std::map<VkShaderStageFlagBits, std::string>& explicitStageFiles)
	{
		(void)kind;

		std::vector<VansShaderStageFileDesc> stages;
		stages.reserve(explicitStageFiles.size());

		for (const auto& [stage, file] : explicitStageFiles)
		{
			if (file.empty())
			{
				continue;
			}

			std::filesystem::path stageFile(file);
			if (stageFile.is_relative())
			{
				stageFile = std::filesystem::path(shaderPath) / stageFile;
			}

			VansShaderStageFileDesc stageDesc{};
			stageDesc.stage = stage;
			stageDesc.file = stageFile.string();
			stageDesc.entryPoint = "main";
			stages.emplace_back(std::move(stageDesc));
		}

		return stages;
	}

	VansPipelineDescriptorKey VansPipelineDescriptorBuilder::BuildPipelineKey(
		const VansPipelineProgramDesc& programDesc,
		const VansPipelineRuntimeDesc& runtimeDesc)
	{
		std::ostringstream stream;
		stream
			<< "name=" << programDesc.name
			<< "|kind=" << ToString(programDesc.kind)
			<< "|shaderPath=" << programDesc.shaderPath
			<< "|push=" << programDesc.pushConstantSize
			<< "|runtimePush=" << runtimeDesc.pushConstantSize
			<< "|renderPass=" << HandleToKey(runtimeDesc.renderPass)
			<< "|subpass=" << runtimeDesc.subpass
			<< "|setLayouts=" << runtimeDesc.descriptorSetLayoutCount
			<< "|vertexBindings=" << runtimeDesc.vertexBindingCount
			<< "|vertexAttributes=" << runtimeDesc.vertexAttributeCount
			<< "|samples=" << static_cast<uint32_t>(runtimeDesc.rasterizationSamples)
			<< "|sampleShading=" << runtimeDesc.sampleShadingEnable;

		for (uint64_t layoutKey : runtimeDesc.descriptorSetLayoutKeys)
		{
			stream << "|setLayoutKey=" << layoutKey;
		}

		stream
			<< "|depthTest=" << programDesc.graphicsState.depthTest
			<< "|depthWrite=" << programDesc.graphicsState.depthWrite
			<< "|depthCompare=" << static_cast<uint32_t>(programDesc.graphicsState.depthCompareOp)
			<< "|depthBoundsTest=" << programDesc.graphicsState.depthBoundsTest
			<< "|stencilTest=" << programDesc.graphicsState.stencilTest
			<< "|minDepthBounds=" << programDesc.graphicsState.minDepthBounds
			<< "|maxDepthBounds=" << programDesc.graphicsState.maxDepthBounds
			<< "|cull=" << programDesc.graphicsState.cullMode
			<< "|polygonMode=" << static_cast<uint32_t>(programDesc.graphicsState.polygonMode)
			<< "|frontFace=" << static_cast<uint32_t>(programDesc.graphicsState.frontFace)
			<< "|topology=" << static_cast<uint32_t>(programDesc.graphicsState.primitiveTopology)
			<< "|primitiveRestart=" << programDesc.graphicsState.primitiveRestart
			<< "|patchPoints=" << programDesc.graphicsState.patchControlPoints
			<< "|depthClamp=" << programDesc.graphicsState.depthClamp
			<< "|rasterizerDiscard=" << programDesc.graphicsState.rasterizerDiscard
			<< "|depthBias=" << programDesc.graphicsState.depthBias
			<< "|depthBiasConstant=" << programDesc.graphicsState.depthBiasConstantFactor
			<< "|depthBiasClamp=" << programDesc.graphicsState.depthBiasClamp
			<< "|depthBiasSlope=" << programDesc.graphicsState.depthBiasSlopeFactor
			<< "|lineWidth=" << programDesc.graphicsState.lineWidth
			<< "|alphaToCoverage=" << programDesc.graphicsState.alphaToCoverage
			<< "|alphaToOne=" << programDesc.graphicsState.alphaToOne
			<< "|colorAttachments=" << programDesc.graphicsState.colorAttachmentCount
			<< "|alphaBlend=" << programDesc.graphicsState.enableAlphaBlend
			<< "|decalBlend=" << programDesc.graphicsState.enableDecalBlend
			<< "|additiveBlend=" << programDesc.graphicsState.enableAdditiveBlend
			<< "|additiveMask=" << programDesc.graphicsState.additiveBlendAttachmentMask
			<< "|premultipliedAlphaBlend=" << programDesc.graphicsState.enablePremultipliedAlphaBlend;

		AppendStencilState(stream, "stencilFront", programDesc.graphicsState.stencilFront);
		AppendStencilState(stream, "stencilBack", programDesc.graphicsState.stencilBack);

		for (const auto& stage : programDesc.stages)
		{
			stream
				<< "|stage=" << static_cast<uint32_t>(stage.stage)
				<< ":" << stage.file
				<< ":" << stage.entryPoint;
		}

		for (const auto& pass : programDesc.materialPasses)
		{
			stream << "|materialPass=" << pass;
		}

		VansPipelineDescriptorKey key{};
		key.text = stream.str();
		key.hash = HashFNV1a64(key.text);
		return key;
	}

	VansPipelineRuntimeDesc VansPipelineDescriptorBuilder::BuildRuntimeDesc(
		const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
		uint32_t pushConstantSize)
	{
		VansPipelineRuntimeDesc runtimeDesc{};
		runtimeDesc.descriptorSetLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
		runtimeDesc.descriptorSetLayoutKeys = BuildDescriptorSetLayoutKeys(descriptorSetLayouts);
		runtimeDesc.pushConstantSize = pushConstantSize;
		return runtimeDesc;
	}

	VansPipelineRuntimeDesc VansPipelineDescriptorBuilder::BuildRuntimeDesc(
		VkRenderPass renderPass,
		uint32_t subpass,
		const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
		uint32_t pushConstantSize,
		uint32_t vertexBindingCount,
		uint32_t vertexAttributeCount,
		VkSampleCountFlagBits rasterizationSamples,
		VkBool32 sampleShadingEnable)
	{
		VansPipelineRuntimeDesc runtimeDesc = BuildRuntimeDesc(descriptorSetLayouts, pushConstantSize);
		runtimeDesc.renderPass = renderPass;
		runtimeDesc.subpass = subpass;
		runtimeDesc.vertexBindingCount = vertexBindingCount;
		runtimeDesc.vertexAttributeCount = vertexAttributeCount;
		runtimeDesc.rasterizationSamples = rasterizationSamples;
		runtimeDesc.sampleShadingEnable = sampleShadingEnable;
		return runtimeDesc;
	}

	std::vector<uint64_t> VansPipelineDescriptorBuilder::BuildDescriptorSetLayoutKeys(
		const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts)
	{
		std::vector<uint64_t> layoutKeys;
		layoutKeys.reserve(descriptorSetLayouts.size());
		for (VkDescriptorSetLayout descriptorSetLayout : descriptorSetLayouts)
		{
			layoutKeys.emplace_back(HandleToKey(descriptorSetLayout));
		}
		return layoutKeys;
	}

	const char* VansPipelineDescriptorBuilder::ToString(VansPipelineProgramKind kind)
	{
		switch (kind)
		{
		case VansPipelineProgramKind::Graphics:
			return "Graphics";
		case VansPipelineProgramKind::Compute:
			return "Compute";
		case VansPipelineProgramKind::RayTracing:
			return "RayTracing";
		}

		return "Unknown";
	}
}
