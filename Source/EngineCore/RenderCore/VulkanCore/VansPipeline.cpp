#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansPipeline.h"
#include "VansPipelineCacheService.h"
#include "VansVKDescriptorManager.h"
#include "../../Util/VansLog.h"
#include <iostream>

bool VansGraphics::VansVKGraphicsPipeline::CreateGraphicsPipelineInfo(VkDevice& logic_device, GraphicsPipeCreateInfo& create_info, GlobalStateData& global_state_data, VkGraphicsPipelineCreateInfo& final_create_info)
{
	const uint32_t vertexBindingCount = global_state_data.vertexInputBindingDescriptions
		? static_cast<uint32_t>(global_state_data.vertexInputBindingDescriptions->size())
		: 0;
	const uint32_t vertexAttributeCount = global_state_data.vertexInputAttributeDescriptions
		? static_cast<uint32_t>(global_state_data.vertexInputAttributeDescriptions->size())
		: 0;
	VansPipelineRuntimeDesc runtimeDesc = VansPipelineDescriptorBuilder::BuildRuntimeDesc(
		global_state_data.currentRenderPass,
		global_state_data.currentSubpass,
		create_info.descriptorset_layouts,
		static_cast<uint32_t>(create_info.push_constant_size),
		vertexBindingCount,
		vertexAttributeCount,
		global_state_data.rasterizationSamples,
		global_state_data.sampleShadingEnable);
	if (global_state_data.vertexInputBindingDescriptions)
		runtimeDesc.vertexBindings = *global_state_data.vertexInputBindingDescriptions;
	if (global_state_data.vertexInputAttributeDescriptions)
		runtimeDesc.vertexAttributes = *global_state_data.vertexInputAttributeDescriptions;
	m_DescriptorKey = VansPipelineDescriptorBuilder::BuildPipelineKey(
		create_info.pipeline_program_desc,
		runtimeDesc);

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
	 VansGraphics::vkDestroyShaderModule( logical_device, shader_module, nullptr );
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
	// If vertex input pointers are nullptr, use empty arrays (e.g. fullscreen shaders
	// that fetch all data from SSBOs and have no vertex attributes).
	if (global_state_data.vertexInputBindingDescriptions)
		binding_descriptions = *global_state_data.vertexInputBindingDescriptions;
	else
		binding_descriptions.clear();

	// 
	//设置attributer的描述，处于哪个binding,哪个location，以及格式和偏移
	if (global_state_data.vertexInputAttributeDescriptions)
		attribute_descriptions = *global_state_data.vertexInputAttributeDescriptions;
	else
		attribute_descriptions.clear();
	
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
		 create_info.attachment_blend_states.empty() ? nullptr : create_info.attachment_blend_states.data(),
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

	uint32_t pushConstantRangeCount = 0;
	VkPushConstantRange pushConstantRange = {};
	VkPushConstantRange* pushConstantRangeData = nullptr;
	if (create_info.push_constant_size>0)
	{
		pushConstantRangeCount = 1;
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = create_info.push_constant_size;
		pushConstantRangeData = &pushConstantRange;
	}

	//pipe line和descriptor关联
	VkPipelineLayoutCreateInfo pipeline_layout_create_info = 
	{
		 VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		 nullptr,
		 0,
		 static_cast<uint32_t>(create_info.descriptorset_layouts.size()),
		 create_info.descriptorset_layouts.data(),
		 pushConstantRangeCount,
		 pushConstantRangeData
	};
	VkResult vresult = VansGraphics::vkCreatePipelineLayout(logic_device, &pipeline_layout_create_info, nullptr, &m_VansPipelineLayout);
	if (vresult != VK_SUCCESS)
	{
		VANS_LOG_ERROR("create pipeline layout failed");
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

bool VansGraphics::VansVKGraphicsPipeline::CreateGraphicsPipeline(VkDevice& logic_device, const VkGraphicsPipelineCreateInfo& create_info)
{
	m_Device = logic_device;
	auto cacheAccess = VansPipelineCacheService::AcquireForDevice(logic_device);

	VkResult result = VansGraphics::vkCreateGraphicsPipelines(
		logic_device, 
		cacheAccess.GetHandle(),
		1,
		&create_info, nullptr, &m_GraphicsPipeline);
	if (VK_SUCCESS != result) 
	{
		VANS_LOG_ERROR("Could not create a graphics pipeline.");
		return false;
	}
	cacheAccess.NotifyPipelineCreated(VansPipelineCachePipelineKind::Graphics);
	return true;
}

void VansGraphics::VansVKGraphicsPipeline::BindGraphicsPipeline(VkCommandBuffer& command_buffer)
{
	VansGraphics::vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);
}

void VansGraphics::VansVKGraphicsPipeline::DestroyPipeline(VkDevice& logic_device)
{
	if (VK_NULL_HANDLE != m_GraphicsPipeline)
	{
		VansGraphics::vkDestroyPipeline(logic_device, m_GraphicsPipeline, nullptr);
		m_GraphicsPipeline = VK_NULL_HANDLE;
	}
}

void VansGraphics::VansVKGraphicsPipeline::DestroyPipelineLayout(VkDevice& logic_device)
{
	if (VK_NULL_HANDLE != m_VansPipelineLayout)
	{
		VansGraphics::vkDestroyPipelineLayout(logic_device, m_VansPipelineLayout, nullptr);
		m_VansPipelineLayout = VK_NULL_HANDLE;
	}
}

bool VansGraphics::VansVKComputePipeline::CreateComputePipeline(VkDevice& logic_device, VkPipelineShaderStageCreateInfo& compute_shader_stage, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts, int pushConstRangeCount, VkPushConstantRange* pushConstRange, const VansPipelineProgramDesc* programDesc)
{
	m_Device = logic_device;
	if (programDesc != nullptr)
	{
		const uint32_t pushConstantSize = pushConstRangeCount > 0 && pushConstRange != nullptr
			? pushConstRange->size
			: 0;
		const VansPipelineRuntimeDesc runtimeDesc = VansPipelineDescriptorBuilder::BuildRuntimeDesc(
			descriptorset_layouts,
			pushConstantSize);
		m_DescriptorKey = VansPipelineDescriptorBuilder::BuildPipelineKey(*programDesc, runtimeDesc);
	}
#if defined(_DEBUG)
	VkPipelineCreateFlags additional_options = static_cast<VkPipelineCreateFlags>(VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT);
#else
	VkPipelineCreateFlags additional_options = 0u;
#endif
	VkPipelineLayoutCreateInfo pipeline_layout_create_info =
	{
		 VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		 nullptr,
		 0,
		 static_cast<uint32_t>(descriptorset_layouts.size()),
		 descriptorset_layouts.data(),
		 static_cast<uint32_t>(pushConstRangeCount),
		 pushConstRange
	};
	VkResult result = VansGraphics::vkCreatePipelineLayout(logic_device, &pipeline_layout_create_info, nullptr, &m_VansPipelineLayout);
	if (result != VK_SUCCESS)
	{
		VANS_LOG_ERROR("create pipeline layout failed");
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
	auto cacheAccess = VansPipelineCacheService::AcquireForDevice(logic_device);
	result = VansGraphics::vkCreateComputePipelines(logic_device, cacheAccess.GetHandle(), 1, &compute_pipeline_create_info, nullptr, &m_ComputePipeline);
	if (VK_SUCCESS != result) 
	{
		VANS_LOG_ERROR("Could not create compute pipeline.");
		return false;
	}
	cacheAccess.NotifyPipelineCreated(VansPipelineCachePipelineKind::Compute);
	return true;
}

void VansGraphics::VansVKComputePipeline::BindComputePipeline(VkCommandBuffer& command_buffer)
{
	VansGraphics::vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputePipeline);
}

void VansGraphics::VansVKComputePipeline::DestroyPipeline(VkDevice& logic_device)
{
	if (VK_NULL_HANDLE != m_ComputePipeline)
	{
		VansGraphics::vkDestroyPipeline(logic_device, m_ComputePipeline, nullptr);
		m_ComputePipeline = VK_NULL_HANDLE;
	}
}

void VansGraphics::VansVKComputePipeline::DestroyPipelineLayout(VkDevice& logic_device)
{
	if (VK_NULL_HANDLE != m_VansPipelineLayout)
	{
		VansGraphics::vkDestroyPipelineLayout(logic_device, m_VansPipelineLayout, nullptr);
		m_VansPipelineLayout = VK_NULL_HANDLE;
	}
}

void VansGraphics::VansVKComputePipeline::DispatchCompute(VkCommandBuffer& command_buffer, int x, int y, int z)
{
	VansGraphics::vkCmdDispatch(command_buffer, x, y, z);
}

bool VansGraphics::VansVKRayTracingPipeline::CreateRayTracingPipeline(VkDevice& logic_device, std::vector<VkRayTracingShaderGroupCreateInfoKHR>& shaderGroupCreateInfo, std::vector<VkPipelineShaderStageCreateInfo>& shaderStageCreateInfo, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts, int pushConstRangeCount, VkPushConstantRange* pushConstRange, const VansPipelineProgramDesc* programDesc)
{
	m_Device = logic_device;
	if (programDesc != nullptr)
	{
		const uint32_t pushConstantSize = pushConstRangeCount > 0 && pushConstRange != nullptr
			? pushConstRange->size
			: 0;
		const VansPipelineRuntimeDesc runtimeDesc = VansPipelineDescriptorBuilder::BuildRuntimeDesc(
			descriptorset_layouts,
			pushConstantSize);
		m_DescriptorKey = VansPipelineDescriptorBuilder::BuildPipelineKey(*programDesc, runtimeDesc);
	}

	//创建管线layout
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorset_layouts.size());
	pipelineLayoutInfo.pSetLayouts = descriptorset_layouts.data();
	pipelineLayoutInfo.pushConstantRangeCount = pushConstRangeCount;
	pipelineLayoutInfo.pPushConstantRanges = pushConstRange;

	VkResult layoutResult = VansGraphics::vkCreatePipelineLayout(logic_device, &pipelineLayoutInfo, nullptr, &m_RayTracingLayout);
	if (VK_SUCCESS != layoutResult)
	{
		VANS_LOG_ERROR("Could not create ray tracing pipeline layout. VkResult: " << layoutResult);
		return false;
	}

	//创建SBT ： shader binding table
	//shader binding table就是一个装了若干shader group handle的buffer , 不同场景下会有不同的shader的组合
	//intersection shader，any hit shader和closest hit shader三个shader是紧密关联在一起的，它们会打包成一个shader group并形成一个handle
	//其它类型的shader(如ray generation和miss)则都是general shader，它们一个shader对应一个shader group

	//创建管线
	VkRayTracingPipelineCreateInfoKHR pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
	pipelineInfo.stageCount = static_cast<uint32_t>(shaderStageCreateInfo.size());
	pipelineInfo.pStages = shaderStageCreateInfo.data();
	pipelineInfo.groupCount = static_cast<uint32_t>(shaderGroupCreateInfo.size());
	pipelineInfo.pGroups = shaderGroupCreateInfo.data();
	pipelineInfo.maxPipelineRayRecursionDepth = 1;
	pipelineInfo.layout = m_RayTracingLayout;

	auto cacheAccess = VansPipelineCacheService::AcquireForDevice(logic_device);
	VkResult result = VansGraphics::vkCreateRayTracingPipelinesKHR(logic_device, VK_NULL_HANDLE, cacheAccess.GetHandle(), 1, &pipelineInfo, nullptr, &m_RayTracingPipeline);
	if (VK_SUCCESS != result)
	{
		VANS_LOG_ERROR("Could not create ray tracing pipeline. VkResult: " << result
			<< " stageCount=" << pipelineInfo.stageCount
			<< " groupCount=" << pipelineInfo.groupCount);
		return false;
	}
	cacheAccess.NotifyPipelineCreated(VansPipelineCachePipelineKind::RayTracing);
	return true;
}

VkResult VansGraphics::VansVKRayTracingPipeline::GetShaderGroupHandles(VkDevice& logic_device, uint32_t firstGroup, uint32_t groupCount, size_t dataSize, void* data) const
{
	return VansGraphics::vkGetRayTracingShaderGroupHandlesKHR(
		logic_device,
		m_RayTracingPipeline,
		firstGroup,
		groupCount,
		dataSize,
		data);
}

void VansGraphics::VansVKRayTracingPipeline::DestroyPipeline(VkDevice& logic_device)
{
	if (VK_NULL_HANDLE != m_RayTracingPipeline)
	{
		VansGraphics::vkDestroyPipeline(logic_device, m_RayTracingPipeline, nullptr);
		m_RayTracingPipeline = VK_NULL_HANDLE;
	}
}

void VansGraphics::VansVKRayTracingPipeline::DestroyPipelineLayout(VkDevice& logic_device)
{
	if (VK_NULL_HANDLE != m_RayTracingLayout)
	{
		VansGraphics::vkDestroyPipelineLayout(logic_device, m_RayTracingLayout, nullptr);
		m_RayTracingLayout = VK_NULL_HANDLE;
	}
}
