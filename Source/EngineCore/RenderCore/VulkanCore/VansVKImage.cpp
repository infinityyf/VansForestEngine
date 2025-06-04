#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKImage.h"
#include "VansVKMemoryManager.h"
#include "VansVKSampler.h"
#include <iostream>

VkImageViewType VansVulkan::VansVKImage::ConvertImageViewType(VkImageType type, bool isCube, int layer_num)
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

VkImageAspectFlags VansVulkan::VansVKImage::ConvertImageViewAspect(VkImageUsageFlags usage)
{
    VkImageAspectFlags aspect = 0;
    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
    {
		aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
        return aspect;
	}
    if (usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
    {
        aspect |= VK_IMAGE_ASPECT_COLOR_BIT;
        return aspect;
    }
	return aspect;
}

bool VansVulkan::VansVKImage::CreateVulkanImage(VkDevice& logical_device, VkExtent3D size, VkFormat format, uint32_t mip_num, uint32_t layer_num, VkImageType type, VkImageUsageFlags usage, VkSampleCountFlagBits samples, bool isCube, bool need_raw_Data, bool combined_sampler)
{
    //VK_IMAGE_TILING_OPTIMAL : 贴图在内存里的排布往往不是线性的，需要适配硬件快速采样，一般linear的tiling用于直接从COU上初始化或读取

    VkImageCreateInfo image_create_info = 
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
         VK_SHARING_MODE_EXCLUSIVE,
         0,
         nullptr,
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

    VkResult result = vkCreateImage(logical_device, &image_create_info, nullptr, &m_VansVKImage);
    if (VK_SUCCESS != result) 
    {
        std::cout << "Could not create an image." << std::endl;
        return false;
    }


    //create image view
    VkImageViewType view_type = ConvertImageViewType(type, isCube, layer_num);
    m_ImageAspect = ConvertImageViewAspect(usage);
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
             m_ImageAspect,
             0,
             VK_REMAINING_MIP_LEVELS,
             0,
             VK_REMAINING_ARRAY_LAYERS
         }
    };

    //给image开辟内存
    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(logical_device, m_VansVKImage, &memory_requirements);

    m_VansVKImageMemory = VK_NULL_HANDLE;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    //if (need_raw_Data)
    //{
    //    memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    //}
    bool allocateResult = VansVKMemoryManager::GetInstance()->AllocateMemory(memory_requirements, m_VansVKImageMemory, memory_properties);
    if (!allocateResult || VK_NULL_HANDLE == m_VansVKImageMemory)
    {
        std::cout << "Could not allocate memory for an image." << std::endl;
        return false;
    }

    result = vkBindImageMemory(logical_device, m_VansVKImage, m_VansVKImageMemory,0);
    if (VK_SUCCESS != result) 
    {
        std::cout << "Could not bind memory object to an image." << std::endl;
        return false;
    }

    //imgee view创建时必须时需要绑定memory
    result = vkCreateImageView(logical_device, &image_view_create_info, nullptr, &m_VansVKImageView);
    if (VK_SUCCESS != result)
    {
        std::cout << "Could not create an image view." << std::endl;
        return false;
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
            VK_SAMPLER_ADDRESS_MODE_REPEAT, 
            VK_SAMPLER_ADDRESS_MODE_REPEAT, 
            VK_SAMPLER_ADDRESS_MODE_REPEAT, 
            0.0f, 
            VK_FALSE, 
            0.0f, 
            VK_FALSE, 
            VK_COMPARE_OP_NEVER, 
            0.0f, 
            0.0f, 
            VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK, 
            VK_FALSE
            );

        if (!result)
        {
            std::cout << "sampler created failed" << std::endl;
            return false;
        }
    }


    return true;
}

void VansVulkan::VansVKImage::DestroyVulkanImage(VkDevice& logical_device)
{
    if (VK_NULL_HANDLE != m_VansVKImageView) 
    {
        vkDestroyImageView(logical_device, m_VansVKImageView, nullptr);
        m_VansVKImageView = VK_NULL_HANDLE;
    }

    if (VK_NULL_HANDLE != m_VansVKImage) 
    {
        vkDestroyImage(logical_device, m_VansVKImage, nullptr);
        m_VansVKImage = VK_NULL_HANDLE;
    }

    VansVKMemoryManager::GetInstance()->FreeMemory(m_VansVKImageMemory);

    if (VK_NULL_HANDLE != m_Sampler)
    {
        VansVKSampler::DestroySampler(logical_device, m_Sampler);
        m_Sampler = VK_NULL_HANDLE;
    }
}

void VansVulkan::VansVKImage::SetRawImageData(VkDevice& logical_device, void* data, int size)
{
    VansVKMemoryManager::GetInstance()->MapMemoryFromHost(m_VansVKImageMemory, 0, size, data, true);
}

void VansVulkan::VansVKImage::AddTransitionImageAccess(ImageTransition& transition)
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


void VansVulkan::VansVKImage::SetImageMemoryBarrier(VkPipelineStageFlags generating_stages, VkPipelineStageFlags consuming_stages, ImageTransition imageTransition)
{
    this->AddTransitionImageAccess(imageTransition);
    VansVKMemoryManager::GetInstance()->SetImageMemoryBarrier(m_ImageMemoryBarriers, generating_stages, consuming_stages);
    m_ImageMemoryBarriers.clear();
    //更新下新layout
    m_ImageLayout = imageTransition.NewLayout;
}

VkImageView VansVulkan::VansVKImage::GetImageView()
{
    return m_VansVKImageView;
}

VkImageLayout VansVulkan::VansVKImage::GetImageLayout()
{
    return m_ImageLayout;
}

VkSampler VansVulkan::VansVKImage::GetSampler()
{
    return m_Sampler;
}

VkImage VansVulkan::VansVKImage::GetImage()
{
    return m_VansVKImage;
}

VkImageAspectFlags VansVulkan::VansVKImage::GetImageAspect()
{
    return m_ImageAspect;
}
