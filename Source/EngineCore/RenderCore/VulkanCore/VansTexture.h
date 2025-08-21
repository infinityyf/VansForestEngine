#pragma once
#include "VansVKImage.h"
#include "VansVKCommandBuffer.h"
#include "../VansAsset.h"
#include <string>
#include <vector>

using namespace VansVulkan;
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
		void LoadTexture(VansVKCommandBuffer& command_buffer, std::string texture_path, bool isSRGB = true);

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

		VkFormat CheckTextureFormat(int channel, bool isHdr = false, bool isSRGB = false);

		VkFormat CheckTextureHighPrecisionFormat(int channel);

		VkFormat CheckTextureMidPrecisionFormat(int channel);

	private:

		int m_TextureWidth;

		int m_TextureHeight;

		int m_TextureSlice;
	};
}