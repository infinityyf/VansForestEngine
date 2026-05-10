#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKDescriptorManager.h"
#include "../../Util/VansLog.h"
#include <iostream>

VansGraphics::VansVKDescriptorManager* VansGraphics::VansVKDescriptorManager::instance = nullptr;

VansGraphics::VansVKDescriptorManager::VansVKDescriptorManager()
{
}

void VansGraphics::VansVKDescriptorManager::CreateDescriptorPool(bool free_individual_sets)
{
	//类似command buffer pool，用于allocate descriptor sets
	//指定最大set数量，以及每个类型额描述符数量
	//但是不能多线程同时分配
	VkDescriptorPoolCreateInfo descriptor_pool_create_info = 
	{
		 VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		 nullptr,
		 // 注意：必须包含 UPDATE_AFTER_BIND_BIT，以支持全局 bindless 纹理数组在 GPU 执行期间更新
		 (free_individual_sets ? VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT : 0u)
		     | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		 m_MaxSetsCount,
		 static_cast<uint32_t>(m_DescriptorPoolSizes.size()),
		 m_DescriptorPoolSizes.data()
	};

	VkResult result = vkCreateDescriptorPool(m_LogicalDevice, &descriptor_pool_create_info, nullptr, &m_DescriptorPool);
	if (VK_SUCCESS != result) 
	{
		VANS_LOG_ERROR("Could not create a descriptor pool.");
	}
}

bool VansGraphics::VansVKDescriptorManager::ResetDescriptorPool()
{
	VkResult result = vkResetDescriptorPool(m_LogicalDevice, m_DescriptorPool, 0);
	if (VK_SUCCESS != result) {
		VANS_LOG_ERROR("Error occurred during descriptor pool reset.");
		return false;
	}
	return true;
}

void VansGraphics::VansVKDescriptorManager::DestroyDescriptorPool()
{
	if (VK_NULL_HANDLE != m_DescriptorPool)
	{
		vkDestroyDescriptorPool(m_LogicalDevice, m_DescriptorPool, nullptr);
		m_DescriptorPool = VK_NULL_HANDLE;
	}
}


bool VansGraphics::VansVKDescriptorManager::CreateDesciptorSetLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings, VkDescriptorSetLayout& descriptor_set_layout)
{
	//每一个资源都需要被一个descriptor set包含
	//这里记录了梭有的bingding信息，binding point 和类型
	//VkDescriptorSetLayoutBinding bindings = 
	//{
	//	0,
	//	VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	//	1,
	//	VK_SHADER_STAGE_VERTEX_BIT,
	//	nullptr
	//};
	VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = 
	{
		 VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		 nullptr,
		 0,
		 static_cast<uint32_t>(bindings.size()),
		 bindings.data()
	};

	VkResult result = vkCreateDescriptorSetLayout(m_LogicalDevice, &descriptor_set_layout_create_info, nullptr, &descriptor_set_layout);
	if (VK_SUCCESS != result) 
	{
		VANS_LOG_ERROR("Could not create a layout for descriptor sets.");
		return false;
	}
	return true;
}

bool VansGraphics::VansVKDescriptorManager::CreateDesciptorSetLayoutWithFlags(
	const std::vector<VkDescriptorSetLayoutBinding>& bindings,
	const std::vector<VkDescriptorBindingFlags>&     bindingFlags,
	VkDescriptorSetLayoutCreateFlags                 layoutFlags,
	VkDescriptorSetLayout&                           descriptor_set_layout)
{
	// bindingFlags 长度须与 bindings 一致
	VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
	flagsInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	flagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
	flagsInfo.pBindingFlags = bindingFlags.data();

	VkDescriptorSetLayoutCreateInfo layoutCI{};
	layoutCI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutCI.pNext        = &flagsInfo;
	layoutCI.flags        = layoutFlags;
	layoutCI.bindingCount = static_cast<uint32_t>(bindings.size());
	layoutCI.pBindings    = bindings.data();

	VkResult result = vkCreateDescriptorSetLayout(m_LogicalDevice, &layoutCI, nullptr, &descriptor_set_layout);
	if (VK_SUCCESS != result)
	{
		VANS_LOG_ERROR("Could not create descriptor set layout with binding flags.");
		return false;
	}
	return true;
}

void VansGraphics::VansVKDescriptorManager::DestroyDescriptorSetLayout(VkDescriptorSetLayout& descriptor_set_layout)
{
	if (VK_NULL_HANDLE != descriptor_set_layout) 
	{
		vkDestroyDescriptorSetLayout(m_LogicalDevice, descriptor_set_layout, nullptr);
		descriptor_set_layout = VK_NULL_HANDLE;
	}
}

bool VansGraphics::VansVKDescriptorManager::AllocateDescriptorSet(const std::vector<VkDescriptorSetLayout>& discriptor_set_layout, std::vector<VkDescriptorSet>& descriptor_sets)
{
	VkDescriptorSetAllocateInfo descriptor_set_allocate_info = 
	{
		 VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		 nullptr,
		 m_DescriptorPool,
		 static_cast<uint32_t>(discriptor_set_layout.size()),
		 discriptor_set_layout.data()
	};

	descriptor_sets.resize(discriptor_set_layout.size());
	VkResult result = vkAllocateDescriptorSets(m_LogicalDevice,&descriptor_set_allocate_info, descriptor_sets.data());
	if (VK_SUCCESS != result) 
	{
		VANS_LOG_ERROR("Could not allocate descriptor sets.");
		return false;
	}
	return true;
}

bool VansGraphics::VansVKDescriptorManager::DestroyDescriptorSet(std::vector<VkDescriptorSet>& descriptor_sets)
{
	if (descriptor_sets.empty())
		return true;

	VkResult result = vkFreeDescriptorSets(m_LogicalDevice, m_DescriptorPool, static_cast<uint32_t>(descriptor_sets.size()), descriptor_sets.data());
	if (VK_SUCCESS != result) 
	{
		VANS_LOG_ERROR("Error occurred during freeing descriptor sets.");
		return false;
	}
	descriptor_sets.clear();
	return true;
}

void VansGraphics::VansVKDescriptorManager::UpdateDescriptorSets()
{
	std::vector<VkWriteDescriptorSet> write_descriptors;
	//TargetArrayElement is the starting element in that array.
	//If the descriptor binding identified by dstSet and dstBinding has a descriptor type of 
	//VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK then dstArrayElement specifies the starting byte offset within the binding.
	for (auto& image_descriptor : m_ImageDescInfos)
	{
		write_descriptors.push_back(
			{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				nullptr,
				image_descriptor.TargetDescriptorSet,
				image_descriptor.TargetDescriptorBinding,
				image_descriptor.TargetArrayElement,
				static_cast<uint32_t>(image_descriptor.ImageInfos.size()),
				image_descriptor.TargetDescriptorType,
				image_descriptor.ImageInfos.data(),
				nullptr,
				nullptr
			});
	}
	for (auto& buffer_descriptor : m_BufferDescInfos)
	{
		write_descriptors.push_back(
			{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				nullptr,
				buffer_descriptor.TargetDescriptorSet,
				buffer_descriptor.TargetDescriptorBinding,
				buffer_descriptor.TargetArrayElement,
				static_cast<uint32_t>(buffer_descriptor.BufferInfos.size()),
				buffer_descriptor.TargetDescriptorType,
				nullptr,
				buffer_descriptor.BufferInfos.data(),
				nullptr
			});
	}
	for (auto& texel_buffer_descriptor : m_TexelBufferDescInfos)
	{
		write_descriptors.push_back(
			{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				nullptr,
				texel_buffer_descriptor.TargetDescriptorSet,
				texel_buffer_descriptor.TargetDescriptorBinding,
				texel_buffer_descriptor.TargetArrayElement,
				static_cast<uint32_t>(texel_buffer_descriptor.TexelBufferViews.size()),
				texel_buffer_descriptor.TargetDescriptorType,
				nullptr,
				nullptr,
				texel_buffer_descriptor.TexelBufferViews.data()
			});
	}
	for (auto& as_descriptor : m_RayTraceASInfos)
	{
		VkWriteDescriptorSetAccelerationStructureKHR tlasWrite{};
		tlasWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
		tlasWrite.accelerationStructureCount = 1;
		tlasWrite.pAccelerationStructures = &as_descriptor.TargetAS;

		write_descriptors.push_back(
			{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				& tlasWrite,
				as_descriptor.TargetDescriptorSet,
				as_descriptor.TargetDescriptorBinding,
				as_descriptor.TargetArrayElement,
				1, // 需要和accelerationStructureCount保持一致
				as_descriptor.TargetDescriptorType,
				nullptr,
				nullptr,
				nullptr
			});
	}

	std::vector<VkCopyDescriptorSet> copy_descriptors;
	for (auto& copy_descriptor : m_CopyDescInfos)
	{
		copy_descriptors.push_back(
			{
				VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET,
				nullptr,
				copy_descriptor.SourceDescriptorSet,
				copy_descriptor.SourceDescriptorBinding,
				copy_descriptor.SourceArrayElement,
				copy_descriptor.TargetDescriptorSet,
				copy_descriptor.TargetDescriptorBinding,
				copy_descriptor.TargetArrayElement,
				copy_descriptor.DescriptorCount
			});
	}

	vkUpdateDescriptorSets(m_LogicalDevice, static_cast<uint32_t>(write_descriptors.size()), write_descriptors.data(), static_cast<uint32_t>(copy_descriptors.size()), copy_descriptors.data());
}

void VansGraphics::VansVKDescriptorManager::DirectUpdateImageDescriptors(
	VkDescriptorSet dstSet,
	uint32_t        binding,
	uint32_t        firstElement,
	const std::vector<VkDescriptorImageInfo>& imageInfos,
	VkDescriptorType type)
{
	if (dstSet == VK_NULL_HANDLE || imageInfos.empty())
		return;

	VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	write.dstSet          = dstSet;
	write.dstBinding      = binding;
	write.dstArrayElement = firstElement;
	write.descriptorCount = static_cast<uint32_t>(imageInfos.size());
	write.descriptorType  = type;
	write.pImageInfo      = imageInfos.data();

	vkUpdateDescriptorSets(m_LogicalDevice, 1, &write, 0, nullptr);
}

