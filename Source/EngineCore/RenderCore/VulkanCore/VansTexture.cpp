#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansTexture.h"
#include "VansVKDevice.h"
#include "VansVKCommandBuffer.h"
#include <iostream>
#include <vector>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#include <../../STBImge/stb_image.h>

// Add stb_dxt implementation
#define STB_DXT_IMPLEMENTATION
#include <../../STBImge/stb_dxt.h>

namespace VansGraphics
{

	// Replace with your compressor implementation (stb_dxt/squish/ISPC/BasisU)
	// outDst must receive bytesPerBlock bytes for a single 4x4 RGBA block (row-major, RGBA8 per texel).
	static void compress_block(uint8_t* outDst, const uint8_t rgba4x4[16 * 4], bool hasAlpha)
	{
		// stb_compress_dxt_block(output, input_rgba16, alpha, mode)
		// alpha: 0 -> DXT1 (8 bytes), 1 -> DXT5 (16 bytes)
		// mode: 0 = default/fast, 1 = high quality (if supported)
		int alpha = hasAlpha ? 1 : 0;
		int mode = 0;
		stb_compress_dxt_block(outDst, rgba4x4, alpha, mode);
	}

	// 2x2 box downsample for RGBA8
	static std::vector<uint8_t> Downsample2x2_RGBA8(const uint8_t* src, int w, int h)
	{
		int outW = std::max(1, w / 2);
		int outH = std::max(1, h / 2);
		std::vector<uint8_t> dst(size_t(outW) * size_t(outH) * 4);

		auto sample = [&](int x, int y)->const uint8_t* {
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

				uint32_t r = p00[0] + p10[0] + p01[0] + p11[0];
				uint32_t g = p00[1] + p10[1] + p01[1] + p11[1];
				uint32_t b = p00[2] + p10[2] + p01[2] + p11[2];
				uint32_t a = p00[3] + p10[3] + p01[3] + p11[3];

				uint8_t* d = &dst[(y * outW + x) * 4];
				d[0] = uint8_t(r >> 2);
				d[1] = uint8_t(g >> 2);
				d[2] = uint8_t(b >> 2);
				d[3] = uint8_t(a >> 2);
			}
		}
		return dst;
	}

	VansTexture::~VansTexture()
	{
		m_Image.DestroyVulkanImage(*(VkDevice*)m_GraphicsDevice->GetNativeGraphicsDevice());
	}

	void VansTexture::LoadTexture(VansVKCommandBuffer& command_buffer, std::string texture_path, bool isSRGB, bool useCompress, bool need_mip, TexturePrecision texture_precision, int import_channel)
	{
		std::cout << "Load Texture : " << texture_path << std::endl;
        int width = 0;
        int height = 0;
        int num_components = 0;
        void* pixel_data = nullptr;
        int bytes_per_channel = 1;

        // 1. Load image data based on precision
        if (texture_precision == VansGraphics::HIGH_PRES_32)
        {
            pixel_data = stbi_loadf(texture_path.c_str(), &width, &height, &num_components, import_channel);
            bytes_per_channel = 4; // float
        }
        else if (texture_precision == VansGraphics::MID_PRES_16)
        {
            pixel_data = stbi_load_16(texture_path.c_str(), &width, &height, &num_components, import_channel);
            bytes_per_channel = 2; // uint16
        }
        else // LOW_PRES_8
        {
            pixel_data = stbi_load(texture_path.c_str(), &width, &height, &num_components, import_channel);
            bytes_per_channel = 1; // uint8
        }

        if ((!pixel_data) ||
            (0 >= width) ||
            (0 >= height) ||
            (0 >= num_components))
        {
            std::cout << "Could not read image!" << std::endl;
            return;
        }

        // If import_channel is set (not 0), stbi forces the output channels to that value.
        // Otherwise, num_components holds the file's original channel count.
        if (import_channel != 0)
        {
            num_components = import_channel;
        }

        // ---------------- compress into block layout (tight packed) ----------------
        VansVKDevice* vkDevicePtr = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
        VkDevice nativeDevice = vkDevicePtr->GetLogicDevice();
        VkQueue graphicsQueue = vkDevicePtr->GetGraphicsQueue();
        VkPhysicalDevice phys = vkDevicePtr->GetPhysicalDevice();

        // Compression logic currently only supports 8-bit RGBA (4 channels)
        // If you want to support compression, ensure input is compatible or convert it.
        // For now, we disable compression if precision is not 8-bit or channels != 4 for simplicity, 
        // or you can expand the compression logic later.
        if (useCompress && texture_precision == VansGraphics::LOW_PRES_8 && num_components == 4)
        {
            const uint8_t* srcBase = static_cast<uint8_t*>(pixel_data);
            bool hasAlpha = true;
            // Choose compressed format (BC1/BC3)
            VkFormat chosenFormat = hasAlpha ? (isSRGB ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK)
                                            : (isSRGB ? VK_FORMAT_BC1_RGB_SRGB_BLOCK : VK_FORMAT_BC1_RGB_UNORM_BLOCK);
            int bytesPerBlock = hasAlpha ? 16 : 8;

            // compute full mip chain count
            int mipLevels = 1 + (int)std::floor(std::log2(std::max(width, height)));

            m_TextureWidth = width;
            m_TextureHeight = height;

            // Create compressed image with all mips
            VkExtent3D extent0 = { (uint32_t)width, (uint32_t)height, 1 };
            m_Image.CreateVulkanImage(
                nativeDevice,
                extent0,
                chosenFormat,
                mipLevels,
                1,
                VK_IMAGE_TYPE_2D,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_SAMPLE_COUNT_1_BIT,
                false,
                true,
                true
            );

            // Build uncompressed mip chain on CPU, then compress each level and upload
            std::vector<uint8_t> mipRGBA;                 // current level RGBA
            mipRGBA.assign(srcBase, srcBase + width * height * 4);

            int mipW = width, mipH = height;

            for (int m = 0; m < mipLevels; ++m)
            {
                int blocksX = (mipW + 3) / 4;
                int blocksY = (mipH + 3) / 4;
                size_t mipCompressedSize = size_t(blocksX) * size_t(blocksY) * size_t(bytesPerBlock);
                std::vector<uint8_t> mipCompressed(mipCompressedSize);

                // compress mipRGBA into BC blocks
                uint8_t blockRGBA[16 * 4];
                uint8_t* dstPtr = mipCompressed.data();

                auto fetch = [&](int x, int y)->const uint8_t* {
                    x = std::min(std::max(x, 0), mipW - 1);
                    y = std::min(std::max(y, 0), mipH - 1);
                    return &mipRGBA[(y * mipW + x) * 4];
                };

                for (int by = 0; by < blocksY; ++by)
                {
                    for (int bx = 0; bx < blocksX; ++bx)
                    {
                        for (int y = 0; y < 4; ++y)
                        {
                            for (int x = 0; x < 4; ++x)
                            {
                                int sx = bx * 4 + x;
                                int sy = by * 4 + y;
                                const uint8_t* s = fetch(sx, sy);
                                int idx = (y * 4 + x) * 4;
                                blockRGBA[idx + 0] = s[0];
                                blockRGBA[idx + 1] = s[1];
                                blockRGBA[idx + 2] = s[2];
                                blockRGBA[idx + 3] = s[3];
                            }
                        }
                        compress_block(dstPtr, blockRGBA, hasAlpha);
                        dstPtr += bytesPerBlock;
                    }
                }

                // Upload this mip level (tightly packed)
                VkExtent3D mipExtent = { (uint32_t)mipW, (uint32_t)mipH, 1 };
                VkOffset3D image_offset = { 0, 0, 0 };
                vkDevicePtr->SetDeviceImageData(m_Image, command_buffer, mipCompressed.data(),
                                                0, static_cast<int>(mipCompressed.size()),
                                                image_offset, mipExtent, m, 0);

                // prepare next mip level (downsample)
                if (m + 1 < mipLevels)
                {
                    std::vector<uint8_t> nextMip = Downsample2x2_RGBA8(mipRGBA.data(), mipW, mipH);
                    mipRGBA.swap(nextMip);
                    mipW = std::max(1, mipW / 2);
                    mipH = std::max(1, mipH / 2);
                }
            }

            // Transition all mips to SHADER_READ_ONLY
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
            command_buffer.EndCommandBufferRecord();
            VansVKCommandBuffer::SubmitCommands(graphicsQueue, nativeDevice, { command_buffer.GetVKCommandBuffer() }, {}, {}, command_buffer.m_CommandBufferFinishSubmitFence);
            command_buffer.ResetCommandBuffer(false);
            
            stbi_image_free(pixel_data);
            return;
        }

        // Non-compressed path (supports 8/16/32 bit and 1-4 channels)
        
        // Calculate total data size in bytes
        size_t data_size = (size_t)width * (size_t)height * (size_t)num_components * (size_t)bytes_per_channel;
        m_ImageData.resize(data_size);
        std::memcpy(m_ImageData.data(), pixel_data, data_size);
        
        stbi_image_free(pixel_data);

        VkExtent3D extent = { (uint32_t)width, (uint32_t)height, 1 };
        
        VkFormat format = VK_FORMAT_UNDEFINED;
        switch (texture_precision)
        {
        case VansGraphics::LOW_PRES_8:
            format = CheckTextureFormat(num_components, false, isSRGB);
            break;
        case VansGraphics::MID_PRES_16:
            format = CheckTextureMidPrecisionFormat(num_components);
            break;
        case VansGraphics::HIGH_PRES_32:
            format = CheckTextureHighPrecisionFormat(num_components);
            break;
        default:
            break;
        }
        
        // compute mip levels
        int mipNum = need_mip ? 1 + static_cast<int>(std::floor(std::log2(std::max(width, height)))) : 1;
        m_TextureWidth = width;
        m_TextureHeight = height;
        
        m_Image.CreateVulkanImage(
            nativeDevice,
            extent,
            format,
            mipNum,
            1,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_SAMPLE_COUNT_1_BIT,
            false,
            true,
            true
        );

        VkOffset3D image_offset = { 0, 0, 0 };
        vkDevicePtr->SetDeviceImageData(m_Image, command_buffer, m_ImageData.data(), 0, data_size, image_offset, extent, 0, 0);
		//切换layout到：VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		//

		command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		VkCommandBuffer cmd = command_buffer.GetVKCommandBuffer();

		if (mipNum > 1)
		{
			int32_t texWidth = width;
			int32_t texHeight = height;

			// transition base level (mip 0) to TRANSFER_SRC if needed: currently it's after upload in TRANSFER_DST, set to TRANSFER_SRC before blit
			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.image = m_Image.GetImage();
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.subresourceRange.aspectMask = m_Image.GetImageAspect();
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;

			for (int i = 1; i < mipNum; ++i)
			{
				// transition mip i-1 -> TRANSFER_SRC
				barrier.subresourceRange.baseMipLevel = i - 1;
				barrier.subresourceRange.levelCount = 1;
				barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				vkCmdPipelineBarrier(cmd,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
					0, nullptr, 0, nullptr, 1, &barrier);

				// transition mip i -> TRANSFER_DST
				barrier.subresourceRange.baseMipLevel = i;
				barrier.subresourceRange.levelCount = 1;
				barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				barrier.srcAccessMask = 0;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				vkCmdPipelineBarrier(cmd,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
					0, nullptr, 0, nullptr, 1, &barrier);

				VkImageBlit blit{};
				blit.srcSubresource.aspectMask = m_Image.GetImageAspect();
				blit.srcSubresource.mipLevel = i - 1;
				blit.srcSubresource.baseArrayLayer = 0;
				blit.srcSubresource.layerCount = 1;
				blit.srcOffsets[0] = { 0, 0, 0 };
				blit.srcOffsets[1] = { texWidth, texHeight, 1 };

				int32_t dstWidth = std::max(1, texWidth / 2);
				int32_t dstHeight = std::max(1, texHeight / 2);

				blit.dstSubresource.aspectMask = m_Image.GetImageAspect();
				blit.dstSubresource.mipLevel = i;
				blit.dstSubresource.baseArrayLayer = 0;
				blit.dstSubresource.layerCount = 1;
				blit.dstOffsets[0] = { 0, 0, 0 };
				blit.dstOffsets[1] = { dstWidth, dstHeight, 1 };

				vkCmdBlitImage(cmd,
					m_Image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					m_Image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1, &blit, VK_FILTER_LINEAR);

				texWidth = dstWidth;
				texHeight = dstHeight;
			}

			// finally transition all mips to SHADER_READ_ONLY_OPTIMAL
			VkImageMemoryBarrier finalBarrier{};
			finalBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			finalBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			finalBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			finalBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			finalBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			finalBarrier.image = m_Image.GetImage();
			finalBarrier.subresourceRange.aspectMask = m_Image.GetImageAspect();
			finalBarrier.subresourceRange.baseMipLevel = 0;
			finalBarrier.subresourceRange.levelCount = mipNum;
			finalBarrier.subresourceRange.baseArrayLayer = 0;
			finalBarrier.subresourceRange.layerCount = 1;

			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
				0, nullptr, 0, nullptr, 1, &finalBarrier);
		}
		else
		{
			// fallback: directly transition full image to SHADER_READ_ONLY_OPTIMAL (no mipgen)
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

		command_buffer.EndCommandBufferRecord();
		VansVKCommandBuffer::SubmitCommands(graphicsQueue, nativeDevice, { command_buffer.GetVKCommandBuffer() }, {}, {}, command_buffer.m_CommandBufferFinishSubmitFence);
		command_buffer.ResetCommandBuffer(false);

		m_ImageData.clear();
	}

	VkFormat VansTexture::CheckTextureFormat(int channel, bool isHdr, bool isSRGB)
	{
		VkFormat format = VK_FORMAT_UNDEFINED;
		switch (channel)
		{
		case 1:
			format = isSRGB ? VK_FORMAT_R8_SRGB : VK_FORMAT_R8_UNORM;
			break;
		case 2:
			format = isSRGB ? VK_FORMAT_R8G8_SRGB : VK_FORMAT_R8G8_UNORM;
			break;
		case 3:
			format = isSRGB ? VK_FORMAT_R8G8B8_SRGB : VK_FORMAT_R8G8B8_UNORM;
			break;
		case 4:
			format = isSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
			break;
		default:
			break;
		}

		return format;
	}

	VkFormat VansTexture::CheckTextureHighPrecisionFormat(int channel)
	{
		VkFormat format = VK_FORMAT_UNDEFINED;
		switch (channel)
		{
		case 1:
			format = VK_FORMAT_R32_SFLOAT;
			break;
		case 2:
			format = VK_FORMAT_R32G32_SFLOAT;
			break;
		case 3:
			format = VK_FORMAT_R32G32B32_SFLOAT;
			break;
		case 4:
			format = VK_FORMAT_R32G32B32A32_SFLOAT;
			break;
		default:
			break;
		}

		return format;
	}

	VkFormat VansTexture::CheckTextureMidPrecisionFormat(int channel)
	{
		VkFormat format = VK_FORMAT_UNDEFINED;
		switch (channel)
		{
		case 1:
			format = VK_FORMAT_R16_UNORM;
			break;
		case 2:
			format = VK_FORMAT_R16G16_UNORM;
			break;
		case 3:
			format = VK_FORMAT_R16G16B16_UNORM;
			break;
		case 4:
			format = VK_FORMAT_R16G16B16A16_UNORM;
			break;
		default:
			break;
		}

		return format;
	}

	void VansTexture::LoadCubeTexture(VansVKCommandBuffer& command_buffer, std::string texture_parent_path, bool isSRGB)
	{
		int width = 0;
		int height = 0;
		int data_size = 0;
		bool isImageCreated = false;

		VansVKDevice* vkDevicePtr = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
		VkDevice nativeDevice = vkDevicePtr->GetLogicDevice();
		VkQueue graphicsQueue = vkDevicePtr->GetGraphicsQueue();

		std::vector<std::string> texture_path;
		texture_path.push_back(texture_parent_path + "/Right.hdr");
		texture_path.push_back(texture_parent_path + "/Left.hdr");
		texture_path.push_back(texture_parent_path + "/Top.hdr");
		texture_path.push_back(texture_parent_path + "/Bottom.hdr");
		texture_path.push_back(texture_parent_path + "/Front.hdr");
		texture_path.push_back(texture_parent_path + "/Back.hdr");

		for (int textureIndex = 0; textureIndex < texture_path.size(); textureIndex++)
		{
			std::string path = texture_path[textureIndex];
			int num_components = 0;
			std::unique_ptr<unsigned char, void(*)(void*)> stbi_data(stbi_load(
				path.c_str(), &width, &height, &num_components, 4),
				stbi_image_free);

			if ((!stbi_data) ||
				(0 >= width) ||
				(0 >= height) ||
				(0 >= num_components))
			{
				std::cout << "Could not read image!" << std::endl;
				return;
			}

			num_components = 4;
			data_size = width * height * num_components;
			m_ImageData.resize(data_size);
			std::memcpy(m_ImageData.data(), stbi_data.get(), data_size);

			//创建GPU数据
			VkExtent3D extent = { (uint32_t)width, (uint32_t)height, 1 };
			VkFormat format = CheckTextureFormat(num_components, false, isSRGB);

			if (!isImageCreated)
			{
				m_Image.CreateVulkanImage(
					nativeDevice,
					extent,
					format,
					1,
					1,
					VK_IMAGE_TYPE_2D,
					VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
					VK_SAMPLE_COUNT_1_BIT,
					true,
					true,
					true
				);
				isImageCreated = true;
			}

			VkOffset3D image_offset = { 0, 0, 0 };
			vkDevicePtr->SetDeviceImageData(m_Image, command_buffer, m_ImageData.data(), 0, data_size, image_offset, extent, 0, textureIndex);
		}

		m_TextureWidth = width;
		m_TextureHeight = height;

		//切换layout到：VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		//

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

		command_buffer.EndCommandBufferRecord();
		VansVKCommandBuffer::SubmitCommands(graphicsQueue, nativeDevice, { command_buffer.GetVKCommandBuffer() }, {}, {}, command_buffer.m_CommandBufferFinishSubmitFence);
		command_buffer.ResetCommandBuffer(false);
	}

	void VansTexture::InitTextureWithoutData(VansVKCommandBuffer& command_buffer, int width, int height, int slice, int num_components, bool isCube, bool generateMip, bool enabeRandonWrite, TexturePrecision texture_precision)
	{
		m_TextureWidth = width;
		m_TextureHeight = height;
		m_TextureSlice = slice;

		bool createTexture3D = slice > 1 ? true : false;

		VkExtent3D extent = { (uint32_t)width, (uint32_t)height, (uint32_t)slice };
		VansVKDevice* vkDevicePtr = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
		VkDevice nativeDevice = vkDevicePtr->GetLogicDevice();
		VkQueue graphicsQueue = vkDevicePtr->GetGraphicsQueue();
		VkFormat format = VK_FORMAT_UNDEFINED;
		switch (texture_precision)
		{
		case VansGraphics::LOW_PRES_8:
			format = CheckTextureFormat(num_components);
			break;
		case VansGraphics::MID_PRES_16:
			format = CheckTextureMidPrecisionFormat(num_components);
			break;
		case VansGraphics::HIGH_PRES_32:
			format = CheckTextureHighPrecisionFormat(num_components);
			break;
		default:
			break;
		}
		int mipNum = generateMip ? static_cast<int>(std::floor(std::log2(static_cast<float>(width)))) + 1 : 1;
		m_Image.CreateVulkanImage(
			nativeDevice,
			extent,
			format,
			mipNum,
			1,
			createTexture3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_SAMPLE_COUNT_1_BIT,
			isCube,
			true,
			true
		);

		command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		m_Image.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			{
				m_Image.GetImage(),
				VK_ACCESS_NONE,
				VK_ACCESS_NONE,
				m_Image.GetImageLayout(),
				enabeRandonWrite ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				m_Image.GetImageAspect()
			});

		command_buffer.EndCommandBufferRecord();
		VansVKCommandBuffer::SubmitCommands(graphicsQueue, nativeDevice, { command_buffer.GetVKCommandBuffer() }, {}, {}, command_buffer.m_CommandBufferFinishSubmitFence);
		command_buffer.ResetCommandBuffer(false);
	}
}


