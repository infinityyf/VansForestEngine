#pragma once
#include "VansVKImage.h"
#include "VansVKCommandBuffer.h"
#include "../VansAsset.h"
#include <string>
#include <vector>

namespace VansGraphics
{
	enum TextureType
	{
		TEXTURE_2D = 0,
		TEXTURE_3D = 1,
		TEXTURE_CUBE = 2,
	};

	enum TexturePrecision
	{
		LOW_PRES_8 = 0,
		MID_PRES_16 = 1,
		HIGH_PRES_32 = 2
	};



	class VansTexture : public VansAsset
	{
	public:
		~VansTexture();

		//读取texture数据
		void LoadTexture(VansVKCommandBuffer& command_buffer, 
			std::string texture_path, 
			bool isSRGB = true, 
			bool useCompress = false, 
			bool need_mip = false, 
			TexturePrecision texture_precesion = LOW_PRES_8, 
			int import_channel = 4,
			VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);
		
		void LoadCubeTexture(VansVKCommandBuffer& command_buffer, std::string texture_path, bool isSRGB = true);

		//直接创建一个GPU上的texture
		void InitTextureWithoutData(VansVKCommandBuffer& command_buffer, int width, int height, int slice, int num_components, bool isCube, bool generateMip, bool enabeRandonWrite, TexturePrecision texture_precision = LOW_PRES_8, VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);

		// 从内存原始像素数据创建一个 2D 贴图（无 mipmap，无文件 IO）。
		// 调用方负责保证 dataSize == width * height * bytes_per_texel(format)。
		// 用于内嵌型 LUT（例如 LTC）以及任何 stb_image 不支持的格式。
		void LoadFromMemory(VansVKCommandBuffer& command_buffer,
			const void* data, size_t dataSize,
			int width, int height, VkFormat format,
			VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

		// 创建 VK_IMAGE_TYPE_2D 的二维贴图数组（sampler2DArray），带完整 mip 链。
		// 所有层均初始化为 SHADER_READ_ONLY 布局；数据未填充，可按需调用 LoadTextureLayer 上传。
		void InitTextureArray(VansVKCommandBuffer& command_buffer,
			int width, int height, int layerCount, int numComponents,
			bool generateMip, TexturePrecision texture_precision = LOW_PRES_8,
			VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

		// 将单张图片文件上传到贴图数组的指定层（mip 0）。
		// 若图片分辨率与数组不一致则进行最近邻缩放。layerIndex 必须 < layerCount。
		// 返回 true 表示加载成功；false 表示文件不存在或读取失败（不会修改贴图数据）。
		bool LoadTextureLayer(VansVKCommandBuffer& command_buffer,
			const std::string& texturePath, int layerIndex, bool isSRGB = true,
			VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

		// 将已在 CPU 内存中的 RGBA8 像素上传到贴图数组的指定层（mip 0）并重新生成 mip 链。
		// 像素格式必须为 RGBA8（与数组一致）。srcW/srcH 可与数组不一致，内部做最近邻缩放。
		// 专为每帧视频帧更新面光源 emissive 数组层设计；返回 false 表示参数无效。
		bool UpdateArrayLayerFromPixels(VansVKCommandBuffer& command_buffer,
			const uint8_t* pixels, int srcW, int srcH, int layerIndex);

		VansVKImage& GetImage() { return m_Image; }

		TextureType m_TextureType;

		int GetWidth() { return m_TextureWidth; }

		int GetHeight() { return m_TextureHeight; }

		int GetSlice() { return m_TextureSlice; }

	private:
		VansVKImage m_Image;

		// 格式选择（统一替代原有三个CheckTexture*Format方法）
		static VkFormat ChooseFormat(int channel, TexturePrecision precision, bool isSRGB = false);

		// 文件读取
		void* ReadTextureFile(const std::string& texture_path, TexturePrecision texture_precision, int& bytes_per_channel, int& width, int& height, int& num_components, int import_channel);

		// 通用辅助
		void SubmitAndWait(VansVKCommandBuffer& command_buffer, VkQueue queue, VkDevice device);
		void GenerateMipmaps(VkCommandBuffer cmd, int width, int height, int mipLevels);

		// GenerateMipmaps 的贴图数组变体：只为指定层生成 mip 链。
		// 调用前 mip 0 必须处于 TRANSFER_DST_OPTIMAL；高层级 mip 必须处于 SHADER_READ_ONLY_OPTIMAL。
		// 调用后所有 mip 均转换为 SHADER_READ_ONLY_OPTIMAL。
		void GenerateMipmapsForLayer(VkCommandBuffer cmd, int width, int height, int mipLevels, int layerIndex);

		// 上传路径
		void UploadCompressedTexture(VansVKCommandBuffer& command_buffer, const uint8_t* srcData, int width, int height, bool isSRGB, VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);
		void UploadUncompressedTexture(VansVKCommandBuffer& command_buffer, const void* data, size_t dataSize, int width, int height, VkFormat format, bool needMip, VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);

		int m_TextureWidth;
		int m_TextureHeight;
		int m_TextureSlice;
	};
}