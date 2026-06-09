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

namespace VansGraphics
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

		bool RefreshShaderMoudle();

		bool InitRayTracingShader(VkDevice& logic_device, const std::string& shader_folder);

		bool CheckRefreshShader(VkDevice& logic_device);

		void SetPushConstant(int size) { m_PushConstantSize = size; }

		int GetPushConstantSize() const { return m_PushConstantSize; }

		void SetPushConstantData(void* data) { m_PushConstantData = data; }

		void* GetPushConstantData() { return m_PushConstantData; }

		std::string GetShaderFolder() { return m_ShaderFolder; }

		std::map<VkShaderStageFlagBits, ShaderModuleData> m_ShaderModuleDataMap;

		~VansShader()
		{
			DestroyShaderMoulde();
		}
	private:
		bool CreateShaderModule(VkDevice& logic_device);

		bool TranslateToSPIRV(const std::string& shader_folder, ShaderType shaderType = ShaderType::Normal);

		std::string m_ShaderFolder;

	protected:
		VkDevice m_LogicDevice;

		void DestroyShaderMoulde();

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

		void SetPolygonMode(VkPolygonMode mode);

		void SetEnableAlphaBlend(VkBool32 enable) { m_DrawStateData.enableAlphaBlend = enable; }

		// 贴花专用：MRT 3 附件 Alpha Blend，GBuffer1 colorMask 仅 R+G
		void SetEnableDecalBlend(VkBool32 enable) { m_DrawStateData.enableDecalBlend = enable; }

		// 显式指定颜色附件数量（用于非主 GBuffer 的 MRT pass，如水面 GBuffer 的 2 个附件）。
		// count > 0 时生成 count 个不混合、写入 RGBA 的 blend state，覆盖自动推断。
		void SetColorAttachmentCount(int count) { m_ColorAttachmentCount = count; }

		void TriggerReCreateGraphicsPipeline();

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

		// 显式颜色附件数量（-1 = 自动推断），用于非主 GBuffer 的 MRT pass
		int m_ColorAttachmentCount = -1;

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

		/// 场景切换时调用：销毁 pipeline / SBT，使下次 GetRayTracingPipeline 重建
		void CleanupPipeline()
		{
			if (m_VansVkRayTracingPipeline != nullptr)
			{
				delete m_VansVkRayTracingPipeline;
				m_VansVkRayTracingPipeline = nullptr;
			}
			m_SBTBuffer.DestroyVulkanBuffer(m_LogicDevice);

			// 销毁旧 VkShaderModule，防止泄漏
			DestroyShaderMoulde();
		}

	private:

		VansVKBuffer m_SBTBuffer;

		VansVKRayTracingPipeline* m_VansVkRayTracingPipeline;

		void CreateShaderBindingTable(VansVKDevice* device);

		bool CreateRayTracingPipeline(VkDevice& logic_device, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts);
	};
}