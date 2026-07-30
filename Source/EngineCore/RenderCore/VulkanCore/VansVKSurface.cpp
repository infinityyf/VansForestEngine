#include "VansVKSurface.h"
#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKCommandBuffer.h"
#include "VansVKMemoryManager.h"
#include "../../Util/VansLog.h"
#include <iostream>
#include <vector>

namespace VansGraphics
{

	bool VansVKSurface::CheckVulkanSurfacePresentMode(VkPhysicalDevice& physical_device, VkPresentModeKHR& desired_present_mode)
	{
		//get present mode
		uint32_t present_modes_count;
		VkResult result = VansGraphics::vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, m_VansVKPresentSurface, &present_modes_count, nullptr);
		if (result != VK_SUCCESS || present_modes_count == 0)
		{
			VANS_LOG_ERROR("Could not get the number of available present modes. Dont suppoty FIFO MODE");
			return false;
		}

		std::vector<VkPresentModeKHR> present_modes;
		present_modes.resize(present_modes_count);
		result = VansGraphics::vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, m_VansVKPresentSurface, &present_modes_count, &present_modes[0]);
		if (result != VK_SUCCESS)
		{
			VANS_LOG_ERROR("Could not enumerate present modes.");
			return false;
		}

		for (auto& mode : present_modes)
		{
			if (mode == desired_present_mode)
			{
				m_VansVKPresentMode = desired_present_mode;
				return true;
			}
		}

		//default mode, all vulkan device should support
		m_VansVKPresentMode = VK_PRESENT_MODE_FIFO_KHR;
		return true;
	}

	bool VansVKSurface::CheckVulkanSwapChainImageCount(VkSurfaceCapabilitiesKHR& surface_capabilities)
	{
		//set swapchain image count
		m_VansVKImageCount = surface_capabilities.minImageCount + 1;
		if ((surface_capabilities.maxImageCount > 0) && (m_VansVKImageCount > surface_capabilities.maxImageCount))
		{
			m_VansVKImageCount = surface_capabilities.maxImageCount;
		}
		return true;
	}

	bool VansVKSurface::CheckVulkanSurfaceExtent(VkSurfaceCapabilitiesKHR& surface_capabilities)
	{
		//set swapchain image format
		//surface is created from window
		//it conatians the size of windows client : else branch
		//on other os the window's size is determined by the size of swapchain images : if branch
		if (0xFFFFFFFF == surface_capabilities.currentExtent.width)
		{
			//on os window size is determined by the size of swapchain images
			m_VansVKSwapChainImageExtent = { 640, 480 };
			if (m_VansVKSwapChainImageExtent.width < surface_capabilities.minImageExtent.width)
			{
				m_VansVKSwapChainImageExtent.width = surface_capabilities.minImageExtent.width;
			}
			else if (m_VansVKSwapChainImageExtent.width > surface_capabilities.maxImageExtent.width)
			{
				m_VansVKSwapChainImageExtent.width = surface_capabilities.maxImageExtent.width;
			}
			if (m_VansVKSwapChainImageExtent.height < surface_capabilities.minImageExtent.height)
			{
				m_VansVKSwapChainImageExtent.height = surface_capabilities.minImageExtent.height;
			}
			else if (m_VansVKSwapChainImageExtent.height > surface_capabilities.maxImageExtent.height)
			{
				m_VansVKSwapChainImageExtent.height = surface_capabilities.maxImageExtent.height;
			}
		}
		else
		{
			//use windows client size
			m_VansVKSwapChainImageExtent = surface_capabilities.currentExtent;
		}
		return true;
	}

	bool VansVKSurface::CheckVulkanSurfaceImageUsage(VkSurfaceCapabilitiesKHR& surface_capabilities, VkImageUsageFlags& desired_image_usage)
	{
		//select swapchain images usage, color aattachment is basic
		m_VansVKSwapChainImageUsage = surface_capabilities.supportedUsageFlags & desired_image_usage;
		if (m_VansVKSwapChainImageUsage != desired_image_usage)
		{
			return false;
		}
		return true;
	}

	bool VansVKSurface::CheckVulkanSurfaceTransform(VkSurfaceCapabilitiesKHR& surface_capabilities, VkSurfaceTransformFlagBitsKHR& desired_image_transform)
	{
		//set swapchain transorm, for mobile
		if (surface_capabilities.supportedTransforms & desired_image_transform)
		{
			m_VansVKSwapChainImageTransform = desired_image_transform;
		}
		else
		{
			m_VansVKSwapChainImageTransform = surface_capabilities.currentTransform;
		}
		return true;
	}

	bool VansVKSurface::CheckVulkanSurfaceFormat(VkPhysicalDevice& physical_device, VkSurfaceFormatKHR& desired_surface_format)
	{
		//color format , date type, color space : formate + color space = surface format
		uint32_t  formats_count;
		VkResult result = VansGraphics::vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, m_VansVKPresentSurface, &formats_count, nullptr);
		if ((VK_SUCCESS != result) || (0 == formats_count))
		{
			VANS_LOG_ERROR("Could not get the number of supported surface formats.");
			return false;
		}

		std::vector<VkSurfaceFormatKHR> surface_formats(formats_count);
		result = VansGraphics::vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, m_VansVKPresentSurface, &formats_count, &surface_formats[0]);
		if ((VK_SUCCESS != result) || (0 == formats_count)) 
		{
			VANS_LOG_ERROR("Could not enumerate supported surface formats.");
			return false;
		}

		//if only VK_FORMAT_UNDEFINED, it support every formate
		if((1 == surface_formats.size()) && (VK_FORMAT_UNDEFINED == surface_formats[0].format)) 
		{
			m_VansVKSwapChainImageFormat = desired_surface_format.format;
			m_VansVKSwapChainColorSpace = desired_surface_format.colorSpace;
		}
		else
		{
			bool foundSurfaceFormat = false;
			for (auto& surface_format : surface_formats)
			{
				if ((desired_surface_format.format == surface_format.format) && (desired_surface_format.colorSpace == surface_format.colorSpace))
				{
					m_VansVKSwapChainImageFormat = desired_surface_format.format;
					m_VansVKSwapChainColorSpace = desired_surface_format.colorSpace;
					foundSurfaceFormat = true;
					break;
				}
			}
			if (!foundSurfaceFormat)
			{
				VANS_LOG_ERROR("Could not found desired surface formats.");
				return false;
			}
		}
		return true;
	}

	bool VansVKSurface::GetVulkanSwapChainImages(VkDevice& logical_device, VkSwapchainKHR& swapchain)
	{
		uint32_t images_count = 0;
		VkResult result = VK_SUCCESS;
		result = VansGraphics::vkGetSwapchainImagesKHR(logical_device, swapchain, &images_count, nullptr);
		if ((VK_SUCCESS != result) || (0 == images_count)) 
		{
			VANS_LOG_ERROR("Could not get the number of swapchain images.");
			return false;
		}
		else
		{
			VANS_LOG("get the number of swapchain images : " << images_count);
		}

		m_VansVKSwapChainImages.resize(images_count);
		m_VansVKSwapChainImageViews.resize(images_count);
		result = VansGraphics::vkGetSwapchainImagesKHR(logical_device, swapchain, &images_count, &m_VansVKSwapChainImages[0]);
		if ((VK_SUCCESS != result) || (0 == images_count)) 
		{
			VANS_LOG_ERROR("Could not enumerate swapchain images.");
			return false;
		}

		// Create an image view for each swapchain image
		for (size_t i = 0; i < images_count; i++)
		{
			VkImageViewCreateInfo createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			createInfo.image = m_VansVKSwapChainImages[i];
			createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			createInfo.format = m_VansVKSwapChainImageFormat;
			createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			createInfo.subresourceRange.baseMipLevel = 0;
			createInfo.subresourceRange.levelCount = 1;
			createInfo.subresourceRange.baseArrayLayer = 0;
			createInfo.subresourceRange.layerCount = 1;

			if (VansGraphics::vkCreateImageView(logical_device, &createInfo, nullptr, &m_VansVKSwapChainImageViews[i]) != VK_SUCCESS)
			{
				VANS_LOG_ERROR("Failed to create image view for swapchain image " << i);
				return false;
			}
		}

		return true;
	}

	VkResult VansVKSurface::AcquireVulkanSwapChainImage(VkDevice& logical_device, uint32_t& image_index, VkSemaphore& image_acquired_semaphore)
	{
		// Return the exact Vulkan result. The caller may wait on the acquire
		// semaphore only when this call actually acquired an image.
		return VansGraphics::vkAcquireNextImageKHR(
			logical_device,
			m_VansVKSwapChain,
			2000000000,
			image_acquired_semaphore,
			VK_NULL_HANDLE,
			&image_index);
	}

	VkResult VansVKSurface::PresentImage(VkQueue& queue, const std::vector<VkSemaphore>& rendering_semaphores, uint32_t image_index)
	{

		VkPresentInfoKHR present_info =
		{
			 VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			 nullptr,
			 static_cast<uint32_t>(rendering_semaphores.size()),
			 rendering_semaphores.size() > 0 ? &rendering_semaphores[0] : nullptr,
			 1,
			 &m_VansVKSwapChain,
			 &image_index,
			 nullptr
		};

		return VansGraphics::vkQueuePresentKHR(queue, &present_info);
	}

	void VansVKSurface::RecordSwapChainImageBarrier(
		VansVKCommandBuffer& command_buffer,
		VkPipelineStageFlags src_stage,
		VkPipelineStageFlags dst_stage,
		const ImageTransition& transition)
	{
		VkImageMemoryBarrier image_memory_barrier = {};
		image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_memory_barrier.srcAccessMask = transition.CurrentAccess;
		image_memory_barrier.dstAccessMask = transition.NewAccess;
		image_memory_barrier.oldLayout = transition.CurrentLayout;
		image_memory_barrier.newLayout = transition.NewLayout;
		image_memory_barrier.srcQueueFamilyIndex = transition.CurrentQueueFamily;
		image_memory_barrier.dstQueueFamilyIndex = transition.NewQueueFamily;
		image_memory_barrier.image = transition.Image;
		image_memory_barrier.subresourceRange =
		{
			transition.Aspect,
			0,
			1,
			0,
			1
		};

		command_buffer.PipelineBarrier(src_stage, dst_stage, {}, {}, { image_memory_barrier });
	}

	bool VansVKSurface::DestroyVulkanSwapChain(VkDevice& logical_device)
	{
		// Destroy image views before destroying the swap chain
		for (auto& imageView : m_VansVKSwapChainImageViews)
		{
			if (imageView != VK_NULL_HANDLE)
				VansGraphics::vkDestroyImageView(logical_device, imageView, nullptr);
		}
		m_VansVKSwapChainImageViews.clear();
		m_VansVKSwapChainImages.clear();

		if (m_VansVKSwapChain) 
		{
			VansGraphics::vkDestroySwapchainKHR(logical_device, m_VansVKSwapChain, nullptr);
			m_VansVKSwapChain = VK_NULL_HANDLE;
		}
		return true;
	}

	bool VansVKSurface::RecreateSwapChain(VkPhysicalDevice& physical_device, VkDevice& logical_device)
	{
		// Destroy old image views
		for (auto& imageView : m_VansVKSwapChainImageViews)
		{
			if (imageView != VK_NULL_HANDLE)
				VansGraphics::vkDestroyImageView(logical_device, imageView, nullptr);
		}
		m_VansVKSwapChainImageViews.clear();
		m_VansVKSwapChainImages.clear();

		// Hand old swap chain off — CreateVulkanSwapChain will use it and destroy it
		m_VansVKOldSwapChain = m_VansVKSwapChain;
		m_VansVKSwapChain    = VK_NULL_HANDLE;

		if (!CreateVulkanSwapChain(physical_device, logical_device))
		{
			VANS_LOG_ERROR("RecreateSwapChain: CreateVulkanSwapChain failed.");
			return false;
		}

		VANS_LOG("Swap chain recreated: " << m_VansVKSwapChainImageExtent.width << "x" << m_VansVKSwapChainImageExtent.height);
		return true;
	}

	bool VansVKSurface::DestroyVulkanPresentSurface(VkInstance& instance)
	{
		if (m_VansVKPresentSurface) 
		{
			VansGraphics::vkDestroySurfaceKHR(instance, m_VansVKPresentSurface, nullptr);
			m_VansVKPresentSurface = VK_NULL_HANDLE;
		}
		return true;
	}

	void VansVKSurface::CreateSwapChainCreateParams()
	{
		m_VansSwapChainCreateParams.desired_image_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		m_VansSwapChainCreateParams.desired_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;// VK_PRESENT_MODE_FIFO_KHR;
		m_VansSwapChainCreateParams.desired_surface_format = { VK_FORMAT_R8G8B8A8_SRGB ,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
		m_VansSwapChainCreateParams.desired_image_usage =
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}

	bool VansVKSurface::CreateVulkanPresentSurface(VkInstance& instance, GLFWwindow* window)
	{
		if (window == nullptr)
		{
			return false;
		}
		VkResult result;
		result = glfwCreateWindowSurface(instance, window, nullptr, &m_VansVKPresentSurface);
		if (result != VK_SUCCESS || m_VansVKPresentSurface == VK_NULL_HANDLE)
		{
			VANS_LOG_ERROR("Could not create presentation surface.");
			return false;
		}

		//set swapchain create params
		CreateSwapChainCreateParams();

		//初始设置old swapchain
		m_VansVKOldSwapChain = VK_NULL_HANDLE;

		return true;
	}

	bool VansVKSurface::CreateVulkanSwapChain(VkPhysicalDevice& physical_device, VkDevice& logical_device)
	{
		if (!CheckVulkanSurfacePresentMode(physical_device, m_VansSwapChainCreateParams.desired_present_mode))
		{
			return false;
		}

		//prepare to create swapchain
		VkSurfaceCapabilitiesKHR surface_capabilities;
		VkResult result = VansGraphics::vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, m_VansVKPresentSurface, &surface_capabilities);
		if (result != VK_SUCCESS)
		{
			VANS_LOG_ERROR("Could not get the capabilities of the presentation surface.");
			return false;
		}

		CheckVulkanSwapChainImageCount(surface_capabilities);
		
		CheckVulkanSurfaceExtent(surface_capabilities);

		if (!CheckVulkanSurfaceImageUsage(surface_capabilities, m_VansSwapChainCreateParams.desired_image_usage))
		{
			return false;
		}

		CheckVulkanSurfaceTransform(surface_capabilities, m_VansSwapChainCreateParams.desired_image_transform);

		//select formate
		if (!CheckVulkanSurfaceFormat(physical_device, m_VansSwapChainCreateParams.desired_surface_format))
		{
			return false;
		}

		//create swapchain, swap old new swapchain
		VkSwapchainCreateInfoKHR swapchain_create_info = 
		{
			 VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
			 nullptr,
			 0,
			 m_VansVKPresentSurface,
			 m_VansVKImageCount,
			 m_VansVKSwapChainImageFormat,
			 m_VansVKSwapChainColorSpace,
			 m_VansVKSwapChainImageExtent,
			 1, // dont use stero
			 m_VansVKSwapChainImageUsage,
			 VK_SHARING_MODE_EXCLUSIVE, //shareing mode
			 0,
			 nullptr,
			 m_VansVKSwapChainImageTransform,
			 VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			 m_VansVKPresentMode,
			 VK_TRUE,
			 m_VansVKOldSwapChain
		};

		result = VansGraphics::vkCreateSwapchainKHR(logical_device, &swapchain_create_info, nullptr, &m_VansVKSwapChain);
		if ((VK_SUCCESS != result) || (VK_NULL_HANDLE == m_VansVKSwapChain)) 
		{
			VANS_LOG_ERROR("Could not create a swapchain.");
			return false;
		}

		if (VK_NULL_HANDLE != m_VansVKOldSwapChain)
		{ 
			VansGraphics::vkDestroySwapchainKHR(logical_device, m_VansVKOldSwapChain, nullptr);
			m_VansVKOldSwapChain = VK_NULL_HANDLE;
		}

		//get swapchain images
		if (!GetVulkanSwapChainImages(logical_device, m_VansVKSwapChain))
		{
			return false;
		}
		return true;
	}
}
