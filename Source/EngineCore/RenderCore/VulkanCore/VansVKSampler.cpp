#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKSampler.h"
#include <iostream>

bool VansVulkan::VansVKSampler::CreateSampler(
	VkDevice&				logical_device,
	VkSampler&				sampler,
	VkFilter                magFilter,
	VkFilter                minFilter,
	VkSamplerMipmapMode     mipmapMode,
	VkSamplerAddressMode    addressModeU,
	VkSamplerAddressMode    addressModeV,
	VkSamplerAddressMode    addressModeW,
	float                   mipLodBias,
	VkBool32                anisotropyEnable,
	float                   maxAnisotropy,
	VkBool32                compareEnable,
	VkCompareOp             compareOp,
	float                   minLod,
	float                   maxLod,
	VkBorderColor           borderColor,
	VkBool32                unnormalizedCoordinates
)
{
	VkSamplerCreateInfo sampler_create_info =
	{
		 VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		 nullptr,
		 0,
		 magFilter,
		 minFilter,
		 mipmapMode,
		 addressModeU,
		 addressModeV,
		 addressModeW,
		 mipLodBias,
		 anisotropyEnable,
		 maxAnisotropy,
		 compareEnable,
		 compareOp,
		 minLod,
		 maxLod,
		 borderColor,
		 unnormalizedCoordinates,
	};

	VkResult result = vkCreateSampler(logical_device, &sampler_create_info, nullptr, &sampler);
	if (VK_SUCCESS != result)
	{
		std::cout << "Could not create sampler." << std::endl;
		return false;
	}
	return true;
}

void VansVulkan::VansVKSampler::DestroySampler(VkDevice& logical_device, VkSampler& sampler)
{
	if (VK_NULL_HANDLE != sampler)
	{
		vkDestroySampler(logical_device, sampler, nullptr);
		sampler = VK_NULL_HANDLE;
	}
}