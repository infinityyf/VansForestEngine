#include "VansPipelineDescriptor.h"

#include <filesystem>
#include <sstream>
#include <utility>

namespace VansGraphics
{
	namespace
	{
		constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
		constexpr uint64_t kFnvPrime = 1099511628211ull;

		uint64_t HashFNV1a64(const std::string& text)
		{
			uint64_t hash = kFnvOffsetBasis;
			for (unsigned char c : text)
			{
				hash ^= static_cast<uint64_t>(c);
				hash *= kFnvPrime;
			}
			return hash == 0 ? 1 : hash;
		}

		template<typename T>
		void HashValue(uint64_t& hash, const T& value)
		{
			const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
			for (size_t i = 0; i < sizeof(T); ++i)
			{
				hash ^= static_cast<uint64_t>(bytes[i]);
				hash *= kFnvPrime;
			}
		}

		void HashGraphicsState(uint64_t& hash, const VansGraphicsPipelineStateDesc& state)
		{
			HashValue(hash, state.depthTest);
			HashValue(hash, state.depthWrite);
			HashValue(hash, state.depthCompareOp);
			HashValue(hash, state.depthBoundsTest);
			HashValue(hash, state.stencilTest);
			HashValue(hash, state.stencilFront.failOp);
			HashValue(hash, state.stencilFront.passOp);
			HashValue(hash, state.stencilFront.depthFailOp);
			HashValue(hash, state.stencilFront.compareOp);
			HashValue(hash, state.stencilFront.compareMask);
			HashValue(hash, state.stencilFront.writeMask);
			HashValue(hash, state.stencilFront.reference);
			HashValue(hash, state.stencilBack.failOp);
			HashValue(hash, state.stencilBack.passOp);
			HashValue(hash, state.stencilBack.depthFailOp);
			HashValue(hash, state.stencilBack.compareOp);
			HashValue(hash, state.stencilBack.compareMask);
			HashValue(hash, state.stencilBack.writeMask);
			HashValue(hash, state.stencilBack.reference);
			HashValue(hash, state.minDepthBounds);
			HashValue(hash, state.maxDepthBounds);
			HashValue(hash, state.cullMode);
			HashValue(hash, state.polygonMode);
			HashValue(hash, state.frontFace);
			HashValue(hash, state.primitiveTopology);
			HashValue(hash, state.primitiveRestart);
			HashValue(hash, state.patchControlPoints);
			HashValue(hash, state.depthClamp);
			HashValue(hash, state.rasterizerDiscard);
			HashValue(hash, state.depthBias);
			HashValue(hash, state.depthBiasConstantFactor);
			HashValue(hash, state.depthBiasClamp);
			HashValue(hash, state.depthBiasSlopeFactor);
			HashValue(hash, state.lineWidth);
			HashValue(hash, state.alphaToCoverage);
			HashValue(hash, state.alphaToOne);
			HashValue(hash, state.colorAttachmentCount);
			HashValue(hash, state.enableAlphaBlend);
			HashValue(hash, state.enableDecalBlend);
			HashValue(hash, state.enableAdditiveBlend);
			HashValue(hash, state.additiveBlendAttachmentMask);
			HashValue(hash, state.enablePremultipliedAlphaBlend);
		}

		bool StencilStateEquals(const VkStencilOpState& lhs, const VkStencilOpState& rhs)
		{
			return lhs.failOp == rhs.failOp && lhs.passOp == rhs.passOp &&
				lhs.depthFailOp == rhs.depthFailOp && lhs.compareOp == rhs.compareOp &&
				lhs.compareMask == rhs.compareMask && lhs.writeMask == rhs.writeMask &&
				lhs.reference == rhs.reference;
		}

		bool GraphicsStateEquals(const VansGraphicsPipelineStateDesc& lhs, const VansGraphicsPipelineStateDesc& rhs)
		{
			return lhs.depthTest == rhs.depthTest && lhs.depthWrite == rhs.depthWrite &&
				lhs.depthCompareOp == rhs.depthCompareOp && lhs.depthBoundsTest == rhs.depthBoundsTest &&
				lhs.stencilTest == rhs.stencilTest &&
				StencilStateEquals(lhs.stencilFront, rhs.stencilFront) &&
				StencilStateEquals(lhs.stencilBack, rhs.stencilBack) &&
				lhs.minDepthBounds == rhs.minDepthBounds && lhs.maxDepthBounds == rhs.maxDepthBounds &&
				lhs.cullMode == rhs.cullMode && lhs.polygonMode == rhs.polygonMode &&
				lhs.frontFace == rhs.frontFace && lhs.primitiveTopology == rhs.primitiveTopology &&
				lhs.primitiveRestart == rhs.primitiveRestart && lhs.patchControlPoints == rhs.patchControlPoints &&
				lhs.depthClamp == rhs.depthClamp && lhs.rasterizerDiscard == rhs.rasterizerDiscard &&
				lhs.depthBias == rhs.depthBias && lhs.depthBiasConstantFactor == rhs.depthBiasConstantFactor &&
				lhs.depthBiasClamp == rhs.depthBiasClamp && lhs.depthBiasSlopeFactor == rhs.depthBiasSlopeFactor &&
				lhs.lineWidth == rhs.lineWidth && lhs.alphaToCoverage == rhs.alphaToCoverage &&
				lhs.alphaToOne == rhs.alphaToOne && lhs.colorAttachmentCount == rhs.colorAttachmentCount &&
				lhs.enableAlphaBlend == rhs.enableAlphaBlend && lhs.enableDecalBlend == rhs.enableDecalBlend &&
				lhs.enableAdditiveBlend == rhs.enableAdditiveBlend &&
				lhs.additiveBlendAttachmentMask == rhs.additiveBlendAttachmentMask &&
				lhs.enablePremultipliedAlphaBlend == rhs.enablePremultipliedAlphaBlend;
		}

		bool BindingEquals(const VkVertexInputBindingDescription& lhs, const VkVertexInputBindingDescription& rhs)
		{
			return lhs.binding == rhs.binding && lhs.stride == rhs.stride && lhs.inputRate == rhs.inputRate;
		}

		bool AttributeEquals(const VkVertexInputAttributeDescription& lhs, const VkVertexInputAttributeDescription& rhs)
		{
			return lhs.location == rhs.location && lhs.binding == rhs.binding &&
				lhs.format == rhs.format && lhs.offset == rhs.offset;
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
			<< "|shaderBinaryHash=" << programDesc.shaderBinaryHash
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
		for (const auto& binding : runtimeDesc.vertexBindings)
		{
			stream << "|vertexBinding=" << binding.binding
				<< ":" << binding.stride
				<< ":" << static_cast<uint32_t>(binding.inputRate);
		}
		for (const auto& attribute : runtimeDesc.vertexAttributes)
		{
			stream << "|vertexAttribute=" << attribute.location
				<< ":" << attribute.binding
				<< ":" << static_cast<uint32_t>(attribute.format)
				<< ":" << attribute.offset;
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

	uint64_t VansPipelineDescriptorBuilder::BuildVariantHash(
		const VansPipelineProgramDesc& programDesc,
		VkRenderPass renderPass,
		uint32_t subpass,
		const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
		uint32_t pushConstantSize,
		const std::vector<VkVertexInputBindingDescription>* vertexBindings,
		const std::vector<VkVertexInputAttributeDescription>* vertexAttributes,
		VkSampleCountFlagBits rasterizationSamples,
		VkBool32 sampleShadingEnable)
	{
		uint64_t hash = kFnvOffsetBasis;
		HashValue(hash, programDesc.kind);
		HashValue(hash, programDesc.shaderBinaryHash);
		HashValue(hash, programDesc.pushConstantSize);
		if (programDesc.kind == VansPipelineProgramKind::Graphics)
			HashGraphicsState(hash, programDesc.graphicsState);

		const uint64_t renderPassKey = HandleToKey(renderPass);
		HashValue(hash, renderPassKey);
		HashValue(hash, subpass);
		HashValue(hash, pushConstantSize);
		HashValue(hash, rasterizationSamples);
		HashValue(hash, sampleShadingEnable);

		const uint32_t layoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
		HashValue(hash, layoutCount);
		for (VkDescriptorSetLayout layout : descriptorSetLayouts)
		{
			const uint64_t layoutKey = HandleToKey(layout);
			HashValue(hash, layoutKey);
		}

		const uint32_t bindingCount = vertexBindings ? static_cast<uint32_t>(vertexBindings->size()) : 0;
		HashValue(hash, bindingCount);
		if (vertexBindings)
		{
			for (const auto& binding : *vertexBindings)
			{
				HashValue(hash, binding.binding);
				HashValue(hash, binding.stride);
				HashValue(hash, binding.inputRate);
			}
		}

		const uint32_t attributeCount = vertexAttributes ? static_cast<uint32_t>(vertexAttributes->size()) : 0;
		HashValue(hash, attributeCount);
		if (vertexAttributes)
		{
			for (const auto& attribute : *vertexAttributes)
			{
				HashValue(hash, attribute.location);
				HashValue(hash, attribute.binding);
				HashValue(hash, attribute.format);
				HashValue(hash, attribute.offset);
			}
		}

		return hash == 0 ? 1 : hash;
	}

	bool VansPipelineDescriptorBuilder::MatchesVariant(
		const VansPipelineVariantIdentity& identity,
		const VansPipelineProgramDesc& programDesc,
		VkRenderPass renderPass,
		uint32_t subpass,
		const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
		uint32_t pushConstantSize,
		const std::vector<VkVertexInputBindingDescription>* vertexBindings,
		const std::vector<VkVertexInputAttributeDescription>* vertexAttributes,
		VkSampleCountFlagBits rasterizationSamples,
		VkBool32 sampleShadingEnable)
	{
		if (identity.shaderBinaryHash != programDesc.shaderBinaryHash ||
			identity.pushConstantSize != programDesc.pushConstantSize ||
			(programDesc.kind == VansPipelineProgramKind::Graphics &&
				!GraphicsStateEquals(identity.graphicsState, programDesc.graphicsState)))
		{
			return false;
		}

		const VansPipelineRuntimeDesc& runtime = identity.runtimeDesc;
		if (runtime.renderPass != renderPass || runtime.subpass != subpass ||
			runtime.pushConstantSize != pushConstantSize ||
			runtime.rasterizationSamples != rasterizationSamples ||
			runtime.sampleShadingEnable != sampleShadingEnable ||
			runtime.descriptorSetLayoutKeys.size() != descriptorSetLayouts.size())
		{
			return false;
		}
		for (size_t i = 0; i < descriptorSetLayouts.size(); ++i)
		{
			if (runtime.descriptorSetLayoutKeys[i] != HandleToKey(descriptorSetLayouts[i]))
				return false;
		}

		const size_t bindingCount = vertexBindings ? vertexBindings->size() : 0;
		if (runtime.vertexBindings.size() != bindingCount)
			return false;
		for (size_t i = 0; i < bindingCount; ++i)
		{
			if (!BindingEquals(runtime.vertexBindings[i], (*vertexBindings)[i]))
				return false;
		}

		const size_t attributeCount = vertexAttributes ? vertexAttributes->size() : 0;
		if (runtime.vertexAttributes.size() != attributeCount)
			return false;
		for (size_t i = 0; i < attributeCount; ++i)
		{
			if (!AttributeEquals(runtime.vertexAttributes[i], (*vertexAttributes)[i]))
				return false;
		}

		return true;
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
