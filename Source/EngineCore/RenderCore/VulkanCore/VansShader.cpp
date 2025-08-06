#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansPipeline.h"
#include "VansShader.h"
#include "VansVKCommandBuffer.h"
#include "VansVKDescriptorManager.h"
#include "../../Util/VansFileUtil.h"
#include <iostream>
#include <cstdlib>


bool VansVulkan::VansShader::InitShader(VkDevice& logic_device, const std::string& shader_folder)
{
	std::string shader_folder_string = shader_folder;

	//如果是延迟管线需要切换使用的shader
	bool supportDeferred = SwitchToDeferredShaderPath(shader_folder_string);
	m_SupportMRTOutput = supportDeferred;

	bool result = TranslateToSPIRV(shader_folder_string);
	if (!result)
	{
		std::cout << "shader translation failed" << std::endl;
		return false;
	}

	result = CreateShaderModule(logic_device);
	if (!result)
	{
		std::cout << "create shader module failed" << std::endl;
		return false;
	}

	m_LogicDevice = logic_device;

	m_PushConstantSize = 0;

	m_PushConstantData = nullptr;

	return true;
}

bool VansVulkan::VansShader::CheckRefreshShader(VkDevice& logic_device)
{
	return false;
}



bool VansVulkan::VansShader::TranslateToSPIRV(const std::string& shader_folder)
{
	//根据路径下文件后缀先读取源glsl
	std::vector<std::string> shader_files = GetFilesInFolder(shader_folder);

	if (shader_files.size() == 0)
	{
		std::cout << "no shader files found:" << shader_folder << std::endl;
		return false;
	}

	//根据文件后缀找到对应的shader类型生成module data
	for (auto& shader_file : shader_files)
	{
		std::string shader_type = GetFileExtension(shader_file);
		if (shader_type == "spv")
		{
			continue;
		}
		auto shader_type_iter = m_ShaderTypeMap.find(shader_type);
		if (shader_type_iter == m_ShaderTypeMap.end())
		{
			std::cout << "unknow shader type" << std::endl;
			return false;
		}
		VkShaderStageFlagBits shader_stage = shader_type_iter->second;
		ShaderModuleData shader_module_data;
		shader_module_data.m_ShaderType = shader_type;
		shader_module_data.m_ShaderTextResourceFileName = shader_folder + "\\" + shader_file;
		m_ShaderModuleDataMap[shader_stage] = shader_module_data;
	}

	std::string command = "glslangValidator -V ";
	//遍历所有的shader moudle data然后调用glslang将text resource编译成spirv
	for (auto& shader_module_data : m_ShaderModuleDataMap)
	{
		//创建对应的spirv文件
		std::string spirv_file_name = GetFileWithoutExtension(shader_module_data.second.m_ShaderTextResourceFileName);
		spirv_file_name += shader_module_data.second.m_ShaderType + ".spv";
		spirv_file_name = shader_folder + "\\" + spirv_file_name;

		//构建命令行
		std::string shader_command = command + " " + shader_module_data.second.m_ShaderTextResourceFileName;
		shader_command += " -o " + spirv_file_name;
		int result = system(shader_command.c_str());

		if (result == 0) 
		{
			std::cout << "glslangValidator pass " << shader_module_data.first << std::endl;
		}
		else 
		{
			std::cerr << "glslangValidator failed "<< std::endl;
			return false;
		}

		//读取spirv文件
		ReadFile(spirv_file_name, shader_module_data.second.m_ShaderSPIRVCode);
		if (shader_module_data.second.m_ShaderSPIRVCode.empty())
		{
			std::cout << "read spirv file failed" << std::endl;
			return false;
		}
	}
	return true;
}

void VansVulkan::VansShader::DestroyShaderMoulde()
{
	//编译data map destroy shader module data
	for (auto& shader_module_data : m_ShaderModuleDataMap)
	{
		vkDestroyShaderModule(m_LogicDevice, shader_module_data.second.m_ShaderModule, nullptr);
	}
}

bool VansVulkan::VansShader::CreateShaderModule(VkDevice& logic_device)
{
	//遍历所有的module data
	for (auto& shader_module_data : m_ShaderModuleDataMap)
	{
		VkShaderModuleCreateInfo shader_module_create_info = 
		{
			VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			nullptr,
			0,
			shader_module_data.second.m_ShaderSPIRVCode.size(),
			reinterpret_cast<uint32_t const*>(shader_module_data.second.m_ShaderSPIRVCode.data())
		};
		VkResult result = vkCreateShaderModule(logic_device, &shader_module_create_info, nullptr, &shader_module_data.second.m_ShaderModule);
		if (VK_SUCCESS != result)
		{
			std::cout << "Could not create a shader module." << std::endl;
			return false;
		}
	}
	return true;
}

VansVulkan::VansVKGraphicsPipeline* VansVulkan::VansGraphicsShader::GetGraphicsPipeline(VkDevice& logic_device, GlobalStateData& global_state_data,const std::vector<VkDescriptorSetLayout>& descriptorset_layouts)
{
	if (m_GraphicsPipeline != VK_NULL_HANDLE)
	{
		return m_GraphicsPipeline;
	}
	//设置描述符layout，用于创建pipeline
	m_GraphicsPipelineCreateInfo.descriptorset_layouts.resize(descriptorset_layouts.size());
	for (int i = 0; i < descriptorset_layouts.size(); i++)
	{
		m_GraphicsPipelineCreateInfo.descriptorset_layouts[i]=descriptorset_layouts[i];
	}

	m_GraphicsPipelineCreateInfo.push_constant_size = m_PushConstantSize;

	//创建pipeline
	InitGraphicsPipelinInfo(global_state_data);
	bool result = CreateGraphicsPipeline(logic_device, global_state_data);
	if (!result)
	{
		std::cout << "create graphics pipeline failed" << std::endl;
		return NULL;
	}
	return m_GraphicsPipeline;
}

void VansVulkan::VansGraphicsShader::SetDrawStateData(VkBool32 depthTestEnable, VkBool32 depthWriteEnable, VkCompareOp depthCompareOp, VkCullModeFlags cullmode)
{
	m_DrawStateData.depthTestEnable = depthTestEnable;
	m_DrawStateData.depthWriteEnable = depthWriteEnable;
	m_DrawStateData.depthCompareOp = depthCompareOp;
	m_DrawStateData.cullMode = cullmode;
}

void InitAttachmentBlendStates(std::vector<VkPipelineColorBlendAttachmentState>& states ,bool enableDeferred)
{
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

void VansVulkan::VansGraphicsShader::InitGraphicsPipelinInfo(GlobalStateData& global_state_data)
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
	
	InitAttachmentBlendStates(m_GraphicsPipelineCreateInfo.attachment_blend_states, enableDeferred);
}

bool VansVulkan::VansGraphicsShader::CreateGraphicsPipeline(VkDevice& logic_device, GlobalStateData& global_state_data)
{
	if (m_GraphicsPipeline == nullptr)
	{
		m_GraphicsPipeline = new VansVKGraphicsPipeline();
	}

	bool result = m_GraphicsPipeline->CreateGraphicsPipelineInfo(logic_device, m_GraphicsPipelineCreateInfo, global_state_data, m_VkGraphicsPipelineCreateInfo);
	if (!result)
	{
		std::cout << "create graphics pipeline info failed" << std::endl;
		return false;
	}
	return m_GraphicsPipeline->CreateGraphicsPipeline(logic_device, m_VkGraphicsPipelineCreateInfo);
}


VansVulkan::VansVKComputePipeline* VansVulkan::VansComputeShader::GetComputePipeline(VkDevice& logic_device, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts)
{
	if (m_ComputePipeline != VK_NULL_HANDLE)
	{
		return m_ComputePipeline;
	}
	bool result = CreateComputePipeline(logic_device, descriptorset_layouts);
	if (!result)
	{
		std::cout << "create compute pipeline failed" << std::endl;
		return NULL;
	}
	return m_ComputePipeline;
}

bool VansVulkan::VansComputeShader::CreateComputePipeline(VkDevice& logic_device, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts)
{
	if (m_ComputePipeline == nullptr)
	{
		m_ComputePipeline = new VansVKComputePipeline();
	}

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

	return m_ComputePipeline->CreateComputePipeline(logic_device, compute_shader_stage, VK_NULL_HANDLE, descriptorset_layouts, pushConstRangeCount, pushConstRangePtr);
}

