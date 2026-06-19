#pragma once
#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif


#include "vulkan/vulkan.h"
#include <vector>

// Forward declare VMA opaque allocation handle.
struct VmaAllocation_T;
typedef struct VmaAllocation_T* VmaAllocation;

namespace VansGraphics
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

		// VMA-managed allocation backing the image (replaces raw VkDeviceMemory).
		VmaAllocation m_VansVKImageAllocation = nullptr;

		//defines additional metadata used for accessing the image. Through it, we can
		//specify the parts of an image that should be accessed by the commands
		//also defines how an image's memory should be interpreted.
		VkImageView m_VansVKImageView;
		std::vector<VkImageView> m_VansVKImageMipViews;
		std::vector<VkImageView> m_OwnedAuxiliaryViews;

		// depth+stencil combined attachment view（仅对 D32S8/D24S8 等带 stencil 格式创建）
		// framebuffer attachment 使用此 view 以支持 stencil 操作
		VkImageView m_DepthStencilView = VK_NULL_HANDLE;

		//如果是combined sample image这里持有sampler，可能是空
		VkSampler m_Sampler;

		VkImageCreateInfo m_ImageCreateInfo;

	private:
		VkImageViewType ConvertImageViewType(VkImageType type, bool isCube = false, int layer_num = 1);

		// format 参数用于识别带 stencil 的深度格式（D32S8、D24S8 等）
		VkImageAspectFlags ConvertImageViewAspect(VkImageUsageFlags usage, VkFormat format);

	private:
		VkExtent3D m_ImageDimention;

		VkFormat m_ImageFormat;

		uint32_t m_ImageMip;

		uint32_t m_ImageLayers; //cubemap has 6 layers

		VkImageLayout m_ImageLayout;

		VkImageAspectFlags m_ImageAspect;

		std::vector<VkImageMemoryBarrier> m_ImageMemoryBarriers;

	public:
		bool CreateVulkanImage(VkDevice& logical_device, VkExtent3D size, VkFormat format, uint32_t mip_num, uint32_t layer_num, VkImageType type, VkImageUsageFlags usage, VkSampleCountFlagBits samples, bool isCube = false, bool need_raw_Data = false, bool combined_sampler = false, VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);

		void DestroyVulkanImage(VkDevice& logical_device);

		void SetRawImageData(VkDevice& logical_device, void* data, int size);
		
		//设置image memory barrier
		void SetImageMemoryBarrier(VkPipelineStageFlags generating_stages, VkPipelineStageFlags consuming_stages, ImageTransition bufferTransition);

		VkImageView GetImageView();

		// 获取 depth+stencil combined view；若格式无 stencil 则退回 GetImageView()
		VkImageView GetDepthStencilView() const { return m_DepthStencilView != VK_NULL_HANDLE ? m_DepthStencilView : m_VansVKImageView; }

		VkImageView GetImageMipView(int mip);

		// Creates a caller-owned 2D view for one array layer and one mip. Used by
		// editor previews of cubemap-array faces.
		VkImageView CreateLayerMipView(VkDevice device, uint32_t arrayLayer, uint32_t mipLevel);
		VkImageView CreateMipArrayView(VkDevice device, uint32_t mipLevel) const;

		VkImageLayout GetImageLayout();

		// 仅更新引擎侧记录的布局状态；调用方必须已向目标 command buffer 记录等价 barrier。
		void SetTrackedImageLayout(VkImageLayout layout) { m_ImageLayout = layout; }

		VkSampler GetSampler();

		VkImage GetImage();

		VkImageAspectFlags GetImageAspect();

		VkExtent3D GetImageDimension();

		VkImageCreateInfo& GetImageCreateInfo()
		{
			return m_ImageCreateInfo;
		}

	private:
		void AddTransitionImageAccess(ImageTransition& transition);
	};

}
