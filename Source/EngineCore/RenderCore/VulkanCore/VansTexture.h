#pragma once
#include "VansVKImage.h"
#include "VansVKCommandBuffer.h"
#include "../VansAsset.h"
#include <cstddef>
#include <cstdint>
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
		HIGH_PRES_32 = 2,
		HDR_PRES_16 = 3
	};



	class VansTexture : public VansAsset
	{
	public:
		struct TextureLoadDesc
		{
			std::string path;
			std::string cookedPath;
			bool isSRGB = true;
			bool useCompress = false;
			bool needMip = false;
			TexturePrecision precision = LOW_PRES_8;
			int importChannel = 4;
			VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		};

		~VansTexture();

		static std::uint64_t GetUploadFailureCount();
		static void ResetUploadFailureCount();

		//读取texture数据
		void LoadTexture(VansVKCommandBuffer& command_buffer, const TextureLoadDesc& loadDesc);

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

		// 从按 Z 轴编号导出的 PNG 切片组装 3D 纹理。
		// slicePathFormat 需要包含一个整数格式占位符，例如 "Slice_Z_%03d.png"。
		bool LoadTexture3DFromSlices(VansVKCommandBuffer& command_buffer,
			const std::string& slicePathFormat, int sliceCount,
			int importChannel = 4,
			VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);

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

		// Creates a samplerCubeArray. cubeCount is the number of cubemaps, while
		// uploads address physical faces as [cubeIndex * 6 + faceIndex].
		void InitCubeTextureArray(VansVKCommandBuffer& command_buffer,
			int width, int height, int cubeCount, int numComponents,
			bool generateMip, TexturePrecision texturePrecision = MID_PRES_16,
			VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

		// 将单张图片文件上传到贴图数组的指定层（mip 0）。
		// 若图片分辨率与数组不一致则进行最近邻缩放。layerIndex 必须 < layerCount。
		// 返回 true 表示加载成功；false 表示文件不存在或读取失败（不会修改贴图数据）。
		bool LoadTextureLayer(VansVKCommandBuffer& command_buffer,
			const std::string& texturePath, int layerIndex, bool isSRGB = true,
			VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

		// Uploads an HDR/PNG face into an RGBA16F array layer.
		bool LoadHDRTextureLayer(VansVKCommandBuffer& command_buffer,
			const std::string& texturePath, int layerIndex);

		bool UpdateHDRArrayLayerFromPixels(VansVKCommandBuffer& command_buffer,
			const float* rgbaPixels, int srcWidth, int srcHeight, int layerIndex);

		// 将已在 CPU 内存中的 RGBA8 像素上传到贴图数组的指定层（mip 0）并重新生成 mip 链。
		// 像素格式必须为 RGBA8（与数组一致）。srcW/srcH 可与数组不一致，内部做最近邻缩放。
		// 专为每帧视频帧更新面光源 emissive 数组层设计；返回 false 表示参数无效。
		bool UpdateArrayLayerFromPixels(VansVKCommandBuffer& command_buffer,
			const uint8_t* pixels, int srcW, int srcH, int layerIndex);

		// 在已 Begin 的图形 command buffer 中记录贴图数组层更新，不独立提交或等待 fence。
		// 供视频面光源逐帧更新使用，效果与 UpdateArrayLayerFromPixels 保持一致。
		bool RecordArrayLayerUploadFromPixels(VansVKCommandBuffer& command_buffer,
			const uint8_t* pixels, int srcW, int srcH, int layerIndex);

		// 从已有 GPU 2D 纹理直接拷贝/缩放到数组层，避免视频面光源二次 CPU staging 上传。
		bool RecordArrayLayerCopyFromTexture(VansVKCommandBuffer& command_buffer,
			VansTexture* sourceTexture, int layerIndex);

		VansVKImage& GetImage() { return m_Image; }

		TextureType m_TextureType;

		int GetWidth() { return m_TextureWidth; }

		int GetHeight() { return m_TextureHeight; }

		int GetSlice() { return m_TextureSlice; }

	private:
		VansVKImage m_Image;

		enum class TextureCompressionMode
		{
			None,
			BC3
		};

		enum class TextureMipGeneration
		{
			None,
			GPUBlit,
			CPUCompressedChain
		};

		enum class TextureContentIntent
		{
			Color,
			LinearData,
			HDRData
		};

		struct TextureUploadPlan
		{
			VkFormat format = VK_FORMAT_UNDEFINED;
			TextureContentIntent contentIntent = TextureContentIntent::Color;
			int sourceChannels = 4;
			int bytesPerChannel = 1;
			int mipLevels = 1;
			TextureCompressionMode compressionMode = TextureCompressionMode::None;
			TextureMipGeneration mipGeneration = TextureMipGeneration::None;
			int compressedBlockWidth = 4;
			int compressedBlockHeight = 4;
			int compressedBytesPerBlock = 0;
			bool compressedHasAlpha = false;
			VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		};

		struct TextureUploadRequest
		{
			const void* sourceData = nullptr;
			size_t sourceDataSize = 0;
			int width = 0;
			int height = 0;
			TextureUploadPlan plan{};
			VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		};

		struct TextureCompressionProfile
		{
			TextureCompressionMode mode = TextureCompressionMode::None;
			VkFormat format = VK_FORMAT_UNDEFINED;
			int blockWidth = 4;
			int blockHeight = 4;
			int bytesPerBlock = 0;
			bool hasAlpha = false;
			bool forceFullMipChain = false;
		};

		// 格式选择（统一替代原有三个CheckTexture*Format方法）
		static VkFormat ChooseFormat(int channel, TexturePrecision precision, bool isSRGB = false);
		static TextureContentIntent DetermineTextureContentIntent(bool isSRGB, TexturePrecision precision);
		static TextureCompressionProfile ChooseCompressionProfile(
			int sourceChannels, int bytesPerChannel,
			bool isSRGB, bool useCompress, TexturePrecision precision);
		static int CalculateMipLevels(int width, int height, bool generateMip);
		static TextureUploadPlan BuildTextureUploadPlan(
			int width, int height, int sourceChannels, int bytesPerChannel,
			bool isSRGB, bool useCompress, bool needMip, TexturePrecision precision);
		static TextureUploadRequest BuildTextureUploadRequest(
			const void* sourceData,
			int width, int height,
			bool isSRGB, bool useCompress, bool needMip,
			TexturePrecision precision,
			int sourceChannels, int bytesPerChannel,
			VkSamplerAddressMode addressMode);
		static size_t CalculateTextureDataSize(int width, int height, int channels, int bytesPerChannel);
		static bool IsValidTextureUploadRequest(const TextureUploadRequest& request);
		static const char* ToString(TextureCompressionMode mode);
		static const char* ToString(TextureMipGeneration mode);
		static const char* ToString(TextureContentIntent intent);

		// 文件读取
		void* ReadTextureFile(const std::string& texture_path, TexturePrecision texture_precision, int& bytes_per_channel, int& width, int& height, int& num_components, int import_channel);

		// 通用辅助
		bool SubmitAndWait(VansVKCommandBuffer& command_buffer, VkQueue queue, VkDevice device);
		void GenerateMipmaps(VansVKCommandBuffer& command_buffer, int width, int height, int mipLevels);
		void GenerateMipmapsForLayer(VansVKCommandBuffer& command_buffer, int width, int height, int mipLevels, int layerIndex);
		void GenerateMipmapsForLayer(
			VansVKCommandBuffer& command_buffer,
			int width,
			int height,
			int mipLevels,
			int layerIndex,
			VkImageLayout targetMipInitialLayout,
			VkAccessFlags targetMipInitialAccessMask,
			VkPipelineStageFlags targetMipInitialStage);

		void FinalizeUploadedLayer(VansVKCommandBuffer& command_buffer, int width, int height, int layerIndex);

		// 上传路径
		bool UploadTexture(VansVKCommandBuffer& command_buffer, const TextureUploadRequest& request);
		bool LoadCookedTexture(VansVKCommandBuffer& command_buffer, const TextureLoadDesc& loadDesc);
		bool UploadCompressedTexture(VansVKCommandBuffer& command_buffer, const uint8_t* srcData, int width, int height, const TextureUploadPlan& uploadPlan, VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);
		bool UploadUncompressedTexture(VansVKCommandBuffer& command_buffer, const void* data, size_t dataSize, int width, int height, const TextureUploadPlan& uploadPlan, VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);

		int m_TextureWidth;
		int m_TextureHeight;
		int m_TextureSlice;
	};
}
