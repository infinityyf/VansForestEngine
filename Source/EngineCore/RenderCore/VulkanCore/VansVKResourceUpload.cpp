#include "VansVKDevice.h"
#include "VansVKMemoryManager.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansProfiler.h"

namespace VansGraphics
{
	void VansVKDevice::ResetFrameStageUploadAllocator()
	{
		m_FrameStageBufferOffset = 0;
	}

	bool VansVKDevice::RecordDeviceImageData(VansVKImage& destImage,
		VansVKCommandBuffer& cmd,
		const void* data,
		int dataSize,
		VkOffset3D imageOffset,
		VkExtent3D imageSize,
		int mipLevel,
		int layerLevel,
		VkImageLayout finalLayout)
	{
		if (!data || dataSize <= 0)
			return false;

		VANS_PROFILE_SCOPE("Video::Upload.RecordDeviceImageData", Vans::ProfileCategory::Video);

		constexpr VkDeviceSize UPLOAD_ALIGNMENT = 256;
		VkDeviceSize uploadOffset = (m_FrameStageBufferOffset + UPLOAD_ALIGNMENT - 1) & ~(UPLOAD_ALIGNMENT - 1);
		VkDeviceSize uploadEnd = uploadOffset + static_cast<VkDeviceSize>(dataSize);
		if (uploadEnd > m_StageBuffer.GetBufferSize())
		{
			VANS_LOG_ERROR("[VansVKDevice] 本帧 staging 上传空间不足，size=" << dataSize);
			return false;
		}

		{
			VANS_PROFILE_SCOPE("Video::Upload.StageMemcpy", Vans::ProfileCategory::Video);
			if (!m_StageBuffer.SetBufferData(data, uploadOffset, dataSize))
				return false;
		}

		{
			VANS_PROFILE_SCOPE("Video::Upload.RecordCopyAndBarriers", Vans::ProfileCategory::Video);
			const VkImageLayout originalLayout = destImage.m_ImageLayout;
			const VkPipelineStageFlags beforeStage = originalLayout == VK_IMAGE_LAYOUT_UNDEFINED
				? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
				: VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			const VkAccessFlags beforeAccess = originalLayout == VK_IMAGE_LAYOUT_UNDEFINED
				? 0
				: VK_ACCESS_SHADER_READ_BIT;
			VkImageMemoryBarrier toTransferBarrier{};
			toTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			toTransferBarrier.srcAccessMask = beforeAccess;
			toTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			toTransferBarrier.oldLayout = originalLayout;
			toTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			toTransferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toTransferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toTransferBarrier.image = destImage.m_VansVKImage;
			toTransferBarrier.subresourceRange.aspectMask = destImage.m_ImageAspect;
			toTransferBarrier.subresourceRange.baseMipLevel = static_cast<uint32_t>(mipLevel);
			toTransferBarrier.subresourceRange.levelCount = 1;
			toTransferBarrier.subresourceRange.baseArrayLayer = static_cast<uint32_t>(layerLevel);
			toTransferBarrier.subresourceRange.layerCount = 1;
			cmd.PipelineBarrier(
				beforeStage,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				{}, {}, { toTransferBarrier });

			VkImageSubresourceLayers destinationImageSubresource{};
			destinationImageSubresource.aspectMask = destImage.m_ImageAspect;
			destinationImageSubresource.mipLevel = static_cast<uint32_t>(mipLevel);
			destinationImageSubresource.baseArrayLayer = static_cast<uint32_t>(layerLevel);
			destinationImageSubresource.layerCount = 1;

			VansVKMemoryManager::CopyBufferToImage(cmd, m_StageBuffer, destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				{
					{
						uploadOffset,
						0,
						0,
						destinationImageSubresource,
						imageOffset,
						imageSize,
					}
				});

			if (finalLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
			{
				VkImageMemoryBarrier toFinalBarrier{};
				toFinalBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				toFinalBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				toFinalBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				toFinalBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				toFinalBarrier.newLayout = finalLayout;
				toFinalBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toFinalBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toFinalBarrier.image = destImage.m_VansVKImage;
				toFinalBarrier.subresourceRange = toTransferBarrier.subresourceRange;
				cmd.PipelineBarrier(
					VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					{}, {}, { toFinalBarrier });
			}
		}

		destImage.m_ImageLayout = finalLayout;
		m_FrameStageBufferOffset = uploadEnd;
		return true;
	}

	bool VansVKDevice::RecordDeviceImageBufferData(VansVKImage& destImage,
		VansVKCommandBuffer& cmd,
		VansVKBuffer& sourceBuffer,
		VkDeviceSize sourceOffset,
		VkOffset3D imageOffset,
		VkExtent3D imageSize,
		int mipLevel,
		int layerLevel,
		VkImageLayout finalLayout)
	{
		VANS_PROFILE_SCOPE("Video::Upload.RecordDeviceImageBufferData", Vans::ProfileCategory::Video);

		VANS_PROFILE_SCOPE("Video::Upload.RecordCopyAndBarriers", Vans::ProfileCategory::Video);
		const VkImageLayout originalLayout = destImage.m_ImageLayout;
		const VkPipelineStageFlags beforeStage = originalLayout == VK_IMAGE_LAYOUT_UNDEFINED
			? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
			: VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		const VkAccessFlags beforeAccess = originalLayout == VK_IMAGE_LAYOUT_UNDEFINED
			? 0
			: VK_ACCESS_SHADER_READ_BIT;
		VkImageMemoryBarrier toTransferBarrier{};
		toTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toTransferBarrier.srcAccessMask = beforeAccess;
		toTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toTransferBarrier.oldLayout = originalLayout;
		toTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toTransferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toTransferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toTransferBarrier.image = destImage.m_VansVKImage;
		toTransferBarrier.subresourceRange.aspectMask = destImage.m_ImageAspect;
		toTransferBarrier.subresourceRange.baseMipLevel = static_cast<uint32_t>(mipLevel);
		toTransferBarrier.subresourceRange.levelCount = 1;
		toTransferBarrier.subresourceRange.baseArrayLayer = static_cast<uint32_t>(layerLevel);
		toTransferBarrier.subresourceRange.layerCount = 1;
		cmd.PipelineBarrier(
			beforeStage,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			{}, {}, { toTransferBarrier });

		VkImageSubresourceLayers destinationImageSubresource{};
		destinationImageSubresource.aspectMask = destImage.m_ImageAspect;
		destinationImageSubresource.mipLevel = static_cast<uint32_t>(mipLevel);
		destinationImageSubresource.baseArrayLayer = static_cast<uint32_t>(layerLevel);
		destinationImageSubresource.layerCount = 1;

		VansVKMemoryManager::CopyBufferToImage(cmd, sourceBuffer, destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			{
				{
					sourceOffset,
					0,
					0,
					destinationImageSubresource,
					imageOffset,
					imageSize,
				}
			});

		if (finalLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			VkImageMemoryBarrier toFinalBarrier{};
			toFinalBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			toFinalBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			toFinalBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			toFinalBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			toFinalBarrier.newLayout = finalLayout;
			toFinalBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toFinalBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toFinalBarrier.image = destImage.m_VansVKImage;
			toFinalBarrier.subresourceRange = toTransferBarrier.subresourceRange;
			cmd.PipelineBarrier(
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				{}, {}, { toFinalBarrier });
		}

		destImage.m_ImageLayout = finalLayout;
		return true;
	}

	bool VansVKDevice::SetDeviceBufferData(VansVKBuffer& dest_buffer, void* data, int data_offset, int data_size, VkDeviceSize buffer_offset, VkDeviceSize buffer_size)
	{
		if (!m_StageBuffer.SetBufferData(data, data_offset, data_size))
		{
			VANS_LOG_ERROR("[VansVKDevice] Failed to stage buffer upload data. size=" << data_size);
			return false;
		}

		if (!m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
		{
			return false;
		}

		dest_buffer.SetBufferMemoryBarrier(m_VansVKCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			{
				dest_buffer.m_VansVKBuffer,
				VK_ACCESS_TRANSFER_READ_BIT,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED
			}
		);

		VansVKMemoryManager::CopyBufferData(m_VansVKCommandBuffer, m_StageBuffer, dest_buffer, { { VkDeviceSize(0),buffer_offset, buffer_size } });

		dest_buffer.SetBufferMemoryBarrier(m_VansVKCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			{
				dest_buffer.m_VansVKBuffer,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_TRANSFER_READ_BIT,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED
			}
		);
		if (!m_VansVKCommandBuffer.EndCommandBufferRecord())
		{
			return false;
		}

		if (!VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, {}, {}, m_VansVKCommandBuffer.m_CommandBufferFinishSubmitFence))
		{
			return false;
		}
		return m_VansVKCommandBuffer.ResetCommandBuffer(false);
	}

	bool VansVKDevice::SetDeviceImageData(VansVKImage& dest_image, VansVKCommandBuffer& cmd, void* data, int data_offset, int data_size, VkOffset3D image_offset, VkExtent3D image_size, int mip_level, int layer_level)
	{
		return SetDeviceImageData(
			dest_image,
			cmd,
			data,
			data_offset,
			data_size,
			image_offset,
			image_size,
			mip_level,
			layer_level,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	bool VansVKDevice::SetDeviceImageData(VansVKImage& dest_image, VansVKCommandBuffer& cmd, void* data, int data_offset, int data_size, VkOffset3D image_offset, VkExtent3D image_size, int mip_level, int layer_level, VkImageLayout finalLayout)
	{
		if (!m_StageBuffer.SetBufferData(data, data_offset, data_size))
		{
			VANS_LOG_ERROR("[VansVKDevice] Failed to stage image upload data. size=" << data_size);
			return false;
		}

		if (!cmd.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
		{
			return false;
		}

		// 必须在第一个 barrier 调用之前保存原始 layout。
		// SetImageMemoryBarrier 会立即更新 m_ImageLayout = NewLayout，
		// 若此处不缓存，第二个 barrier 将错误地使用已更新后的 TRANSFER_DST_OPTIMAL
		// 作为目标 layout，导致图像永久停留在 TRANSFER_DST_OPTIMAL，
		// GPU 采样时触发未定义行为并最终崩溃（VK_ERROR_DEVICE_LOST）。
		const VkImageLayout originalLayout = dest_image.m_ImageLayout;

		const VkPipelineStageFlags beforeStage = originalLayout == VK_IMAGE_LAYOUT_UNDEFINED
			? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
			: VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		const VkAccessFlags beforeAccess = originalLayout == VK_IMAGE_LAYOUT_UNDEFINED
			? 0
			: VK_ACCESS_SHADER_READ_BIT;

		dest_image.SetImageMemoryBarrier(cmd, beforeStage, VK_PIPELINE_STAGE_TRANSFER_BIT,
			{
				dest_image.m_VansVKImage,
				beforeAccess,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				originalLayout,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				dest_image.m_ImageAspect
			}
		);

		VkImageSubresourceLayers destination_image_subresource =
		{
			dest_image.m_ImageAspect,
			static_cast<uint32_t>(mip_level),
			static_cast<uint32_t>(layer_level),
			1
		};

		VansVKMemoryManager::CopyBufferToImage(cmd, m_StageBuffer, dest_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			{
				{
					0,
					0,
					0,
					destination_image_subresource,
					image_offset,
					image_size,
				}
			});

		if (finalLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			dest_image.SetImageMemoryBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				{
					dest_image.m_VansVKImage,
					VK_ACCESS_TRANSFER_WRITE_BIT,
					VK_ACCESS_SHADER_READ_BIT,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					finalLayout,
					VK_QUEUE_FAMILY_IGNORED,
					VK_QUEUE_FAMILY_IGNORED,
					dest_image.m_ImageAspect
				}
			);
		}

		if (!cmd.EndCommandBufferRecord())
		{
			return false;
		}

		if (!VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { cmd.GetVKCommandBuffer() }, {}, {}, cmd.m_CommandBufferFinishSubmitFence))
		{
			return false;
		}
		return cmd.ResetCommandBuffer(false);
	}

	bool VansVKDevice::SetDeviceImageMipChainData(VansVKImage& destImage,
		VansVKCommandBuffer& cmd,
		const void* data,
		int dataSize,
		const std::vector<VkBufferImageCopy>& regions,
		VkImageLayout finalLayout)
	{
		if (!data || dataSize <= 0 || regions.empty() ||
			static_cast<VkDeviceSize>(dataSize) > m_StageBuffer.GetBufferSize())
		{
			VANS_LOG_ERROR("[VansVKDevice] Invalid cooked texture mip-chain upload request. size=" << dataSize);
			return false;
		}
		if (!m_StageBuffer.SetBufferData(data, 0, static_cast<VkDeviceSize>(dataSize)))
			return false;
		if (!cmd.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
			return false;

		const VkImageLayout originalLayout = destImage.m_ImageLayout;
		const VkPipelineStageFlags beforeStage = originalLayout == VK_IMAGE_LAYOUT_UNDEFINED
			? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
			: VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		const VkAccessFlags beforeAccess = originalLayout == VK_IMAGE_LAYOUT_UNDEFINED
			? 0
			: VK_ACCESS_SHADER_READ_BIT;
		destImage.SetImageMemoryBarrier(cmd, beforeStage, VK_PIPELINE_STAGE_TRANSFER_BIT,
			{
				destImage.m_VansVKImage,
				beforeAccess,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				originalLayout,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				destImage.m_ImageAspect
			});

		VansVKMemoryManager::CopyBufferToImage(
			cmd, m_StageBuffer, destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, regions);

		if (finalLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			destImage.SetImageMemoryBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				{
					destImage.m_VansVKImage,
					VK_ACCESS_TRANSFER_WRITE_BIT,
					VK_ACCESS_SHADER_READ_BIT,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					finalLayout,
					VK_QUEUE_FAMILY_IGNORED,
					VK_QUEUE_FAMILY_IGNORED,
					destImage.m_ImageAspect
				});
		}
		if (!cmd.EndCommandBufferRecord())
			return false;
		if (!VansVKCommandBuffer::SubmitCommands(
			m_VansVKGraphicsQueue,
			m_VansVKLogicDevice,
			{ cmd.GetVKCommandBuffer() },
			{}, {},
			cmd.m_CommandBufferFinishSubmitFence))
		{
			return false;
		}
		return cmd.ResetCommandBuffer(false);
	}
}
