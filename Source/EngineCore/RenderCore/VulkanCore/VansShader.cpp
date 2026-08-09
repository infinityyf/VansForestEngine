#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansPipeline.h"
#include "VansShader.h"
#include "VansVKCommandBuffer.h"
#include "VansVKDescriptorManager.h"
#include "VansVKDevice.h"

#include "../../AssetCore/Importers/Shader/VansShaderArtifactCache.h"
#include "../../Util/VansLog.h"

#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <vector>

namespace
{
std::filesystem::path FindShaderRoot(const std::filesystem::path& sourceFolder)
{
	std::filesystem::path current = sourceFolder;
	while (!current.empty() && current != current.root_path())
	{
		std::string name = current.filename().string();
		std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c)
		{
			return static_cast<char>(std::tolower(c));
		});
		if (name == "shaders")
			return current;
		current = current.parent_path();
	}
	return sourceFolder;
}

std::vector<std::string> GetShaderFilesInFolder(const std::string& directory)
{
	std::vector<std::string> files;
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
	{
		if (ec)
			break;
		if (entry.is_regular_file(ec) && !ec)
			files.push_back(entry.path().filename().string());
		ec.clear();
	}
	if (ec)
		VANS_LOG_WARN("failed to enumerate shader folder: " << directory << " (" << ec.message() << ")");
	return files;
}

std::string GetFileExtensionWithoutDot(const std::filesystem::path& path)
{
	std::string extension = path.extension().string();
	if (!extension.empty() && extension.front() == '.')
		extension.erase(extension.begin());
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c)
	{
		return static_cast<char>(std::tolower(c));
	});
	return extension;
}

std::string BuildStableUnmanagedProgramId(const std::string& shaderFolder)
{
	const std::filesystem::path normalized = std::filesystem::path(shaderFolder).lexically_normal();
	std::filesystem::path relative;
	bool afterShaderRoot = false;
	for (const auto& component : normalized)
	{
		std::string name = component.string();
		std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c)
		{
			return static_cast<char>(std::tolower(c));
		});
		if (afterShaderRoot)
			relative /= component;
		else if (name == "shaders")
			afterShaderRoot = true;
	}
	return afterShaderRoot && !relative.empty()
		? std::string("Unmanaged/") + relative.generic_u8string()
		: std::string("Unmanaged/") + normalized.filename().generic_u8string();
}

bool CompileShaderModuleData(
	const std::string& shader_folder,
	VansGraphics::ShaderType shaderType,
	const std::map<VkShaderStageFlagBits, std::string>& explicitStageFiles,
	const std::string& programId,
	std::map<VkShaderStageFlagBits, VansGraphics::ShaderModuleData>& outModuleData,
	Vans::VansShaderArtifactPrepareResult& outPrepared)
{
	if (Vans::VansShaderArtifactCache::IsCookedOnlyMode())
	{
		// Packaged runtime resolves complete stage sets from the cooked program index.
		// Source paths are authoring metadata only and must not be inspected here.
		outModuleData.clear();
	}
	else if (explicitStageFiles.empty())
	{
		std::vector<std::string> shader_files = GetShaderFilesInFolder(shader_folder);
		if (shader_files.empty())
		{
			VANS_LOG_WARN("no shader files found:" << shader_folder);
		}

		outModuleData.clear();
		for (auto& shader_file : shader_files)
		{
			std::string shader_type = GetFileExtensionWithoutDot(shader_file);
			if (shader_type == "spv")
				continue;

			VkShaderStageFlagBits shader_stage = {};
			if (shaderType == VansGraphics::ShaderType::Normal)
			{
				auto shader_type_iter = VansGraphics::m_ShaderTypeMap.find(shader_type);
				if (shader_type_iter == VansGraphics::m_ShaderTypeMap.end())
				{
					VANS_LOG_WARN("unknown shader type");
					return false;
				}
				shader_stage = shader_type_iter->second;
			}
			else
			{
				auto shader_type_iter = VansGraphics::m_RayTracingShaderTypeMap.find(shader_type);
				if (shader_type_iter == VansGraphics::m_RayTracingShaderTypeMap.end())
				{
					VANS_LOG_WARN("unknown ray tracing shader type");
					return false;
				}
				shader_stage = shader_type_iter->second;
			}

			VansGraphics::ShaderModuleData shader_module_data;
			shader_module_data.m_ShaderType = shader_type;
			shader_module_data.m_ShaderTextResourceFileName = shader_folder + "\\" + shader_file;
			outModuleData[shader_stage] = shader_module_data;
		}
	}
	else
	{
		outModuleData.clear();
		for (const auto& [shader_stage, shader_file] : explicitStageFiles)
		{
			if (shader_file.empty())
				continue;

			VansGraphics::ShaderModuleData shader_module_data;
			shader_module_data.m_ShaderType = GetFileExtensionWithoutDot(shader_file);
			std::filesystem::path filePath(shader_file);
			if (filePath.is_relative())
				filePath = std::filesystem::path(shader_folder) / filePath;
			shader_module_data.m_ShaderTextResourceFileName = filePath.string();
			outModuleData[shader_stage] = shader_module_data;
		}
	}

	Vans::VansShaderCompileRequest request;
	request.programId = programId.empty() ? shader_folder : programId;
	request.sourceFolder = shader_folder;
	request.includeRoots.push_back(FindShaderRoot(request.sourceFolder));
	for (const auto& [stage, moduleData] : outModuleData)
	{
		(void)stage;
		Vans::VansShaderStageCompileSource source;
		source.stage = moduleData.m_ShaderType;
		source.sourcePath = moduleData.m_ShaderTextResourceFileName;
		request.stages.emplace_back(std::move(source));
	}

	outPrepared = Vans::VansShaderArtifactCache::Get().Prepare(request, true);
	for (const std::string& diagnostic : outPrepared.compileResult.diagnostics)
	{
		if (outPrepared.success)
			VANS_LOG_WARN("[ShaderArtifact] " << request.programId << ": " << diagnostic);
		else
			VANS_LOG_ERROR("[ShaderArtifact] " << request.programId << ": " << diagnostic);
	}
	if (!outPrepared.success)
		return false;

	for (const auto& compiled : outPrepared.compileResult.stages)
	{
		VkShaderStageFlagBits stage = static_cast<VkShaderStageFlagBits>(0);
		if (shaderType == VansGraphics::ShaderType::Normal)
		{
			const auto found = VansGraphics::m_ShaderTypeMap.find(compiled.stage);
			if (found != VansGraphics::m_ShaderTypeMap.end())
				stage = found->second;
		}
		else
		{
			const auto found = VansGraphics::m_RayTracingShaderTypeMap.find(compiled.stage);
			if (found != VansGraphics::m_RayTracingShaderTypeMap.end())
				stage = found->second;
		}
		if (stage == 0 || compiled.spirv.empty())
		{
			VANS_LOG_ERROR("[ShaderArtifact] Artifact stage set does not match program '" << request.programId << "'");
			return false;
		}
		auto module = outModuleData.find(stage);
		if (module == outModuleData.end())
		{
			if (!request.stages.empty())
			{
				VANS_LOG_ERROR("[ShaderArtifact] Artifact contains an unexpected stage for '" << request.programId << "'");
				return false;
			}
			VansGraphics::ShaderModuleData moduleData;
			moduleData.m_ShaderType = compiled.stage;
			module = outModuleData.emplace(stage, std::move(moduleData)).first;
		}
		module->second.m_ShaderSPIRVCode.resize(compiled.spirv.size() * sizeof(std::uint32_t));
		std::memcpy(module->second.m_ShaderSPIRVCode.data(), compiled.spirv.data(), module->second.m_ShaderSPIRVCode.size());
	}
	for (const auto& [stage, moduleData] : outModuleData)
	{
		(void)stage;
		if (moduleData.m_ShaderSPIRVCode.empty())
			return false;
	}
	return true;
}

void DestroyShaderModuleData(VkDevice device, std::map<VkShaderStageFlagBits, VansGraphics::ShaderModuleData>& moduleData)
{
	for (auto& shader_module_data : moduleData)
	{
		if (shader_module_data.second.m_ShaderModule != VK_NULL_HANDLE)
			VansGraphics::vkDestroyShaderModule(device, shader_module_data.second.m_ShaderModule, nullptr);
		shader_module_data.second.m_ShaderModule = VK_NULL_HANDLE;
	}
	moduleData.clear();
}

bool CreateShaderModulesFromData(VkDevice device, std::map<VkShaderStageFlagBits, VansGraphics::ShaderModuleData>& moduleData)
{
	for (auto& shader_module_data : moduleData)
	{
		VkShaderModuleCreateInfo shader_module_create_info =
		{
			VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			nullptr,
			0,
			shader_module_data.second.m_ShaderSPIRVCode.size(),
			reinterpret_cast<uint32_t const*>(shader_module_data.second.m_ShaderSPIRVCode.data())
		};
		VkResult result = VansGraphics::vkCreateShaderModule(device, &shader_module_create_info, nullptr, &shader_module_data.second.m_ShaderModule);
		if (VK_SUCCESS != result)
		{
			VANS_LOG_ERROR("Could not create a shader module.");
			DestroyShaderModuleData(device, moduleData);
			return false;
		}
	}
	return true;
}
}

bool VansGraphics::VansShader::InitShader(VkDevice& logic_device, const std::string& shader_folder)
{
	return InitShader(logic_device, shader_folder, {});
}

bool VansGraphics::VansShader::InitShader(VkDevice& logic_device, const std::string& shader_folder, const std::map<VkShaderStageFlagBits, std::string>& stageFiles)
{
	m_ShaderFolder = shader_folder;
	m_ShaderType = ShaderType::Normal;
	m_ExplicitStageFiles = stageFiles;
	m_PipelineProgramDesc.name = m_PipelineProgramDesc.name.empty()
		? BuildStableUnmanagedProgramId(shader_folder) : m_PipelineProgramDesc.name;
	m_PipelineProgramDesc.shaderPath = shader_folder;
	m_PipelineProgramDesc.kind = VansPipelineProgramKind::Graphics;
	m_PipelineProgramDesc.stages = VansPipelineDescriptorBuilder::BuildStageFiles(
		shader_folder,
		m_PipelineProgramDesc.kind,
		stageFiles);

	m_SupportMRTOutput = false;

	std::map<VkShaderStageFlagBits, ShaderModuleData> moduleData;
	Vans::VansShaderArtifactPrepareResult prepared;
	bool result = CompileShaderModuleData(
		m_ShaderFolder, m_ShaderType, m_ExplicitStageFiles,
		m_PipelineProgramDesc.name, moduleData, prepared);
	if (!result)
	{
		VANS_LOG_ERROR("shader translation failed");
		return false;
	}

	result = CreateShaderModulesFromData(logic_device, moduleData);
	if (!result)
	{
		VANS_LOG_ERROR("create shader module failed");
		return false;
	}

	DestroyShaderModuleData(logic_device, m_ShaderModuleDataMap);
	m_ShaderModuleDataMap = std::move(moduleData);
	m_PipelineProgramDesc.shaderBinaryHash = prepared.binaryHash;
	if (!Vans::VansShaderArtifactCache::Get().CommitActive(prepared))
		VANS_LOG_WARN("[ShaderArtifact] Failed to commit active artifact for '" << m_PipelineProgramDesc.name << "'");

	m_LogicDevice = logic_device;

	m_PushConstantSize = 0;

	m_PushConstantData = nullptr;

	return true;
}

bool VansGraphics::VansShader::ReplaceShaderModulesFromSPIRV(
	const std::map<VkShaderStageFlagBits, std::vector<std::uint32_t>>& stageSpirv,
	std::string& error)
{
	if (stageSpirv.empty())
	{
		error = "compiled shader candidate has no stages";
		return false;
	}

	std::map<VkShaderStageFlagBits, ShaderModuleData> candidate;
	for (const auto& [stage, words] : stageSpirv)
	{
		if (words.empty() || words.front() != 0x07230203u)
		{
			error = "compiled shader candidate contains invalid SPIR-V";
			return false;
		}

		ShaderModuleData moduleData;
		const auto activeIt = m_ShaderModuleDataMap.find(stage);
		if (activeIt != m_ShaderModuleDataMap.end())
		{
			moduleData.m_ShaderTextResourceFileName = activeIt->second.m_ShaderTextResourceFileName;
			moduleData.m_ShaderType = activeIt->second.m_ShaderType;
		}

		moduleData.m_ShaderSPIRVCode.resize(words.size() * sizeof(std::uint32_t));
		std::memcpy(moduleData.m_ShaderSPIRVCode.data(), words.data(), moduleData.m_ShaderSPIRVCode.size());
		candidate.emplace(stage, std::move(moduleData));
	}

	if (!CreateShaderModulesFromData(m_LogicDevice, candidate))
	{
		error = "failed to create Vulkan shader modules from compiled candidate";
		return false;
	}

	DestroyShaderModuleData(m_LogicDevice, m_ShaderModuleDataMap);
	m_ShaderModuleDataMap = std::move(candidate);

	std::uint64_t binaryHash = 14695981039346656037ull;
	for (const auto& [stage, module] : m_ShaderModuleDataMap)
	{
		const std::uint32_t stageValue = static_cast<std::uint32_t>(stage);
		for (const auto* bytes = reinterpret_cast<const unsigned char*>(&stageValue);
			bytes != reinterpret_cast<const unsigned char*>(&stageValue) + sizeof(stageValue); ++bytes)
		{
			binaryHash ^= *bytes;
			binaryHash *= 1099511628211ull;
		}
		for (unsigned char byte : module.m_ShaderSPIRVCode)
		{
			binaryHash ^= byte;
			binaryHash *= 1099511628211ull;
		}
	}
	m_PipelineProgramDesc.shaderBinaryHash = binaryHash == 0 ? 1 : binaryHash;
	error.clear();
	return true;
}

bool VansGraphics::VansShader::InitRayTracingShader(VkDevice& logic_device, const std::string& shader_folder)
{
	return InitRayTracingShader(logic_device, shader_folder, {});
}

bool VansGraphics::VansShader::InitRayTracingShader(VkDevice& logic_device, const std::string& shader_folder, const std::map<VkShaderStageFlagBits, std::string>& stageFiles)
{
	std::string shader_folder_string = shader_folder;
	m_ShaderFolder = shader_folder;
	m_ShaderType = ShaderType::RayTracing;
	m_ExplicitStageFiles = stageFiles;
	m_PipelineProgramDesc.name = m_PipelineProgramDesc.name.empty()
		? BuildStableUnmanagedProgramId(shader_folder) : m_PipelineProgramDesc.name;
	m_PipelineProgramDesc.shaderPath = shader_folder;
	m_PipelineProgramDesc.kind = VansPipelineProgramKind::RayTracing;
	m_PipelineProgramDesc.stages = VansPipelineDescriptorBuilder::BuildStageFiles(
		shader_folder,
		m_PipelineProgramDesc.kind,
		stageFiles);
	std::map<VkShaderStageFlagBits, ShaderModuleData> moduleData;
	Vans::VansShaderArtifactPrepareResult prepared;
	bool result = CompileShaderModuleData(
		shader_folder_string, ShaderType::RayTracing, m_ExplicitStageFiles,
		m_PipelineProgramDesc.name, moduleData, prepared);
	if (!result)
	{
		VANS_LOG_ERROR("shader translation failed");
		return false;
	}
	VANS_LOG("before ray tracing create shader module");
	result = CreateShaderModulesFromData(logic_device, moduleData);
	if (!result)
	{
		VANS_LOG_ERROR("create shader module failed");
		return false;
	}
	DestroyShaderModuleData(logic_device, m_ShaderModuleDataMap);
	m_ShaderModuleDataMap = std::move(moduleData);
	m_PipelineProgramDesc.shaderBinaryHash = prepared.binaryHash;
	if (!Vans::VansShaderArtifactCache::Get().CommitActive(prepared))
		VANS_LOG_WARN("[ShaderArtifact] Failed to commit active artifact for '" << m_PipelineProgramDesc.name << "'");
	m_LogicDevice = logic_device;

	m_PushConstantSize = 0;

	m_PushConstantData = nullptr;
	return true;
}

void VansGraphics::VansShader::DestroyShaderMoulde()
{
	DestroyShaderModuleData(m_LogicDevice, m_ShaderModuleDataMap);
}

VansGraphics::VansVKGraphicsPipeline* VansGraphics::VansGraphicsShader::GetGraphicsPipeline(VkDevice& logic_device, GlobalStateData& global_state_data,const std::vector<VkDescriptorSetLayout>& descriptorset_layouts)
{
	std::lock_guard<std::mutex> lock(m_GraphicsPipelineMutex);
	m_PipelineProgramDesc.kind = VansPipelineProgramKind::Graphics;
	m_PipelineProgramDesc.pushConstantSize = m_PushConstantSize;
	const uint32_t pushConstantSize = m_PushConstantSize > 0
		? static_cast<uint32_t>(m_PushConstantSize) : 0;
	const auto* vertexBindings = global_state_data.vertexInputBindingDescriptions;
	const auto* vertexAttributes = global_state_data.vertexInputAttributeDescriptions;
	const uint64_t variantHash = VansPipelineDescriptorBuilder::BuildVariantHash(
		m_PipelineProgramDesc,
		global_state_data.currentRenderPass,
		global_state_data.currentSubpass,
		descriptorset_layouts,
		pushConstantSize,
		vertexBindings,
		vertexAttributes,
		global_state_data.rasterizationSamples,
		global_state_data.sampleShadingEnable);

	auto matchesRequest = [&](const GraphicsPipelineVariantEntry& entry)
	{
		return entry.identity.hash == variantHash &&
			VansPipelineDescriptorBuilder::MatchesVariant(
				entry.identity,
				m_PipelineProgramDesc,
				global_state_data.currentRenderPass,
				global_state_data.currentSubpass,
				descriptorset_layouts,
				pushConstantSize,
				vertexBindings,
				vertexAttributes,
				global_state_data.rasterizationSamples,
				global_state_data.sampleShadingEnable);
	};

	if (m_LastGraphicsPipelineVariant != nullptr && matchesRequest(*m_LastGraphicsPipelineVariant))
	{
		m_GraphicsPipeline = m_LastGraphicsPipelineVariant->pipeline;
		return m_GraphicsPipeline.get();
	}

	const auto variantRange = m_GraphicsPipelineVariants.equal_range(variantHash);
	for (auto it = variantRange.first; it != variantRange.second; ++it)
	{
		if (!matchesRequest(it->second))
			continue;
		m_LastGraphicsPipelineVariant = &it->second;
		m_GraphicsPipeline = it->second.pipeline;
		return m_GraphicsPipeline.get();
	}

	// The expensive canonical descriptor and Vulkan create-info are cold-path
	// work: only build them when this shader/runtime combination is new.
	m_GraphicsPipelineCreateInfo.Clear();
	m_GraphicsPipelineCreateInfo.descriptorset_layouts = descriptorset_layouts;
	m_GraphicsPipelineCreateInfo.push_constant_size = m_PushConstantSize;
	m_GraphicsPipelineCreateInfo.pipeline_program_desc = m_PipelineProgramDesc;

	//创建pipeline
	InitGraphicsPipelinInfo(global_state_data);
	bool result = CreateGraphicsPipeline(logic_device, global_state_data);
	if (!result)
	{
		VANS_LOG_ERROR("create graphics pipeline failed");
		return NULL;
	}

	const uint32_t vertexBindingCount = vertexBindings
		? static_cast<uint32_t>(vertexBindings->size()) : 0;
	const uint32_t vertexAttributeCount = vertexAttributes
		? static_cast<uint32_t>(vertexAttributes->size()) : 0;
	VansPipelineRuntimeDesc runtimeDesc = VansPipelineDescriptorBuilder::BuildRuntimeDesc(
		global_state_data.currentRenderPass,
		global_state_data.currentSubpass,
		descriptorset_layouts,
		pushConstantSize,
		vertexBindingCount,
		vertexAttributeCount,
		global_state_data.rasterizationSamples,
		global_state_data.sampleShadingEnable);
	if (vertexBindings)
		runtimeDesc.vertexBindings = *vertexBindings;
	if (vertexAttributes)
		runtimeDesc.vertexAttributes = *vertexAttributes;

	VansPipelineVariantIdentity identity;
	identity.hash = variantHash;
	identity.shaderBinaryHash = m_PipelineProgramDesc.shaderBinaryHash;
	identity.pushConstantSize = m_PipelineProgramDesc.pushConstantSize;
	identity.graphicsState = m_PipelineProgramDesc.graphicsState;
	identity.runtimeDesc = std::move(runtimeDesc);
	GraphicsPipelineVariantEntry entry{ std::move(identity), m_GraphicsPipeline };
	auto inserted = m_GraphicsPipelineVariants.emplace(variantHash, std::move(entry));
	m_LastGraphicsPipelineVariant = &inserted->second;
	return m_GraphicsPipeline.get();
}

void VansGraphics::VansGraphicsShader::SetDrawStateData(VkBool32 depthTestEnable, VkBool32 depthWriteEnable, VkCompareOp depthCompareOp, VkCullModeFlags cullmode)
{
	m_DrawStateData.depthTestEnable = depthTestEnable;
	m_DrawStateData.depthWriteEnable = depthWriteEnable;
	m_DrawStateData.depthCompareOp = depthCompareOp;
	m_DrawStateData.cullMode = cullmode;
	SyncPipelineGraphicsStateDesc();
}

void VansGraphics::VansGraphicsShader::ApplyGraphicsStateDesc(const VansGraphicsPipelineStateDesc& graphicsState)
{
	m_DrawStateData.depthTestEnable = graphicsState.depthTest;
	m_DrawStateData.depthWriteEnable = graphicsState.depthWrite;
	m_DrawStateData.depthCompareOp = graphicsState.depthCompareOp;
	m_DrawStateData.depthBoundsTestEnable = graphicsState.depthBoundsTest;
	m_DrawStateData.stencilTestEnable = graphicsState.stencilTest;
	m_DrawStateData.front = graphicsState.stencilFront;
	m_DrawStateData.back = graphicsState.stencilBack;
	m_DrawStateData.minDepthBounds = graphicsState.minDepthBounds;
	m_DrawStateData.maxDepthBounds = graphicsState.maxDepthBounds;
	m_DrawStateData.cullMode = graphicsState.cullMode;
	m_DrawStateData.polygonMode = graphicsState.polygonMode;
	m_DrawStateData.frontFace = graphicsState.frontFace;
	m_DrawStateData.primitiveTopology = graphicsState.primitiveTopology;
	m_DrawStateData.primitiveRestartEnable = graphicsState.primitiveRestart;
	m_DrawStateData.patchControlPoints = graphicsState.patchControlPoints;
	m_DrawStateData.depthClampEnable = graphicsState.depthClamp;
	m_DrawStateData.rasterizerDiscardEnable = graphicsState.rasterizerDiscard;
	m_DrawStateData.depthBiasEnable = graphicsState.depthBias;
	m_DrawStateData.depthBiasConstantFactor = graphicsState.depthBiasConstantFactor;
	m_DrawStateData.depthBiasClamp = graphicsState.depthBiasClamp;
	m_DrawStateData.depthBiasSlopeFactor = graphicsState.depthBiasSlopeFactor;
	m_DrawStateData.lineWidth = graphicsState.lineWidth;
	m_DrawStateData.alphaToCoverageEnable = graphicsState.alphaToCoverage;
	m_DrawStateData.alphaToOneEnable = graphicsState.alphaToOne;
	m_DrawStateData.enableAlphaBlend = graphicsState.enableAlphaBlend ? VK_TRUE : VK_FALSE;
	m_DrawStateData.enableDecalBlend = graphicsState.enableDecalBlend ? VK_TRUE : VK_FALSE;
	m_DrawStateData.enableAdditiveBlend = graphicsState.enableAdditiveBlend ? VK_TRUE : VK_FALSE;
	m_DrawStateData.additiveBlendAttachmentMask = graphicsState.additiveBlendAttachmentMask;
	m_DrawStateData.enablePremultipliedAlphaBlend = graphicsState.enablePremultipliedAlphaBlend ? VK_TRUE : VK_FALSE;
	m_ColorAttachmentCount = graphicsState.colorAttachmentCount;
	SyncPipelineGraphicsStateDesc();
	TriggerReCreateGraphicsPipeline();
}

void VansGraphics::VansGraphicsShader::SetPolygonMode(VkPolygonMode mode)
{
	m_DrawStateData.polygonMode = mode;
	SyncPipelineGraphicsStateDesc();
}

void VansGraphics::VansGraphicsShader::SyncPipelineGraphicsStateDesc()
{
	VansGraphicsPipelineStateDesc& graphicsState = m_PipelineProgramDesc.graphicsState;
	graphicsState.depthTest = m_DrawStateData.depthTestEnable;
	graphicsState.depthWrite = m_DrawStateData.depthWriteEnable;
	graphicsState.depthCompareOp = m_DrawStateData.depthCompareOp;
	graphicsState.depthBoundsTest = m_DrawStateData.depthBoundsTestEnable;
	graphicsState.stencilTest = m_DrawStateData.stencilTestEnable;
	graphicsState.stencilFront = m_DrawStateData.front;
	graphicsState.stencilBack = m_DrawStateData.back;
	graphicsState.minDepthBounds = m_DrawStateData.minDepthBounds;
	graphicsState.maxDepthBounds = m_DrawStateData.maxDepthBounds;
	graphicsState.cullMode = m_DrawStateData.cullMode;
	graphicsState.polygonMode = m_DrawStateData.polygonMode;
	graphicsState.frontFace = m_DrawStateData.frontFace;
	graphicsState.primitiveTopology = m_DrawStateData.primitiveTopology;
	graphicsState.primitiveRestart = m_DrawStateData.primitiveRestartEnable;
	graphicsState.patchControlPoints = m_DrawStateData.patchControlPoints;
	graphicsState.depthClamp = m_DrawStateData.depthClampEnable;
	graphicsState.rasterizerDiscard = m_DrawStateData.rasterizerDiscardEnable;
	graphicsState.depthBias = m_DrawStateData.depthBiasEnable;
	graphicsState.depthBiasConstantFactor = m_DrawStateData.depthBiasConstantFactor;
	graphicsState.depthBiasClamp = m_DrawStateData.depthBiasClamp;
	graphicsState.depthBiasSlopeFactor = m_DrawStateData.depthBiasSlopeFactor;
	graphicsState.lineWidth = m_DrawStateData.lineWidth;
	graphicsState.alphaToCoverage = m_DrawStateData.alphaToCoverageEnable;
	graphicsState.alphaToOne = m_DrawStateData.alphaToOneEnable;
	graphicsState.colorAttachmentCount = m_ColorAttachmentCount;
	graphicsState.enableAlphaBlend = m_DrawStateData.enableAlphaBlend == VK_TRUE;
	graphicsState.enableDecalBlend = m_DrawStateData.enableDecalBlend == VK_TRUE;
	graphicsState.enableAdditiveBlend = m_DrawStateData.enableAdditiveBlend == VK_TRUE;
	graphicsState.additiveBlendAttachmentMask = m_DrawStateData.additiveBlendAttachmentMask;
	graphicsState.enablePremultipliedAlphaBlend = m_DrawStateData.enablePremultipliedAlphaBlend == VK_TRUE;
}

void InitAttachmentBlendStates(std::vector<VkPipelineColorBlendAttachmentState>& states, bool enableDeferred, bool enableAlphaBlend = false, bool enableDecalBlend = false, bool enableAdditiveBlend = false, bool enablePremultipliedAlphaBlend = false, int explicitCount = -1, uint32_t additiveBlendAttachmentMask = 0)
{
	// 显式指定颜色附件数量（如水面 GBuffer 的 2 个附件）：生成 count 个不混合、写入 RGBA 的 state
	if (explicitCount == 0)
	{
		states.clear();
		return;
	}

	if (explicitCount > 0)
	{
		VkPipelineColorBlendAttachmentState disabledState =
		{
			VK_FALSE,
			 VK_BLEND_FACTOR_ONE,
			 VK_BLEND_FACTOR_ONE,
			 VK_BLEND_OP_ADD,
			 VK_BLEND_FACTOR_ONE,
			 VK_BLEND_FACTOR_ONE,
			 VK_BLEND_OP_ADD,
			 VK_COLOR_COMPONENT_R_BIT |
			 VK_COLOR_COMPONENT_G_BIT |
			 VK_COLOR_COMPONENT_B_BIT |
			 VK_COLOR_COMPONENT_A_BIT
		};
		VkPipelineColorBlendAttachmentState additiveState = disabledState;
		additiveState.blendEnable = VK_TRUE;
		states.resize(static_cast<size_t>(explicitCount), disabledState);
		if (enableAdditiveBlend)
		{
			for (int i = 0; i < explicitCount; ++i)
			{
				if (additiveBlendAttachmentMask == 0 || (additiveBlendAttachmentMask & (1u << i)) != 0)
					states[static_cast<size_t>(i)] = additiveState;
			}
		}
		return;
	}

	if (enableDeferred)
	{
		states.resize(4,
			{
				false,
				 VK_BLEND_FACTOR_ONE,
				 VK_BLEND_FACTOR_ONE,
				 VK_BLEND_OP_ADD,
				 VK_BLEND_FACTOR_ONE,
				 VK_BLEND_FACTOR_ONE,
				 VK_BLEND_OP_ADD,
				 VK_COLOR_COMPONENT_R_BIT |
				 VK_COLOR_COMPONENT_G_BIT |
				 VK_COLOR_COMPONENT_B_BIT |
				 VK_COLOR_COMPONENT_A_BIT
			});
	}
	else if (enableDecalBlend)
	{
		// 贴花 MRT 混合：3 个 GBuffer 颜色附件（Normal / GBuffer0 / GBuffer1），全部开启 Alpha Blend
		// GBuffer1 只写入 R（metallic）和 G（AO），跳过 B（materialID）和 A
		VkPipelineColorBlendAttachmentState alphaBlendState =
		{
			VK_TRUE,
			VK_BLEND_FACTOR_SRC_ALPHA,
			VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			VK_BLEND_OP_ADD,
			VK_BLEND_FACTOR_ONE,
			VK_BLEND_FACTOR_ZERO,
			VK_BLEND_OP_ADD,
			VK_COLOR_COMPONENT_R_BIT |
			VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT |
			VK_COLOR_COMPONENT_A_BIT
		};
		states.resize(3, alphaBlendState);
		// GBuffer1（索引2）：跳过 B（materialID）和 A，仅写 R+G
		states[2].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;
	}
	else if (enableAlphaBlend)
	{
		// Standard alpha blending: srcColor * srcAlpha + dstColor * (1 - srcAlpha)
		states =
		{
			 {
				VK_TRUE,
				 VK_BLEND_FACTOR_SRC_ALPHA,
				 VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
				 VK_BLEND_OP_ADD,
				 VK_BLEND_FACTOR_ONE,
				 VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
				 VK_BLEND_OP_ADD,
				 VK_COLOR_COMPONENT_R_BIT |
				 VK_COLOR_COMPONENT_G_BIT |
				 VK_COLOR_COMPONENT_B_BIT |
				 VK_COLOR_COMPONENT_A_BIT
			 }
		};
	}
	else if (enablePremultipliedAlphaBlend)
	{
		states =
		{
			 {
				VK_TRUE,
				 VK_BLEND_FACTOR_ONE,
				 VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
				 VK_BLEND_OP_ADD,
				 VK_BLEND_FACTOR_ONE,
				 VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
				 VK_BLEND_OP_ADD,
				 VK_COLOR_COMPONENT_R_BIT |
				 VK_COLOR_COMPONENT_G_BIT |
				 VK_COLOR_COMPONENT_B_BIT |
				 VK_COLOR_COMPONENT_A_BIT
			 }
		};
	}
	else if (enableAdditiveBlend)
	{
		states =
		{
			 {
				VK_TRUE,
				 VK_BLEND_FACTOR_ONE,
				 VK_BLEND_FACTOR_ONE,
				 VK_BLEND_OP_ADD,
				 VK_BLEND_FACTOR_ONE,
				 VK_BLEND_FACTOR_ONE,
				 VK_BLEND_OP_ADD,
				 VK_COLOR_COMPONENT_R_BIT |
				 VK_COLOR_COMPONENT_G_BIT |
				 VK_COLOR_COMPONENT_B_BIT |
				 VK_COLOR_COMPONENT_A_BIT
			 }
		};
	}
	else
	{
		states = 
		{
			 {
				false,
				 VK_BLEND_FACTOR_ONE,
				 VK_BLEND_FACTOR_ONE,
				 VK_BLEND_OP_ADD,
				 VK_BLEND_FACTOR_ONE,
				 VK_BLEND_FACTOR_ONE,
				 VK_BLEND_OP_ADD,
				 VK_COLOR_COMPONENT_R_BIT |
				 VK_COLOR_COMPONENT_G_BIT |
				 VK_COLOR_COMPONENT_B_BIT |
				 VK_COLOR_COMPONENT_A_BIT
			 }
		};
	}
}

void VansGraphics::VansGraphicsShader::InitGraphicsPipelinInfo(GlobalStateData& global_state_data)
{
	bool enableDeferred = m_SupportMRTOutput;

	//便利所有的module data来创建params
	for (auto& shader_module_data : m_ShaderModuleDataMap)
	{
		ShaderStageParameters shader_stage_params =
		{
			shader_module_data.first,
			shader_module_data.second.m_ShaderModule,
			"main",
			nullptr
		};
		m_GraphicsPipelineCreateInfo.shader_stage_params.push_back(shader_stage_params);
	}

	m_GraphicsPipelineCreateInfo.input_assembly_state_create_info =
	{
		 VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		 nullptr,
		 0,
		 m_DrawStateData.primitiveTopology,
		 m_DrawStateData.primitiveRestartEnable
	};

	m_GraphicsPipelineCreateInfo.tessellation_state_create_info =
	{
		 VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
		 nullptr,
		 0,
		 m_DrawStateData.patchControlPoints
	};

	m_GraphicsPipelineCreateInfo.rasterization_state_create_info =
	{
		 VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		 nullptr,
		 0,
		 m_DrawStateData.depthClampEnable,
		 m_DrawStateData.rasterizerDiscardEnable,
		 m_DrawStateData.polygonMode,
		 m_DrawStateData.cullMode,
		 m_DrawStateData.frontFace,
		 m_DrawStateData.depthBiasEnable,
		 m_DrawStateData.depthBiasConstantFactor,
		 m_DrawStateData.depthBiasClamp,
		 m_DrawStateData.depthBiasSlopeFactor,
		 m_DrawStateData.lineWidth
	};

	m_GraphicsPipelineCreateInfo.multisample_state_create_info =
	{
		 VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		 nullptr,
		 0,
		 global_state_data.rasterizationSamples,
		 //是否支持fs在每个sample位置invocation，shader中可以通过sampleId
		 global_state_data.sampleShadingEnable,
		 global_state_data.minSampleShading,
		 global_state_data.pSampleMask,
		 m_DrawStateData.alphaToCoverageEnable,
		 m_DrawStateData.alphaToOneEnable
	};

	m_GraphicsPipelineCreateInfo.stencil_test_parameters =
	{
		 VK_STENCIL_OP_KEEP,
		 VK_STENCIL_OP_KEEP,
		 VK_STENCIL_OP_KEEP,
		 VK_COMPARE_OP_ALWAYS,
		 0,
		 0,
		 0
	};

	m_GraphicsPipelineCreateInfo.depth_and_stencil_state_create_info =
	{
		 VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		 nullptr,
		 0,
		 m_DrawStateData.depthTestEnable,
		 m_DrawStateData.depthWriteEnable,
		 m_DrawStateData.depthCompareOp,
		 m_DrawStateData.depthBoundsTestEnable,
		 m_DrawStateData.stencilTestEnable,
		 m_DrawStateData.front,
		 m_DrawStateData.back,
		 m_DrawStateData.minDepthBounds,
		 m_DrawStateData.maxDepthBounds
	};

	//需要根据deferred的模式，设置每个rt的blend state，不然shader写不到对应的rt上
	
	InitAttachmentBlendStates(m_GraphicsPipelineCreateInfo.attachment_blend_states, enableDeferred, m_DrawStateData.enableAlphaBlend, m_DrawStateData.enableDecalBlend, m_DrawStateData.enableAdditiveBlend, m_DrawStateData.enablePremultipliedAlphaBlend, m_ColorAttachmentCount, m_DrawStateData.additiveBlendAttachmentMask);
}

bool VansGraphics::VansGraphicsShader::CreateGraphicsPipeline(VkDevice& logic_device, GlobalStateData& global_state_data)
{
	m_PipelineProgramDesc.kind = VansPipelineProgramKind::Graphics;
	m_PipelineProgramDesc.pushConstantSize = m_PushConstantSize;

	std::shared_ptr<VansVKGraphicsPipeline> pipeline = std::make_shared<VansVKGraphicsPipeline>();
	bool result = pipeline->CreateGraphicsPipelineInfo(logic_device, m_GraphicsPipelineCreateInfo, global_state_data, m_VkGraphicsPipelineCreateInfo);
	if (!result)
	{
		VANS_LOG_ERROR("create graphics pipeline info failed");
		return false;
	}

	if (std::shared_ptr<VansVKGraphicsPipeline> cachedPipeline = VansPipelineRegistry::Get().FindGraphicsPipeline(pipeline->GetDescriptorKey()))
	{
		m_GraphicsPipeline = cachedPipeline;
		return true;
	}

	result = pipeline->CreateGraphicsPipeline(logic_device, m_VkGraphicsPipelineCreateInfo);
	if (!result)
	{
		return false;
	}

	m_GraphicsPipeline = VansPipelineRegistry::Get().StoreGraphicsPipeline(pipeline);
	return true;
}

void VansGraphics::VansGraphicsShader::TriggerReCreateGraphicsPipeline()
{
	std::lock_guard<std::mutex> lock(m_GraphicsPipelineMutex);
	m_GraphicsPipeline.reset();
	m_LastGraphicsPipelineVariant = nullptr;
	m_GraphicsPipelineVariants.clear();
	m_GraphicsPipelineCreateInfo.Clear();
}


VansGraphics::VansVKComputePipeline* VansGraphics::VansComputeShader::GetComputePipeline(VkDevice& logic_device, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts)
{
	m_PipelineProgramDesc.kind = VansPipelineProgramKind::Compute;
	m_PipelineProgramDesc.pushConstantSize = m_PushConstantSize;
	const uint32_t pushConstantSize = m_PushConstantSize > 0
		? static_cast<uint32_t>(m_PushConstantSize) : 0;
	const uint64_t variantHash = VansPipelineDescriptorBuilder::BuildVariantHash(
		m_PipelineProgramDesc,
		VK_NULL_HANDLE,
		0,
		descriptorset_layouts,
		pushConstantSize,
		nullptr,
		nullptr,
		VK_SAMPLE_COUNT_1_BIT,
		VK_FALSE);

	auto matchesRequest = [&](const ComputePipelineVariantEntry& entry)
	{
		return entry.identity.hash == variantHash &&
			VansPipelineDescriptorBuilder::MatchesVariant(
				entry.identity,
				m_PipelineProgramDesc,
				VK_NULL_HANDLE,
				0,
				descriptorset_layouts,
				pushConstantSize,
				nullptr,
				nullptr,
				VK_SAMPLE_COUNT_1_BIT,
				VK_FALSE);
	};

	if (m_LastComputePipelineVariant != nullptr && matchesRequest(*m_LastComputePipelineVariant))
	{
		m_ComputePipeline = m_LastComputePipelineVariant->pipeline;
		return m_ComputePipeline.get();
	}

	const auto variantRange = m_ComputePipelineVariants.equal_range(variantHash);
	for (auto it = variantRange.first; it != variantRange.second; ++it)
	{
		if (!matchesRequest(it->second))
			continue;
		m_LastComputePipelineVariant = &it->second;
		m_ComputePipeline = it->second.pipeline;
		return m_ComputePipeline.get();
	}

	bool result = CreateComputePipeline(logic_device, descriptorset_layouts);
	if (!result)
	{
		VANS_LOG_ERROR("create compute pipeline failed");
		return NULL;
	}

	VansPipelineVariantIdentity identity;
	identity.hash = variantHash;
	identity.shaderBinaryHash = m_PipelineProgramDesc.shaderBinaryHash;
	identity.pushConstantSize = m_PipelineProgramDesc.pushConstantSize;
	identity.runtimeDesc = VansPipelineDescriptorBuilder::BuildRuntimeDesc(
		descriptorset_layouts, pushConstantSize);
	ComputePipelineVariantEntry entry{ std::move(identity), m_ComputePipeline };
	auto inserted = m_ComputePipelineVariants.emplace(variantHash, std::move(entry));
	m_LastComputePipelineVariant = &inserted->second;
	return m_ComputePipeline.get();
}

bool VansGraphics::VansComputeShader::CreateComputePipeline(VkDevice& logic_device, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts)
{
	m_PipelineProgramDesc.kind = VansPipelineProgramKind::Compute;
	m_PipelineProgramDesc.pushConstantSize = m_PushConstantSize;

	std::map<VkShaderStageFlagBits, ShaderModuleData>::iterator it = m_ShaderModuleDataMap.find(VK_SHADER_STAGE_COMPUTE_BIT);
	if (it == m_ShaderModuleDataMap.end())
	{
		return false;
	}
	VkPipelineShaderStageCreateInfo compute_shader_stage =
	{
		 VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		 nullptr,
		 0,
		 VK_SHADER_STAGE_COMPUTE_BIT,
		 it->second.m_ShaderModule,
		 "main",
		 nullptr
	};

	int pushConstRangeCount = 0;
	VkPushConstantRange* pushConstRangePtr = nullptr;
	VkPushConstantRange pushConstantRange = {};
	if (m_PushConstantSize > 0)
	{
		pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = m_PushConstantSize;
		pushConstRangePtr = &pushConstantRange;
		pushConstRangeCount = 1;
	}

	const VansPipelineRuntimeDesc runtimeDesc = VansPipelineDescriptorBuilder::BuildRuntimeDesc(
		descriptorset_layouts,
		m_PushConstantSize > 0 ? static_cast<uint32_t>(m_PushConstantSize) : 0);
	const VansPipelineDescriptorKey key = VansPipelineDescriptorBuilder::BuildPipelineKey(m_PipelineProgramDesc, runtimeDesc);
	if (std::shared_ptr<VansVKComputePipeline> cachedPipeline = VansPipelineRegistry::Get().FindComputePipeline(key))
	{
		m_ComputePipeline = cachedPipeline;
		return true;
	}

	std::shared_ptr<VansVKComputePipeline> pipeline = std::make_shared<VansVKComputePipeline>();
	const bool result = pipeline->CreateComputePipeline(logic_device, compute_shader_stage, descriptorset_layouts, pushConstRangeCount, pushConstRangePtr, &m_PipelineProgramDesc);
	if (!result)
	{
		return false;
	}

	m_ComputePipeline = VansPipelineRegistry::Get().StoreComputePipeline(pipeline);
	return true;
}

VansGraphics::VansVKRayTracingPipeline* VansGraphics::VansRayTracingShader::GetRayTracingPipeline(VansVKDevice* device, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts)
{
	m_LogicDevice = device->GetLogicDevice();
	if (m_VansVkRayTracingPipeline != nullptr)
	{
		return m_VansVkRayTracingPipeline.get();
	}

	bool result = CreateRayTracingPipeline(m_LogicDevice, descriptorset_layouts);
	if (!result)
	{
		VANS_LOG_ERROR("create raytracing pipeline failed");
		return NULL;
	}

	//创建并设置SBT
	CreateShaderBindingTable(device);

	return m_VansVkRayTracingPipeline.get();
}

void VansGraphics::VansRayTracingShader::CreateShaderBindingTable(VansVKDevice* device)
{
	//Shader Binding Table(SBT) : 
	//the structure that makes this runtime shader selection possible.
	//This is essentially a table of opaque shader handles(probably device addresses), 
	//analagous to a C++ vtable

	// 定义SBT条目大小
	//注意这里要和property 里面的对齐值对齐，并且要和pipeLine中的stage顺序对齐
	const uint32_t raygenCount = 1;
	const uint32_t missCount = 1;
	const uint32_t hitCount = 1;
	const uint32_t groupCount = raygenCount + missCount + hitCount;


	auto properties = device->GetRayTracingProperties();
	uint32_t handleSize = properties.shaderGroupHandleSize;

	// The SBT (buffer) need to have starting groups to be aligned and handles in the group to be aligned.
	uint32_t handleSizeAligned = AlignUp(handleSize, properties.shaderGroupHandleAlignment);
	
	m_VansVkRayTracingPipeline->m_RaygenShaderBindingTable.stride = AlignUp(handleSizeAligned, properties.shaderGroupBaseAlignment);
	m_VansVkRayTracingPipeline->m_RaygenShaderBindingTable.size = m_VansVkRayTracingPipeline->m_RaygenShaderBindingTable.stride;  // The size member of pRayGenShaderBindingTable must be equal to its stride member
	
	m_VansVkRayTracingPipeline->m_MissShaderBindingTable.stride = handleSizeAligned;
	m_VansVkRayTracingPipeline->m_MissShaderBindingTable.size = AlignUp(missCount * handleSizeAligned, properties.shaderGroupBaseAlignment);
	
	m_VansVkRayTracingPipeline->m_HitShaderBindingTable.stride = handleSizeAligned;
	m_VansVkRayTracingPipeline->m_HitShaderBindingTable.size = AlignUp(hitCount * handleSizeAligned, properties.shaderGroupBaseAlignment);

	m_SBTBuffer.CreatVulkanBuffer(
		m_LogicDevice,
		m_VansVkRayTracingPipeline->m_RaygenShaderBindingTable.size + 
		m_VansVkRayTracingPipeline->m_MissShaderBindingTable.size + 
		m_VansVkRayTracingPipeline->m_HitShaderBindingTable.size +
		m_VansVkRayTracingPipeline->m_CallableShaderBindingTable.size,
		VK_FORMAT_R32_SFLOAT,
		VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	//从pipeline中获取handle的数据
	std::vector<uint8_t> handles(groupCount * handleSize);

	// 获取所有着色器组句柄
	VkResult result = m_VansVkRayTracingPipeline->GetShaderGroupHandles(
		m_LogicDevice,
		0,
		groupCount,
		groupCount * handleSize,
		handles.data());
	if (result != VK_SUCCESS)
	{
		VANS_LOG_ERROR("Could not get ray tracing shader group handles. VkResult: " << result);
		return;
	}

	uint8_t* handleData = handles.data();
	VkDeviceSize offset = 0;
	m_SBTBuffer.SetBufferData(handleData, offset, handleSize);
	handleData += handleSize;
	offset = m_VansVkRayTracingPipeline->m_RaygenShaderBindingTable.size;
	//miss
	for (uint32_t i = 0; i < missCount; i++)
	{
		m_SBTBuffer.SetBufferData(handleData, offset, handleSize);
		handleData += handleSize;
		offset += m_VansVkRayTracingPipeline->m_MissShaderBindingTable.stride;
	}

	offset = m_VansVkRayTracingPipeline->m_RaygenShaderBindingTable.size + m_VansVkRayTracingPipeline->m_MissShaderBindingTable.size;
	// Hit
	for (uint32_t i = 0; i < hitCount; i++)
	{
		m_SBTBuffer.SetBufferData(handleData, offset, handleSize);
		handleData += handleSize;
		offset += m_VansVkRayTracingPipeline->m_HitShaderBindingTable.stride;
	}

	const VkDeviceAddress sbtAddress = m_SBTBuffer.GetDeviceAddress(m_LogicDevice);

	// 射线生成区域
	m_VansVkRayTracingPipeline->m_RaygenShaderBindingTable.deviceAddress = sbtAddress;

	// 未命中区域
	m_VansVkRayTracingPipeline->m_MissShaderBindingTable.deviceAddress = sbtAddress +
		m_VansVkRayTracingPipeline->m_RaygenShaderBindingTable.size;

	// 命中区域
	m_VansVkRayTracingPipeline->m_HitShaderBindingTable.deviceAddress =
		m_VansVkRayTracingPipeline->m_MissShaderBindingTable.deviceAddress + 
		m_VansVkRayTracingPipeline->m_MissShaderBindingTable.size;
}

bool VansGraphics::VansRayTracingShader::CreateRayTracingPipeline(VkDevice& logic_device, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts)
{
	std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroupCreateInfo;
	std::vector<VkPipelineShaderStageCreateInfo> rayTracingStages;

	// 一个 GI program 使用一个 raygen、一个 miss 和一个三角形 hit group。any-hit、
	// closest-hit 必须进入同一个 hit group；把它们拆成两个 group 会令 any-hit 永远
	// 不会在 SBT 选中的 closest-hit group 上执行。显式顺序也避免 unordered_map
	// 迭代顺序改变 SBT group 索引。
	const std::array<VkShaderStageFlagBits, 5> stageOrder =
	{
		VK_SHADER_STAGE_RAYGEN_BIT_KHR,
		VK_SHADER_STAGE_MISS_BIT_KHR,
		VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
		VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
		VK_SHADER_STAGE_INTERSECTION_BIT_KHR
	};
	std::unordered_map<VkShaderStageFlagBits, uint32_t> stageIndices;
	for (const VkShaderStageFlagBits stageBit : stageOrder)
	{
		const auto it = m_ShaderModuleDataMap.find(stageBit);
		if (it == m_ShaderModuleDataMap.end())
			continue;

		VkPipelineShaderStageCreateInfo stage{};
		stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage.stage = stageBit;
		stage.module = it->second.m_ShaderModule;
		stage.pName = "main";
		stageIndices.emplace(stageBit, static_cast<uint32_t>(rayTracingStages.size()));
		rayTracingStages.push_back(stage);
	}

	const auto raygenIt = stageIndices.find(VK_SHADER_STAGE_RAYGEN_BIT_KHR);
	const auto missIt = stageIndices.find(VK_SHADER_STAGE_MISS_BIT_KHR);
	const auto closestHitIt = stageIndices.find(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
	if (raygenIt == stageIndices.end() || missIt == stageIndices.end() || closestHitIt == stageIndices.end())
	{
		VANS_LOG_ERROR("Ray tracing program is missing required raygen, miss, or closest-hit stage");
		return false;
	}

	VkRayTracingShaderGroupCreateInfoKHR raygenGroup{};
	raygenGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
	raygenGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	raygenGroup.generalShader = raygenIt->second;
	raygenGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
	raygenGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
	raygenGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
	shaderGroupCreateInfo.push_back(raygenGroup);

	VkRayTracingShaderGroupCreateInfoKHR missGroup{};
	missGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
	missGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	missGroup.generalShader = missIt->second;
	missGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
	missGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
	missGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
	shaderGroupCreateInfo.push_back(missGroup);

	VkRayTracingShaderGroupCreateInfoKHR hitGroup{};
	hitGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
	hitGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
	hitGroup.generalShader = VK_SHADER_UNUSED_KHR;
	hitGroup.closestHitShader = closestHitIt->second;
	hitGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
	hitGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
	if (const auto anyHitIt = stageIndices.find(VK_SHADER_STAGE_ANY_HIT_BIT_KHR); anyHitIt != stageIndices.end())
		hitGroup.anyHitShader = anyHitIt->second;
	if (const auto intersectionIt = stageIndices.find(VK_SHADER_STAGE_INTERSECTION_BIT_KHR); intersectionIt != stageIndices.end())
		hitGroup.intersectionShader = intersectionIt->second;
	shaderGroupCreateInfo.push_back(hitGroup);

	int pushConstRangeCount = 0;
	VkPushConstantRange* pushConstRangePtr = nullptr;
	VkPushConstantRange pushConstantRange = {};
	if (m_PushConstantSize > 0)
	{
		pushConstantRange.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR |
			VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
		pushConstantRange.offset = 0;
		pushConstantRange.size = m_PushConstantSize;
		pushConstRangePtr = &pushConstantRange;
		pushConstRangeCount = 1;
	}

	m_PipelineProgramDesc.kind = VansPipelineProgramKind::RayTracing;
	m_PipelineProgramDesc.pushConstantSize = m_PushConstantSize;

	const VansPipelineRuntimeDesc runtimeDesc = VansPipelineDescriptorBuilder::BuildRuntimeDesc(
		descriptorset_layouts,
		m_PushConstantSize > 0 ? static_cast<uint32_t>(m_PushConstantSize) : 0);
	const VansPipelineDescriptorKey key = VansPipelineDescriptorBuilder::BuildPipelineKey(m_PipelineProgramDesc, runtimeDesc);
	if (std::shared_ptr<VansVKRayTracingPipeline> cachedPipeline = VansPipelineRegistry::Get().FindRayTracingPipeline(key))
	{
		m_VansVkRayTracingPipeline = cachedPipeline;
		return true;
	}

	std::shared_ptr<VansVKRayTracingPipeline> pipeline = std::make_shared<VansVKRayTracingPipeline>();
	const bool result = pipeline->CreateRayTracingPipeline(logic_device, shaderGroupCreateInfo, rayTracingStages, descriptorset_layouts, pushConstRangeCount, pushConstRangePtr, &m_PipelineProgramDesc);
	if (!result)
	{
		return false;
	}

	m_VansVkRayTracingPipeline = VansPipelineRegistry::Get().StoreRayTracingPipeline(pipeline);
	return true;
}
