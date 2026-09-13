#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKDescriptorManager.h"
#include "../../Util/VansLog.h"
#include <iostream>
#include <unordered_map>
#include <algorithm>
#include <numeric>

VansGraphics::VansVKDescriptorManager* VansGraphics::VansVKDescriptorManager::instance = nullptr;

VansGraphics::VansVKDescriptorManager::VansVKDescriptorManager()
{
}

bool VansGraphics::VansVKDescriptorManager::CreateDescriptorPoolHandle(VkDescriptorPoolCreateFlags flags, VkDescriptorPool& outPool)
{
	VkDescriptorPoolCreateInfo descriptor_pool_create_info =
	{
		 VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		 nullptr,
		 flags,
		 m_MaxSetsCount,
		 static_cast<uint32_t>(m_DescriptorPoolSizes.size()),
		 m_DescriptorPoolSizes.data()
	};

	VkResult result = VansGraphics::vkCreateDescriptorPool(m_LogicalDevice, &descriptor_pool_create_info, nullptr, &outPool);
	if (VK_SUCCESS != result)
	{
		VANS_LOG_ERROR("Could not create a descriptor pool.");
		outPool = VK_NULL_HANDLE;
		return false;
	}

	return true;
}

bool VansGraphics::VansVKDescriptorManager::UsesUpdateAfterBindPool(const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts) const
{
	std::lock_guard<std::mutex> lock(m_LayoutMutex);
	for (VkDescriptorSetLayout layout : descriptorSetLayouts)
	{
		if (m_UpdateAfterBindLayouts.find(layout) != m_UpdateAfterBindLayouts.end())
		{
			return true;
		}
	}

	return false;
}

bool VansGraphics::VansVKDescriptorManager::AllocateDescriptorSetFromPool(
	VkDescriptorPool descriptorPool,
	const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
	std::vector<VkDescriptorSet>& descriptorSets,
	VkResult& outResult,
	VansDescriptorLifetimeRole lifetimeRole)
{
	if (descriptorPool == VK_NULL_HANDLE)
	{
		outResult = VK_ERROR_INITIALIZATION_FAILED;
		return false;
	}

	VkDescriptorSetAllocateInfo descriptor_set_allocate_info =
	{
		 VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		 nullptr,
		 descriptorPool,
		 static_cast<uint32_t>(descriptorSetLayouts.size()),
		 descriptorSetLayouts.data()
	};

	descriptorSets.assign(descriptorSetLayouts.size(), VK_NULL_HANDLE);
	outResult = VansGraphics::vkAllocateDescriptorSets(m_LogicalDevice, &descriptor_set_allocate_info, descriptorSets.data());
	if (outResult != VK_SUCCESS)
	{
		descriptorSets.clear();
		return false;
	}

	for (VkDescriptorSet descriptorSet : descriptorSets)
	{
		if (descriptorSet != VK_NULL_HANDLE)
		{
			m_DescriptorSetPools[descriptorSet] = descriptorPool;
			m_DescriptorSetRoles[descriptorSet] = lifetimeRole;
		}
	}

	return true;
}

void VansGraphics::VansVKDescriptorManager::CreateDescriptorPool(bool free_individual_sets)
{
	// Standard descriptors should not inherit UPDATE_AFTER_BIND. The dedicated
	// update-after-bind pool is selected only for layouts that explicitly ask for it.
	m_DescriptorPoolFlags = free_individual_sets ? VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT : 0u;
	CreateDescriptorPoolHandle(m_DescriptorPoolFlags, m_DescriptorPool);
	if (m_DescriptorPool != VK_NULL_HANDLE)
	{
		m_DescriptorPools.push_back(m_DescriptorPool);
	}
	CreateDescriptorPoolHandle(
		m_DescriptorPoolFlags | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		m_UpdateAfterBindDescriptorPool);
	if (m_UpdateAfterBindDescriptorPool != VK_NULL_HANDLE)
	{
		m_UpdateAfterBindDescriptorPools.push_back(m_UpdateAfterBindDescriptorPool);
	}
}

VansGraphics::VansDescriptorPoolDiagnostics VansGraphics::VansVKDescriptorManager::GetDiagnostics() const
{
	std::lock_guard<std::mutex> lock(m_LayoutMutex);
	VansDescriptorPoolDiagnostics diagnostics{};
	diagnostics.standardPoolCount = static_cast<uint32_t>(m_DescriptorPools.size());
	diagnostics.updateAfterBindPoolCount = static_cast<uint32_t>(m_UpdateAfterBindDescriptorPools.size());
	diagnostics.trackedDescriptorSetCount = static_cast<uint32_t>(m_DescriptorSetPools.size());
	diagnostics.updateAfterBindLayoutCount = static_cast<uint32_t>(m_UpdateAfterBindLayouts.size());
	diagnostics.sharedLayoutCount = static_cast<uint32_t>(m_SharedLayouts.size());
	diagnostics.layoutCacheHits = m_LayoutCacheHits;
	for (const auto& descriptorRole : m_DescriptorSetRoles)
	{
		switch (descriptorRole.second)
		{
		case VansDescriptorLifetimeRole::GlobalPersistent:
			++diagnostics.globalPersistentSetCount;
			break;
		case VansDescriptorLifetimeRole::ScenePersistent:
			++diagnostics.scenePersistentSetCount;
			break;
		case VansDescriptorLifetimeRole::FrameTransient:
			++diagnostics.frameTransientSetCount;
			break;
		case VansDescriptorLifetimeRole::PassPersistent:
			++diagnostics.passPersistentSetCount;
			break;
		case VansDescriptorLifetimeRole::UploadScratch:
			++diagnostics.uploadScratchSetCount;
			break;
		case VansDescriptorLifetimeRole::RayTracingPersistent:
			++diagnostics.rayTracingPersistentSetCount;
			break;
		}
	}
	return diagnostics;
}

bool VansGraphics::VansVKDescriptorManager::ResetDescriptorPool()
{
	for (VkDescriptorPool descriptorPool : m_DescriptorPools)
	{
		if (descriptorPool == VK_NULL_HANDLE)
		{
			continue;
		}
		VkResult result = VansGraphics::vkResetDescriptorPool(m_LogicalDevice, descriptorPool, 0);
		if (VK_SUCCESS != result) {
			VANS_LOG_ERROR("Error occurred during descriptor pool reset.");
			return false;
		}
	}
	for (VkDescriptorPool descriptorPool : m_UpdateAfterBindDescriptorPools)
	{
		if (descriptorPool == VK_NULL_HANDLE)
		{
			continue;
		}
		VkResult result = VansGraphics::vkResetDescriptorPool(m_LogicalDevice, descriptorPool, 0);
		if (VK_SUCCESS != result) {
			VANS_LOG_ERROR("Error occurred during update-after-bind descriptor pool reset.");
			return false;
		}
	}
	m_DescriptorSetPools.clear();
	m_DescriptorSetRoles.clear();
	return true;
}

void VansGraphics::VansVKDescriptorManager::DestroyDescriptorPool()
{
	std::lock_guard<std::mutex> lock(m_LayoutMutex);
	for (VkDescriptorPool descriptorPool : m_UpdateAfterBindDescriptorPools)
	{
		if (descriptorPool != VK_NULL_HANDLE)
		{
			VansGraphics::vkDestroyDescriptorPool(m_LogicalDevice, descriptorPool, nullptr);
		}
	}
	for (VkDescriptorPool descriptorPool : m_DescriptorPools)
	{
		if (descriptorPool != VK_NULL_HANDLE)
		{
			VansGraphics::vkDestroyDescriptorPool(m_LogicalDevice, descriptorPool, nullptr);
		}
	}
	m_UpdateAfterBindDescriptorPools.clear();
	m_DescriptorPools.clear();
	m_UpdateAfterBindDescriptorPool = VK_NULL_HANDLE;
	m_DescriptorPool = VK_NULL_HANDLE;
	m_UpdateAfterBindLayouts.clear();
	m_DescriptorSetPools.clear();
	m_DescriptorSetRoles.clear();
	// 此处沿用设备关闭时已等待 GPU、释放管线的边界，只销毁一次共享布局。
	for (const auto& entry : m_SharedLayouts)
		VansGraphics::vkDestroyDescriptorSetLayout(m_LogicalDevice, entry.second, nullptr);
	m_SharedLayouts.clear();
	m_SharedLayoutHandles.clear();
	m_LayoutCacheHits = 0;
}


bool VansGraphics::VansVKDescriptorManager::CreateDesciptorSetLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings, VkDescriptorSetLayout& descriptor_set_layout)
{
	return CreateDesciptorSetLayoutWithFlags(bindings, {}, 0, descriptor_set_layout);
}

bool VansGraphics::VansVKDescriptorManager::CreateDesciptorSetLayoutWithFlags(
	const std::vector<VkDescriptorSetLayoutBinding>& bindings,
	const std::vector<VkDescriptorBindingFlags>&     bindingFlags,
	VkDescriptorSetLayoutCreateFlags                 layoutFlags,
	VkDescriptorSetLayout&                           descriptor_set_layout)
{
	descriptor_set_layout = VK_NULL_HANDLE;
	if (!bindingFlags.empty() && bindingFlags.size() != bindings.size()) return false;
	std::lock_guard<std::mutex> lock(m_LayoutMutex);
	std::vector<size_t> order(bindings.size());
	std::iota(order.begin(), order.end(), size_t(0));
	std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return bindings[a].binding < bindings[b].binding; });
	std::vector<uint64_t> key{layoutFlags, static_cast<uint64_t>(bindings.size())};
	bool shareable = true;
	for (size_t n = 0; n < order.size(); ++n)
	{
		const size_t i = order[n];
		const auto& binding = bindings[i];
		if (n && binding.binding == bindings[order[n-1]].binding) return false;
		key.insert(key.end(), {binding.binding, static_cast<uint64_t>(binding.descriptorType),
			binding.descriptorCount, binding.stageFlags, bindingFlags.empty() ? 0u : bindingFlags[i]});
		// immutable sampler 由调用方管理寿命，含此类引用的布局随调用方释放。
		if (binding.pImmutableSamplers && binding.descriptorCount) shareable = false;
	}
	if (shareable)
	{
		const auto found = m_SharedLayouts.find(key);
		if (found != m_SharedLayouts.end())
		{
			descriptor_set_layout = found->second;
			++m_LayoutCacheHits;
			return true;
		}
	}
	// flags 与各 binding 成对参与完整结构比较，不把不同 bindless 语义合并。
	VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
	flagsInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	flagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
	flagsInfo.pBindingFlags = bindingFlags.data();

	VkDescriptorSetLayoutCreateInfo layoutCI{};
	layoutCI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutCI.pNext        = bindingFlags.empty() ? nullptr : &flagsInfo;
	layoutCI.flags        = layoutFlags;
	layoutCI.bindingCount = static_cast<uint32_t>(bindings.size());
	layoutCI.pBindings    = bindings.data();

	VkResult result = VansGraphics::vkCreateDescriptorSetLayout(m_LogicalDevice, &layoutCI, nullptr, &descriptor_set_layout);
	if (VK_SUCCESS != result)
	{
		VANS_LOG_ERROR("Could not create descriptor set layout with binding flags.");
		return false;
	}
	if ((layoutFlags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT) != 0)
	{
		m_UpdateAfterBindLayouts.insert(descriptor_set_layout);
	}
	if (shareable)
	{
		m_SharedLayouts.emplace(std::move(key), descriptor_set_layout);
		m_SharedLayoutHandles.insert(descriptor_set_layout);
	}
	return true;
}

void VansGraphics::VansVKDescriptorManager::ReleaseDescriptorSetLayout(VkDescriptorSetLayout& descriptor_set_layout)
{
	std::lock_guard<std::mutex> lock(m_LayoutMutex);
	if (VK_NULL_HANDLE != descriptor_set_layout) 
	{
		if (m_SharedLayoutHandles.count(descriptor_set_layout) == 0)
		{
			m_UpdateAfterBindLayouts.erase(descriptor_set_layout);
			VansGraphics::vkDestroyDescriptorSetLayout(m_LogicalDevice, descriptor_set_layout, nullptr);
		}
		descriptor_set_layout = VK_NULL_HANDLE;
	}
}

bool VansGraphics::VansVKDescriptorManager::AllocateDescriptorSet(
	const std::vector<VkDescriptorSetLayout>& discriptor_set_layout,
	std::vector<VkDescriptorSet>& descriptor_sets,
	VansDescriptorLifetimeRole lifetimeRole)
{
	if (discriptor_set_layout.empty())
	{
		descriptor_sets.clear();
		return true;
	}

	const bool useUpdateAfterBindPool = UsesUpdateAfterBindPool(discriptor_set_layout);
	std::vector<VkDescriptorPool>& descriptorPools = useUpdateAfterBindPool
		? m_UpdateAfterBindDescriptorPools
		: m_DescriptorPools;
	const VkDescriptorPoolCreateFlags poolFlags = useUpdateAfterBindPool
		? (m_DescriptorPoolFlags | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT)
		: m_DescriptorPoolFlags;

	VkResult lastResult = VK_SUCCESS;
	for (VkDescriptorPool descriptorPool : descriptorPools)
	{
		if (AllocateDescriptorSetFromPool(descriptorPool, discriptor_set_layout, descriptor_sets, lastResult, lifetimeRole))
		{
			return true;
		}
		if (lastResult != VK_ERROR_OUT_OF_POOL_MEMORY && lastResult != VK_ERROR_FRAGMENTED_POOL)
		{
			VANS_LOG_ERROR("Could not allocate descriptor sets. VkResult=" << static_cast<int>(lastResult));
			return false;
		}
	}

	VkDescriptorPool newPool = VK_NULL_HANDLE;
	if (!CreateDescriptorPoolHandle(poolFlags, newPool) || newPool == VK_NULL_HANDLE)
	{
		VANS_LOG_ERROR("Could not grow descriptor pool chain.");
		return false;
	}

	descriptorPools.push_back(newPool);
	if (useUpdateAfterBindPool && m_UpdateAfterBindDescriptorPool == VK_NULL_HANDLE)
	{
		m_UpdateAfterBindDescriptorPool = newPool;
	}
	else if (!useUpdateAfterBindPool && m_DescriptorPool == VK_NULL_HANDLE)
	{
		m_DescriptorPool = newPool;
	}

	if (AllocateDescriptorSetFromPool(newPool, discriptor_set_layout, descriptor_sets, lastResult, lifetimeRole))
	{
		return true;
	}

	VANS_LOG_ERROR("Could not allocate descriptor sets from a newly grown descriptor pool. VkResult=" << static_cast<int>(lastResult));
	return false;
}

bool VansGraphics::VansVKDescriptorManager::DestroyDescriptorSet(std::vector<VkDescriptorSet>& descriptor_sets)
{
	if (descriptor_sets.empty())
		return true;

	std::unordered_map<VkDescriptorPool, std::vector<VkDescriptorSet>> setsByPool;
	for (VkDescriptorSet descriptorSet : descriptor_sets)
	{
		if (descriptorSet == VK_NULL_HANDLE)
		{
			continue;
		}

		auto iter = m_DescriptorSetPools.find(descriptorSet);
		VkDescriptorPool ownerPool = iter != m_DescriptorSetPools.end()
			? iter->second
			: m_DescriptorPool;
		setsByPool[ownerPool].push_back(descriptorSet);
	}

	for (auto& poolSets : setsByPool)
	{
		if (poolSets.first == VK_NULL_HANDLE || poolSets.second.empty())
		{
			continue;
		}

		VkResult result = VansGraphics::vkFreeDescriptorSets(
			m_LogicalDevice,
			poolSets.first,
			static_cast<uint32_t>(poolSets.second.size()),
			poolSets.second.data());
		if (VK_SUCCESS != result)
		{
			VANS_LOG_ERROR("Error occurred during freeing descriptor sets.");
			return false;
		}

		for (VkDescriptorSet descriptorSet : poolSets.second)
		{
			m_DescriptorSetPools.erase(descriptorSet);
			m_DescriptorSetRoles.erase(descriptorSet);
		}
	}
	descriptor_sets.clear();
	return true;
}

void VansGraphics::VansVKDescriptorManager::WriteImageDescriptor(
	VkDescriptorSet dstSet,
	uint32_t binding,
	VkDescriptorType type,
	const std::vector<VkDescriptorImageInfo>& imageInfos,
	uint32_t firstElement)
{
	if (dstSet == VK_NULL_HANDLE || imageInfos.empty())
		return;

	m_ImageDescInfos.push_back({ dstSet, binding, firstElement, type, imageInfos });
}

void VansGraphics::VansVKDescriptorManager::WriteBufferDescriptor(
	VkDescriptorSet dstSet,
	uint32_t binding,
	VkDescriptorType type,
	const std::vector<VkDescriptorBufferInfo>& bufferInfos,
	uint32_t firstElement)
{
	if (dstSet == VK_NULL_HANDLE || bufferInfos.empty())
		return;

	m_BufferDescInfos.push_back({ dstSet, binding, firstElement, type, bufferInfos });
}

void VansGraphics::VansVKDescriptorManager::WriteTexelBufferDescriptor(
	VkDescriptorSet dstSet,
	uint32_t binding,
	VkDescriptorType type,
	const std::vector<VkBufferView>& texelBufferViews,
	uint32_t firstElement)
{
	if (dstSet == VK_NULL_HANDLE || texelBufferViews.empty())
		return;

	m_TexelBufferDescInfos.push_back({ dstSet, binding, firstElement, type, texelBufferViews });
}

void VansGraphics::VansVKDescriptorManager::WriteAccelerationStructureDescriptor(
	VkDescriptorSet dstSet,
	uint32_t binding,
	VkAccelerationStructureKHR accelerationStructure,
	uint32_t firstElement)
{
	if (dstSet == VK_NULL_HANDLE || accelerationStructure == VK_NULL_HANDLE)
		return;

	m_RayTraceASInfos.push_back({
		dstSet,
		binding,
		firstElement,
		VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
		accelerationStructure
	});
}

void VansGraphics::VansVKDescriptorManager::CopyDescriptor(
	VkDescriptorSet dstSet,
	uint32_t dstBinding,
	VkDescriptorSet srcSet,
	uint32_t srcBinding,
	uint32_t descriptorCount,
	uint32_t dstArrayElement,
	uint32_t srcArrayElement)
{
	if (dstSet == VK_NULL_HANDLE || srcSet == VK_NULL_HANDLE || descriptorCount == 0)
		return;

	m_CopyDescInfos.push_back({
		dstSet,
		dstBinding,
		dstArrayElement,
		srcSet,
		srcBinding,
		srcArrayElement,
		descriptorCount
	});
}

void VansGraphics::VansVKDescriptorManager::CommitDescriptorUpdates()
{
	UpdateDescriptorSets();
	ResetState();
}

void VansGraphics::VansVKDescriptorManager::UpdateDescriptorSets()
{
	std::vector<VkWriteDescriptorSet> write_descriptors;
	std::vector<VkWriteDescriptorSetAccelerationStructureKHR> acceleration_structure_writes;
	write_descriptors.reserve(
		m_ImageDescInfos.size()
		+ m_BufferDescInfos.size()
		+ m_TexelBufferDescInfos.size()
		+ m_RayTraceASInfos.size());
	// VkWriteDescriptorSet::pNext must remain valid until vkUpdateDescriptorSets
	// consumes the complete batch. Keep every AS extension structure in stable
	// storage instead of pointing at a loop-local temporary.
	acceleration_structure_writes.reserve(m_RayTraceASInfos.size());
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
		acceleration_structure_writes.push_back({});
		auto& tlasWrite = acceleration_structure_writes.back();
		tlasWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
		tlasWrite.accelerationStructureCount = 1;
		tlasWrite.pAccelerationStructures = &as_descriptor.TargetAS;

		write_descriptors.push_back(
			{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				&tlasWrite,
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

	VansGraphics::vkUpdateDescriptorSets(m_LogicalDevice, static_cast<uint32_t>(write_descriptors.size()), write_descriptors.data(), static_cast<uint32_t>(copy_descriptors.size()), copy_descriptors.data());
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

	VansGraphics::vkUpdateDescriptorSets(m_LogicalDevice, 1, &write, 0, nullptr);
}

