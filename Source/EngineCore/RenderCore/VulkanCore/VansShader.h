#pragma once
#include <vector>
#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif
#include "vulkan/vulkan.h"
#include "VansPipeline.h"
#include "VansVKBuffer.h"
#include "../VansAsset.h"
#include <string>
#include <map>
#include <unordered_map>
using namespace VansGraphics;

namespace VansVulkan
{
	enum ShaderType
	{
		Normal = 0,
		RayTracing = 1,
	};
	struct ShaderModuleData
	{
		std::string m_ShaderTextResourceFileName;

		std::string m_ShaderType;

		//A single module may contain code for multiple shader stages
		//一般包含一个stage
		VkShaderModule m_ShaderModule;

		//需要通过glsl翻译
		std::vector<unsigned char> m_ShaderSPIRVCode;
	};

	const std::map<std::string, VkShaderStageFlagBits> m_ShaderTypeMap =
	{
		{"vert", VK_SHADER_STAGE_VERTEX_BIT},
		{"tesc", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT},
		{"tese", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT},
		{"geom", VK_SHADER_STAGE_GEOMETRY_BIT},
		{"frag", VK_SHADER_STAGE_FRAGMENT_BIT},
		{"comp", VK_SHADER_STAGE_COMPUTE_BIT},
	};

	const std::unordered_map<std::string, VkShaderStageFlagBits> m_RayTracingShaderTypeMap =
	{
		{"rgen", VK_SHADER_STAGE_RAYGEN_BIT_KHR},
		{"rmiss", VK_SHADER_STAGE_MISS_BIT_KHR},
		{"rahit", VK_SHADER_STAGE_ANY_HIT_BIT_KHR},
		{"rchit", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
		{"rint", VK_SHADER_STAGE_INTERSECTION_BIT_KHR},
	};

	class VansShader : public VansAsset
	{
	public:
		//每个shader一个路径，路径里都是对应的所有shader
		bool InitShader(VkDevice& logic_device, const std::string& shader_folder);

		bool InitRayTracingShader(VkDevice& logic_device, const std::string& shader_folder);

		bool CheckRefreshShader(VkDevice& logic_device);

		void SetPushConstant(int size) { m_PushConstantSize = size; }

		int GetPushConstantSize() const { return m_PushConstantSize; }

		void SetPushConstantData(void* data) { m_PushConstantData = data; }

		void* GetPushConstantData() { return m_PushConstantData; }

		std::map<VkShaderStageFlagBits, ShaderModuleData> m_ShaderModuleDataMap;

		~VansShader()
		{
			DestroyShaderMoulde();
		}

	public:
		VkDescriptorSet m_DescriptorSet;

	private:
		void DestroyShaderMoulde();

		bool CreateShaderModule(VkDevice& logic_device);

		bool TranslateToSPIRV(const std::string& shader_folder, ShaderType shaderType = ShaderType::Normal);

		VkDevice m_LogicDevice;

	protected:

		bool m_SupportMRTOutput;

		int m_PushConstantSize;

		void* m_PushConstantData;
	};

	class VansComputeShader : public VansShader
	{
	public:
		VansVKComputePipeline* GetComputePipeline(VkDevice& logic_device, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts);

		VansVKComputePipeline* GetComputePipeline() const { return m_ComputePipeline; };

		VansComputeShader() : m_ComputePipeline(nullptr)
		{

		}

		~VansComputeShader()
		{
			if (m_ComputePipeline != nullptr)
			{
				delete m_ComputePipeline;
			}
			
		}

	private:

		VkPipelineShaderStageCreateInfo m_ComputeShaderStageCreateInfo;

		VansVKComputePipeline* m_ComputePipeline;

		bool CreateComputePipeline(VkDevice& logic_device, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts);

	};



	class VansGraphicsShader : public VansShader
	{
	public :
		VansVKGraphicsPipeline* GetGraphicsPipeline(VkDevice& logic_device, GlobalStateData& global_state_data, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts);

		VansVKGraphicsPipeline* GetGraphicsPipeline() const { return m_GraphicsPipeline; };

		void SetDrawStateData(VkBool32 depthTestEnable, VkBool32 depthWriteEnable, VkCompareOp depthCompareOp, VkCullModeFlags cullmode);

		VansGraphicsShader() : m_GraphicsPipeline(nullptr)
		{

		}

		~VansGraphicsShader()
		{
			if (m_GraphicsPipeline != nullptr)
			{
				delete m_GraphicsPipeline;
			}
		}
	private:
		//记录当前pipeline的渲染状态,initshader时就可以被设置
		DrawStateData m_DrawStateData;

	private:
		//之后graphics shader才有效
		GraphicsPipeCreateInfo m_GraphicsPipelineCreateInfo;

		VansVKGraphicsPipeline* m_GraphicsPipeline;

		VkGraphicsPipelineCreateInfo m_VkGraphicsPipelineCreateInfo;

	private:
		void InitGraphicsPipelinInfo(GlobalStateData& global_state_data);

		bool CreateGraphicsPipeline(VkDevice& logic_device, GlobalStateData& global_state_data);

	};

	class VansRayTracingShader : public VansShader
	{
	public:

		VansVKRayTracingPipeline* GetRayTracingPipeline(VansVKDevice* device, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts);

		VansVKRayTracingPipeline* GetRayTracingPipeline() const { return m_VansVkRayTracingPipeline; };

		VansRayTracingShader() : m_VansVkRayTracingPipeline(nullptr)
		{
		}

		~VansRayTracingShader()
		{
			if (m_VansVkRayTracingPipeline != nullptr)
			{
				delete m_VansVkRayTracingPipeline;
			}

			m_SBTBuffer.DestroyVulkanBuffer(m_LogicDevice);
		}

	private:

		VkDevice m_LogicDevice;

		VansVKBuffer m_SBTBuffer;

		VansVKRayTracingPipeline* m_VansVkRayTracingPipeline;

		void CreateShaderBindingTable(VansVKDevice* device);

		bool CreateRayTracingPipeline(VkDevice& logic_device, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts);
	};
}