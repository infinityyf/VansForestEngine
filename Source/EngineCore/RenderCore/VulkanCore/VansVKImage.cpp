#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKImage.h"
#include "VansVKMemoryManager.h"
#include "VansVKMemoryAllocator.h"
#include "VansVKSampler.h"
#include "../../Util/VansLog.h"
#include <iostream>

namespace VansGraphics
{
	VkImageView VansVKImage::CreateLayerMipView(VkDevice device, uint32_t arrayLayer, uint32_t mipLevel)
	{
		if (arrayLayer >= m_ImageCreateInfo.arrayLayers || mipLevel >= m_ImageCreateInfo.mipLevels)
			return VK_NULL_HANDLE;
		VkImageViewCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.image = m_VansVKImage;
		info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		info.format = m_ImageCreateInfo.format;
		info.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
			VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
		info.subresourceRange = { m_ImageAspect, mipLevel, 1u, arrayLayer, 1u };
		VkImageView result = VK_NULL_HANDLE;
		if (vkCreateImageView(device, &info, nullptr, &result) != VK_SUCCESS) return VK_NULL_HANDLE;
		m_OwnedAuxiliaryViews.push_back(result);
		return result;
	}

	VkImageView VansVKImage::CreateMipArrayView(VkDevice device, uint32_t mipLevel) const
	{
		if (mipLevel >= m_ImageCreateInfo.mipLevels) return VK_NULL_HANDLE;
		VkImageViewCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.image = m_VansVKImage; info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; info.format = m_ImageCreateInfo.format;
		info.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
		info.subresourceRange = { m_ImageAspect, mipLevel, 1u, 0u, m_ImageCreateInfo.arrayLayers };
		VkImageView result = VK_NULL_HANDLE;
		return vkCreateImageView(device, &info, nullptr, &result) == VK_SUCCESS ? result : VK_NULL_HANDLE;
	}

    VkImageViewType VansVKImage::ConvertImageViewType(VkImageType type, bool isCube, int layer_num)
    {
        VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
        if (type < VK_IMAGE_TYPE_MAX_ENUM)
        {
            if (isCube)
            {
                view_type = VK_IMAGE_VIEW_TYPE_CUBE;
            }
            else
            {
                view_type = static_cast<VkImageViewType>(type);
            }

            if (layer_num > 1)
            {
                switch (view_type)
                {
                case VK_IMAGE_VIEW_TYPE_1D:
                    view_type = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
                    break;
                case VK_IMAGE_VIEW_TYPE_2D:
                    view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                    break;
                case VK_IMAGE_VIEW_TYPE_CUBE:
                    view_type = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
                    break;
                default:
                    break;
                }
            }
        }
        return view_type;
    }

    // 判断深度格式是否附带 stencil 平面
    static bool HasStencilComponent(VkFormat format)
    {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
               format == VK_FORMAT_D24_UNORM_S8_UINT  ||
               format == VK_FORMAT_D16_UNORM_S8_UINT;
    }

    VkImageAspectFlags VansVKImage::ConvertImageViewAspect(VkImageUsageFlags usage, VkFormat format)
    {
        VkImageAspectFlags aspect = 0;
        if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
            // 带 stencil 格式需要在 barrier aspect 中包含 stencil，
            // 确保两个平面都能被 pipeline barrier 正确转换
            if (HasStencilComponent(format))
            {
                aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
            return aspect;
        }
        if (usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
        {
            aspect |= VK_IMAGE_ASPECT_COLOR_BIT;
            return aspect;
        }
        return aspect;
    }

    bool VansVKImage::CreateVulkanImage(VkDevice& logical_device, VkExtent3D size, VkFormat format, uint32_t mip_num, uint32_t layer_num, VkImageType type, VkImageUsageFlags usage, VkSampleCountFlagBits samples, bool isCube, bool need_raw_Data, bool combined_sampler, VkSamplerAddressMode addressMode)
    {
        m_ImageDimention = size;
        //VK_IMAGE_TILING_OPTIMAL : 贴图在内存里的排布往往不是线性的，需要适配硬件快速采样，一般linear的tiling用于直接从COU上初始化或读取

        const auto& sharingFamilies = VansVKMemoryManager::GetInstance()->GetSharingQueueFamilyIndices();

        m_ImageCreateInfo =
        {
             VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
             nullptr,
             isCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u,
             type,
             format,
             size,
             mip_num,
             isCube ? 6 * layer_num : layer_num,
             samples,
             VK_IMAGE_TILING_OPTIMAL,
             //如果需要被sample需要设置sample bit
             usage,
             VK_SHARING_MODE_CONCURRENT,
             static_cast<uint32_t>(sharingFamilies.size()),
             sharingFamilies.data(),
             VK_IMAGE_LAYOUT_UNDEFINED
        };
        //由于image的内存排布是不确定的，就选哟一个stagig resource去read 或者初始化一个image
        m_ImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        /*
        When we want to use an image as a sampled image, before we load data from it inside
        shaders, we need to transition the image's layout to
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL.
        */

        /*
        * Before we can load or store data in storage images from shaders, we must perform a
        transition to a VK_IMAGE_LAYOUT_GENERAL layout. It is the only layout in which these
        operations are supported.
        */

        VkResult result = VK_SUCCESS;

        // Render targets / depth attachments are large, single-purpose, and
        // benefit from a dedicated allocation.
        const bool preferDedicated =
            (usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                    | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                    | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT)) != 0;

        if (!VansVKMemoryAllocator::Get().CreateImage(
                m_ImageCreateInfo, VansMemoryUsage::GpuOnly,
                preferDedicated, m_VansVKImage, m_VansVKImageAllocation))
        {
            VANS_LOG_ERROR("Could not create image / allocate memory (VMA).");
            return false;
        }

        //create image view
        VkImageViewType view_type = ConvertImageViewType(type, isCube, layer_num);
        // m_ImageAspect 包含所有平面（depth+stencil），用于 pipeline barrier 覆盖全部平面
        m_ImageAspect = ConvertImageViewAspect(usage, format);
        // 采样 view 只能有单一 aspect：depth-stencil 图像的采样 view 只用 DEPTH_BIT，
        // 否则 Vulkan 验证层报错（combined aspect view 不可绑定为 sampler2D）
        VkImageAspectFlags viewAspect = m_ImageAspect;
        if ((m_ImageAspect & VK_IMAGE_ASPECT_DEPTH_BIT) && (m_ImageAspect & VK_IMAGE_ASPECT_STENCIL_BIT))
        {
            viewAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        VkImageViewCreateInfo image_view_create_info =
        {
             VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
             nullptr,
             0,
             m_VansVKImage,
             view_type,
             format,
             {
                 VK_COMPONENT_SWIZZLE_IDENTITY,
                 VK_COMPONENT_SWIZZLE_IDENTITY,
                 VK_COMPONENT_SWIZZLE_IDENTITY,
                 VK_COMPONENT_SWIZZLE_IDENTITY
             },
             {
                 viewAspect,
                 0,
                 VK_REMAINING_MIP_LEVELS,
                 0,
                 VK_REMAINING_ARRAY_LAYERS
             }
        };

#ifdef _DEBUG
        VkDebugUtilsObjectNameInfoEXT nameInfo = {};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_VansVKImage);
        nameInfo.pObjectName = "image";
        vkSetDebugUtilsObjectNameEXT(logical_device, &nameInfo);
#endif

        //imgee view创建时必须时需要绑定memory
        result = vkCreateImageView(logical_device, &image_view_create_info, nullptr, &m_VansVKImageView);
        if (VK_SUCCESS != result)
        {
            VANS_LOG_ERROR("Could not create an image view.");
            return false;
        }

        // 对 depth+stencil 格式额外创建 combined attachment view（DEPTH|STENCIL），
        // 用于 framebuffer attachment，以支持 stencil 写入/读取操作
        if ((m_ImageAspect & VK_IMAGE_ASPECT_DEPTH_BIT) && (m_ImageAspect & VK_IMAGE_ASPECT_STENCIL_BIT))
        {
            VkImageViewCreateInfo dsViewInfo      = image_view_create_info;
            dsViewInfo.subresourceRange.aspectMask = m_ImageAspect; // DEPTH|STENCIL
            result = vkCreateImageView(logical_device, &dsViewInfo, nullptr, &m_DepthStencilView);
            if (VK_SUCCESS != result)
            {
                VANS_LOG_ERROR("Could not create depth-stencil attachment image view.");
                return false;
            }
        }

        //创建多个mip的imageview
        if (mip_num > 1)
        {
            m_VansVKImageMipViews.resize(mip_num);
            for (int miplevel = 0; miplevel < mip_num; miplevel++)
            {
                VkImageViewCreateInfo mip_view_create_info =
                {
                     VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                     nullptr,
                     0,
                     m_VansVKImage,
                     view_type,
                     format,
                     {
                         VK_COMPONENT_SWIZZLE_IDENTITY,
                         VK_COMPONENT_SWIZZLE_IDENTITY,
                         VK_COMPONENT_SWIZZLE_IDENTITY,
                         VK_COMPONENT_SWIZZLE_IDENTITY
                     },
                     {
                         m_ImageAspect,
                         miplevel,
                         1,
                         0,
                         VK_REMAINING_ARRAY_LAYERS
                     }
                };
                vkCreateImageView(logical_device, &mip_view_create_info, nullptr, &m_VansVKImageMipViews[miplevel]);
            }
        }

        m_Sampler = VK_NULL_HANDLE;
        if (combined_sampler)
        {
            bool result = VansVKSampler::CreateSampler(
                logical_device,
                m_Sampler,
                VK_FILTER_LINEAR,
                VK_FILTER_LINEAR,
                VK_SAMPLER_MIPMAP_MODE_LINEAR,
                addressMode,
                addressMode,
                addressMode,
                0.0f,
                VK_FALSE,
                0.0f,
                VK_FALSE,
                VK_COMPARE_OP_NEVER,
                0.0f,
                mip_num - 1,
                VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
                VK_FALSE
            );

            if (!result)
            {
                VANS_LOG_ERROR("sampler created failed");
                return false;
            }
        }


        return true;
    }

    void VansVKImage::DestroyVulkanImage(VkDevice& logical_device)
    {
		for (VkImageView view : m_OwnedAuxiliaryViews)
			if (view != VK_NULL_HANDLE) vkDestroyImageView(logical_device, view, nullptr);
		m_OwnedAuxiliaryViews.clear();
        if (VK_NULL_HANDLE != m_DepthStencilView)
        {
            vkDestroyImageView(logical_device, m_DepthStencilView, nullptr);
            m_DepthStencilView = VK_NULL_HANDLE;
        }
        if (VK_NULL_HANDLE != m_VansVKImageView)
        {
            vkDestroyImageView(logical_device, m_VansVKImageView, nullptr);
            m_VansVKImageView = VK_NULL_HANDLE;
        }

        for (auto& view : m_VansVKImageMipViews)
        {
            if (VK_NULL_HANDLE != view)
            {
                vkDestroyImageView(logical_device, view, nullptr);
                view = VK_NULL_HANDLE;
            }
        }
        m_VansVKImageMipViews.clear();

        // VMA owns both the VkImage and the VkDeviceMemory.
        VansVKMemoryAllocator::Get().DestroyImage(m_VansVKImage, m_VansVKImageAllocation);

        if (VK_NULL_HANDLE != m_Sampler)
        {
            VansVKSampler::DestroySampler(logical_device, m_Sampler);
            m_Sampler = VK_NULL_HANDLE;
        }
    }

    void VansVKImage::SetRawImageData(VkDevice& logical_device, void* data, int size)
    {
        VansVKMemoryAllocator::Get().WriteToAllocation(m_VansVKImageAllocation, 0, size, data);
    }

    void VansVKImage::AddTransitionImageAccess(ImageTransition& transition)
    {
        m_ImageMemoryBarriers.push_back(
            {
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                nullptr,
                transition.CurrentAccess,
                transition.NewAccess,
                transition.CurrentLayout,
                transition.NewLayout,
                transition.CurrentQueueFamily,
                transition.NewQueueFamily,
                transition.Image,
                {
                    transition.Aspect,
                    0,
                    VK_REMAINING_MIP_LEVELS,
                    0,
                    VK_REMAINING_ARRAY_LAYERS
                }
            });
    }


    void VansVKImage::SetImageMemoryBarrier(VkPipelineStageFlags generating_stages, VkPipelineStageFlags consuming_stages, ImageTransition imageTransition)
    {
        this->AddTransitionImageAccess(imageTransition);
        VansVKMemoryManager::GetInstance()->SetImageMemoryBarrier(m_ImageMemoryBarriers, generating_stages, consuming_stages);
        m_ImageMemoryBarriers.clear();
        //更新下新layout
        m_ImageLayout = imageTransition.NewLayout;
    }

    VkImageView VansVKImage::GetImageView()
    {
        return m_VansVKImageView;
    }

    VkImageView VansVKImage::GetImageMipView(int target_mip)
    {
        if (target_mip >= m_VansVKImageMipViews.size())
        {
            return m_VansVKImageView;
        }

        return m_VansVKImageMipViews[target_mip];
    }

    VkImageLayout VansVKImage::GetImageLayout()
    {
        return m_ImageLayout;
    }

    VkSampler VansVKImage::GetSampler()
    {
        return m_Sampler;
    }

    VkImage VansVKImage::GetImage()
    {
        return m_VansVKImage;
    }

    VkImageAspectFlags VansVKImage::GetImageAspect()
    {
        return m_ImageAspect;
    }

    VkExtent3D VansVKImage::GetImageDimension()
    {
        return m_ImageDimention;
    }
}
