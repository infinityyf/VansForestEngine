#include "VansRenderGraphVulkanSync.h"

#include "VansVKCommandBuffer.h"
#include "VansVKImage.h"

#include <sstream>

namespace VansGraphics
{
	VansVulkanRenderGraphSyncPlan VansRenderGraphVulkanSyncMapper::BuildSyncPlan(
		const VansRenderGraphBarrierPlan& barrierPlan)
	{
		VansVulkanRenderGraphSyncPlan syncPlan{};
		syncPlan.frameNumber = barrierPlan.frameNumber;
		syncPlan.passPlans.reserve(barrierPlan.passPlans.size());
		syncPlan.dependencies.reserve(barrierPlan.dependencies.size());
		syncPlan.warnings = barrierPlan.warnings;

		for (const auto& passPlan : barrierPlan.passPlans)
		{
			VansVulkanPassSyncPlan vulkanPassPlan{};
			vulkanPassPlan.passIndex = passPlan.passIndex;
			vulkanPassPlan.passName = passPlan.passName;
			vulkanPassPlan.incomingDependencies.reserve(passPlan.incomingDependencies.size());

			for (const auto& dependency : passPlan.incomingDependencies)
			{
				vulkanPassPlan.incomingDependencies.emplace_back(MapDependency(dependency));
			}

			syncPlan.passPlans.emplace_back(std::move(vulkanPassPlan));
		}

		for (const auto& dependency : barrierPlan.dependencies)
		{
			syncPlan.dependencies.emplace_back(MapDependency(dependency));
		}

		return syncPlan;
	}

	VansVulkanResourceSyncState VansRenderGraphVulkanSyncMapper::MapResourceUsage(
		VansRenderResourceUsage usage)
	{
		VansVulkanResourceSyncState state{};
		state.usage = usage;

		switch (usage)
		{
		case VansRenderResourceUsage::ColorAttachmentWrite:
			state.stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			state.accessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			state.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			break;
		case VansRenderResourceUsage::DepthStencilAttachmentRead:
			state.stageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			state.accessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			state.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			break;
		case VansRenderResourceUsage::DepthStencilAttachmentWrite:
			state.stageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			state.accessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			state.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			break;
		case VansRenderResourceUsage::SampledRead:
			state.stageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
				| VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
				| VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			state.accessMask = VK_ACCESS_SHADER_READ_BIT;
			state.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			break;
		case VansRenderResourceUsage::StorageRead:
			state.stageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
				| VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			state.accessMask = VK_ACCESS_SHADER_READ_BIT;
			state.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			break;
		case VansRenderResourceUsage::StorageWrite:
			state.stageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
				| VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			state.accessMask = VK_ACCESS_SHADER_WRITE_BIT;
			state.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			break;
		case VansRenderResourceUsage::TransferSrc:
			state.stageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			state.accessMask = VK_ACCESS_TRANSFER_READ_BIT;
			state.imageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			break;
		case VansRenderResourceUsage::TransferDst:
			state.stageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			state.accessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			state.imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			break;
		case VansRenderResourceUsage::IndirectArgumentRead:
			state.stageMask = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
			state.accessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
			state.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			state.imageLayoutRelevant = false;
			break;
		case VansRenderResourceUsage::AccelerationStructureBuildRead:
			state.stageMask = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
				| VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
			state.accessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
			state.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			state.imageLayoutRelevant = false;
			break;
		case VansRenderResourceUsage::AccelerationStructureBuildWrite:
			state.stageMask = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
			state.accessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
			state.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			state.imageLayoutRelevant = false;
			break;
		case VansRenderResourceUsage::Present:
			state.stageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			state.accessMask = 0;
			state.imageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			break;
		}

		state.imageLayoutRelevant = IsImageLayoutRelevant(usage);
		return state;
	}

	VkImageUsageFlags VansRenderGraphVulkanSyncMapper::MapImageUsage(
		VansRenderResourceUsage usage)
	{
		switch (usage)
		{
		case VansRenderResourceUsage::ColorAttachmentWrite:
			return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		case VansRenderResourceUsage::DepthStencilAttachmentRead:
		case VansRenderResourceUsage::DepthStencilAttachmentWrite:
			return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		case VansRenderResourceUsage::SampledRead:
			return VK_IMAGE_USAGE_SAMPLED_BIT;
		case VansRenderResourceUsage::StorageRead:
		case VansRenderResourceUsage::StorageWrite:
			return VK_IMAGE_USAGE_STORAGE_BIT;
		case VansRenderResourceUsage::TransferSrc:
			return VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		case VansRenderResourceUsage::TransferDst:
			return VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		case VansRenderResourceUsage::IndirectArgumentRead:
		case VansRenderResourceUsage::AccelerationStructureBuildRead:
		case VansRenderResourceUsage::AccelerationStructureBuildWrite:
		case VansRenderResourceUsage::Present:
			return 0;
		}
		return 0;
	}

	VkImageUsageFlags VansRenderGraphVulkanSyncMapper::BuildImageUsage(
		std::initializer_list<VansRenderResourceUsage> usages)
	{
		VkImageUsageFlags flags = 0;
		for (const VansRenderResourceUsage usage : usages)
		{
			flags |= MapImageUsage(usage);
		}
		return flags;
	}

	VansVulkanSyncDependency VansRenderGraphVulkanSyncMapper::MapDependency(
		const VansRenderGraphDependency& dependency)
	{
		const auto srcState = MapResourceUsage(dependency.srcUsage);
		const auto dstState = MapResourceUsage(dependency.dstUsage);

		VansVulkanSyncDependency vulkanDependency{};
		vulkanDependency.dependency = dependency;
		vulkanDependency.srcStageMask = srcState.stageMask;
		vulkanDependency.dstStageMask = dstState.stageMask;
		vulkanDependency.srcAccessMask = srcState.accessMask;
		vulkanDependency.dstAccessMask = dstState.accessMask;
		vulkanDependency.oldLayout = srcState.imageLayout;
		vulkanDependency.newLayout = dstState.imageLayout;
		vulkanDependency.imageLayoutTransition =
			srcState.imageLayoutRelevant
			&& dstState.imageLayoutRelevant
			&& srcState.imageLayout != dstState.imageLayout;
		vulkanDependency.queueOwnershipTransfer =
			dependency.type == VansRenderDependencyType::QueueTransition;

		return vulkanDependency;
	}

	bool VansRenderGraphVulkanSyncMapper::IsImageLayoutRelevant(VansRenderResourceUsage usage)
	{
		switch (usage)
		{
		case VansRenderResourceUsage::IndirectArgumentRead:
		case VansRenderResourceUsage::AccelerationStructureBuildRead:
		case VansRenderResourceUsage::AccelerationStructureBuildWrite:
			return false;
		default:
			return true;
		}
	}

	VansVulkanSyncRecordResult VansRenderGraphVulkanSyncRecorder::RecordMemoryDependencies(
		VansVKCommandBuffer& commandBuffer,
		const VansVulkanPassSyncPlan& passSyncPlan)
	{
		return RecordMemoryDependencies(commandBuffer, passSyncPlan.incomingDependencies);
	}

	VansVulkanSyncRecordResult VansRenderGraphVulkanSyncRecorder::RecordMemoryDependencies(
		VansVKCommandBuffer& commandBuffer,
		const std::vector<VansVulkanSyncDependency>& dependencies)
	{
		VansVulkanSyncRecordResult result{};
		std::vector<VkMemoryBarrier> memoryBarriers;
		memoryBarriers.reserve(dependencies.size());

		VkPipelineStageFlags mergedSrcStageMask = 0;
		VkPipelineStageFlags mergedDstStageMask = 0;

		for (const auto& dependency : dependencies)
		{
			if (dependency.queueOwnershipTransfer)
			{
				++result.skippedQueueOwnershipTransfers;
				result.warnings.emplace_back(
					"RenderGraph Vulkan sync recorder skipped queue ownership transfer for resource: "
					+ dependency.dependency.resourceName);
				continue;
			}

			if (dependency.imageLayoutTransition)
			{
				++result.skippedImageLayoutTransitions;
				result.warnings.emplace_back(
					"RenderGraph Vulkan sync recorder skipped image layout transition for resource: "
					+ dependency.dependency.resourceName);
				continue;
			}

			VkMemoryBarrier memoryBarrier{};
			memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			memoryBarrier.srcAccessMask = dependency.srcAccessMask;
			memoryBarrier.dstAccessMask = dependency.dstAccessMask;
			memoryBarriers.emplace_back(memoryBarrier);

			mergedSrcStageMask |= dependency.srcStageMask;
			mergedDstStageMask |= dependency.dstStageMask;
		}

		if (!memoryBarriers.empty())
		{
			if (mergedSrcStageMask == 0)
			{
				mergedSrcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			}
			if (mergedDstStageMask == 0)
			{
				mergedDstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			}

			commandBuffer.PipelineBarrier(
				mergedSrcStageMask,
				mergedDstStageMask,
				memoryBarriers,
				{},
				{});

			result.recordedMemoryBarriers = static_cast<uint32_t>(memoryBarriers.size());
		}

		return result;
	}

	VansVulkanSyncRecordResult VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
		VansVKCommandBuffer& commandBuffer,
		VansVKImage& image,
		const VansVulkanSyncDependency& dependency)
	{
		if (dependency.queueOwnershipTransfer)
		{
			VansVulkanSyncRecordResult result{};
			++result.skippedQueueOwnershipTransfers;
			result.warnings.emplace_back(
				"RenderGraph Vulkan sync recorder skipped image queue ownership transfer for resource: "
				+ dependency.dependency.resourceName);
			return result;
		}

		return RecordImageTransition(
			commandBuffer,
			image,
			dependency.srcStageMask,
			dependency.dstStageMask,
			dependency.srcAccessMask,
			dependency.dstAccessMask,
			dependency.oldLayout,
			dependency.newLayout);
	}

	VansVulkanSyncRecordResult VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
		VansVKCommandBuffer& commandBuffer,
		VansVKImage& image,
		VkPipelineStageFlags srcStageMask,
		VkPipelineStageFlags dstStageMask,
		VkAccessFlags srcAccessMask,
		VkAccessFlags dstAccessMask,
		VkImageLayout oldLayout,
		VkImageLayout newLayout)
	{
		VansVulkanSyncRecordResult result{};

		image.SetImageMemoryBarrier(
			commandBuffer,
			srcStageMask,
			dstStageMask,
			{
				image.GetImage(),
				srcAccessMask,
				dstAccessMask,
				oldLayout,
				newLayout,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				image.GetImageAspect()
			});

		result.recordedImageBarriers = 1;
		return result;
	}

	std::string VansRenderGraphVulkanSyncDebugDumper::BuildSyncPlanSummary(
		const VansVulkanRenderGraphSyncPlan& syncPlan)
	{
		std::ostringstream stream;
		uint32_t imageLayoutTransitions = 0;
		uint32_t queueOwnershipTransfers = 0;
		for (const auto& dependency : syncPlan.dependencies)
		{
			if (dependency.imageLayoutTransition)
			{
				++imageLayoutTransitions;
			}
			if (dependency.queueOwnershipTransfer)
			{
				++queueOwnershipTransfers;
			}
		}

		stream << "VulkanRenderGraphSyncPlan frame=" << syncPlan.frameNumber
			<< " passPlans=" << syncPlan.passPlans.size()
			<< " dependencies=" << syncPlan.dependencies.size()
			<< " imageLayoutTransitions=" << imageLayoutTransitions
			<< " queueOwnershipTransfers=" << queueOwnershipTransfers
			<< " warnings=" << syncPlan.warnings.size() << "\n";

		for (const auto& dependency : syncPlan.dependencies)
		{
			stream << "  sync " << dependency.dependency.resourceName
				<< " " << dependency.dependency.srcPassName << "[" << dependency.dependency.srcPassIndex << "] -> "
				<< dependency.dependency.dstPassName << "[" << dependency.dependency.dstPassIndex << "]"
				<< " type=" << VansRenderGraphDebugDumper::ToString(dependency.dependency.type)
				<< " srcStage=0x" << std::hex << dependency.srcStageMask
				<< " dstStage=0x" << dependency.dstStageMask
				<< " srcAccess=0x" << dependency.srcAccessMask
				<< " dstAccess=0x" << dependency.dstAccessMask
				<< std::dec
				<< " oldLayout=" << static_cast<int>(dependency.oldLayout)
				<< " newLayout=" << static_cast<int>(dependency.newLayout)
				<< " imageLayoutTransition=" << (dependency.imageLayoutTransition ? "true" : "false")
				<< " queueOwnershipTransfer=" << (dependency.queueOwnershipTransfer ? "true" : "false")
				<< "\n";
		}

		for (const auto& warning : syncPlan.warnings)
		{
			stream << "  warning " << warning << "\n";
		}

		return stream.str();
	}
}
