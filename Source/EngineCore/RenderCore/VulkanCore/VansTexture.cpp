#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansTexture.h"
#include "VansVKDevice.h"
#include "VansVKCommandBuffer.h"
#include "../../AssetCore/Importers/VansTextureCooker.h"
#include "../../Util/VansJobSystem.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansProfiler.h"
#include <atomic>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include <../../STBImge/stb_image.h>

#include <../../STBImge/stb_dxt.h>

namespace VansGraphics
{
	namespace
	{
		std::atomic<std::uint64_t> g_TextureUploadFailures{ 0 };

		void RecordTextureUploadFailure()
		{
			g_TextureUploadFailures.fetch_add(1, std::memory_order_relaxed);
		}
	}

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

	static size_t CalculateBlockCompressedDataSize(
		int width,
		int height,
		int blockWidth,
		int blockHeight,
		int bytesPerBlock)
	{
		const int blocksX = (width + blockWidth - 1) / blockWidth;
		const int blocksY = (height + blockHeight - 1) / blockHeight;
		return static_cast<size_t>(blocksX) * static_cast<size_t>(blocksY) * static_cast<size_t>(bytesPerBlock);
	}

	//将单层RGBA8数据压缩为BC块格式
	static std::vector<uint8_t> CompressMipToBC(
		const uint8_t* rgba,
		int w,
		int h,
		int blockWidth,
		int blockHeight,
		int bytesPerBlock,
		bool hasAlpha)
	{
		int blocksX = (w + blockWidth - 1) / blockWidth;
		int blocksY = (h + blockHeight - 1) / blockHeight;
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
				for (int ty = 0; ty < blockHeight; ++ty)
				{
					for (int tx = 0; tx < blockWidth; ++tx)
					{
						const uint8_t* s = fetch(bx * blockWidth + tx, by * blockHeight + ty);
						int idx = (ty * blockWidth + tx) * 4;
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

	struct RGBA8LayerUploadView
	{
		const uint8_t* pixels = nullptr;
		int width = 0;
		int height = 0;
		std::vector<uint8_t> resizedPixels;
	};

	static RGBA8LayerUploadView PrepareRGBA8LayerUpload(
		const uint8_t* sourcePixels,
		int sourceWidth,
		int sourceHeight,
		int targetWidth,
		int targetHeight)
	{
		RGBA8LayerUploadView upload{};
		upload.pixels = sourcePixels;
		upload.width = sourceWidth;
		upload.height = sourceHeight;

		if (sourceWidth == targetWidth && sourceHeight == targetHeight)
		{
			return upload;
		}

		upload.resizedPixels.resize(size_t(targetWidth) * targetHeight * 4);
		const float scaleX = float(sourceWidth) / float(targetWidth);
		const float scaleY = float(sourceHeight) / float(targetHeight);
		for (int y = 0; y < targetHeight; ++y)
		{
			for (int x = 0; x < targetWidth; ++x)
			{
				const int srcX = std::min(static_cast<int>(x * scaleX), sourceWidth - 1);
				const int srcY = std::min(static_cast<int>(y * scaleY), sourceHeight - 1);
				const uint8_t* src = sourcePixels + (size_t(srcY) * sourceWidth + srcX) * 4;
				uint8_t* dst = upload.resizedPixels.data() + (size_t(y) * targetWidth + x) * 4;
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
				dst[3] = src[3];
			}
		}

		upload.pixels = upload.resizedPixels.data();
		upload.width = targetWidth;
		upload.height = targetHeight;
		return upload;
	}

	// =====================================================================
	// VansTexture 实现
	// =====================================================================

	VansTexture::~VansTexture()
	{
		m_Image.DestroyVulkanImage(*(VkDevice*)m_GraphicsDevice->GetNativeGraphicsDevice());
	}

	std::uint64_t VansTexture::GetUploadFailureCount()
	{
		return g_TextureUploadFailures.load(std::memory_order_relaxed);
	}

	void VansTexture::ResetUploadFailureCount()
	{
		g_TextureUploadFailures.store(0, std::memory_order_relaxed);
	}

	// ----- 格式选择 -----

	VkFormat VansTexture::ChooseFormat(int channel, TexturePrecision precision, bool isSRGB)
	{
		static const VkFormat formats8_unorm[] = { VK_FORMAT_UNDEFINED, VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };
		static const VkFormat formats8_srgb[]  = { VK_FORMAT_UNDEFINED, VK_FORMAT_R8_SRGB,  VK_FORMAT_R8G8_SRGB,  VK_FORMAT_R8G8B8_SRGB,  VK_FORMAT_R8G8B8A8_SRGB  };
		static const VkFormat formats16[] = { VK_FORMAT_UNDEFINED, VK_FORMAT_R16_UNORM, VK_FORMAT_R16G16_UNORM, VK_FORMAT_R16G16B16_UNORM, VK_FORMAT_R16G16B16A16_UNORM };
		static const VkFormat formats16f[] = { VK_FORMAT_UNDEFINED, VK_FORMAT_R16_SFLOAT, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R16G16B16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT };
		static const VkFormat formats32[] = { VK_FORMAT_UNDEFINED, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT };

		if (channel < 1 || channel > 4) return VK_FORMAT_UNDEFINED;

		switch (precision)
		{
		case HIGH_PRES_32: return formats32[channel];
		case HDR_PRES_16:  return formats16f[channel];
		case MID_PRES_16:  return formats16[channel];
		case LOW_PRES_8:
		default:           return isSRGB ? formats8_srgb[channel] : formats8_unorm[channel];
		}
	}

	VansTexture::TextureContentIntent VansTexture::DetermineTextureContentIntent(bool isSRGB, TexturePrecision precision)
	{
		if (precision == HDR_PRES_16 || precision == HIGH_PRES_32)
		{
			return TextureContentIntent::HDRData;
		}

		return isSRGB
			? TextureContentIntent::Color
			: TextureContentIntent::LinearData;
	}

	VansTexture::TextureCompressionProfile VansTexture::ChooseCompressionProfile(
		int sourceChannels, int bytesPerChannel,
		bool isSRGB, bool useCompress, TexturePrecision precision)
	{
		TextureCompressionProfile profile{};
		const bool canUseBC3 =
			useCompress
			&& precision == LOW_PRES_8
			&& sourceChannels == 4
			&& bytesPerChannel == 1;

		if (canUseBC3)
		{
			profile.mode = TextureCompressionMode::BC3;
			profile.format = isSRGB ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
			profile.blockWidth = 4;
			profile.blockHeight = 4;
			profile.bytesPerBlock = 16;
			profile.hasAlpha = true;
			profile.forceFullMipChain = true;
		}

		return profile;
	}

	int VansTexture::CalculateMipLevels(int width, int height, bool generateMip)
	{
		if (!generateMip)
		{
			return 1;
		}

		return 1 + static_cast<int>(std::floor(std::log2(std::max(width, height))));
	}

	VansTexture::TextureUploadPlan VansTexture::BuildTextureUploadPlan(
		int width, int height, int sourceChannels, int bytesPerChannel,
		bool isSRGB, bool useCompress, bool needMip, TexturePrecision precision)
	{
		TextureUploadPlan uploadPlan{};
		uploadPlan.contentIntent = DetermineTextureContentIntent(isSRGB, precision);
		uploadPlan.sourceChannels = sourceChannels;
		uploadPlan.bytesPerChannel = bytesPerChannel;
		const TextureCompressionProfile compressionProfile = ChooseCompressionProfile(
			sourceChannels, bytesPerChannel,
			isSRGB, useCompress, precision);

		if (compressionProfile.mode != TextureCompressionMode::None)
		{
			// Preserve the previous compression behavior: RGBA8 uploads use BC3
			// and build the full compressed mip chain on the CPU.
			uploadPlan.format = compressionProfile.format;
			uploadPlan.mipLevels = CalculateMipLevels(width, height, compressionProfile.forceFullMipChain);
			uploadPlan.compressionMode = compressionProfile.mode;
			uploadPlan.mipGeneration = TextureMipGeneration::CPUCompressedChain;
			uploadPlan.compressedBlockWidth = compressionProfile.blockWidth;
			uploadPlan.compressedBlockHeight = compressionProfile.blockHeight;
			uploadPlan.compressedBytesPerBlock = compressionProfile.bytesPerBlock;
			uploadPlan.compressedHasAlpha = compressionProfile.hasAlpha;
			uploadPlan.imageUsage =
				VK_IMAGE_USAGE_SAMPLED_BIT
				| VK_IMAGE_USAGE_TRANSFER_DST_BIT
				| VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		}
		else
		{
			uploadPlan.format = ChooseFormat(sourceChannels, precision, isSRGB);
			uploadPlan.mipLevels = CalculateMipLevels(width, height, needMip);
			uploadPlan.compressionMode = TextureCompressionMode::None;
			uploadPlan.mipGeneration = uploadPlan.mipLevels > 1
				? TextureMipGeneration::GPUBlit
				: TextureMipGeneration::None;
			uploadPlan.imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			if (uploadPlan.mipGeneration == TextureMipGeneration::GPUBlit)
			{
				uploadPlan.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			}
		}

		return uploadPlan;
	}

	VansTexture::TextureUploadRequest VansTexture::BuildTextureUploadRequest(
		const void* sourceData,
		int width, int height,
		bool isSRGB, bool useCompress, bool needMip,
		TexturePrecision precision,
		int sourceChannels, int bytesPerChannel,
		VkSamplerAddressMode addressMode)
	{
		TextureUploadRequest request{};
		request.sourceData = sourceData;
		request.width = width;
		request.height = height;
		request.plan = BuildTextureUploadPlan(
			width, height, sourceChannels, bytesPerChannel,
			isSRGB, useCompress, needMip, precision);
		request.sourceDataSize = CalculateTextureDataSize(
			width, height, sourceChannels, bytesPerChannel);
		request.addressMode = addressMode;
		return request;
	}

	size_t VansTexture::CalculateTextureDataSize(int width, int height, int channels, int bytesPerChannel)
	{
		return static_cast<size_t>(width)
			* static_cast<size_t>(height)
			* static_cast<size_t>(channels)
			* static_cast<size_t>(bytesPerChannel);
	}

	bool VansTexture::IsValidTextureUploadRequest(const TextureUploadRequest& request)
	{
		if (request.sourceData == nullptr
			|| request.width <= 0
			|| request.height <= 0
			|| request.sourceDataSize == 0
			|| request.plan.format == VK_FORMAT_UNDEFINED
			|| request.plan.mipLevels <= 0)
		{
			return false;
		}

		if (request.plan.compressionMode == TextureCompressionMode::BC3)
		{
			return request.plan.sourceChannels == 4
				&& request.plan.bytesPerChannel == 1
				&& request.plan.compressedBlockWidth > 0
				&& request.plan.compressedBlockHeight > 0
				&& request.plan.compressedBytesPerBlock > 0;
		}

		return request.plan.compressionMode == TextureCompressionMode::None;
	}

	const char* VansTexture::ToString(TextureCompressionMode mode)
	{
		switch (mode)
		{
		case TextureCompressionMode::None:
			return "None";
		case TextureCompressionMode::BC3:
			return "BC3";
		}

		return "Unknown";
	}

	const char* VansTexture::ToString(TextureMipGeneration mode)
	{
		switch (mode)
		{
		case TextureMipGeneration::None:
			return "None";
		case TextureMipGeneration::GPUBlit:
			return "GPUBlit";
		case TextureMipGeneration::CPUCompressedChain:
			return "CPUCompressedChain";
		}

		return "Unknown";
	}

	const char* VansTexture::ToString(TextureContentIntent intent)
	{
		switch (intent)
		{
		case TextureContentIntent::Color:
			return "Color";
		case TextureContentIntent::LinearData:
			return "LinearData";
		case TextureContentIntent::HDRData:
			return "HDRData";
		}

		return "Unknown";
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

	bool VansTexture::SubmitAndWait(VansVKCommandBuffer& command_buffer, VkQueue queue, VkDevice device)
	{
		if (!command_buffer.EndCommandBufferRecord())
		{
			VANS_LOG_ERROR("[VansTexture] Failed to end texture upload command buffer.");
			return false;
		}
		if (!VansVKCommandBuffer::SubmitCommands(queue, device, { command_buffer.GetVKCommandBuffer() }, {}, {}, command_buffer.m_CommandBufferFinishSubmitFence))
		{
			VANS_LOG_ERROR("[VansTexture] Failed to submit texture upload command buffer.");
			return false;
		}
		if (!command_buffer.ResetCommandBuffer(false))
		{
			VANS_LOG_ERROR("[VansTexture] Failed to reset texture upload command buffer.");
			return false;
		}
		return true;
	}

	void VansTexture::GenerateMipmaps(VansVKCommandBuffer& command_buffer, int width, int height, int mipLevels)
	{
		GenerateMipmapsForLayer(
			command_buffer,
			width,
			height,
			mipLevels,
			0,
			VK_IMAGE_LAYOUT_UNDEFINED,
			0,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
	}

	bool VansTexture::UploadTexture(VansVKCommandBuffer& command_buffer, const TextureUploadRequest& request)
	{
		if (!IsValidTextureUploadRequest(request))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("Invalid texture upload request.");
			return false;
		}

		switch (request.plan.compressionMode)
		{
		case TextureCompressionMode::BC3:
			return UploadCompressedTexture(
				command_buffer,
				static_cast<const uint8_t*>(request.sourceData),
				request.width,
				request.height,
				request.plan,
				request.addressMode);
		case TextureCompressionMode::None:
			return UploadUncompressedTexture(
				command_buffer,
				request.sourceData,
				request.sourceDataSize,
				request.width,
				request.height,
				request.plan,
				request.addressMode);
		}

		VANS_LOG_ERROR("Unsupported texture compression mode.");
		return false;
	}

	// ----- mip 链生成 -----

	void VansTexture::GenerateMipmapsForLayer(VansVKCommandBuffer& command_buffer, int width, int height, int mipLevels, int layerIndex)
	{
		GenerateMipmapsForLayer(
			command_buffer,
			width,
			height,
			mipLevels,
			layerIndex,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}

	void VansTexture::GenerateMipmapsForLayer(
		VansVKCommandBuffer& command_buffer,
		int width,
		int height,
		int mipLevels,
		int layerIndex,
		VkImageLayout targetMipInitialLayout,
		VkAccessFlags targetMipInitialAccessMask,
		VkPipelineStageFlags targetMipInitialStage)
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
			command_buffer.PipelineBarrier(
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				{}, {}, { barrier });

			// mip i：按调用方声明的初始布局转换为 TRANSFER_DST，作为本次 blit 目标。
			barrier.subresourceRange.baseMipLevel = i;
			barrier.oldLayout = targetMipInitialLayout;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.srcAccessMask = targetMipInitialAccessMask;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			command_buffer.PipelineBarrier(
				targetMipInitialStage,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				{}, {}, { barrier });

			int32_t dstW = std::max(1, mipW / 2);
			int32_t dstH = std::max(1, mipH / 2);

			VkImageBlit blit{};
			blit.srcSubresource = { m_Image.GetImageAspect(), (uint32_t)(i - 1), (uint32_t)layerIndex, 1 };
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { mipW, mipH, 1 };
			blit.dstSubresource = { m_Image.GetImageAspect(), (uint32_t)i, (uint32_t)layerIndex, 1 };
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { dstW, dstH, 1 };

			command_buffer.BlitImageRegions(
				m_Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				{ blit }, VK_FILTER_LINEAR);

			// mip i-1：blit 完成后转回 SHADER_READ_ONLY，可被后续帧采样
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			command_buffer.PipelineBarrier(
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				{}, {}, { barrier });

			mipW = dstW;
			mipH = dstH;
		}

		// 最后一级 mip 作为 blit 目标，从 TRANSFER_DST → SHADER_READ_ONLY
		barrier.subresourceRange.baseMipLevel = mipLevels - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		command_buffer.PipelineBarrier(
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			{}, {}, { barrier });
		m_Image.SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	void VansTexture::FinalizeUploadedLayer(VansVKCommandBuffer& command_buffer, int width, int height, int layerIndex)
	{
		const int mipLevels = static_cast<int>(m_Image.GetImageCreateInfo().mipLevels);
		if (mipLevels > 1)
		{
			GenerateMipmapsForLayer(command_buffer, width, height, mipLevels, layerIndex);
			return;
		}

		VkImageMemoryBarrier toShaderRead{};
		toShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toShaderRead.image = m_Image.GetImage();
		toShaderRead.subresourceRange = { m_Image.GetImageAspect(), 0, 1u, static_cast<uint32_t>(layerIndex), 1u };
		command_buffer.PipelineBarrier(
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			{}, {}, { toShaderRead });
		m_Image.SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	bool VansTexture::UploadCompressedTexture(VansVKCommandBuffer& command_buffer, const uint8_t* srcData, int width, int height, const TextureUploadPlan& uploadPlan, VkSamplerAddressMode addressMode)
	{
		VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
		VkDevice device = vkDevice->GetLogicDevice();
		VkQueue queue = vkDevice->GetGraphicsQueue();

		//创建GPU Image
		VkExtent3D extent = { (uint32_t)width, (uint32_t)height, 1 };
		m_Image.CreateVulkanImage(device, extent, uploadPlan.format, uploadPlan.mipLevels, 1,
			VK_IMAGE_TYPE_2D, uploadPlan.imageUsage,
			VK_SAMPLE_COUNT_1_BIT, false, true, true, addressMode);

		//CPU端逐级降采样 + 压缩 + 上传
		std::vector<uint8_t> mipRGBA(srcData, srcData + width * height * 4);
		int mipW = width, mipH = height;

		for (int m = 0; m < uploadPlan.mipLevels; ++m)
		{
			std::vector<uint8_t> compressed = CompressMipToBC(
				mipRGBA.data(),
				mipW,
				mipH,
				uploadPlan.compressedBlockWidth,
				uploadPlan.compressedBlockHeight,
				uploadPlan.compressedBytesPerBlock,
				uploadPlan.compressedHasAlpha);
			const size_t expectedCompressedSize = CalculateBlockCompressedDataSize(
				mipW,
				mipH,
				uploadPlan.compressedBlockWidth,
				uploadPlan.compressedBlockHeight,
				uploadPlan.compressedBytesPerBlock);
		if (compressed.size() != expectedCompressedSize)
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("Compressed mip size mismatch.");
			return false;
		}

			VkExtent3D mipExtent = { (uint32_t)mipW, (uint32_t)mipH, 1 };
			VkOffset3D offset = { 0, 0, 0 };
			if (!vkDevice->SetDeviceImageData(m_Image, command_buffer, compressed.data(),
				0, static_cast<int>(compressed.size()), offset, mipExtent, m, 0,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("Compressed texture upload failed at mip " << m << ".");
			return false;
		}

			//降采样生成下一级
			if (m + 1 < uploadPlan.mipLevels)
			{
				mipRGBA = Downsample2x2_RGBA8(mipRGBA.data(), mipW, mipH);
				mipW = std::max(1, mipW / 2);
				mipH = std::max(1, mipH / 2);
			}
		}

		//全部mip -> SHADER_READ_ONLY
		if (!command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("Failed to begin compressed texture final layout command buffer.");
			return false;
		}
		m_Image.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
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
		if (!SubmitAndWait(command_buffer, queue, device))
		{
			RecordTextureUploadFailure();
			return false;
		}
		return true;
	}

	// ----- 非压缩贴图上传 -----

	bool VansTexture::UploadUncompressedTexture(VansVKCommandBuffer& command_buffer, const void* data, size_t dataSize, int width, int height, const TextureUploadPlan& uploadPlan, VkSamplerAddressMode addressMode)
	{
		VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
		VkDevice device = vkDevice->GetLogicDevice();
		VkQueue queue = vkDevice->GetGraphicsQueue();

		//创建GPU Image
		VkExtent3D extent = { (uint32_t)width, (uint32_t)height, 1 };
		m_Image.CreateVulkanImage(device, extent, uploadPlan.format, uploadPlan.mipLevels, 1,
			VK_IMAGE_TYPE_2D, uploadPlan.imageUsage,
			VK_SAMPLE_COUNT_1_BIT, false, true, true, addressMode);

		//上传mip 0
		VkOffset3D offset = { 0, 0, 0 };
		if (!vkDevice->SetDeviceImageData(m_Image, command_buffer, const_cast<void*>(data), 0, static_cast<int>(dataSize), offset, extent, 0, 0,
			uploadPlan.mipGeneration == TextureMipGeneration::GPUBlit
				? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
				: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("Uncompressed texture upload failed.");
			return false;
		}

		//生成mip链或直接转换layout
		if (!command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("Failed to begin texture mip/final layout command buffer.");
			return false;
		}

		if (uploadPlan.mipGeneration == TextureMipGeneration::GPUBlit)
		{
			GenerateMipmaps(command_buffer, width, height, uploadPlan.mipLevels);
		}
		else
		{
			m_Image.SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}

		if (!SubmitAndWait(command_buffer, queue, device))
		{
			RecordTextureUploadFailure();
			return false;
		}
		return true;
	}

	// =====================================================================
	// 公开接口
	// =====================================================================

	void VansTexture::LoadTexture(VansVKCommandBuffer& command_buffer, std::string texture_path, bool isSRGB, bool useCompress, bool need_mip, TexturePrecision texture_precision, int import_channel, VkSamplerAddressMode addressMode)
	{
		TextureLoadDesc loadDesc{};
		loadDesc.path = std::move(texture_path);
		loadDesc.isSRGB = isSRGB;
		loadDesc.useCompress = useCompress;
		loadDesc.needMip = need_mip;
		loadDesc.precision = texture_precision;
		loadDesc.importChannel = import_channel;
		loadDesc.addressMode = addressMode;
		LoadTexture(command_buffer, loadDesc);
	}

	void VansTexture::LoadTexture(VansVKCommandBuffer& command_buffer, const TextureLoadDesc& loadDesc)
	{
		if (!loadDesc.cookedPath.empty() && LoadCookedTexture(command_buffer, loadDesc))
			return;

		// 1. 读取文件
		int width = 0, height = 0, num_components = 0, bytes_per_channel = 1;
		void* pixel_data = ReadTextureFile(
			loadDesc.path,
			loadDesc.precision,
			bytes_per_channel,
			width,
			height,
			num_components,
			loadDesc.importChannel);

		if (!pixel_data || width <= 0 || height <= 0 || num_components <= 0)
		{
			VANS_LOG_ERROR("Could not read image!");
			return;
		}

		if (loadDesc.importChannel != 0)
			num_components = loadDesc.importChannel;

		m_TextureWidth = width;
		m_TextureHeight = height;

		// 2. 生成数据驱动上传请求：格式、压缩路径、mip 策略和数据大小集中在这里决定。
		const TextureUploadRequest uploadRequest = BuildTextureUploadRequest(
			pixel_data,
			width, height,
			loadDesc.isSRGB, loadDesc.useCompress, loadDesc.needMip,
			loadDesc.precision,
			num_components, bytes_per_channel,
			loadDesc.addressMode);

		if (!UploadTexture(command_buffer, uploadRequest))
		{
			VANS_LOG_ERROR("Texture upload failed: " << loadDesc.path);
		}

		stbi_image_free(pixel_data);
	}

	bool VansTexture::LoadCookedTexture(VansVKCommandBuffer& command_buffer, const TextureLoadDesc& loadDesc)
	{
		Vans::VansCookedTextureData cooked;
		std::string error;
		if (!Vans::VansTextureCooker::LoadArtifact(loadDesc.cookedPath, cooked, error))
		{
			VANS_LOG_WARN("[VansTexture] Cooked texture unavailable, falling back to source: "
				<< error);
			return false;
		}
		if (cooked.format != Vans::VansCookedTextureFormat::BC3 ||
			cooked.width == 0 || cooked.height == 0 || cooked.mips.empty() || cooked.data.empty() ||
			cooked.data.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
		{
			VANS_LOG_WARN("[VansTexture] Unsupported cooked texture payload: " << loadDesc.cookedPath);
			return false;
		}

		VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
		if (!vkDevice)
			return false;
		VkDevice device = vkDevice->GetLogicDevice();
		const VkFormat format = loadDesc.isSRGB
			? VK_FORMAT_BC3_SRGB_BLOCK
			: VK_FORMAT_BC3_UNORM_BLOCK;
		const VkExtent3D extent = { cooked.width, cooked.height, 1 };
		if (!m_Image.CreateVulkanImage(
			device,
			extent,
			format,
			static_cast<uint32_t>(cooked.mips.size()),
			1,
			VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_SAMPLE_COUNT_1_BIT,
			false,
			true,
			true,
			loadDesc.addressMode))
		{
			VANS_LOG_WARN("[VansTexture] Failed to create image for cooked texture: " << loadDesc.cookedPath);
			return false;
		}

		std::vector<VkBufferImageCopy> regions;
		regions.reserve(cooked.mips.size());
		for (std::size_t mipIndex = 0; mipIndex < cooked.mips.size(); ++mipIndex)
		{
			const Vans::VansCookedTextureMip& mip = cooked.mips[mipIndex];
			VkBufferImageCopy region{};
			region.bufferOffset = static_cast<VkDeviceSize>(mip.offset);
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource = {
				VK_IMAGE_ASPECT_COLOR_BIT,
				static_cast<uint32_t>(mipIndex),
				0,
				1
			};
			region.imageOffset = { 0, 0, 0 };
			region.imageExtent = { mip.width, mip.height, 1 };
			regions.push_back(region);
		}

		if (!vkDevice->SetDeviceImageMipChainData(
			m_Image,
			command_buffer,
			cooked.data.data(),
			static_cast<int>(cooked.data.size()),
			regions))
		{
			m_Image.DestroyVulkanImage(device);
			VANS_LOG_WARN("[VansTexture] Cooked texture upload failed, falling back to source: "
				<< loadDesc.cookedPath);
			return false;
		}

		m_TextureWidth = static_cast<int>(cooked.width);
		m_TextureHeight = static_cast<int>(cooked.height);
		VANS_LOG("[VansTexture] Loaded cooked texture: " << loadDesc.cookedPath);
		return true;
	}

	bool VansTexture::TryPrepareCookedBatchUpload(
		VansVKDevice& vkDevice,
		const TextureLoadDesc& loadDesc,
		VansTextureMipChainUpload& upload,
		std::vector<std::uint8_t>& uploadStorage)
	{
		if (loadDesc.cookedPath.empty())
			return false;

		Vans::VansCookedTextureData cooked;
		std::string error;
		if (!Vans::VansTextureCooker::LoadArtifact(loadDesc.cookedPath, cooked, error))
			return false;
		if (cooked.format != Vans::VansCookedTextureFormat::BC3 ||
			cooked.width == 0 || cooked.height == 0 || cooked.mips.empty() || cooked.data.empty() ||
			cooked.data.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
		{
			return false;
		}

		VkDevice device = vkDevice.GetLogicDevice();
		const VkFormat format = loadDesc.isSRGB
			? VK_FORMAT_BC3_SRGB_BLOCK
			: VK_FORMAT_BC3_UNORM_BLOCK;
		const VkExtent3D extent = { cooked.width, cooked.height, 1 };
		if (!m_Image.CreateVulkanImage(
			device,
			extent,
			format,
			static_cast<uint32_t>(cooked.mips.size()),
			1,
			VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_SAMPLE_COUNT_1_BIT,
			false,
			true,
			true,
			loadDesc.addressMode))
		{
			return false;
		}

		std::vector<VkBufferImageCopy> regions;
		regions.reserve(cooked.mips.size());
		for (std::size_t mipIndex = 0; mipIndex < cooked.mips.size(); ++mipIndex)
		{
			const Vans::VansCookedTextureMip& mip = cooked.mips[mipIndex];
			VkBufferImageCopy region{};
			region.bufferOffset = static_cast<VkDeviceSize>(mip.offset);
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource = {
				VK_IMAGE_ASPECT_COLOR_BIT,
				static_cast<uint32_t>(mipIndex),
				0,
				1
			};
			region.imageOffset = { 0, 0, 0 };
			region.imageExtent = { mip.width, mip.height, 1 };
			regions.push_back(region);
		}

		uploadStorage = std::move(cooked.data);
		upload.destImage = &m_Image;
		upload.data = uploadStorage.data();
		upload.dataSize = static_cast<int>(uploadStorage.size());
		upload.regions = std::move(regions);
		upload.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		m_TextureWidth = static_cast<int>(cooked.width);
		m_TextureHeight = static_cast<int>(cooked.height);
		return true;
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
			if (!vkDevice->SetDeviceImageData(m_Image, command_buffer, stbi_data.get(), 0, dataSize, offset, extent, 0, face))
			{
				RecordTextureUploadFailure();
				VANS_LOG_ERROR("Cube texture upload failed at face " << face << ": " << path);
				return;
			}

			if (face == 0)
			{
				m_TextureWidth = width;
				m_TextureHeight = height;
			}
		}

		//切换layout到SHADER_READ_ONLY_OPTIMAL
		if (!command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("Cube texture final layout command buffer begin failed: " << texture_parent_path);
			return;
		}
		m_Image.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
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
		if (!SubmitAndWait(command_buffer, queue, device))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("Cube texture final layout submit failed: " << texture_parent_path);
		}
	}

	void VansTexture::LoadFromMemory(VansVKCommandBuffer& command_buffer,
		const void* data, size_t dataSize,
		int width, int height, VkFormat format,
		VkSamplerAddressMode addressMode)
	{
		m_TextureWidth = width;
		m_TextureHeight = height;
		m_TextureSlice = 1;
		TextureUploadRequest uploadRequest{};
		uploadRequest.sourceData = data;
		uploadRequest.sourceDataSize = dataSize;
		uploadRequest.width = width;
		uploadRequest.height = height;
		uploadRequest.plan.format = format;
		uploadRequest.plan.mipLevels = 1;
		uploadRequest.plan.imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		uploadRequest.addressMode = addressMode;
		if (!UploadTexture(command_buffer, uploadRequest))
		{
			VANS_LOG_ERROR("Texture memory upload failed.");
		}
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
		int mipLevels = CalculateMipLevels(width, height, generateMip);

		// 创建 2D 贴图数组：VK_IMAGE_TYPE_2D + layer_num > 1 → VK_IMAGE_VIEW_TYPE_2D_ARRAY
		// TRANSFER_SRC_BIT：BlitImageRegions 将已上传的 mip 0 逐级下采样时需要读源
		VkExtent3D extent = { (uint32_t)width, (uint32_t)height, 1 };
		m_Image.CreateVulkanImage(device, extent, format, mipLevels, (uint32_t)layerCount,
			VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			VK_SAMPLE_COUNT_1_BIT, false, true, true, addressMode);

		// 将所有层从 UNDEFINED → SHADER_READ_ONLY_OPTIMAL
		if (!command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("Texture array initial layout command buffer begin failed.");
			return;
		}
		m_Image.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
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
		if (!SubmitAndWait(command_buffer, queue, device))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("Texture array initial layout submit failed.");
		}
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

		RGBA8LayerUploadView upload = PrepareRGBA8LayerUpload(
			static_cast<const uint8_t*>(pixelData),
			fileW, fileH,
			m_TextureWidth, m_TextureHeight);

		size_t dataSize = size_t(upload.width) * upload.height * 4;
		VkExtent3D extent = { (uint32_t)upload.width, (uint32_t)upload.height, 1 };
		VkOffset3D zeroOffset = { 0, 0, 0 };
		if (!vkDevice->SetDeviceImageData(m_Image, command_buffer,
			const_cast<uint8_t*>(upload.pixels), 0, (int)dataSize, zeroOffset, extent, 0, layerIndex,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("LoadTextureLayer: GPU upload failed for layer " << layerIndex << ": " << texturePath);
			stbi_image_free(pixelData);
			return false;
		}

		// SetDeviceImageData 将 mip 0 of layerIndex 置于 TRANSFER_DST_OPTIMAL。
		// 若分配了多级 mip（generateMip=true），则用 BlitImageRegions 逐级下采样生成完整 mip 链；
		// 单 mip 时直接转换回 SHADER_READ_ONLY_OPTIMAL 即可。
		{
			VkQueue queue = vkDevice->GetGraphicsQueue();
			VkDevice device = vkDevice->GetLogicDevice();

			if (!command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
			{
				RecordTextureUploadFailure();
				VANS_LOG_ERROR("LoadTextureLayer: mip/final layout command buffer begin failed for layer " << layerIndex << ": " << texturePath);
				stbi_image_free(pixelData);
				return false;
			}
			FinalizeUploadedLayer(command_buffer, upload.width, upload.height, layerIndex);
			if (!SubmitAndWait(command_buffer, queue, device))
			{
				RecordTextureUploadFailure();
				VANS_LOG_ERROR("LoadTextureLayer: mip/final layout submit failed for layer " << layerIndex << ": " << texturePath);
				stbi_image_free(pixelData);
				return false;
			}
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

		RGBA8LayerUploadView upload{};
		{
			VANS_PROFILE_SCOPE("RectLightVideo::ResizeCPU", Vans::ProfileCategory::Video);
			upload = PrepareRGBA8LayerUpload(pixels, srcW, srcH, m_TextureWidth, m_TextureHeight);
		}

		size_t dataSize = size_t(upload.width) * upload.height * 4;
		VkExtent3D extent = { (uint32_t)upload.width, (uint32_t)upload.height, 1 };
		VkOffset3D zeroOffset = { 0, 0, 0 };
		// SetDeviceImageData 上传 mip 0，并通过 fence 同步等待
		if (!vkDevice->SetDeviceImageData(m_Image, command_buffer,
			const_cast<uint8_t*>(upload.pixels), 0, (int)dataSize, zeroOffset, extent, 0, layerIndex,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("[VansTexture] UpdateArrayLayerFromPixels: GPU upload failed layer=" << layerIndex);
			return false;
		}

		// 重新生成该层的完整 mip 链（与 LoadTextureLayer 逻辑完全一致）
		{
			VkQueue queue = vkDevice->GetGraphicsQueue();
			VkDevice device = vkDevice->GetLogicDevice();

			if (!command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
			{
				RecordTextureUploadFailure();
				VANS_LOG_ERROR("[VansTexture] UpdateArrayLayerFromPixels: mip/final layout command buffer begin failed layer=" << layerIndex);
				return false;
			}
			FinalizeUploadedLayer(command_buffer, upload.width, upload.height, layerIndex);
			if (!SubmitAndWait(command_buffer, queue, device))
			{
				RecordTextureUploadFailure();
				VANS_LOG_ERROR("[VansTexture] UpdateArrayLayerFromPixels: mip/final layout submit failed layer=" << layerIndex);
				return false;
			}
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
		RGBA8LayerUploadView upload = PrepareRGBA8LayerUpload(
			pixels, srcW, srcH,
			m_TextureWidth, m_TextureHeight);

		size_t dataSize = size_t(upload.width) * upload.height * 4;
		VkExtent3D extent = { (uint32_t)upload.width, (uint32_t)upload.height, 1 };
		VkOffset3D zeroOffset = { 0, 0, 0 };
		{
			VANS_PROFILE_SCOPE("RectLightVideo::UploadArrayLayer", Vans::ProfileCategory::Video);
			if (!vkDevice->RecordDeviceImageData(m_Image, command_buffer,
				upload.pixels, static_cast<int>(dataSize), zeroOffset, extent,
				0, layerIndex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
			{
				RecordTextureUploadFailure();
				return false;
			}
		}

		FinalizeUploadedLayer(command_buffer, upload.width, upload.height, layerIndex);

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

		FinalizeUploadedLayer(command_buffer, m_TextureWidth, m_TextureHeight, layerIndex);
		return true;
	}

	void VansTexture::InitTextureWithoutData(VansVKCommandBuffer& command_buffer, int width, int height, int slice, VkFormat format, bool isCube, bool generateMip, bool enableRandomWrite, VkSamplerAddressMode addressMode)
	{
		m_TextureWidth = width;
		m_TextureHeight = height;
		m_TextureSlice = slice;

		VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
		VkDevice device = vkDevice->GetLogicDevice();
		VkQueue queue = vkDevice->GetGraphicsQueue();

		bool is3D = slice > 1;
		int mipLevels = CalculateMipLevels(width, height, generateMip);

		VkExtent3D extent = { (uint32_t)width, (uint32_t)height, (uint32_t)slice };
		m_Image.CreateVulkanImage(device, extent, format, mipLevels, 1,
			is3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			VK_SAMPLE_COUNT_1_BIT, isCube, true, true, addressMode);

		VkImageLayout targetLayout = enableRandomWrite ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		if (!command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("InitTextureWithoutData: initial layout command buffer begin failed.");
			return;
		}
		m_Image.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
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
		if (!SubmitAndWait(command_buffer, queue, device))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("InitTextureWithoutData: initial layout submit failed.");
		}
	}

	static uint16_t FloatToHalf(float value)
	{
		uint32_t bits = 0; std::memcpy(&bits, &value, sizeof(bits));
		const uint32_t sign = (bits >> 16) & 0x8000u;
		int32_t exponent = int32_t((bits >> 23) & 0xffu) - 127 + 15;
		uint32_t mantissa = bits & 0x7fffffu;
		if (exponent <= 0)
		{
			if (exponent < -10) return (uint16_t)sign;
			mantissa = (mantissa | 0x800000u) >> (1 - exponent);
			return (uint16_t)(sign | ((mantissa + 0x1000u) >> 13));
		}
		if (exponent >= 31) return (uint16_t)(sign | 0x7c00u);
		return (uint16_t)(sign | (uint32_t(exponent) << 10) | ((mantissa + 0x1000u) >> 13));
	}

	bool VansTexture::LoadHDRTextureLayer(VansVKCommandBuffer& command_buffer,
		const std::string& texturePath, int layerIndex)
	{
		if (layerIndex < 0 || layerIndex >= m_TextureSlice ||
			m_Image.GetImageCreateInfo().format != VK_FORMAT_R16G16B16A16_SFLOAT) return false;
		int fileW = 0, fileH = 0, components = 0, bytes = 0;
		float* pixels = static_cast<float*>(ReadTextureFile(texturePath, HIGH_PRES_32, bytes, fileW, fileH, components, 4));
		if (!pixels || fileW <= 0 || fileH <= 0) return false;
		std::vector<uint16_t> upload(size_t(m_TextureWidth) * m_TextureHeight * 4u);
		const float scaleX = float(fileW) / float(m_TextureWidth);
		const float scaleY = float(fileH) / float(m_TextureHeight);
		for (int y = 0; y < m_TextureHeight; ++y)
		for (int x = 0; x < m_TextureWidth; ++x)
		{
			const int sx = std::min(int(x * scaleX), fileW - 1);
			const int sy = std::min(int(y * scaleY), fileH - 1);
			for (int c = 0; c < 4; ++c)
				upload[(size_t(y) * m_TextureWidth + x) * 4u + c] = FloatToHalf(pixels[(size_t(sy) * fileW + sx) * 4u + c]);
		}
		stbi_image_free(pixels);
		VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
		const VkExtent3D extent = { (uint32_t)m_TextureWidth, (uint32_t)m_TextureHeight, 1u };
		const VkOffset3D offset = { 0, 0, 0 };
		if (!vkDevice->SetDeviceImageData(m_Image, command_buffer, upload.data(), 0,
			(int)(upload.size() * sizeof(uint16_t)), offset, extent, 0, layerIndex,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("LoadHDRTextureLayer: GPU upload failed for layer " << layerIndex << ": " << texturePath);
			return false;
		}
		VkQueue queue = vkDevice->GetGraphicsQueue(); VkDevice device = vkDevice->GetLogicDevice();
		if (!command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("LoadHDRTextureLayer: final layout command buffer begin failed for layer " << layerIndex << ": " << texturePath);
			return false;
		}
		FinalizeUploadedLayer(command_buffer, m_TextureWidth, m_TextureHeight, layerIndex);
		if (!SubmitAndWait(command_buffer, queue, device))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("LoadHDRTextureLayer: final layout submit failed for layer " << layerIndex << ": " << texturePath);
			return false;
		}
		return true;
	}

	bool VansTexture::UpdateHDRArrayLayerFromPixels(VansVKCommandBuffer& command_buffer,
		const float* rgbaPixels, int srcWidth, int srcHeight, int layerIndex)
	{
		if (!rgbaPixels || srcWidth <= 0 || srcHeight <= 0 || layerIndex < 0 || layerIndex >= m_TextureSlice ||
			m_Image.GetImageCreateInfo().format != VK_FORMAT_R16G16B16A16_SFLOAT) return false;
		std::vector<uint16_t> upload(size_t(m_TextureWidth) * m_TextureHeight * 4u);
		const float scaleX = float(srcWidth) / float(m_TextureWidth);
		const float scaleY = float(srcHeight) / float(m_TextureHeight);
		for (int y = 0; y < m_TextureHeight; ++y) for (int x = 0; x < m_TextureWidth; ++x)
		{
			const int sx = std::min(int(x * scaleX), srcWidth - 1);
			const int sy = std::min(int(y * scaleY), srcHeight - 1);
			for (int c = 0; c < 4; ++c) upload[(size_t(y) * m_TextureWidth + x) * 4u + c] = FloatToHalf(rgbaPixels[(size_t(sy) * srcWidth + sx) * 4u + c]);
		}
		VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
		const VkExtent3D extent = { (uint32_t)m_TextureWidth, (uint32_t)m_TextureHeight, 1u };
		const VkOffset3D offset = { 0, 0, 0 };
		if (!vkDevice->SetDeviceImageData(m_Image, command_buffer, upload.data(), 0, (int)(upload.size() * sizeof(uint16_t)), offset, extent, 0, layerIndex,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("UpdateHDRArrayLayerFromPixels: GPU upload failed for layer " << layerIndex);
			return false;
		}
		VkQueue queue = vkDevice->GetGraphicsQueue(); VkDevice device = vkDevice->GetLogicDevice();
		if (!command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("UpdateHDRArrayLayerFromPixels: final layout command buffer begin failed for layer " << layerIndex);
			return false;
		}
		FinalizeUploadedLayer(command_buffer, m_TextureWidth, m_TextureHeight, layerIndex);
		if (!SubmitAndWait(command_buffer, queue, device))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("UpdateHDRArrayLayerFromPixels: final layout submit failed for layer " << layerIndex);
			return false;
		}
		return true;
	}

	void VansTexture::InitCubeTextureArray(VansVKCommandBuffer& command_buffer,
		int width, int height, int cubeCount, int numComponents,
		bool generateMip, TexturePrecision texturePrecision, VkSamplerAddressMode addressMode)
	{
		m_TextureType = TEXTURE_CUBE;
		m_TextureWidth = width;
		m_TextureHeight = height;
		m_TextureSlice = std::max(cubeCount, 1) * 6;

		VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
		VkDevice device = vkDevice->GetLogicDevice();
		VkQueue queue = vkDevice->GetGraphicsQueue();
		const VkFormat format = ChooseFormat(numComponents, texturePrecision);
		const int mipLevels = CalculateMipLevels(width, height, generateMip);
		const VkExtent3D extent = { (uint32_t)width, (uint32_t)height, 1u };

		m_Image.CreateVulkanImage(device, extent, format, mipLevels,
			(uint32_t)std::max(cubeCount, 1), VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			VK_SAMPLE_COUNT_1_BIT, true, true, true, addressMode);

		if (!command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("Cube texture array initial layout command buffer begin failed.");
			return;
		}
		m_Image.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			{ m_Image.GetImage(), VK_ACCESS_NONE, VK_ACCESS_NONE,
			  m_Image.GetImageLayout(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			  VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			  m_Image.GetImageAspect() });
		if (!SubmitAndWait(command_buffer, queue, device))
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("Cube texture array initial layout submit failed.");
		}
	}

	bool VansTexture::LoadTexture3DFromSlices(VansVKCommandBuffer& command_buffer,
		const std::string& slicePathFormat, int sliceCount,
		int importChannel, VkSamplerAddressMode addressMode)
	{
		if (sliceCount <= 0 || importChannel < 1 || importChannel > 4)
		{
			VANS_LOG_ERROR("LoadTexture3DFromSlices: 参数无效, sliceCount=" << sliceCount << ", importChannel=" << importChannel);
			return false;
		}

		VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
		if (vkDevice == nullptr)
		{
			VANS_LOG_ERROR("LoadTexture3DFromSlices: Vulkan 设备无效");
			return false;
		}

		auto makeSlicePath = [&slicePathFormat](int sliceIndex) -> std::string
		{
			char pathBuffer[1024] = {};
			std::snprintf(pathBuffer, sizeof(pathBuffer), slicePathFormat.c_str(), sliceIndex);
			return std::string(pathBuffer);
		};

		int width = 0;
		int height = 0;
		int numComponents = 0;
		std::string firstPath = makeSlicePath(0);
		stbi_uc* firstPixels = stbi_load(firstPath.c_str(), &width, &height, &numComponents, importChannel);
		if (firstPixels == nullptr || width <= 0 || height <= 0)
		{
			VANS_LOG_ERROR("LoadTexture3DFromSlices: 无法读取首张切片: " << firstPath);
			if (firstPixels != nullptr) stbi_image_free(firstPixels);
			return false;
		}

		VANS_LOG("Load 3D Texture Slices: " << slicePathFormat << ", size=" << width << "x" << height << "x" << sliceCount);
		InitTextureWithoutData(command_buffer, width, height, sliceCount,
			ChooseFormat(importChannel, LOW_PRES_8), false, false, false, addressMode);

		auto uploadSlice = [&](const stbi_uc* pixels, int sliceIndex) -> bool
		{
			VkOffset3D imageOffset = { 0, 0, sliceIndex };
			VkExtent3D imageExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1u };
			const int dataSize = width * height * importChannel;
			return vkDevice->SetDeviceImageData(m_Image, command_buffer,
				(void*)pixels, 0, dataSize, imageOffset, imageExtent, 0, 0);
		};

		bool success = uploadSlice(firstPixels, 0);
		stbi_image_free(firstPixels);

		for (int sliceIndex = 1; success && sliceIndex < sliceCount; ++sliceIndex)
		{
			int sliceWidth = 0;
			int sliceHeight = 0;
			int sliceComponents = 0;
			std::string slicePath = makeSlicePath(sliceIndex);
			stbi_uc* pixels = stbi_load(slicePath.c_str(), &sliceWidth, &sliceHeight, &sliceComponents, importChannel);
			if (pixels == nullptr)
			{
				VANS_LOG_ERROR("LoadTexture3DFromSlices: 无法读取切片: " << slicePath);
				success = false;
				break;
			}

			if (sliceWidth != width || sliceHeight != height)
			{
				VANS_LOG_ERROR("LoadTexture3DFromSlices: 切片尺寸不一致: " << slicePath
					<< ", expected=" << width << "x" << height
					<< ", actual=" << sliceWidth << "x" << sliceHeight);
				stbi_image_free(pixels);
				success = false;
				break;
			}

			success = uploadSlice(pixels, sliceIndex);
			stbi_image_free(pixels);
		}

		if (!success)
		{
			RecordTextureUploadFailure();
			VANS_LOG_ERROR("LoadTexture3DFromSlices: 3D 纹理切片上传失败: " << slicePathFormat);
		}
		return success;
	}
}
