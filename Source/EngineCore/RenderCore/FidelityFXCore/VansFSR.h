#pragma once
#include <ffx_api/ffx_api.hpp>
#include <ffx_api/ffx_upscale.hpp>
#include <ffx_api/vk/ffx_api_vk.hpp>

namespace VansGraphics
{
	struct FSRInput
	{
		VkImage color;
		VkImageCreateInfo colorCreateInfo;
		VkImage depth;
		VkImageCreateInfo depthCreateInfo;
		VkImage motionVectors;
		VkImageCreateInfo motionVectorsCreateInfo;

		VkImage reactive;
		VkImage transparencyAndComposition;
		float fovy;
		float nearPlane;
		float farPlane;

		float jitterX;
		float jitterY;
	};

	class VansVKImage;
	class VansFSR
	{
	private:

		ffx::Context m_UpscalingContext;

		uint32_t m_RenderWidth;
		uint32_t m_RenderHeight;
		uint32_t m_DisplayWidth;
		uint32_t m_DisplayHeight;

		VkDevice m_Device;
		VkPhysicalDevice m_PhysicalDevice;

		//用于保存中间结果
		VansVKImage* m_TempFSRImage;

	public :
		void InitializeContext(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t renderWidth, uint32_t renderHeight, uint32_t displayWidth, uint32_t displayHeight);
	
		void DispatchUpscale(VkCommandBuffer& commandBuffer, FSRInput& input);

		void Cleanup();

		VansVKImage& GetTempFSRImage() { return *m_TempFSRImage; }

		VkExtent2D GetDisplayExtent()
		{
			return { m_DisplayWidth, m_DisplayHeight };
		}
	};
}
