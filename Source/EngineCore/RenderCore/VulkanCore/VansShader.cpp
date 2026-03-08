#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansPipeline.h"
#include "VansShader.h"
#include "VansVKCommandBuffer.h"
#include "VansVKDescriptorManager.h"
#include "VansVKDevice.h"

#include "../../Util/VansFileUtil.h"
#include "../../Util/VansLog.h"
//#include "spirv_cross/spirv_cross.hpp"
//#include "spirv_cross/spirv_glsl.hpp"

#include <iostream>
#include <cstdlib>

//static std::vector<uint32_t> ToUint32Words(const std::vector<unsigned char>& bytes)
//{
//	if (bytes.size() % sizeof(uint32_t) != 0)
//		throw std::runtime_error("SPIR-V size is not a multiple of 4 bytes");
//	std::vector<uint32_t> words(bytes.size() / sizeof(uint32_t));
//	memcpy(words.data(), bytes.data(), bytes.size());
//	return words;
//}
//
//void ReflectShaderResources(const std::vector<uint32_t>& spirv_words)
//{
//	if (spirv_words.empty() || spirv_words[0] != 0x07230203u) 
//	{
//		std::cerr << "Invalid SPIR-V (bad magic/empty)\n";
//		return;
//	}
//
//	// 1. Initialize SPIRV-Cross
//	spirv_cross::Compiler compiler(spirv_words);
//
//	// 2. Get a list of all shader resources
//	spirv_cross::ShaderResources resources = compiler.get_shader_resources();
//
//	//std::cout << "--- Uniform Buffers ---" << std::endl;
//	//for (const auto& resource : resources.uniform_buffers) 
//	//{
//	//	// Get resource info
//	//	std::string name = compiler.get_name(resource.id);
//	//	uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
//	//	uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
//
//	//	// Get buffer size and member info
//	//	const auto& type = compiler.get_type(resource.base_type_id);
//	//	size_t buffer_size = compiler.get_declared_struct_size(type);
//
//	//	std::cout << "UBO: " << name
//	//		<< " | Set: " << set
//	//		<< " | Binding: " << binding
//	//		<< " | Size: " << buffer_size << " bytes" << std::endl;
//	//}
//
//	//std::cout << "\n--- Sampled Images (Textures) ---" << std::endl;
//	//for (const auto& resource : resources.sampled_images) 
//	//{
//	//	std::string name = compiler.get_name(resource.id);
//	//	uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
//	//	uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
//
//	//	std::cout << "Texture: " << name
//	//		<< " | Set: " << set
//	//		<< " | Binding: " << binding << std::endl;
//	//}
//
//	//std::cout << "\n--- Stage Outputs ---" << std::endl;
//	//for (const auto& resource : resources.stage_outputs) 
//	//{
//	//	std::string name = compiler.get_name(resource.id);
//	//	uint32_t location = compiler.get_decoration(resource.id, spv::DecorationLocation);
//
//	//	std::cout << "Output: " << name
//	//		<< " | Location: " << location << std::endl;
//	//}
//}


bool VansGraphics::VansShader::InitShader(VkDevice& logic_device, const std::string& shader_folder)
{
	m_ShaderFolder = shader_folder;

	//如果是延迟管线需要切换使用的shader
	bool supportDeferred = SwitchToDeferredShaderPath(m_ShaderFolder);
	
	m_SupportMRTOutput = supportDeferred;

	bool result = TranslateToSPIRV(m_ShaderFolder);
	if (!result)
	{
		VANS_LOG_ERROR("shader translation failed");
		return false;
	}

	result = CreateShaderModule(logic_device);
	if (!result)
	{
		VANS_LOG_ERROR("create shader module failed");
		return false;
	}

	m_LogicDevice = logic_device;

	m_PushConstantSize = 0;

	m_PushConstantData = nullptr;

	return true;
}

bool VansGraphics::VansShader::RefreshShaderMoudle()
{
	bool result = TranslateToSPIRV(m_ShaderFolder);
	if (!result)
	{
		VANS_LOG_ERROR("shader translation failed");
		return false;
	}

	result = CreateShaderModule(m_LogicDevice);
	if (!result)
	{
		VANS_LOG_ERROR("create shader module failed");
		return false;
	}

	return true;
}

bool VansGraphics::VansShader::InitRayTracingShader(VkDevice& logic_device, const std::string& shader_folder)
{
	std::string shader_folder_string = shader_folder;
	bool result = TranslateToSPIRV(shader_folder_string, ShaderType::RayTracing);
	if (!result)
	{
		VANS_LOG_ERROR("shader translation failed");
		return false;
	}
	VANS_LOG("before ray tracing create shader module");
	result = CreateShaderModule(logic_device);
	if (!result)
	{
		VANS_LOG_ERROR("create shader module failed");
		return false;
	}
	m_LogicDevice = logic_device;

	m_PushConstantSize = 0;

	m_PushConstantData = nullptr;
	return true;
}

bool VansGraphics::VansShader::CheckRefreshShader(VkDevice& logic_device)
{
	return false;
}



bool VansGraphics::VansShader::TranslateToSPIRV(const std::string& shader_folder, ShaderType shaderType)
{
	//根据路径下文件后缀先读取源glsl
	std::vector<std::string> shader_files = GetFilesInFolder(shader_folder);

	if (shader_files.size() == 0)
	{
		VANS_LOG_WARN("no shader files found:" << shader_folder);
		return false;
	}
	m_ShaderModuleDataMap.clear();

	//根据文件后缀找到对应的shader类型生成module data
	for (auto& shader_file : shader_files)
	{
		std::string shader_type = GetFileExtension(shader_file);
		if (shader_type == "spv")
		{
			continue;
		}

		switch (shaderType)
		{
		case VansGraphics::Normal:
			{
				auto shader_type_iter = m_ShaderTypeMap.find(shader_type);
				if (shader_type_iter == m_ShaderTypeMap.end())
				{
					VANS_LOG_WARN("unknown shader type");
					return false;
				}
				VkShaderStageFlagBits shader_stage = shader_type_iter->second;
				ShaderModuleData shader_module_data;
				shader_module_data.m_ShaderType = shader_type;
				shader_module_data.m_ShaderTextResourceFileName = shader_folder + "\\" + shader_file;
				m_ShaderModuleDataMap[shader_stage] = shader_module_data;
			}
			break;
		case VansGraphics::RayTracing:
			{
				auto shader_type_iter = m_RayTracingShaderTypeMap.find(shader_type);
				if (shader_type_iter == m_RayTracingShaderTypeMap.end())
				{
					VANS_LOG_WARN("unknown shader type");
					return false;
				}
				VkShaderStageFlagBits shader_stage = shader_type_iter->second;
				ShaderModuleData shader_module_data;
				shader_module_data.m_ShaderType = shader_type;
				shader_module_data.m_ShaderTextResourceFileName = shader_folder + "\\" + shader_file;
				m_ShaderModuleDataMap[shader_stage] = shader_module_data;
			}

			break;
		default:
			break;
		}
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
		shader_command += " --target-env vulkan1.2";
		int result = system(shader_command.c_str());

		if (result == 0) 
		{
			VANS_LOG("glslangValidator pass " << shader_module_data.first);
		}
		else 
		{
			VANS_LOG_ERROR("glslangValidator failed");
			return false;
		}

		//读取spirv文件
		ReadFile(spirv_file_name, shader_module_data.second.m_ShaderSPIRVCode);
		if (shader_module_data.second.m_ShaderSPIRVCode.empty())
		{
			VANS_LOG_ERROR("read spirv file failed");
			return false;
		}
	}
	return true;
}

void VansGraphics::VansShader::DestroyShaderMoulde()
{
	//编译data map destroy shader module data
	for (auto& shader_module_data : m_ShaderModuleDataMap)
	{
		vkDestroyShaderModule(m_LogicDevice, shader_module_data.second.m_ShaderModule, nullptr);
	}
}

bool VansGraphics::VansShader::CreateShaderModule(VkDevice& logic_device)
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
			VANS_LOG_ERROR("Could not create a shader module.");
			return false;
		}

		//// Convert bytes -> words (aligned)
		//std::vector<uint32_t> spirv_words = ToUint32Words(shader_module_data.second.m_ShaderSPIRVCode);
		//ReflectShaderResources(spirv_words);
	}
	return true;
}

VansGraphics::VansVKGraphicsPipeline* VansGraphics::VansGraphicsShader::GetGraphicsPipeline(VkDevice& logic_device, GlobalStateData& global_state_data,const std::vector<VkDescriptorSetLayout>& descriptorset_layouts)
{
	if (m_GraphicsPipeline != nullptr)
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
		VANS_LOG_ERROR("create graphics pipeline failed");
		return NULL;
	}
	return m_GraphicsPipeline;
}

void VansGraphics::VansGraphicsShader::SetDrawStateData(VkBool32 depthTestEnable, VkBool32 depthWriteEnable, VkCompareOp depthCompareOp, VkCullModeFlags cullmode)
{
	m_DrawStateData.depthTestEnable = depthTestEnable;
	m_DrawStateData.depthWriteEnable = depthWriteEnable;
	m_DrawStateData.depthCompareOp = depthCompareOp;
	m_DrawStateData.cullMode = cullmode;
}

void VansGraphics::VansGraphicsShader::SetPolygonMode(VkPolygonMode mode)
{
	m_DrawStateData.polygonMode = mode;
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
	
	InitAttachmentBlendStates(m_GraphicsPipelineCreateInfo.attachment_blend_states, enableDeferred);
}

bool VansGraphics::VansGraphicsShader::CreateGraphicsPipeline(VkDevice& logic_device, GlobalStateData& global_state_data)
{
	if (m_GraphicsPipeline == nullptr)
	{
		m_GraphicsPipeline = new VansVKGraphicsPipeline();
	}

	bool result = m_GraphicsPipeline->CreateGraphicsPipelineInfo(logic_device, m_GraphicsPipelineCreateInfo, global_state_data, m_VkGraphicsPipelineCreateInfo);
	if (!result)
	{
		VANS_LOG_ERROR("create graphics pipeline info failed");
		return false;
	}
	return m_GraphicsPipeline->CreateGraphicsPipeline(logic_device, m_VkGraphicsPipelineCreateInfo);
}

void VansGraphics::VansGraphicsShader::TriggerReCreateGraphicsPipeline()
{
	if (m_GraphicsPipeline != nullptr)
	{
		delete m_GraphicsPipeline;
		m_GraphicsPipeline = nullptr;
	}
	m_GraphicsPipelineCreateInfo.Clear();
}


VansGraphics::VansVKComputePipeline* VansGraphics::VansComputeShader::GetComputePipeline(VkDevice& logic_device, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts)
{
	if (m_ComputePipeline != VK_NULL_HANDLE)
	{
		return m_ComputePipeline;
	}
	bool result = CreateComputePipeline(logic_device, descriptorset_layouts);
	if (!result)
	{
		VANS_LOG_ERROR("create compute pipeline failed");
		return NULL;
	}
	return m_ComputePipeline;
}

bool VansGraphics::VansComputeShader::CreateComputePipeline(VkDevice& logic_device, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts)
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

VansGraphics::VansVKRayTracingPipeline* VansGraphics::VansRayTracingShader::GetRayTracingPipeline(VansVKDevice* device, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts)
{
	m_LogicDevice = device->GetLogicDevice();
	if (m_VansVkRayTracingPipeline != VK_NULL_HANDLE)
	{
		return m_VansVkRayTracingPipeline;
	}

	bool result = CreateRayTracingPipeline(m_LogicDevice, descriptorset_layouts);
	if (!result)
	{
		VANS_LOG_ERROR("create raytracing pipeline failed");
		return NULL;
	}

	//创建并设置SBT
	CreateShaderBindingTable(device);

	return m_VansVkRayTracingPipeline;
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
	VkResult result = vkGetRayTracingShaderGroupHandlesKHR(
		m_LogicDevice,
		m_VansVkRayTracingPipeline->m_RayTracingPipeline,
		0,
		groupCount,
		groupCount * handleSize,
		handles.data());

	uint8_t* handleData = handles.data();
	int offset = 0;
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

	// 设置SBT区域
	VkBufferDeviceAddressInfoKHR addressInfo{};
	addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR;
	addressInfo.buffer = m_SBTBuffer.GetNativeBuffer();
	addressInfo.pNext = nullptr;
	VkDeviceAddress sbtAddress = vkGetBufferDeviceAddressKHR(m_LogicDevice, &addressInfo);

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
	if (m_VansVkRayTracingPipeline == nullptr)
	{
		m_VansVkRayTracingPipeline = new VansVKRayTracingPipeline();
	}

	std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroupCreateInfo;
	std::vector<VkPipelineShaderStageCreateInfo> rayTracingStages;

	//创建着色器组
	int shaderStageInfoIndex = 0;
	for (auto mapIt = m_RayTracingShaderTypeMap.begin(); mapIt != m_RayTracingShaderTypeMap.end(); mapIt++)
	{
		auto stageBit = mapIt->second;
		auto it = m_ShaderModuleDataMap.find(stageBit);
		if (it != m_ShaderModuleDataMap.end())
		{
			VkPipelineShaderStageCreateInfo stage{};
			stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stage.stage = stageBit;
			stage.module = it->second.m_ShaderModule;
			stage.pName = "main";

			rayTracingStages.push_back(stage);

			VkRayTracingShaderGroupCreateInfoKHR group{};
			group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
			group.pNext = nullptr;
			group.generalShader = VK_SHADER_UNUSED_KHR;
			group.closestHitShader = VK_SHADER_UNUSED_KHR;
			group.anyHitShader = VK_SHADER_UNUSED_KHR;
			group.intersectionShader = VK_SHADER_UNUSED_KHR;

			switch (stageBit)
			{
			case VK_SHADER_STAGE_RAYGEN_BIT_KHR:
				group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
				group.generalShader = shaderStageInfoIndex;
				break;
			case VK_SHADER_STAGE_ANY_HIT_BIT_KHR:
				group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
				group.anyHitShader = shaderStageInfoIndex;
				break;
			case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
				group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
				group.closestHitShader = shaderStageInfoIndex;
				break;
			case VK_SHADER_STAGE_MISS_BIT_KHR:
				group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
				group.generalShader = shaderStageInfoIndex;
				break;
			case VK_SHADER_STAGE_INTERSECTION_BIT_KHR:
				group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
				group.intersectionShader = shaderStageInfoIndex;
				break;
			}
			group.pShaderGroupCaptureReplayHandle = nullptr;
			shaderGroupCreateInfo.push_back(group);

			shaderStageInfoIndex++;
		}
	}

	int pushConstRangeCount = 0;
	VkPushConstantRange* pushConstRangePtr = nullptr;
	VkPushConstantRange pushConstantRange = {};
	if (m_PushConstantSize > 0)
	{
		pushConstantRange.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
		pushConstantRange.offset = 0;
		pushConstantRange.size = m_PushConstantSize;
		pushConstRangePtr = &pushConstantRange;
		pushConstRangeCount = 1;
	}
	return m_VansVkRayTracingPipeline->CreateRayTracingPipeline(logic_device, shaderGroupCreateInfo, rayTracingStages, VK_NULL_HANDLE, descriptorset_layouts, pushConstRangeCount, pushConstRangePtr);
}
