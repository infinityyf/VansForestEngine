#pragma once
#include <vector>
#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif
#include "vulkan/vulkan.h"

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "VansVKImage.h"

namespace VansGraphics
{
	struct VansSwapChainCreateParams
	{
		VkPresentModeKHR desired_present_mode;
		VkImageUsageFlags desired_image_usage;
		VkSurfaceTransformFlagBitsKHR desired_image_transform;
		VkSurfaceFormatKHR desired_surface_format;
	};

	//present which image of the swapchain
	struct VansPresentInfo 
	{
		VkSwapchainKHR swapchain;
		uint32_t image_index;
	};
	class VansVKSurface
	{
	public :


		//VansVKDevice is firend of VansVKSurface, can access private members of VansVKSurface
		friend class VansVKDevice;
	private :
		bool CheckVulkanSurfacePresentMode(VkPhysicalDevice& physical_device, VkPresentModeKHR& desired_present_mode);

		bool CheckVulkanSwapChainImageCount(VkSurfaceCapabilitiesKHR& surface_capabilities);

		bool CheckVulkanSurfaceExtent(VkSurfaceCapabilitiesKHR& surface_capabilities);

		bool CheckVulkanSurfaceImageUsage(VkSurfaceCapabilitiesKHR& surface_capabilities, VkImageUsageFlags& desired_image_usage);

		bool CheckVulkanSurfaceTransform(VkSurfaceCapabilitiesKHR& surface_capabilities, VkSurfaceTransformFlagBitsKHR& desired_image_transform);

		bool CheckVulkanSurfaceFormat(VkPhysicalDevice& physical_device, VkSurfaceFormatKHR& desired_surface_format);

		bool GetVulkanSwapChainImages(VkDevice& logical_device, VkSwapchainKHR& swapchain);

		void CreateSwapChainCreateParams();

		VansSwapChainCreateParams m_VansSwapChainCreateParams;


	public:
		bool CreateVulkanPresentSurface(VkInstance& instance, GLFWwindow* window);

		bool CreateVulkanSwapChain(VkPhysicalDevice& physical_device, VkDevice& logical_device);

		bool AcquireVulkanSwapChainImages(VkDevice& logical_device,  uint32_t& image_index, VkSemaphore& aquire_image_semaphore, VkFence& acquire_image_fence);

		bool PresentImage(VkDevice& logical_device, VkQueue& queue, const std::vector<VkSemaphore>& rendering_semaphores, uint32_t image_index);

		bool DestroyVulkanSwapChain(VkDevice& logical_device);

		// 重建交换链（窗口大小改变时调用）
		bool RecreateSwapChain(VkPhysicalDevice& physical_device, VkDevice& logical_device);

		bool DestroyVulkanPresentSurface(VkInstance& instance);

		void SetSwapChainImageBarrier(VkPipelineStageFlags generating_stages, VkPipelineStageFlags consuming_stages, ImageTransition transition, int swap_chain_index = -1);

		VkImage GetSwapChainImage(uint32_t index){return m_VansVKSwapChainImages[index]; }

		VkImageView GetSwapChainImageView(uint32_t index){return m_VansVKSwapChainImageViews[index]; }

		VkSurfaceKHR GetSurface(){return m_VansVKPresentSurface; }

		VkPresentModeKHR GetPresentMode() { return m_VansVKPresentMode; }

		VkSurfaceFormatKHR GetSurfaceFormatFormat() { return m_VansSwapChainCreateParams.desired_surface_format; }

	public:
		VkPresentModeKHR m_VansVKPresentMode;

		uint32_t m_VansVKImageCount;

		VkExtent2D m_VansVKSwapChainImageExtent;

		VkSurfaceTransformFlagBitsKHR m_VansVKSwapChainImageTransform;

		VkFormat m_VansVKSwapChainImageFormat;

		VkColorSpaceKHR m_VansVKSwapChainColorSpace;

		VkSwapchainKHR m_VansVKOldSwapChain;

		VkImageUsageFlags m_VansVKSwapChainImageUsage;
	private:
		VkSwapchainKHR m_VansVKSwapChain;

		VkSurfaceKHR m_VansVKPresentSurface;

		std::vector<VkImage> m_VansVKSwapChainImages;

		std::vector<VkImageView> m_VansVKSwapChainImageViews;

		std::vector<VkImageMemoryBarrier> m_SwapChainImageMemoryBarriers;
	};
}