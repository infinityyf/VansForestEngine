#pragma once
#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif
#include "vulkan/vulkan.h"
#include <vector>


namespace VansVulkan
{
	class VansVKSampler
	{
	public:
		VkSampler m_Sampler;

		static bool CreateSampler(
			VkDevice&				logical_device,
			VkSampler&				sampler,
			VkFilter                magFilter = VK_FILTER_LINEAR,
			VkFilter                minFilter = VK_FILTER_LINEAR,
			VkSamplerMipmapMode     mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			VkSamplerAddressMode    addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			VkSamplerAddressMode    addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			VkSamplerAddressMode    addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			float                   mipLodBias = 0,
			VkBool32                anisotropyEnable = false,
			float                   maxAnisotropy = 4,
			VkBool32                compareEnable = false,
			VkCompareOp             compareOp = VK_COMPARE_OP_ALWAYS,
			float                   minLod = 0,
			float                   maxLod = 8,
			VkBorderColor           borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			VkBool32                unnormalizedCoordinates = false
		);

		static void DestroySampler(VkDevice& logical_device, VkSampler& sampler);
	};
}



