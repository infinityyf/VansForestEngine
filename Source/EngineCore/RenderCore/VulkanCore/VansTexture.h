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
	class VansTexture : public VansAsset
	{
	public:
		~VansTexture();

		//读取texture数据
		void LoadTexture(VansVKCommandBuffer& command_buffer, std::string texture_path);

		void LoadCubeTexture(VansVKCommandBuffer& command_buffer, std::string texture_path);

		//直接创建一个GPU上的texture
		void InitTextureWithoutData(VansVKCommandBuffer& command_buffer, int width, int height, int num_components, bool isCube, bool generateMip);

		VansVKImage& GetImage() { return m_Image; }

		TextureType m_TextureType;

	private:
		VansVKImage m_Image;

		std::vector<unsigned char> m_ImageData;

		VkFormat CheckTextureFormat(int channel, bool isHdr);
	};
}