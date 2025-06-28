#pragma once
#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif


#include "vulkan/vulkan.h"
#include <vector>

namespace VansVulkan
{
	struct ImageTransition 
	{
		VkImage Image;
		VkAccessFlags CurrentAccess;
		VkAccessFlags NewAccess;
		VkImageLayout CurrentLayout;
		VkImageLayout NewLayout;
		uint32_t CurrentQueueFamily;
		uint32_t NewQueueFamily;
		VkImageAspectFlags Aspect;
	};

	class VansVKImage
	{
		friend class VansVKMemoryManager;
		friend class VansVKDevice;
		friend class VansVKCommandBuffer;
		friend class VansRenderPassManager;
	private:
		VkImage m_VansVKImage;

		VkDeviceMemory m_VansVKImageMemory;

		//defines additional metadata used for accessing the image. Through it, we can
		//specify the parts of an image that should be accessed by the commands
		//also defines how an image's memory should be interpreted.
		VkImageView m_VansVKImageView;
		std::vector<VkImageView> m_VansVKImageMipViews;

		//如果是combined sample image这里持有sampler，可能是空
		VkSampler m_Sampler;

	private:
		VkImageViewType ConvertImageViewType(VkImageType type, bool isCube = false, int layer_num = 1);

		VkImageAspectFlags ConvertImageViewAspect(VkImageUsageFlags usage);

	private:
		VkExtent3D m_ImageDimention;

		VkFormat m_ImageFormat;

		uint32_t m_ImageMip;

		uint32_t m_ImageLayers; //cubemap has 6 layers

		VkImageLayout m_ImageLayout;

		VkImageAspectFlags m_ImageAspect;

		std::vector<VkImageMemoryBarrier> m_ImageMemoryBarriers;

	public:
		bool CreateVulkanImage(VkDevice& logical_device, VkExtent3D size, VkFormat format, uint32_t mip_num, uint32_t layer_num, VkImageType type, VkImageUsageFlags usage, VkSampleCountFlagBits samples, bool isCube = false, bool need_raw_Data = false, bool combined_sampler = false);

		void DestroyVulkanImage(VkDevice& logical_device);

		void SetRawImageData(VkDevice& logical_device, void* data, int size);
		
		//设置image memory barrier
		void SetImageMemoryBarrier(VkPipelineStageFlags generating_stages, VkPipelineStageFlags consuming_stages, ImageTransition bufferTransition);

		VkImageView GetImageView();

		VkImageView GetImageMipView(int mip);

		VkImageLayout GetImageLayout();

		VkSampler GetSampler();

		VkImage GetImage();

		VkImageAspectFlags GetImageAspect();

		VkExtent3D GetImageDimension();

	private:
		void AddTransitionImageAccess(ImageTransition& transition);
	};

}