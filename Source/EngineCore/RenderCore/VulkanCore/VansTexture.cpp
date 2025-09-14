#include "VansTexture.h"
#include "VansVKDevice.h"
#include "VansVKCommandBuffer.h"

using namespace VansGraphics;
#include "../VansGraphicsDevice.h"
#include <iostream>
#include <vector>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#include <../../STBImge/stb_image.h>

// Add stb_dxt implementation
#define STB_DXT_IMPLEMENTATION
#include <../../STBImge/stb_dxt.h>

// Replace with your compressor implementation (stb_dxt/squish/ISPC/BasisU)
// outDst must receive bytesPerBlock bytes for a single 4x4 RGBA block (row-major, RGBA8 per texel).
static void compress_block(uint8_t* outDst, const uint8_t rgba4x4[16*4], bool hasAlpha)
{
    // stb_compress_dxt_block(output, input_rgba16, alpha, mode)
    // alpha: 0 -> DXT1 (8 bytes), 1 -> DXT5 (16 bytes)
    // mode: 0 = default/fast, 1 = high quality (if supported)
    int alpha = hasAlpha ? 1 : 0;
    int mode = 0;
    stb_compress_dxt_block(outDst, rgba4x4, alpha, mode);
}

VansGraphics::VansTexture::~VansTexture()
{
	m_Image.DestroyVulkanImage(*(VkDevice*)m_GraphicsDevice->GetNativeGraphicsDevice());
}

void VansGraphics::VansTexture::LoadTexture( VansVKCommandBuffer& command_buffer, std::string texture_path, bool isSRGB, bool useCompress)
{
	std::cout << "Load Texture : " << texture_path << std::endl;
	int width = 0;
	int height = 0;
	int num_components = 0;
	std::unique_ptr<unsigned char, void(*)(void*)> stbi_data(stbi_load(
		texture_path.c_str(), &width, &height, &num_components, 4),
		stbi_image_free);

	if ((!stbi_data) ||
		(0 >= width) ||
		(0 >= height) ||
		(0 >= num_components)) 
	{
		std::cout << "Could not read image!" << std::endl;
		return;
	}

	// ---------------- compress into block layout (tight packed) ----------------
    VansVKDevice* vkDevicePtr = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VkPhysicalDevice phys = vkDevicePtr->GetPhysicalDevice();
    bool hasAlpha = true; // we forced 4 components above

    // Choose compressed format (BC1/BC3)
	VkFormat chosenFormat;
	if (hasAlpha) 
	{
		// prefer sRGB variant if requested and supported
		chosenFormat = isSRGB ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
	}
	else 
	{
		chosenFormat = isSRGB ? VK_FORMAT_BC1_RGB_SRGB_BLOCK : VK_FORMAT_BC1_RGB_UNORM_BLOCK;
	}

	if(useCompress)
    {
        const uint8_t* src = stbi_data.get();
        int blocksX = (width + 3) / 4;
        int blocksY = (height + 3) / 4;
        int bytesPerBlock = hasAlpha ? 16 : 8; // DXT5/BC3 -> 16, DXT1/BC1 -> 8
        size_t compressedSize = size_t(blocksX) * size_t(blocksY) * size_t(bytesPerBlock);
        std::vector<uint8_t> compressed;
        compressed.resize(compressedSize);

        auto sample = [&](int sx, int sy, int c)->uint8_t {
            sx = std::min(std::max(sx, 0), width-1);
            sy = std::min(std::max(sy, 0), height-1);
            return src[(sy * width + sx) * 4 + c];
        };

        uint8_t blockRGBA[16*4];
        uint8_t* dstPtr = compressed.data();
        for (int by = 0; by < blocksY; ++by) {
            for (int bx = 0; bx < blocksX; ++bx) {
                // gather 4x4 block (row-major)
                for (int y = 0; y < 4; ++y) {
                    for (int x = 0; x < 4; ++x) {
                        int sx = bx*4 + x;
                        int sy = by*4 + y;
                        int idx = (y*4 + x) * 4;
                        blockRGBA[idx + 0] = sample(sx, sy, 0);
                        blockRGBA[idx + 1] = sample(sx, sy, 1);
                        blockRGBA[idx + 2] = sample(sx, sy, 2);
                        blockRGBA[idx + 3] = sample(sx, sy, 3);
                    }
                }
                // compress blockRGBA -> dstPtr
                compress_block(dstPtr, blockRGBA, hasAlpha);
                dstPtr += bytesPerBlock;
            }
        }

        m_TextureWidth = width;
        m_TextureHeight = height;

        // Create compressed Vulkan image (use chosenFormat)
        VkExtent3D extent = { (uint32_t)width, (uint32_t)height, 1 };
        VkDevice nativeDevice = vkDevicePtr->GetLogicDevice();
        VkQueue graphicsQueue = vkDevicePtr->GetGraphicsQueue();

        m_Image.CreateVulkanImage(
            nativeDevice,
            extent,
            chosenFormat,        // use compressed format here
            1,
            1,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_SAMPLE_COUNT_1_BIT,
            false,
            true,
            true
        );

        // Upload compressed data:
        // IMPORTANT: SetDeviceImageData must copy compressed data with bufferRowLength=0 and bufferImageHeight=0
        // (i.e. tightly packed) because compressed formats are block-packed.
        VkOffset3D image_offset = { 0, 0, 0 };
        vkDevicePtr->SetDeviceImageData(m_Image, compressed.data(), 0, static_cast<uint32_t>(compressed.size()), image_offset, extent, 0, 0);

        // Transition to shader read only
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
        VansVKCommandBuffer::SubmitCommands(graphicsQueue, nativeDevice, {command_buffer.GetVKCommandBuffer()}, {}, {}, VansVKCommandBuffer::m_CommandBufferFinishSubmitFence);
        command_buffer.ResetCommandBuffer(false);

        // free CPU compressed buffer automatically by vector destructor
        return;
    }


	num_components = 4;
	int data_size = width * height * num_components;
	m_ImageData.resize(data_size);
	std::memcpy(m_ImageData.data(), stbi_data.get(), data_size);

	VkExtent3D extent = { (uint32_t)width, (uint32_t)height, 1 };
	VkDevice nativeDevice = vkDevicePtr->GetLogicDevice();
	VkQueue graphicsQueue = vkDevicePtr->GetGraphicsQueue();
	VkFormat format = CheckTextureFormat(num_components, false, isSRGB);
	m_Image.CreateVulkanImage(
		nativeDevice,
		extent,
		format,
		1,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false,
		true,
		true
	);

	VkOffset3D image_offset = { 0, 0, 0 };
	vkDevicePtr->SetDeviceImageData(m_Image, m_ImageData.data(), 0, data_size, image_offset, extent, 0, 0);

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
	VansVKCommandBuffer::SubmitCommands(graphicsQueue, nativeDevice, {command_buffer.GetVKCommandBuffer()}, {}, {}, VansVKCommandBuffer::m_CommandBufferFinishSubmitFence);
	command_buffer.ResetCommandBuffer(false);

	m_ImageData.clear();
}

VkFormat VansGraphics::VansTexture::CheckTextureFormat(int channel, bool isHdr, bool isSRGB)
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

VkFormat VansGraphics::VansTexture::CheckTextureHighPrecisionFormat(int channel)
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

VkFormat VansGraphics::VansTexture::CheckTextureMidPrecisionFormat(int channel)
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

void VansGraphics::VansTexture::LoadCubeTexture(VansVKCommandBuffer& command_buffer, std::string texture_parent_path, bool isSRGB)
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

	for (int textureIndex = 0;textureIndex< texture_path.size(); textureIndex++)
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
		VkFormat format = CheckTextureFormat(num_components,false, isSRGB);
		
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
		vkDevicePtr->SetDeviceImageData(m_Image, m_ImageData.data(), 0, data_size, image_offset, extent, 0, textureIndex);
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
	VansVKCommandBuffer::SubmitCommands(graphicsQueue, nativeDevice, { command_buffer.GetVKCommandBuffer() }, {}, {}, VansVKCommandBuffer::m_CommandBufferFinishSubmitFence);
	command_buffer.ResetCommandBuffer(false);
}

void VansGraphics::VansTexture::InitTextureWithoutData(VansVKCommandBuffer& command_buffer, int width, int height, int slice, int num_components, bool isCube, bool generateMip, bool enabeRandonWrite, TexturePrecision texture_precision)
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
	int mipNum = generateMip ? static_cast<int>(std::floor( std::log2(static_cast<float>(width)))) + 1 : 1;
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
	VansVKCommandBuffer::SubmitCommands(graphicsQueue, nativeDevice, { command_buffer.GetVKCommandBuffer() }, {}, {}, VansVKCommandBuffer::m_CommandBufferFinishSubmitFence);
	command_buffer.ResetCommandBuffer(false);
}

