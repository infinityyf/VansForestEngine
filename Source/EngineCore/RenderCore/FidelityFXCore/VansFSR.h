#pragma once
#include <ffx_api/ffx_api.hpp>
#include <ffx_api/ffx_upscale.hpp>
#include <ffx_api/vk/ffx_api_vk.hpp>

namespace VansGraphics
{
	struct FSRInput
	{
		VkImage color = VK_NULL_HANDLE;
		VkImageCreateInfo colorCreateInfo;
		VkImage depth = VK_NULL_HANDLE;
		VkImageCreateInfo depthCreateInfo;
		VkImage motionVectors = VK_NULL_HANDLE;
		VkImageCreateInfo motionVectorsCreateInfo;

		// 曝光纹理（1x1 R16F），由 UpdateExposure 写入
		VkImage exposure = VK_NULL_HANDLE;
		VkImageCreateInfo exposureCreateInfo;

		VkImage reactive = VK_NULL_HANDLE;
		VkImage transparencyAndComposition = VK_NULL_HANDLE;
		float fovy;
		float nearPlane;
		float farPlane;

		// 像素空间抖动偏移（[-0.5, 0.5] 范围），FSR API 期望的单位
		float jitterPixelX = 0.0f;
		float jitterPixelY = 0.0f;
		
		bool reset = false;
	};

	class VansVKImage;
	class VansFSR
	{
	private:

		ffx::Context m_UpscalingContext = nullptr;

		uint32_t m_RenderWidth;
		uint32_t m_RenderHeight;
		uint32_t m_DisplayWidth;
		uint32_t m_DisplayHeight;

		VkDevice m_Device;
		VkPhysicalDevice m_PhysicalDevice;

		// FSR 内置抖动序列相位数量（InitializeContext 后查询得到）
		int32_t m_JitterPhaseCount = 0;

		//用于保存中间结果
		VansVKImage* m_TempFSRImage = nullptr;

	public:
		// 锐化强度（0~1），可由外部配置
		float m_Sharpness = 0.5f;

		void InitializeContext(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t renderWidth, uint32_t renderHeight, uint32_t displayWidth, uint32_t displayHeight);
	
		void DispatchUpscale(VkCommandBuffer& commandBuffer, FSRInput& input);

		void Cleanup();

		VansVKImage& GetTempFSRImage() { return *m_TempFSRImage; }

		VkExtent2D GetDisplayExtent()
		{
			return { m_DisplayWidth, m_DisplayHeight };
		}

		// 返回 FSR 内置抖动序列相位数量
		int32_t GetJitterPhaseCount() const { return m_JitterPhaseCount; }

		// 查询 FSR 内置抖动偏移，outX/outY 为像素空间 [-0.5, 0.5]
		void GetJitterOffset(int32_t index, float& outX, float& outY);
	};
}
