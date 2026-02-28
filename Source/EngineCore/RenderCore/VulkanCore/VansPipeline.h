#pragma once
#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif
#include "vulkan/vulkan.h"
#include <vector>

#include "VansVKImage.h"

namespace VansGraphics
{
	struct ViewportInfo 
	{
		std::vector<VkViewport> Viewports;
		std::vector<VkRect2D> Scissors;
	};

	struct ShaderStageParameters
	{
		 VkShaderStageFlagBits ShaderStage;
		 VkShaderModule ShaderModule;
		 char const* EntryPointName;
		 //It allows values of the constant variables defined
		 //in the shader source code to be modified at runtime, during pipeline creation
		 VkSpecializationInfo const* SpecializationInfo;
	};

	//全局的绘制参数
	struct GlobalStateData 
	{
		//multisample
		VkSampleCountFlagBits                   rasterizationSamples;
		//是否支持fs在每个sample位置invocation，shader中可以通过sampleId
		VkBool32                                sampleShadingEnable;
		float                                   minSampleShading;
		VkSampleMask*						    pSampleMask;
		VkRenderPass							currentRenderPass;
		uint32_t								currentSubpass;
		//vb数据
		//attribute data
		std::vector<VkVertexInputAttributeDescription>* vertexInputAttributeDescriptions;
		//vertex bind data
		std::vector<VkVertexInputBindingDescription>* vertexInputBindingDescriptions;
		
		VkViewport								viewport;
		VkRect2D								scissor;
		GlobalStateData() :
			rasterizationSamples(VK_SAMPLE_COUNT_1_BIT),
			sampleShadingEnable(VK_FALSE),
			minSampleShading(1.0f),
			pSampleMask(nullptr),
			currentRenderPass(VK_NULL_HANDLE),
			currentSubpass(0),
			vertexInputAttributeDescriptions(nullptr),
			vertexInputBindingDescriptions(nullptr)
		{
		};
	};

	//shader独立的绘制参数
	struct DrawStateData
	{
		//input_assembly
		VkPrimitiveTopology							primitiveTopology;
		VkBool32									primitiveRestartEnable;

		//tess
		uint32_t									patchControlPoints;

		//rasterization
		VkBool32									depthClampEnable;
		VkBool32									rasterizerDiscardEnable;
		VkPolygonMode								polygonMode;
		VkCullModeFlags								cullMode;
		VkFrontFace									frontFace;
		VkBool32									depthBiasEnable;
		float										depthBiasConstantFactor;
		float										depthBiasClamp;
		float										depthBiasSlopeFactor;
		float										lineWidth;

		//multiesample
		VkBool32									alphaToCoverageEnable;
		VkBool32									alphaToOneEnable;

		//depth
		VkBool32									depthTestEnable;
		VkBool32									depthWriteEnable;
		VkCompareOp									depthCompareOp;
		VkBool32									depthBoundsTestEnable;
		VkBool32									stencilTestEnable;
		VkStencilOpState							front;
		VkStencilOpState							back;
		float										minDepthBounds;
		float										maxDepthBounds;

		DrawStateData():
			primitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
			primitiveRestartEnable(VK_FALSE),
			patchControlPoints(1),
			depthClampEnable(VK_FALSE),
			rasterizerDiscardEnable(VK_FALSE),
			polygonMode(VK_POLYGON_MODE_FILL),
			cullMode(VK_CULL_MODE_BACK_BIT),
			frontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE),
			depthBiasEnable(VK_FALSE),
			depthBiasConstantFactor(0.0f),
			depthBiasClamp(0.0f),
			depthBiasSlopeFactor(0.0f),
			lineWidth(1.0f),
			alphaToCoverageEnable(VK_FALSE),
			alphaToOneEnable(VK_FALSE),
			depthTestEnable(VK_TRUE),
			depthWriteEnable(VK_TRUE),
			depthCompareOp(VK_COMPARE_OP_LESS),
			depthBoundsTestEnable(VK_FALSE),
			stencilTestEnable(VK_FALSE),
			front(),
			back(),
			minDepthBounds(0.0f),
			maxDepthBounds(1.0f)
		{}
	};

	struct GraphicsPipeCreateInfo
	{
		std::vector<ShaderStageParameters> shader_stage_params;

		VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info;

		VkPipelineTessellationStateCreateInfo tessellation_state_create_info;

		VkPipelineRasterizationStateCreateInfo rasterization_state_create_info;

		VkPipelineMultisampleStateCreateInfo multisample_state_create_info;

		VkStencilOpState stencil_test_parameters;

		VkPipelineDepthStencilStateCreateInfo depth_and_stencil_state_create_info;

		std::vector<VkPipelineColorBlendAttachmentState> attachment_blend_states;

		std::vector<VkDescriptorSetLayout>			descriptorset_layouts;

		int push_constant_size;

		void Clear()
		{
			shader_stage_params.clear();
			attachment_blend_states.clear();
			descriptorset_layouts.clear();
			push_constant_size = 0;
		}
	};

	class VansVKGraphicsPipeline
	{
		friend class VansVKCommandBuffer;
	private:

		VkPipelineLayout m_VansPipelineLayout;

		VkPipelineCache m_PipelineCache;

		VkPipeline m_GraphicsPipeline;

		VkDevice m_Device;

	private:

		//创建pipeline create info的数据
		std::vector<VkPipelineShaderStageCreateInfo> shader_stage_create_infos;

		std::vector<VkVertexInputBindingDescription> binding_descriptions;
		std::vector<VkVertexInputAttributeDescription> attribute_descriptions;
		VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info;
		VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info;
		VkPipelineTessellationStateCreateInfo tessellation_state_create_info;

		ViewportInfo viewport_infos;
		VkPipelineViewportStateCreateInfo viewport_state_create_info;
		VkPipelineRasterizationStateCreateInfo rasterization_state_create_info;
		VkPipelineMultisampleStateCreateInfo  multisample_state_create_info;
		VkPipelineDepthStencilStateCreateInfo depth_and_stencil_state_create_info;
		VkPipelineColorBlendStateCreateInfo blend_state_create_info;

		std::vector<VkDynamicState> dynamic_states;
		VkPipelineDynamicStateCreateInfo dynamic_state_creat_info;

	public:
		bool CreateGraphicsPipelineInfo(VkDevice& logic_device, GraphicsPipeCreateInfo& create_info, GlobalStateData& global_state_data, VkGraphicsPipelineCreateInfo& final_create_info);

		bool CreateGraphicsPipeline(VkDevice& logic_device, const VkGraphicsPipelineCreateInfo& create_info);

		bool CreatePipelineCache(VkDevice& logic_device);

		bool GetPipelineCacheData(VkDevice& logic_device);

		void BindGraphicsPipeline(VkCommandBuffer& command_buffer);

		void DestroyPipeline(VkDevice& logic_device);

		void DestroyPipelineCache(VkDevice& logic_device);

		void DestroyPipelineLayout(VkDevice& logic_device);

		~VansVKGraphicsPipeline()
		{
			DestroyPipelineLayout(m_Device);
			DestroyPipeline(m_Device);
			DestroyPipelineCache(m_Device);
		}

	public :
		static bool MergePipelineCache(VkDevice& logic_device, std::vector<VkPipelineCache>& source_pipeline_caches, VkPipelineCache& merged_cache);
	
		static VkPipeline CurrentValidGraphicsPipeline;

		VkPipeline GetNativePipeline() { return m_GraphicsPipeline; }
	};

	class VansVKComputePipeline
	{
		friend class VansVKCommandBuffer;
		//Compute pipelines cannot be used inside render passes.
	private:

		VkPipeline m_ComputePipeline;

		VkPipelineLayout m_VansPipelineLayout;

		VkDevice m_Device;

	public:
		bool CreateComputePipeline(VkDevice& logic_device, VkPipelineShaderStageCreateInfo& compute_shader_stage, const VkPipelineCache& pipeline_cache, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts, int pushConstRangeCount = 0, VkPushConstantRange* pushConstRange = nullptr);
		
		void BindComputePipeline(VkCommandBuffer& command_buffer);

		void DestroyPipeline(VkDevice& logic_device);

		void DestroyPipelineLayout(VkDevice& logic_device);

		void DispatchCompute(VkCommandBuffer& command_buffer, int x, int y, int z);

		~VansVKComputePipeline()
		{
			DestroyPipelineLayout(m_Device);
			DestroyPipeline(m_Device);
		}
	};

	class VansVKRayTracingPipeline
	{
		friend class VansVKCommandBuffer;
		friend class VansRayTracingShader;
		friend class VansRayTracing;
		//Compute pipelines cannot be used inside render passes.
	private:

		VkPipeline m_RayTracingPipeline;

		VkPipelineLayout m_RayTracingLayout;

		VkDevice m_Device;

	private:

		VkStridedDeviceAddressRegionKHR m_RaygenShaderBindingTable{};

		VkStridedDeviceAddressRegionKHR m_MissShaderBindingTable{};

		VkStridedDeviceAddressRegionKHR m_HitShaderBindingTable{};

		VkStridedDeviceAddressRegionKHR m_CallableShaderBindingTable{}; // 未使用

	public:

		bool CreateRayTracingPipeline(VkDevice& logic_device, std::vector<VkRayTracingShaderGroupCreateInfoKHR>& shaderGroupCreateInfo, std::vector<VkPipelineShaderStageCreateInfo>& shaderStageCreateInfo,  const VkPipelineCache& pipeline_cache, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts, int pushConstRangeCount = 0, VkPushConstantRange* pushConstRange = nullptr);

		void DestroyPipeline(VkDevice& logic_device);

		void DestroyPipelineLayout(VkDevice& logic_device);

		~VansVKRayTracingPipeline()
		{
			DestroyPipelineLayout(m_Device);
			DestroyPipeline(m_Device);
		}
	};
}