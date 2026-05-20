#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansTexture.h"
#include "VansVKDevice.h"
#include "VansVKCommandBuffer.h"
#include "../../Util/VansJobSystem.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansProfiler.h"
#include <iostream>
#include <vector>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#include <../../STBImge/stb_image.h>

#define STB_DXT_IMPLEMENTATION
#include <../../STBImge/stb_dxt.h>

namespace VansGraphics
{
	// =====================================================================
	// 静态工具函数
	// =====================================================================

	//BC块压缩：将一个4x4 RGBA8块压缩为BC1(8字节)或BC3(16字节)
	static void CompressBlock(uint8_t* outDst, const uint8_t rgba4x4[16 * 4], bool hasAlpha)
	{
		stb_compress_dxt_block(outDst, rgba4x4, hasAlpha ? 1 : 0, /*mode=*/0);
	}

	//2x2 box降采样（RGBA8）
	static std::vector<uint8_t> Downsample2x2_RGBA8(const uint8_t* src, int w, int h)
	{
		int outW = std::max(1, w / 2);
		int outH = std::max(1, h / 2);
		std::vector<uint8_t> dst(size_t(outW) * size_t(outH) * 4);

		auto sample = [&](int x, int y) -> const uint8_t* {
			x = std::min(std::max(x, 0), w - 1);
			y = std::min(std::max(y, 0), h - 1);
			return &src[(y * w + x) * 4];
		};

		for (int y = 0; y < outH; ++y)
		{
			for (int x = 0; x < outW; ++x)
			{
				const uint8_t* p00 = sample(x * 2 + 0, y * 2 + 0);
				const uint8_t* p10 = sample(x * 2 + 1, y * 2 + 0);
				const uint8_t* p01 = sample(x * 2 + 0, y * 2 + 1);
				const uint8_t* p11 = sample(x * 2 + 1, y * 2 + 1);

				uint8_t* d = &dst[(y * outW + x) * 4];
				d[0] = uint8_t((p00[0] + p10[0] + p01[0] + p11[0]) >> 2);
				d[1] = uint8_t((p00[1] + p10[1] + p01[1] + p11[1]) >> 2);
				d[2] = uint8_t((p00[2] + p10[2] + p01[2] + p11[2]) >> 2);
				d[3] = uint8_t((p00[3] + p10[3] + p01[3] + p11[3]) >> 2);
			}
		}
		return dst;
	}

	//将单层RGBA8数据压缩为BC块格式
	static std::vector<uint8_t> CompressMipToBC(const uint8_t* rgba, int w, int h, bool hasAlpha)
	{
		int bytesPerBlock = hasAlpha ? 16 : 8;
		int blocksX = (w + 3) / 4;
		int blocksY = (h + 3) / 4;
		std::vector<uint8_t> compressed(size_t(blocksX) * size_t(blocksY) * size_t(bytesPerBlock));

		auto fetch = [&](int x, int y) -> const uint8_t* {
			x = std::min(std::max(x, 0), w - 1);
			y = std::min(std::max(y, 0), h - 1);
			return &rgba[(y * w + x) * 4];
		};

		uint8_t blockRGBA[16 * 4];
		uint8_t* dstPtr = compressed.data();

		for (int by = 0; by < blocksY; ++by)
		{
			for (int bx = 0; bx < blocksX; ++bx)
			{
				for (int ty = 0; ty < 4; ++ty)
				{
					for (int tx = 0; tx < 4; ++tx)
					{
						const uint8_t* s = fetch(bx * 4 + tx, by * 4 + ty);
						int idx = (ty * 4 + tx) * 4;
						blockRGBA[idx + 0] = s[0];
						blockRGBA[idx + 1] = s[1];
						blockRGBA[idx + 2] = s[2];
						blockRGBA[idx + 3] = s[3];
					}
				}
				CompressBlock(dstPtr, blockRGBA, hasAlpha);
				dstPtr += bytesPerBlock;
			}
		}
		return compressed;
	}

	// =====================================================================
	// VansTexture 实现
	// =====================================================================

	VansTexture::~VansTexture()
	{
		m_Image.DestroyVulkanImage(*(VkDevice*)m_GraphicsDevice->GetNativeGraphicsDevice());
	}

	// ----- 格式选择 -----

	VkFormat VansTexture::ChooseFormat(int channel, TexturePrecision precision, bool isSRGB)
	{
		static const VkFormat formats8_unorm[] = { VK_FORMAT_UNDEFINED, VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };
		static const VkFormat formats8_srgb[]  = { VK_FORMAT_UNDEFINED, VK_FORMAT_R8_SRGB,  VK_FORMAT_R8G8_SRGB,  VK_FORMAT_R8G8B8_SRGB,  VK_FORMAT_R8G8B8A8_SRGB  };
		static const VkFormat formats16[] = { VK_FORMAT_UNDEFINED, VK_FORMAT_R16_UNORM, VK_FORMAT_R16G16_UNORM, VK_FORMAT_R16G16B16_UNORM, VK_FORMAT_R16G16B16A16_UNORM };
		static const VkFormat formats32[] = { VK_FORMAT_UNDEFINED, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT };

		if (channel < 1 || channel > 4) return VK_FORMAT_UNDEFINED;

		switch (precision)
		{
		case HIGH_PRES_32: return formats32[channel];
		case MID_PRES_16:  return formats16[channel];
		case LOW_PRES_8:
		default:           return isSRGB ? formats8_srgb[channel] : formats8_unorm[channel];
		}
	}

	// ----- 文件读取 -----

	void* VansTexture::ReadTextureFile(const std::string& texture_path, TexturePrecision texture_precision, int& bytes_per_channel, int& width, int& height, int& num_components, int import_channel)
	{
		switch (texture_precision)
		{
		case HIGH_PRES_32:
			bytes_per_channel = 4;
			return stbi_loadf(texture_path.c_str(), &width, &height, &num_components, import_channel);
		case MID_PRES_16:
			bytes_per_channel = 2;
			return stbi_load_16(texture_path.c_str(), &width, &height, &num_components, import_channel);
		default:
			bytes_per_channel = 1;
			return stbi_load(texture_path.c_str(), &width, &height, &num_components, import_channel);
		}
	}

	// ----- 通用辅助方法 -----

	void VansTexture::SubmitAndWait(VansVKCommandBuffer& command_buffer, VkQueue queue, VkDevice device)
	{
		command_buffer.EndCommandBufferRecord();
		VansVKCommandBuffer::SubmitCommands(queue, device, { command_buffer.GetVKCommandBuffer() }, {}, {}, command_buffer.m_CommandBufferFinishSubmitFence);
		command_buffer.ResetCommandBuffer(false);
	}

	void VansTexture::GenerateMipmaps(VkCommandBuffer cmd, int width, int height, int mipLevels)
	{
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = m_Image.GetImage();
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = m_Image.GetImageAspect();
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.levelCount = 1;

		int32_t mipW = width, mipH = height;

		for (int i = 1; i < mipLevels; ++i)
		{
			//上一级 TRANSFER_DST -> TRANSFER_SRC
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			//当前级 UNDEFINED -> TRANSFER_DST
			barrier.subresourceRange.baseMipLevel = i;
			barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			int32_t dstW = std::max(1, mipW / 2);
			int32_t dstH = std::max(1, mipH / 2);

			VkImageBlit blit{};
			blit.srcSubresource = { m_Image.GetImageAspect(), (uint32_t)(i - 1), 0, 1 };
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { mipW, mipH, 1 };
			blit.dstSubresource = { m_Image.GetImageAspect(), (uint32_t)i, 0, 1 };
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { dstW, dstH, 1 };

			vkCmdBlitImage(cmd,
				m_Image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				m_Image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blit, VK_FILTER_LINEAR);

			mipW = dstW;
			mipH = dstH;
		}

		//所有mip级 -> SHADER_READ_ONLY_OPTIMAL
		VkImageMemoryBarrier finalBarrier{};
		finalBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		finalBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		finalBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		finalBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		finalBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		finalBarrier.image = m_Image.GetImage();
		finalBarrier.subresourceRange = { m_Image.GetImageAspect(), 0, (uint32_t)mipLevels, 0, 1 };
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &finalBarrier);
	}

	// ----- 压缩贴图上传 -----

	void VansTexture::GenerateMipmapsForLayer(VkCommandBuffer cmd, int width, int height, int mipLevels, int layerIndex)
	{
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = m_Image.GetImage();
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = m_Image.GetImageAspect();
		barrier.subresourceRange.baseArrayLayer = (uint32_t)layerIndex;
		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.levelCount = 1;

		int32_t mipW = width, mipH = height;

		for (int i = 1; i < mipLevels; ++i)
		{
			// mip i-1：TRANSFER_DST → TRANSFER_SRC（为本次 blit 提供源数据）
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			// mip i：SHADER_READ_ONLY → TRANSFER_DST（InitTextureArray 初始化为 SHADER_READ_ONLY）
			barrier.subresourceRange.baseMipLevel = i;
			barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			int32_t dstW = std::max(1, mipW / 2);
			int32_t dstH = std::max(1, mipH / 2);

			VkImageBlit blit{};
			blit.srcSubresource = { m_Image.GetImageAspect(), (uint32_t)(i - 1), (uint32_t)layerIndex, 1 };
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { mipW, mipH, 1 };
			blit.dstSubresource = { m_Image.GetImageAspect(), (uint32_t)i, (uint32_t)layerIndex, 1 };
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { dstW, dstH, 1 };

			vkCmdBlitImage(cmd,
				m_Image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				m_Image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blit, VK_FILTER_LINEAR);

			// mip i-1：blit 完成后转回 SHADER_READ_ONLY，可被后续帧采样
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			mipW = dstW;
			mipH = dstH;
		}

		// 最后一级 mip 作为 blit 目标，从 TRANSFER_DST → SHADER_READ_ONLY
		barrier.subresourceRange.baseMipLevel = mipLevels - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);
	}

	void VansTexture::UploadCompressedTexture(VansVKCommandBuffer& command_buffer, const uint8_t* srcData, int width, int height, bool isSRGB, VkSamplerAddressMode addressMode)
	{
		VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
		VkDevice device = vkDevice->GetLogicDevice();
		VkQueue queue = vkDevice->GetGraphicsQueue();

		bool hasAlpha = true;
		int bytesPerBlock = hasAlpha ? 16 : 8;
		VkFormat format = hasAlpha
			? (isSRGB ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK)
			: (isSRGB ? VK_FORMAT_BC1_RGB_SRGB_BLOCK : VK_FORMAT_BC1_RGB_UNORM_BLOCK);

		int mipLevels = 1 + (int)std::floor(std::log2(std::max(width, height)));

		//创建GPU Image
		VkExtent3D extent = { (uint32_t)width, (uint32_t)height, 1 };
		m_Image.CreateVulkanImage(device, extent, format, mipLevels, 1,
			VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			VK_SAMPLE_COUNT_1_BIT, false, true, true, addressMode);

		//CPU端逐级降采样 + 压缩 + 上传
		std::vector<uint8_t> mipRGBA(srcData, srcData + width * height * 4);
		int mipW = width, mipH = height;

		for (int m = 0; m < mipLevels; ++m)
		{
			std::vector<uint8_t> compressed = CompressMipToBC(mipRGBA.data(), mipW, mipH, hasAlpha);

			VkExtent3D mipExtent = { (uint32_t)mipW, (uint32_t)mipH, 1 };
			VkOffset3D offset = { 0, 0, 0 };
			vkDevice->SetDeviceImageData(m_Image, command_buffer, compressed.data(),
				0, static_cast<int>(compressed.size()), offset, mipExtent, m, 0);

			//降采样生成下一级
			if (m + 1 < mipLevels)
			{
				mipRGBA = Downsample2x2_RGBA8(mipRGBA.data(), mipW, mipH);
				mipW = std::max(1, mipW / 2);
				mipH = std::max(1, mipH / 2);
			}
		}

		//全部mip -> SHADER_READ_ONLY
		command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		m_Image.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			{
				m_Image.GetImage(),
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_SHADER_READ_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				m_Image.GetImageAspect()
			});
		SubmitAndWait(command_buffer, queue, device);
	}

	// ----- 非压缩贴图上传 -----

	void VansTexture::UploadUncompressedTexture(VansVKCommandBuffer& command_buffer, const void* data, size_t dataSize, int width, int height, VkFormat format, bool needMip, VkSamplerAddressMode addressMode)
	{
		VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
		VkDevice device = vkDevice->GetLogicDevice();
		VkQueue queue = vkDevice->GetGraphicsQueue();

		int mipLevels = needMip ? 1 + (int)std::floor(std::log2(std::max(width, height))) : 1;

		//创建GPU Image
		VkExtent3D extent = { (uint32_t)width, (uint32_t)height, 1 };
		m_Image.CreateVulkanImage(device, extent, format, mipLevels, 1,
			VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_SAMPLE_COUNT_1_BIT, false, true, true, addressMode);

		//上传mip 0
		VkOffset3D offset = { 0, 0, 0 };
		vkDevice->SetDeviceImageData(m_Image, command_buffer, const_cast<void*>(data), 0, static_cast<int>(dataSize), offset, extent, 0, 0);

		//生成mip链或直接转换layout
		command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

		if (mipLevels > 1)
		{
			GenerateMipmaps(command_buffer.GetVKCommandBuffer(), width, height, mipLevels);
		}
		else
		{
			m_Image.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				{
					m_Image.GetImage(),
					VK_ACCESS_TRANSFER_WRITE_BIT,
					VK_ACCESS_SHADER_READ_BIT,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_QUEUE_FAMILY_IGNORED,
					VK_QUEUE_FAMILY_IGNORED,
					m_Image.GetImageAspect()
				});
		}

		SubmitAndWait(command_buffer, queue, device);
	}

	// =====================================================================
	// 公开接口
	// =====================================================================

	void VansTexture::LoadTexture(VansVKCommandBuffer& command_buffer, std::string texture_path, bool isSRGB, bool useCompress, bool need_mip, TexturePrecision texture_precision, int import_channel, VkSamplerAddressMode addressMode)
	{
		VANS_LOG("Load Texture : " << texture_path);

		// 1. 读取文件
		int width = 0, height = 0, num_components = 0, bytes_per_channel = 1;
		void* pixel_data = ReadTextureFile(texture_path, texture_precision, bytes_per_channel, width, height, num_components, import_channel);

		if (!pixel_data || width <= 0 || height <= 0 || num_components <= 0)
		{
			VANS_LOG_ERROR("Could not read image!");
			return;
		}

		if (import_channel != 0)
			num_components = import_channel;

		m_TextureWidth = width;
		m_TextureHeight = height;

		// 2. 根据是否压缩选择上传路径
		bool canCompress = useCompress && texture_precision == LOW_PRES_8 && num_components == 4;

		if (canCompress)
		{
			UploadCompressedTexture(command_buffer, static_cast<const uint8_t*>(pixel_data), width, height, isSRGB, addressMode);
		}
		else
		{
			size_t dataSize = (size_t)width * height * num_components * bytes_per_channel;
			VkFormat format = ChooseFormat(num_components, texture_precision, isSRGB);
			UploadUncompressedTexture(command_buffer, pixel_data, dataSize, width, height, format, need_mip, addressMode);
		}

		stbi_image_free(pixel_data);
	}

	void VansTexture::LoadCubeTexture(VansVKCommandBuffer& command_buffer, std::string texture_parent_path, bool isSRGB)
	{
		VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
		VkDevice device = vkDevice->GetLogicDevice();
		VkQueue queue = vkDevice->GetGraphicsQueue();

		const char* faceNames[] = { "/Right.hdr", "/Left.hdr", "/Top.hdr", "/Bottom.hdr", "/Front.hdr", "/Back.hdr" };
		bool imageCreated = false;

		for (int face = 0; face < 6; ++face)
		{
			std::string path = texture_parent_path + faceNames[face];
			int width = 0, height = 0, num_components = 0;
			std::unique_ptr<unsigned char, void(*)(void*)> stbi_data(
				stbi_load(path.c_str(), &width, &height, &num_components, 4), stbi_image_free);

			if (!stbi_data || width <= 0 || height <= 0)
			{
				VANS_LOG_ERROR("Could not read image!");
				return;
			}

			num_components = 4;
			int dataSize = width * height * num_components;

			if (!imageCreated)
			{
				VkExtent3D extent = { (uint32_t)width, (uint32_t)height, 1 };
				VkFormat format = ChooseFormat(num_components, LOW_PRES_8, isSRGB);
				m_Image.CreateVulkanImage(device, extent, format, 1, 1,
					VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
					VK_SAMPLE_COUNT_1_BIT, true, true, true);
				imageCreated = true;
			}

			VkOffset3D offset = { 0, 0, 0 };
			VkExtent3D extent = { (uint32_t)width, (uint32_t)height, 1 };
			vkDevice->SetDeviceImageData(m_Image, command_buffer, stbi_data.get(), 0, dataSize, offset, extent, 0, face);

			if (face == 0)
			{
				m_TextureWidth = width;
				m_TextureHeight = height;
			}
		}

		//切换layout到SHADER_READ_ONLY_OPTIMAL
		command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		m_Image.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			{
				m_Image.GetImage(),
				VK_ACCESS_NONE,
				VK_ACCESS_NONE,
				m_Image.GetImageLayout(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				m_Image.GetImageAspect()
			});
		SubmitAndWait(command_buffer, queue, device);
	}

	void VansTexture::LoadFromMemory(VansVKCommandBuffer& command_buffer,
		const void* data, size_t dataSize,
		int width, int height, VkFormat format,
		VkSamplerAddressMode addressMode)
	{
		m_TextureWidth = width;
		m_TextureHeight = height;
		m_TextureSlice = 1;
		UploadUncompressedTexture(command_buffer, data, dataSize, width, height, format,
			/*needMip*/ false, addressMode);
	}

	void VansTexture::InitTextureArray(VansVKCommandBuffer& command_buffer,
		int width, int height, int layerCount, int numComponents,
		bool generateMip, TexturePrecision texture_precision, VkSamplerAddressMode addressMode)
	{
		m_TextureWidth = width;
		m_TextureHeight = height;
		m_TextureSlice = layerCount;

		VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
		VkDevice device = vkDevice->GetLogicDevice();
		VkQueue queue = vkDevice->GetGraphicsQueue();

		VkFormat format = ChooseFormat(numComponents, texture_precision);
		int mipLevels = generateMip ? 1 + (int)std::floor(std::log2((float)std::max(width, height))) : 1;

		// 创建 2D 贴图数组：VK_IMAGE_TYPE_2D + layer_num > 1 → VK_IMAGE_VIEW_TYPE_2D_ARRAY
		// TRANSFER_SRC_BIT：vkCmdBlitImage 将已上传的 mip 0 逐级下采样时需要读源
		VkExtent3D extent = { (uint32_t)width, (uint32_t)height, 1 };
		m_Image.CreateVulkanImage(device, extent, format, mipLevels, (uint32_t)layerCount,
			VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			VK_SAMPLE_COUNT_1_BIT, false, true, true, addressMode);

		// 将所有层从 UNDEFINED → SHADER_READ_ONLY_OPTIMAL
		command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		m_Image.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			{
				m_Image.GetImage(),
				VK_ACCESS_NONE,
				VK_ACCESS_SHADER_READ_BIT,
				m_Image.GetImageLayout(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				m_Image.GetImageAspect()
			});
		SubmitAndWait(command_buffer, queue, device);
	}

	bool VansTexture::LoadTextureLayer(VansVKCommandBuffer& command_buffer,
		const std::string& texturePath, int layerIndex, bool isSRGB, VkSamplerAddressMode addressMode)
	{
		VANS_LOG("LoadTextureLayer [" << layerIndex << "]: " << texturePath);

		VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);

		// 读取文件，强制 4 通道 RGBA8
		int fileW = 0, fileH = 0, numComponents = 0, bytesPerChannel = 0;
		void* pixelData = ReadTextureFile(texturePath, LOW_PRES_8, bytesPerChannel, fileW, fileH, numComponents, 4);
		if (!pixelData || fileW <= 0 || fileH <= 0)
		{
			VANS_LOG_ERROR("LoadTextureLayer: 无法读取图片: " << texturePath);
			return false;
		}

		// 若分辨率与数组贴图不一致，进行最近邻缩放
		std::vector<uint8_t> resized;
		const uint8_t* uploadData = static_cast<const uint8_t*>(pixelData);
		int uploadW = fileW, uploadH = fileH;

		if (fileW != m_TextureWidth || fileH != m_TextureHeight)
		{
			resized.resize(size_t(m_TextureWidth) * m_TextureHeight * 4);
			float scaleX = float(fileW) / float(m_TextureWidth);
			float scaleY = float(fileH) / float(m_TextureHeight);
			for (int y = 0; y < m_TextureHeight; ++y)
			{
				for (int x = 0; x < m_TextureWidth; ++x)
				{
					int srcX = std::min((int)(x * scaleX), fileW - 1);
					int srcY = std::min((int)(y * scaleY), fileH - 1);
					const uint8_t* src = static_cast<const uint8_t*>(pixelData) + (srcY * fileW + srcX) * 4;
					uint8_t* dst = resized.data() + (size_t(y) * m_TextureWidth + x) * 4;
					dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
				}
			}
			uploadData = resized.data();
			uploadW = m_TextureWidth;
			uploadH = m_TextureHeight;
		}

		size_t dataSize = size_t(uploadW) * uploadH * 4;
		VkExtent3D extent = { (uint32_t)uploadW, (uint32_t)uploadH, 1 };
		VkOffset3D zeroOffset = { 0, 0, 0 };
		vkDevice->SetDeviceImageData(m_Image, command_buffer,
			const_cast<uint8_t*>(uploadData), 0, (int)dataSize, zeroOffset, extent, 0, layerIndex);

		// SetDeviceImageData 将 mip 0 of layerIndex 置于 TRANSFER_DST_OPTIMAL。
		// 若分配了多级 mip（generateMip=true），则用 vkCmdBlitImage 逐级下采样生成完整 mip 链；
		// 单 mip 时直接转换回 SHADER_READ_ONLY_OPTIMAL 即可。
		{
			VkQueue queue = vkDevice->GetGraphicsQueue();
			VkDevice device = vkDevice->GetLogicDevice();
			int mipLevels = (int)m_Image.GetImageCreateInfo().mipLevels;

			command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
			if (mipLevels > 1)
			{
				GenerateMipmapsForLayer(command_buffer.GetVKCommandBuffer(),
					uploadW, uploadH, mipLevels, layerIndex);
			}
			else
			{
				m_Image.SetImageMemoryBarrier(
					VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					{
						m_Image.GetImage(),
						VK_ACCESS_TRANSFER_WRITE_BIT,
						VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						VK_QUEUE_FAMILY_IGNORED,
						VK_QUEUE_FAMILY_IGNORED,
						m_Image.GetImageAspect()
					});
			}
			SubmitAndWait(command_buffer, queue, device);
		}

		stbi_image_free(pixelData);
		return true;
	}

	// ===========================================================================
	// UpdateArrayLayerFromPixels — 每帧将视频帧 CPU 像素写入贴图数组指定层
	// 与 LoadTextureLayer 相同流程，但直接接收像素指针而无需读取文件。
	// ===========================================================================
	bool VansTexture::UpdateArrayLayerFromPixels(VansVKCommandBuffer& command_buffer,
		const uint8_t* pixels, int srcW, int srcH, int layerIndex)
	{
		if (!pixels || srcW <= 0 || srcH <= 0 || layerIndex < 0 || layerIndex >= m_TextureSlice)
		{
			VANS_LOG_ERROR("[VansTexture] UpdateArrayLayerFromPixels: 参数无效 layer=" << layerIndex);
			return false;
		}

		VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
		if (!vkDevice) return false;

		// 若分辨率与数组贴图不一致，进行最近邻缩放
		std::vector<uint8_t> resized;
		const uint8_t* uploadData = pixels;
		int uploadW = srcW, uploadH = srcH;

		if (srcW != m_TextureWidth || srcH != m_TextureHeight)
		{
			VANS_PROFILE_SCOPE("RectLightVideo::ResizeCPU", Vans::ProfileCategory::Video);
			resized.resize(size_t(m_TextureWidth) * m_TextureHeight * 4);
			float scaleX = float(srcW) / float(m_TextureWidth);
			float scaleY = float(srcH) / float(m_TextureHeight);
			for (int y = 0; y < m_TextureHeight; ++y)
			{
				for (int x = 0; x < m_TextureWidth; ++x)
				{
					int srcX = std::min((int)(x * scaleX), srcW - 1);
					int srcY = std::min((int)(y * scaleY), srcH - 1);
					const uint8_t* src = pixels + (size_t(srcY) * srcW + srcX) * 4;
					uint8_t* dst = resized.data() + (size_t(y) * m_TextureWidth + x) * 4;
					dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
				}
			}
			uploadData = resized.data();
			uploadW = m_TextureWidth;
			uploadH = m_TextureHeight;
		}

		size_t dataSize = size_t(uploadW) * uploadH * 4;
		VkExtent3D extent = { (uint32_t)uploadW, (uint32_t)uploadH, 1 };
		VkOffset3D zeroOffset = { 0, 0, 0 };
		// SetDeviceImageData 上传 mip 0，并通过 fence 同步等待
		vkDevice->SetDeviceImageData(m_Image, command_buffer,
			const_cast<uint8_t*>(uploadData), 0, (int)dataSize, zeroOffset, extent, 0, layerIndex);

		// 重新生成该层的完整 mip 链（与 LoadTextureLayer 逻辑完全一致）
		{
			VkQueue queue = vkDevice->GetGraphicsQueue();
			VkDevice device = vkDevice->GetLogicDevice();
			int mipLevels = (int)m_Image.GetImageCreateInfo().mipLevels;

			command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
			if (mipLevels > 1)
			{
				GenerateMipmapsForLayer(command_buffer.GetVKCommandBuffer(),
					uploadW, uploadH, mipLevels, layerIndex);
			}
			else
			{
				m_Image.SetImageMemoryBarrier(
					VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					{
						m_Image.GetImage(),
						VK_ACCESS_TRANSFER_WRITE_BIT,
						VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						VK_QUEUE_FAMILY_IGNORED,
						VK_QUEUE_FAMILY_IGNORED,
						m_Image.GetImageAspect()
					});
			}
			SubmitAndWait(command_buffer, queue, device);
		}

		return true;
	}

	// ===========================================================================
	// RecordArrayLayerUploadFromPixels — 录制贴图数组层更新，合并进当前帧提交
	// 与 UpdateArrayLayerFromPixels 保持相同的最近邻缩放与 mip 链生成效果。
	// ===========================================================================
	bool VansTexture::RecordArrayLayerUploadFromPixels(VansVKCommandBuffer& command_buffer,
		const uint8_t* pixels, int srcW, int srcH, int layerIndex)
	{
		if (!pixels || srcW <= 0 || srcH <= 0 || layerIndex < 0 || layerIndex >= m_TextureSlice)
		{
			VANS_LOG_ERROR("[VansTexture] RecordArrayLayerUploadFromPixels: 参数无效 layer=" << layerIndex);
			return false;
		}

		VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
		if (!vkDevice) return false;

		// 若分辨率与数组贴图不一致，保持旧路径的最近邻缩放效果。
		std::vector<uint8_t> resized;
		const uint8_t* uploadData = pixels;
		int uploadW = srcW, uploadH = srcH;

		if (srcW != m_TextureWidth || srcH != m_TextureHeight)
		{
			resized.resize(size_t(m_TextureWidth) * m_TextureHeight * 4);
			float scaleX = float(srcW) / float(m_TextureWidth);
			float scaleY = float(srcH) / float(m_TextureHeight);
			for (int y = 0; y < m_TextureHeight; ++y)
			{
				for (int x = 0; x < m_TextureWidth; ++x)
				{
					int srcX = std::min((int)(x * scaleX), srcW - 1);
					int srcY = std::min((int)(y * scaleY), srcH - 1);
					const uint8_t* src = pixels + (size_t(srcY) * srcW + srcX) * 4;
					uint8_t* dst = resized.data() + (size_t(y) * m_TextureWidth + x) * 4;
					dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
				}
			}
			uploadData = resized.data();
			uploadW = m_TextureWidth;
			uploadH = m_TextureHeight;
		}

		size_t dataSize = size_t(uploadW) * uploadH * 4;
		VkExtent3D extent = { (uint32_t)uploadW, (uint32_t)uploadH, 1 };
		VkOffset3D zeroOffset = { 0, 0, 0 };
		{
			VANS_PROFILE_SCOPE("RectLightVideo::UploadArrayLayer", Vans::ProfileCategory::Video);
			if (!vkDevice->RecordDeviceImageData(m_Image, command_buffer,
				uploadData, static_cast<int>(dataSize), zeroOffset, extent,
				0, layerIndex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
			{
				return false;
			}
		}

		int mipLevels = (int)m_Image.GetImageCreateInfo().mipLevels;
		if (mipLevels > 1)
		{
			VANS_PROFILE_SCOPE("RectLightVideo::GenerateMipmaps", Vans::ProfileCategory::Video);
			GenerateMipmapsForLayer(command_buffer.GetVKCommandBuffer(),
				uploadW, uploadH, mipLevels, layerIndex);
		}
		else
		{
			VkImageMemoryBarrier toShaderRead{};
			toShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toShaderRead.image = m_Image.GetImage();
			toShaderRead.subresourceRange = { m_Image.GetImageAspect(), 0, 1u, (uint32_t)layerIndex, 1u };
			command_buffer.PipelineBarrier(
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				{}, {}, { toShaderRead });
		}

		m_Image.SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		return true;
	}

	// ===========================================================================
	// RecordArrayLayerCopyFromTexture — 从 GPU 视频纹理直接写入贴图数组层
	// 避免 RectLight 视频路径对同一帧像素进行第二次 CPU staging 上传。
	// ===========================================================================
	bool VansTexture::RecordArrayLayerCopyFromTexture(VansVKCommandBuffer& command_buffer,
		VansTexture* sourceTexture, int layerIndex)
	{
		if (!sourceTexture || layerIndex < 0 || layerIndex >= m_TextureSlice ||
			sourceTexture->GetWidth() <= 0 || sourceTexture->GetHeight() <= 0)
		{
			VANS_LOG_ERROR("[VansTexture] RecordArrayLayerCopyFromTexture: 参数无效 layer=" << layerIndex);
			return false;
		}

		VansVKImage& sourceImage = sourceTexture->GetImage();
		const int sourceW = sourceTexture->GetWidth();
		const int sourceH = sourceTexture->GetHeight();
		const VkImageLayout sourceOriginalLayout = sourceImage.GetImageLayout();
		const VkImageLayout targetOriginalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		{
			VANS_PROFILE_SCOPE("RectLightVideo::GpuCopy.SetupBarriers", Vans::ProfileCategory::Video);
			VkImageMemoryBarrier sourceToTransfer{};
			sourceToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			sourceToTransfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			sourceToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			sourceToTransfer.oldLayout = sourceOriginalLayout;
			sourceToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			sourceToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			sourceToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			sourceToTransfer.image = sourceImage.GetImage();
			sourceToTransfer.subresourceRange = { sourceImage.GetImageAspect(), 0, 1u, 0, 1u };

			VkImageMemoryBarrier targetToTransfer{};
			targetToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			targetToTransfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			targetToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			targetToTransfer.oldLayout = targetOriginalLayout;
			targetToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			targetToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			targetToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			targetToTransfer.image = m_Image.GetImage();
			targetToTransfer.subresourceRange = { m_Image.GetImageAspect(), 0, 1u, static_cast<uint32_t>(layerIndex), 1u };

			command_buffer.PipelineBarrier(
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				{}, {}, { sourceToTransfer, targetToTransfer });
		}

		if (sourceW == m_TextureWidth && sourceH == m_TextureHeight)
		{
			VANS_PROFILE_SCOPE("RectLightVideo::GpuCopy.CopyImage", Vans::ProfileCategory::Video);
			VkImageCopy copyRegion{};
			copyRegion.srcSubresource = { sourceImage.GetImageAspect(), 0, 0, 1 };
			copyRegion.srcOffset = { 0, 0, 0 };
			copyRegion.dstSubresource = { m_Image.GetImageAspect(), 0, static_cast<uint32_t>(layerIndex), 1 };
			copyRegion.dstOffset = { 0, 0, 0 };
			copyRegion.extent = { static_cast<uint32_t>(m_TextureWidth), static_cast<uint32_t>(m_TextureHeight), 1u };
			command_buffer.CopyImageRegions(sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, { copyRegion });
		}
		else
		{
			VANS_PROFILE_SCOPE("RectLightVideo::GpuCopy.BlitScale", Vans::ProfileCategory::Video);
			VkImageBlit blitRegion{};
			blitRegion.srcSubresource = { sourceImage.GetImageAspect(), 0, 0, 1 };
			blitRegion.srcOffsets[0] = { 0, 0, 0 };
			blitRegion.srcOffsets[1] = { sourceW, sourceH, 1 };
			blitRegion.dstSubresource = { m_Image.GetImageAspect(), 0, static_cast<uint32_t>(layerIndex), 1 };
			blitRegion.dstOffsets[0] = { 0, 0, 0 };
			blitRegion.dstOffsets[1] = { m_TextureWidth, m_TextureHeight, 1 };
			command_buffer.BlitImageRegions(sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, { blitRegion }, VK_FILTER_LINEAR);
		}

		{
			VANS_PROFILE_SCOPE("RectLightVideo::GpuCopy.RestoreSource", Vans::ProfileCategory::Video);
			VkImageMemoryBarrier sourceToOriginal{};
			sourceToOriginal.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			sourceToOriginal.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			sourceToOriginal.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			sourceToOriginal.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			sourceToOriginal.newLayout = sourceOriginalLayout;
			sourceToOriginal.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			sourceToOriginal.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			sourceToOriginal.image = sourceImage.GetImage();
			sourceToOriginal.subresourceRange = { sourceImage.GetImageAspect(), 0, 1u, 0, 1u };

			command_buffer.PipelineBarrier(
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				{}, {}, { sourceToOriginal });
			sourceImage.SetTrackedImageLayout(sourceOriginalLayout);
		}

		int mipLevels = static_cast<int>(m_Image.GetImageCreateInfo().mipLevels);
		if (mipLevels > 1)
		{
			VANS_PROFILE_SCOPE("RectLightVideo::GpuCopy.GenerateMipmaps", Vans::ProfileCategory::Video);
			GenerateMipmapsForLayer(command_buffer.GetVKCommandBuffer(),
				m_TextureWidth, m_TextureHeight, mipLevels, layerIndex);
		}
		else
		{
			VkImageMemoryBarrier targetToShaderRead{};
			targetToShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			targetToShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			targetToShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			targetToShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			targetToShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			targetToShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			targetToShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			targetToShaderRead.image = m_Image.GetImage();
			targetToShaderRead.subresourceRange = { m_Image.GetImageAspect(), 0, 1u, static_cast<uint32_t>(layerIndex), 1u };
			command_buffer.PipelineBarrier(
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				{}, {}, { targetToShaderRead });
		}

		m_Image.SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		return true;
	}

	void VansTexture::InitTextureWithoutData(VansVKCommandBuffer& command_buffer, int width, int height, int slice, int num_components, bool isCube, bool generateMip, bool enabeRandonWrite, TexturePrecision texture_precision, VkSamplerAddressMode addressMode)
	{
		m_TextureWidth = width;
		m_TextureHeight = height;
		m_TextureSlice = slice;

		VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
		VkDevice device = vkDevice->GetLogicDevice();
		VkQueue queue = vkDevice->GetGraphicsQueue();

		bool is3D = slice > 1;
		VkFormat format = ChooseFormat(num_components, texture_precision);
		int mipLevels = generateMip ? 1 + (int)std::floor(std::log2((float)width)) : 1;

		VkExtent3D extent = { (uint32_t)width, (uint32_t)height, (uint32_t)slice };
		m_Image.CreateVulkanImage(device, extent, format, mipLevels, 1,
			is3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			VK_SAMPLE_COUNT_1_BIT, isCube, true, true, addressMode);

		VkImageLayout targetLayout = enabeRandonWrite ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		m_Image.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			{
				m_Image.GetImage(),
				VK_ACCESS_NONE,
				VK_ACCESS_NONE,
				m_Image.GetImageLayout(),
				targetLayout,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				m_Image.GetImageAspect()
			});
		SubmitAndWait(command_buffer, queue, device);
	}
}
