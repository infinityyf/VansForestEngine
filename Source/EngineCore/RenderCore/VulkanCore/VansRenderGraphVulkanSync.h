#pragma once

#include "VansRenderGraph.h"
#include "vulkan/vulkan.h"

#include <cstdint>
#include <string>
#include <vector>

namespace VansGraphics
{
	class VansVKCommandBuffer;
	class VansVKImage;

	struct VansVulkanResourceSyncState
	{
		VansRenderResourceUsage usage = VansRenderResourceUsage::SampledRead;
		VkPipelineStageFlags stageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		VkAccessFlags accessMask = VK_ACCESS_SHADER_READ_BIT;
		VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		bool imageLayoutRelevant = true;
	};

	struct VansVulkanSyncDependency
	{
		VansRenderGraphDependency dependency;
		VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		VkAccessFlags srcAccessMask = 0;
		VkAccessFlags dstAccessMask = 0;
		VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImageLayout newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		bool imageLayoutTransition = false;
		bool queueOwnershipTransfer = false;
	};

	struct VansVulkanPassSyncPlan
	{
		uint32_t passIndex = 0;
		std::string passName;
		std::vector<VansVulkanSyncDependency> incomingDependencies;
	};

	struct VansVulkanRenderGraphSyncPlan
	{
		uint64_t frameNumber = 0;
		std::vector<VansVulkanPassSyncPlan> passPlans;
		std::vector<VansVulkanSyncDependency> dependencies;
		std::vector<std::string> warnings;

		void Clear()
		{
			frameNumber = 0;
			passPlans.clear();
			dependencies.clear();
			warnings.clear();
		}
	};

	struct VansVulkanSyncRecordResult
	{
		uint32_t recordedMemoryBarriers = 0;
		uint32_t recordedImageBarriers = 0;
		uint32_t skippedImageLayoutTransitions = 0;
		uint32_t skippedQueueOwnershipTransfers = 0;
		std::vector<std::string> warnings;

		bool RecordedAny() const { return recordedMemoryBarriers > 0 || recordedImageBarriers > 0; }
	};

	class VansRenderGraphVulkanSyncMapper
	{
	public:
		static VansVulkanRenderGraphSyncPlan BuildSyncPlan(const VansRenderGraphBarrierPlan& barrierPlan);
		static VansVulkanResourceSyncState MapResourceUsage(VansRenderResourceUsage usage);

	private:
		static VansVulkanSyncDependency MapDependency(const VansRenderGraphDependency& dependency);
		static bool IsImageLayoutRelevant(VansRenderResourceUsage usage);
	};

	class VansRenderGraphVulkanSyncRecorder
	{
	public:
		static VansVulkanSyncRecordResult RecordMemoryDependencies(
			VansVKCommandBuffer& commandBuffer,
			const VansVulkanPassSyncPlan& passSyncPlan);

		static VansVulkanSyncRecordResult RecordMemoryDependencies(
			VansVKCommandBuffer& commandBuffer,
			const std::vector<VansVulkanSyncDependency>& dependencies);

		static VansVulkanSyncRecordResult RecordImageTransition(
			VansVKImage& image,
			const VansVulkanSyncDependency& dependency);

		static VansVulkanSyncRecordResult RecordImageTransition(
			VansVKImage& image,
			VkPipelineStageFlags srcStageMask,
			VkPipelineStageFlags dstStageMask,
			VkAccessFlags srcAccessMask,
			VkAccessFlags dstAccessMask,
			VkImageLayout oldLayout,
			VkImageLayout newLayout);
	};

	class VansRenderGraphVulkanSyncDebugDumper
	{
	public:
		static std::string BuildSyncPlanSummary(const VansVulkanRenderGraphSyncPlan& syncPlan);
	};
}
