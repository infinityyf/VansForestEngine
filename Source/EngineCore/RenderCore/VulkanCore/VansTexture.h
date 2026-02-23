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

	struct TextureUploadInfo
	{
		int width = 0;
		int height = 0;
		int channels = 0;
		int mipLevels = 1;

		VkFormat format = VK_FORMAT_UNDEFINED;
		bool isCompressed = false;

		// For uncompressed data
		std::vector<uint8_t> rawData;

		// For compressed data
		struct MipData {
			int width;
			int height;
			std::vector<uint8_t> data;
		};
		std::vector<MipData> mips;
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
			int import_channel = 4);
		
		static TextureUploadInfo PrepareTextureCPU(
			std::string texture_path,
			bool isSRGB,
			bool useCompress,
			bool need_mip,
			TexturePrecision texture_precision,
			int import_channel
		);

		void LoadCubeTexture(VansVKCommandBuffer& command_buffer, std::string texture_path, bool isSRGB = true);

		//直接创建一个GPU上的texture
		void InitTextureWithoutData(VansVKCommandBuffer& command_buffer, int width, int height, int slice, int num_components, bool isCube, bool generateMip, bool enabeRandonWrite, TexturePrecision texture_precision = LOW_PRES_8);

		VansVKImage& GetImage() { return m_Image; }

		TextureType m_TextureType;

		int GetWidth() { return m_TextureWidth; }

		int GetHeight() { return m_TextureHeight; }

		int GetSlice() { return m_TextureSlice; }

	private:
		VansVKImage m_Image;

		std::vector<unsigned char> m_ImageData;

		static VkFormat CheckTextureFormat(int channel, bool isHdr = false, bool isSRGB = false);

		static VkFormat CheckTextureHighPrecisionFormat(int channel);

		static VkFormat CheckTextureMidPrecisionFormat(int channel);

		void* ReadTextureFile(std::string texture_path, TexturePrecision texture_precision, int& bytes_per_channel, int& width, int& height, int& num_components, int import_channel);

	private:

		int m_TextureWidth;

		int m_TextureHeight;

		int m_TextureSlice;
	};
}