#include "VansVKDevice.h"
#include "VansVKMemoryManager.h"

namespace VansGraphics
{
	bool VansVKDevice::SetDeviceBufferData(VansVKBuffer& dest_buffer, void* data, int data_offset, int data_size, VkDeviceSize buffer_offset, VkDeviceSize buffer_size)
	{
		m_StageBuffer.SetBufferData(data, data_offset, data_size);

		if (!m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
		{
			return false;
		}

		dest_buffer.SetBufferMemoryBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			{
				dest_buffer.m_VansVKBuffer,
				VK_ACCESS_TRANSFER_READ_BIT,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED
			}
		);

		VansVKMemoryManager::CopyBufferData(m_VansVKCommandBuffer, m_StageBuffer, dest_buffer, { { VkDeviceSize(0),buffer_offset, buffer_size } });

		dest_buffer.SetBufferMemoryBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
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

		VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, {}, {}, m_VansVKCommandBuffer.m_CommandBufferFinishSubmitFence);
		m_VansVKCommandBuffer.ResetCommandBuffer(false);
		return true;
	}

	bool VansVKDevice::SetDeviceImageData(VansVKImage& dest_image, VansVKCommandBuffer& cmd, void* data, int data_offset, int data_size, VkOffset3D image_offset, VkExtent3D image_size, int mip_level, int layer_level)
	{
		m_StageBuffer.SetBufferData(data, data_offset, data_size);

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

		dest_image.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			{
				dest_image.m_VansVKImage,
				VK_ACCESS_SHADER_READ_BIT,
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
			mip_level,
			layer_level,
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

		dest_image.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			{
				dest_image.m_VansVKImage,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_SHADER_READ_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				originalLayout,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				dest_image.m_ImageAspect
			}
		);

		if (!cmd.EndCommandBufferRecord())
		{
			return false;
		}

		VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { cmd.GetVKCommandBuffer() }, {}, {}, cmd.m_CommandBufferFinishSubmitFence);
		cmd.ResetCommandBuffer(false);
		return true;
	}
}
