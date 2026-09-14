#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Vans
{
// 只负责数据图像的无损编码，不处理地形、PCG、颜色空间或文件写入。
class VansDataPngEncoder
{
public:
	static bool EncodeGray16(std::uint32_t width, std::uint32_t height,
		const std::vector<std::uint16_t>& pixels, std::string& pngBytes, std::string& error);
	static bool EncodeRGBA8(std::uint32_t width, std::uint32_t height,
		const std::vector<std::uint8_t>& pixels, std::string& pngBytes, std::string& error);
};
}
