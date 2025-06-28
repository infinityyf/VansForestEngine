#include "VansTexture.h"
#include "VansVKDevice.h"
#include "VansVKCommandBuffer.h"

using namespace VansGraphics;
#include "../VansGraphicsDevice.h"
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include <../../STBImge/stb_image.h>


VansGraphics::VansTexture::~VansTexture()
{
	m_Image.DestroyVulkanImage(*(VkDevice*)m_GraphicsDevice->GetNativeGraphicsDevice());
}

void VansGraphics::VansTexture::LoadTexture( VansVKCommandBuffer& command_buffer, std::string texture_path, bool isSRGB)
{
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

	m_TextureWidth = width;
	m_TextureHeight = height;

	num_components = 4;
	int data_size = width * height * num_components;
	m_ImageData.resize(data_size);
	std::memcpy(m_ImageData.data(), stbi_data.get(), data_size);

	VkExtent3D extent = { (uint32_t)width, (uint32_t)height, 1 };
	VansVKDevice* vkDevicePtr = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
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
	VansVKCommandBuffer::SubmitCommands(graphicsQueue, nativeDevice, {command_buffer.GetVKCommandBuffer()}, {}, {});
	command_buffer.ResetCommandBuffer(false);
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
	VansVKCommandBuffer::SubmitCommands(graphicsQueue, nativeDevice, { command_buffer.GetVKCommandBuffer() }, {}, {});
	command_buffer.ResetCommandBuffer(false);
}

void VansGraphics::VansTexture::InitTextureWithoutData(VansVKCommandBuffer& command_buffer, int width, int height, int num_components, bool isCube, bool generateMip, bool enabeRandonWrite, TexturePrecision texture_precision)
{
	m_TextureWidth = width;
	m_TextureHeight = height;

	VkExtent3D extent = { (uint32_t)width, (uint32_t)height, 1 };
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
		VK_IMAGE_TYPE_2D,
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
	VansVKCommandBuffer::SubmitCommands(graphicsQueue, nativeDevice, { command_buffer.GetVKCommandBuffer() }, {}, {});
	command_buffer.ResetCommandBuffer(false);
}

