#pragma once

#include "vulkan/vulkan.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
	struct VansFrameSubmitNode;

	struct VansPhysicalResourceIdentity
	{
		VkImage image = VK_NULL_HANDLE;
		VkBuffer buffer = VK_NULL_HANDLE;
		static VansPhysicalResourceIdentity ForImage(VkImage handle) { return { handle, VK_NULL_HANDLE }; }
		static VansPhysicalResourceIdentity ForBuffer(VkBuffer handle) { return { VK_NULL_HANDLE, handle }; }
		bool IsValid() const { return (image != VK_NULL_HANDLE) != (buffer != VK_NULL_HANDLE); }
		bool operator==(const VansPhysicalResourceIdentity& other) const
		{
			return image == other.image && buffer == other.buffer;
		}
	};

	struct VansCrossFrameResourceWait
	{
		VkSemaphore semaphore = VK_NULL_HANDLE;
		uint64_t value = 0;
		VkPipelineStageFlags stages = 0;
	};

	class VansCrossFrameResourceLedger final
	{
	public:
		void CollectWaits(const VansPhysicalResourceIdentity& identity,
			bool write, VkPipelineStageFlags stages,
			std::vector<VansCrossFrameResourceWait>& waits) const;
		void PublishAccess(const VansPhysicalResourceIdentity& identity,
			bool write, VkQueue queue, VkSemaphore semaphore, uint64_t value);
		void Clear();
		void Poison() { m_Poisoned = true; }
		bool IsUsable() const { return !m_Poisoned; }
		std::size_t ResourceCount() const { return m_Entries.size(); }

	private:
		struct Completion
		{
			VkQueue queue = VK_NULL_HANDLE;
			VkSemaphore semaphore = VK_NULL_HANDLE;
			uint64_t value = 0;
		};
		struct Entry
		{
			Completion writer;
			std::vector<Completion> readers;
		};
		struct IdentityHash
		{
			std::size_t operator()(const VansPhysicalResourceIdentity& identity) const noexcept
			{
				const std::size_t imageHash = std::hash<VkImage>{}(identity.image);
				const std::size_t bufferHash = std::hash<VkBuffer>{}(identity.buffer);
				return imageHash ^ (bufferHash + 0x9e3779b97f4a7c15ull
					+ (imageHash << 6) + (imageHash >> 2));
			}
		};
		std::unordered_map<VansPhysicalResourceIdentity, Entry, IdentityHash> m_Entries;
		bool m_Poisoned = false;
	};

	struct VansTrackedResourceState
	{
		std::string name;
		VkPipelineStageFlags lastStages = 0;
		VkAccessFlags lastAccess = 0;
		VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		bool lastAccessWasWrite = false;
		bool persistent = false;
		bool hostReadable = false;
		std::string lastNode;
	};

	class VansResourceStateTracker
	{
	public:
		bool ValidateAndBuild(
			const std::vector<VansFrameSubmitNode>& nodes,
			std::string* error = nullptr);

		const std::vector<VansTrackedResourceState>& GetStates() const { return m_States; }
		std::string BuildDebugSummary() const;

	private:
		std::vector<VansTrackedResourceState> m_States;
	};
}
