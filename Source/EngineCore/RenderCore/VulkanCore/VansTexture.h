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
			int import_channel = 4);
		
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

		// 格式选择（统一替代原有三个CheckTexture*Format方法）
		static VkFormat ChooseFormat(int channel, TexturePrecision precision, bool isSRGB = false);

		// 文件读取
		void* ReadTextureFile(const std::string& texture_path, TexturePrecision texture_precision, int& bytes_per_channel, int& width, int& height, int& num_components, int import_channel);

		// 通用辅助
		void SubmitAndWait(VansVKCommandBuffer& command_buffer, VkQueue queue, VkDevice device);
		void GenerateMipmaps(VkCommandBuffer cmd, int width, int height, int mipLevels);

		// 上传路径
		void UploadCompressedTexture(VansVKCommandBuffer& command_buffer, const uint8_t* srcData, int width, int height, bool isSRGB);
		void UploadUncompressedTexture(VansVKCommandBuffer& command_buffer, const void* data, size_t dataSize, int width, int height, VkFormat format, bool needMip);

		int m_TextureWidth;
		int m_TextureHeight;
		int m_TextureSlice;
	};
}