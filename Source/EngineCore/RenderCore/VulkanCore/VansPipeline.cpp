#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansPipeline.h"
#include "VansVKDescriptorManager.h"
#include <iostream>

bool VansVulkan::VansVKGraphicsPipeline::CreateGraphicsPipelineInfo(VkDevice& logic_device, GraphicsPipeCreateInfo& create_info, GlobalStateData& global_state_data, VkGraphicsPipelineCreateInfo& final_create_info)
{
	shader_stage_create_infos.clear();
	for (auto& shader_stage : create_info.shader_stage_params)
	{
		shader_stage_create_infos.push_back(
			{
				VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				nullptr,
				0,
				shader_stage.ShaderStage,
				shader_stage.ShaderModule,
				shader_stage.EntryPointName,
				shader_stage.SpecializationInfo
			});
	}

	/*
	* shader moudle在编译完之后应该释放，之后不再需要
	if( VK_NULL_HANDLE != shader_module ) {
	 vkDestroyShaderModule( logical_device, shader_module, nullptr );
	 shader_module = VK_NULL_HANDLE;
	}
	*/

	//参考：https://easyvulkan.github.io/Ch3-3%20%E7%AE%A1%E7%BA%BF%E5%B8%83%E5%B1%80%E5%92%8C%E7%AE%A1%E7%BA%BF.html
	//设置vertex binding
	//binding 的描述，指定binding的位置，memory layout,以及如何读取
	//VK_VERTEX_INPUT_RATE_VERTEX specifies that vertex attribute addressing is a function of the vertex index.
	//VK_VERTEX_INPUT_RATE_INSTANCE specifies that vertex attribute addressing is a function of the instance index//This binding is used as a numbered source of data for vertex attributes. We can use at
	//least 16 separate bindings to which we can bind separate vertex buffers or different parts of
	//memory of the same buffer.
	//意思就是我们可以指定几个vettexbuffer的绑定，以及没给顶点的stride
	//这里的bind和sheder无关，attrribute中才设计shader,这里只使用了bind0
	binding_descriptions = 
	{
		*global_state_data.vertexInputBindingDescription
	};
	// 
	//设置attributer的描述，处于哪个binding,哪个location，以及格式和偏移
	attribute_descriptions = *global_state_data.vertexInputAttributeDescriptions;
	
	vertex_input_state_create_info =
	{
		 VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		 nullptr,
		 0,
		 static_cast<uint32_t>(binding_descriptions.size()),
		 binding_descriptions.data(),
		 static_cast<uint32_t>(attribute_descriptions.size()),
		 attribute_descriptions.data()
	};

	//设置输入组装
	input_assembly_state_create_info = create_info.input_assembly_state_create_info;


	/*
	*	We can't use list primitives with a primitive restart option.
		Primitives with adjacency can only be used with geometry shaders. For this to
		work correctly, a geometryShader feature must be enabled during the logical
		device creation.
		When we want to use tessellation shaders, we can only use patch primitives. In
		addition, we also need to remember that a tessellationShader feature must
		be enabled during the logical device creation.
	*/

	tessellation_state_create_info = create_info.tessellation_state_create_info;


	//viewport & scissor
	viewport_infos =
	{
		{
			global_state_data.viewport
		},
		{
			global_state_data.scissor
		}
	};
	uint32_t viewport_count =
		static_cast<uint32_t>(viewport_infos.Viewports.size());
	uint32_t scissor_count =
		static_cast<uint32_t>(viewport_infos.Scissors.size());
	viewport_state_create_info = 
	{
		 VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		 nullptr,
		 0,
		 viewport_count,
		 viewport_infos.Viewports.data(),
		 scissor_count,
		 viewport_infos.Scissors.data()
	};

	//rasterization
	//1. depth calmp： 和viewport中设置的depthminmax对应，超过范围被限制,如歌clamp关闭，超过范围的像素被discard
	//2. rasterizerDiscard： 是否丢弃片段
	//3. polygonMode： 填充模式
	//4. cullMode： 剔除模式
	//5. frontFace： 正面是顺时针还是逆时针
	//6. depthBiasEnable： 是否开启深度偏移
	//7. depthBiasConstantFactor： 偏移常量
	rasterization_state_create_info = create_info.rasterization_state_create_info;


	//A coverage mask is generated for each fragment, based on which samples within that fragment are determined to be within the area of the primitive that generated the fragment
	//alpha_to_coverage_enable：只是用alpha去影响coverage ,这样resolve的时候就能混到别的颜色，产生半透的效果
	//https://easyvulkan.github.io/Ch3-3%20%E7%AE%A1%E7%BA%BF%E5%B8%83%E5%B1%80%E5%92%8C%E7%AE%A1%E7%BA%BF.html
	/*
	Sample shading（采样点着色）对一个像素中的多个采样点执行片段着色。
不开启sample shading时，只会在计算像素的coverage mask（覆盖遮罩，见下文）时选取多个采样点，片段着色器仍旧只对每个像素执行一次（虽然Vulkan标准中没有规定，但通常如此）。因为只有图元边缘的像素可能不被图元完全覆盖，最终只有图元边缘呈现出抗锯齿的效果。
开启sample shading后，选取像素内部一定数量的采样点调用片段着色器，最少的着色次数为minSampleShading * 采样点个数，minSampleShading为1时，即超采样（SSAA）。
注意无论片段着色器对每个像素执行几次，每个像素的采样点个数都是确定的。

VkSampleMask实质上就是uint32_t。Sample mask的比特与采样点一一对应，因为至多64个采样点，pSampleMask所指数组中至多两个元素。
在多重采样中，栅格化时会计算采样点是否被图元覆盖，得到初始的coverage mask。在4xMSAA中，四个采样点皆被图元覆盖时的初始coverage mask为0b1111。
此处指定的sample mask会与coverage mask做位与，这一步叫做sample mask test，发生在栅格化阶段。显然，若在此处把某个索引的采样点对应的bit指定为0，就意味着直接把该索引的采样点一概算作不被覆盖了。
在执行片段着色器前，无论是否经历sample mask test，若一个片段（非MSAA的像素，或MSAA中包含多个采样点的像素）的所有采样点coverage mask为0，则该片段被丢弃，不进行后续其他操作。
	*/
	/*
	若alphaToCoverageEnable为VK_TRUE，则会在执行片段着色器后，根据输出的各个采样点的A通道值，生成一个遮罩与片段着色器后的coverage mask（应用可能发生的sample mask test及片段着色器中可能输出的gl_SampleMask后得到的结果）做位与，A通道值到遮罩的转换是实现特定的，标准仅规定A通道为0时生成的遮罩为0。
	若alphaToOneEnable为VK_TRUE，则会在执行片段着色器后，无视输出颜色的A通道，使得采样点的A通道值为1，这可以在alphaToCoverageEnable为VK_TRUE时使像素的A通道值不至于过低。
	【意思就是这个可能会影响coverage的数量，alpha约小，coverage就可能被砍的更小】
	注意，虽然名称很相似，alphaToCoverageEnable影响coverage mask，而alphaToOneEnable影响A通道。Alpha to coverage的效果在alpha to one前应用，即alpha to one使得采样点A通道值为1并不影响coverage mask。
	*/
	multisample_state_create_info = create_info.multisample_state_create_info;


	//模板测试
	VkStencilOpState stencil_test_parameters = create_info.stencil_test_parameters;

	depth_and_stencil_state_create_info = create_info.depth_and_stencil_state_create_info;

	//每个attachemtn的blend state
	//blend除了做混合，还支持逻辑操作
	blend_state_create_info = 
	{
		 VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		 nullptr,
		 0,
		 false,
		 VK_LOGIC_OP_COPY,
		 static_cast<uint32_t>(create_info.attachment_blend_states.size()),
		 create_info.attachment_blend_states.data(),
		 //颜色mask
		 {
			 1.0f,
			 1.0f,
			 1.0f,
			 1.0f
		 }
	};

	//支持dynamic state，可以支持pipeline中部分参数是可以动态设置的，否则需要重建pipeline
	//但是不是很多状态
	//VK_DYNAMIC_STATE_VIEWPORT
	//VK_DYNAMIC_STATE_SCISSOR
	//	VK_DYNAMIC_STATE_LINE_WIDTH
	//	VK_DYNAMIC_STATE_DEPTH_BIAS
	//	VK_DYNAMIC_STATE_BLEND_CONSTANTS
	//	VK_DYNAMIC_STATE_DEPTH_BOUNDS
	//	VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK
	//	VK_DYNAMIC_STATE_STENCIL_WRITE_MASK
	//	VK_DYNAMIC_STATE_STENCIL_REFERENCE
	//激活后，就可以通过vkCmdSetxxx来动态设置
	dynamic_states =
	{
		 VK_DYNAMIC_STATE_VIEWPORT,
		 VK_DYNAMIC_STATE_SCISSOR
	};
	dynamic_state_creat_info = 
	{
		 VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		 nullptr,
		 0,
		 static_cast<uint32_t>(dynamic_states.size()),
		 dynamic_states.data()
	};

	//pipe line和descriptor关联
	VkPipelineLayoutCreateInfo pipeline_layout_create_info = 
	{
		 VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		 nullptr,
		 0,
		 create_info.descriptorset_layouts.size(),
		 create_info.descriptorset_layouts.data(),
		 0,
		 nullptr
	};
	VkResult vresult = vkCreatePipelineLayout(logic_device, &pipeline_layout_create_info, nullptr, &m_VansPipelineLayout);
	if (vresult != VK_SUCCESS)
	{
		std::cerr << "create pipeline layout failed" << std::endl;
		return false;
	}

	//创建pipeline
	//为了快速创建pipeline，有一个parent pipeline的机制，创建时进行指定子pipeline或者父pipeline
	VkGraphicsPipelineCreateInfo pipeline_create_info =
	{
		 VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		 nullptr,
		 VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT,
		 static_cast<uint32_t>(shader_stage_create_infos.size()),
		 shader_stage_create_infos.data(),
		 &vertex_input_state_create_info,
		 &input_assembly_state_create_info,
		 &tessellation_state_create_info,
		 &viewport_state_create_info,
		 &rasterization_state_create_info,
		 &multisample_state_create_info,
		 &depth_and_stencil_state_create_info,
		 &blend_state_create_info,
		 &dynamic_state_creat_info,
		 m_VansPipelineLayout,
		 global_state_data.currentRenderPass,
		 //指定这个pipeline处于pass中的第几个subpass
		 global_state_data.currentSubpass,
		 VK_NULL_HANDLE,
		 -1
	};
	final_create_info = pipeline_create_info;
	return true;
}

bool VansVulkan::VansVKGraphicsPipeline::CreateGraphicsPipeline(VkDevice& logic_device, const VkGraphicsPipelineCreateInfo& create_info)
{
	m_Device = logic_device;
	/*
	创建一个没有初始数据的VkPipelineCache，在创建管线时传入其handle，Vulkan的实现会向其写入管线的缓存信息。
	如果想在下一次启动时加快创建管线的速度，那么将缓存信息存到文件，在下次启动时读取。
	缓存信息的头部信息（前32位）用于验证管线缓存是否满足显卡驱动的要求，以应对多显卡PC
	所以create的时候传入一个cache，会进行写入操作，也会读取这里里面的数据，进行加速（前提是之前已经初始化过）
	*/

	VkResult result = vkCreateGraphicsPipelines(
		logic_device, 
		m_PipelineCache,
		1,
		&create_info, nullptr, &m_GraphicsPipeline);
	if (VK_SUCCESS != result) 
	{
		std::cout << "Could not create a graphics pipeline." << std::endl;
		return false;
	}
	return true;
}

bool VansVulkan::VansVKGraphicsPipeline::CreatePipelineCache(VkDevice& logic_device)
{
	std::vector<unsigned char>  cache_data;

	//创建pipeline cache避免重复创建
	VkPipelineCacheCreateInfo pipeline_cache_create_info =
	{
		 VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
		 nullptr,
		 0,
		 cache_data.size(),
		 cache_data.data()
	};

	VkResult result = vkCreatePipelineCache(logic_device, &pipeline_cache_create_info, nullptr, &m_PipelineCache);
	if (VK_SUCCESS != result) 
	{
		std::cout << "Could not create pipeline cache." << std::endl;
		return false;
	}
	return true;
}

bool VansVulkan::VansVKGraphicsPipeline::GetPipelineCacheData(VkDevice& logic_device)
{
	std::vector<unsigned char> pipeline_cache_data;
	size_t data_size = 0;
	VkResult result = VK_SUCCESS;
	result = vkGetPipelineCacheData(logic_device, m_PipelineCache, &data_size, nullptr);
	if ((VK_SUCCESS != result) ||(0 == data_size)) 
	{
		std::cout << "Could not get the size of the pipeline cache." <<
			std::endl;
		return false;
	}
	pipeline_cache_data.resize(data_size);

	result = vkGetPipelineCacheData(logic_device, m_PipelineCache, &data_size, pipeline_cache_data.data());
	if ((VK_SUCCESS != result) || (0 == data_size)) 
	{
		std::cout << "Could not acquire pipeline cache data." << std::endl;
		return false;
	}
	return true;
}

void VansVulkan::VansVKGraphicsPipeline::BindGraphicsPipeline(VkCommandBuffer& command_buffer)
{
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);
}

void VansVulkan::VansVKGraphicsPipeline::DestroyPipeline(VkDevice& logic_device)
{
	if (VK_NULL_HANDLE != m_GraphicsPipeline)
	{
		vkDestroyPipeline(logic_device, m_GraphicsPipeline, nullptr);
		m_GraphicsPipeline = VK_NULL_HANDLE;
	}
}

void VansVulkan::VansVKGraphicsPipeline::DestroyPipelineCache(VkDevice& logic_device)
{
	if (VK_NULL_HANDLE != m_PipelineCache)
	{
		vkDestroyPipelineCache(logic_device, m_PipelineCache, nullptr);
		m_PipelineCache = VK_NULL_HANDLE;
	}
}

void VansVulkan::VansVKGraphicsPipeline::DestroyPipelineLayout(VkDevice& logic_device)
{
	if (VK_NULL_HANDLE != m_VansPipelineLayout)
	{
		vkDestroyPipelineLayout(logic_device, m_VansPipelineLayout, nullptr);
		m_VansPipelineLayout = VK_NULL_HANDLE;
	}
}

bool VansVulkan::VansVKGraphicsPipeline::MergePipelineCache(VkDevice& logic_device, std::vector<VkPipelineCache>& source_pipeline_caches, VkPipelineCache& merged_cache)
{
	VkResult result = vkMergePipelineCaches(logic_device, merged_cache, static_cast<uint32_t>(source_pipeline_caches.size()), source_pipeline_caches.data());
	if (VK_SUCCESS != result) 
	{
		std::cout << "Could not merge pipeline cache objects." << std::endl;
		return false;
	}
	return true;
}

bool VansVulkan::VansVKComputePipeline::CreateComputePipeline(VkDevice& logic_device, VkPipelineShaderStageCreateInfo& compute_shader_stage, const VkPipelineCache& pipeline_cache, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts)
{
	m_Device = logic_device;
	VkPipelineCreateFlags additional_options = VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT;
	VkPipelineLayoutCreateInfo pipeline_layout_create_info =
	{
		 VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		 nullptr,
		 0,
		 descriptorset_layouts.size(),
		 descriptorset_layouts.data(),
		 0,
		 nullptr
	};
	VkResult result = vkCreatePipelineLayout(logic_device, &pipeline_layout_create_info, nullptr, &m_VansPipelineLayout);
	if (result != VK_SUCCESS)
	{
		std::cerr << "create pipeline layout failed" << std::endl;
		return false;
	}
	VkComputePipelineCreateInfo compute_pipeline_create_info = 
	{
		 VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		 nullptr,
		 additional_options,
		 compute_shader_stage,
		 m_VansPipelineLayout,
		 VK_NULL_HANDLE,
		 -1
	};
	result = vkCreateComputePipelines(logic_device, pipeline_cache,1, &compute_pipeline_create_info, nullptr, &m_ComputePipeline);
	if (VK_SUCCESS != result) 
	{
		std::cout << "Could not create compute pipeline." << std::endl;
		return false;
	}
	return true;
}

void VansVulkan::VansVKComputePipeline::BindComputePipeline(VkCommandBuffer& command_buffer)
{
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputePipeline);
}

void VansVulkan::VansVKComputePipeline::DestroyPipeline(VkDevice& logic_device)
{
	if (VK_NULL_HANDLE != m_ComputePipeline)
	{
		vkDestroyPipeline(logic_device, m_ComputePipeline, nullptr);
		m_ComputePipeline = VK_NULL_HANDLE;
	}
}

void VansVulkan::VansVKComputePipeline::DestroyPipelineLayout(VkDevice& logic_device)
{
	if (VK_NULL_HANDLE != m_VansPipelineLayout)
	{
		vkDestroyPipelineLayout(logic_device, m_VansPipelineLayout, nullptr);
		m_VansPipelineLayout = VK_NULL_HANDLE;
	}
}

void VansVulkan::VansVKComputePipeline::DispatchCompute(VkCommandBuffer& command_buffer, int x, int y, int z)
{
	vkCmdDispatch(command_buffer, x, y, z);
}
