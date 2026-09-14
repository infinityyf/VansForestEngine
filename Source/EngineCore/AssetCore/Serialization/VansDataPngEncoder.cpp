#include "VansDataPngEncoder.h"

#include <algorithm>
#include <array>
#include <limits>

namespace Vans
{
namespace
{
constexpr std::array<std::uint8_t, 8> PngSignature{
	0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au
};

void AppendBigEndian(std::string& bytes, std::uint32_t value)
{
	bytes.push_back(static_cast<char>((value >> 24u) & 0xffu));
	bytes.push_back(static_cast<char>((value >> 16u) & 0xffu));
	bytes.push_back(static_cast<char>((value >> 8u) & 0xffu));
	bytes.push_back(static_cast<char>(value & 0xffu));
}

std::uint32_t Crc32(const std::uint8_t* data, std::size_t size)
{
	std::uint32_t crc = 0xffffffffu;
	for (std::size_t index = 0; index < size; ++index)
	{
		crc ^= data[index];
		for (int bit = 0; bit < 8; ++bit)
			crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

void AppendChunk(std::string& png, const char type[4], const std::string& data)
{
	AppendBigEndian(png, static_cast<std::uint32_t>(data.size()));
	const std::size_t crcStart = png.size();
	png.append(type, 4);
	png.append(data);
	AppendBigEndian(png, Crc32(
		reinterpret_cast<const std::uint8_t*>(png.data() + crcStart), 4u + data.size()));
}

std::uint32_t Adler32(const std::string& bytes)
{
	constexpr std::uint32_t Mod = 65521u;
	std::uint32_t a = 1u;
	std::uint32_t b = 0u;
	for (const unsigned char value : bytes)
	{
		a = (a + value) % Mod;
		b = (b + a) % Mod;
	}
	return (b << 16u) | a;
}

std::string DeflateStored(const std::string& source)
{
	std::string result;
	result.reserve(source.size() + source.size() / 65535u * 5u + 8u);
	// 0x78 0x01：32K 窗口的无损存储流，保留数据纹理原始量化值。
	result.push_back(static_cast<char>(0x78));
	result.push_back(static_cast<char>(0x01));
	std::size_t offset = 0;
	do
	{
		const std::size_t remaining = source.size() - offset;
		const std::uint16_t length = static_cast<std::uint16_t>(
			std::min<std::size_t>(remaining, 65535u));
		const bool finalBlock = offset + length == source.size();
		result.push_back(static_cast<char>(finalBlock ? 0x01 : 0x00));
		result.push_back(static_cast<char>(length & 0xffu));
		result.push_back(static_cast<char>((length >> 8u) & 0xffu));
		const std::uint16_t inverse = static_cast<std::uint16_t>(~length);
		result.push_back(static_cast<char>(inverse & 0xffu));
		result.push_back(static_cast<char>((inverse >> 8u) & 0xffu));
		result.append(source.data() + offset, length);
		offset += length;
	} while (offset < source.size());
	AppendBigEndian(result, Adler32(source));
	return result;
}

bool EncodePng(
	std::uint32_t width,
	std::uint32_t height,
	std::uint8_t bitDepth,
	std::uint8_t colorType,
	std::string scanlines,
	std::string& png,
	std::string& error)
{
	if (width == 0 || height == 0)
	{
		error = "Terrain image dimensions must be non-zero";
		return false;
	}
	png.assign(reinterpret_cast<const char*>(PngSignature.data()), PngSignature.size());
	std::string header;
	AppendBigEndian(header, width);
	AppendBigEndian(header, height);
	header.push_back(static_cast<char>(bitDepth));
	header.push_back(static_cast<char>(colorType));
	header.append(3, '\0');
	AppendChunk(png, "IHDR", header);
	AppendChunk(png, "IDAT", DeflateStored(scanlines));
	AppendChunk(png, "IEND", {});
	return true;
}

bool ValidPixelCount(std::uint32_t width, std::uint32_t height, std::size_t channels, std::size_t actual)
{
	if (width == 0 || height == 0 || channels == 0)
		return false;
	const std::size_t w = width;
	const std::size_t h = height;
	return w <= (std::numeric_limits<std::size_t>::max)() / h &&
		w * h <= (std::numeric_limits<std::size_t>::max)() / channels &&
		w * h * channels == actual;
}
}

bool VansDataPngEncoder::EncodeGray16(
	std::uint32_t width, std::uint32_t height, const std::vector<std::uint16_t>& pixels,
	std::string& pngBytes,
	std::string& error)
{
	pngBytes.clear();
	error.clear();
	if (!ValidPixelCount(width, height, 1, pixels.size()))
	{
		error = "Grayscale image pixel count does not match its dimensions";
		return false;
	}
	std::string scanlines;
	scanlines.reserve(static_cast<std::size_t>(height) * (1u + static_cast<std::size_t>(width) * 2u));
	for (std::uint32_t y = 0; y < height; ++y)
	{
		scanlines.push_back('\0');
		for (std::uint32_t x = 0; x < width; ++x)
		{
			const std::uint16_t value = pixels[static_cast<std::size_t>(y) * width + x];
			scanlines.push_back(static_cast<char>((value >> 8u) & 0xffu));
			scanlines.push_back(static_cast<char>(value & 0xffu));
		}
	}
	return EncodePng(width, height, 16, 0, std::move(scanlines), pngBytes, error);
}

bool VansDataPngEncoder::EncodeRGBA8(
	std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t>& pixels,
	std::string& pngBytes,
	std::string& error)
{
	pngBytes.clear();
	error.clear();
	if (!ValidPixelCount(width, height, 4, pixels.size()))
	{
		error = "RGBA image pixel count does not match its dimensions";
		return false;
	}
	const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
	std::string scanlines;
	scanlines.reserve(static_cast<std::size_t>(height) * (1u + rowBytes));
	for (std::uint32_t y = 0; y < height; ++y)
	{
		scanlines.push_back('\0');
		const auto* row = pixels.data() + static_cast<std::size_t>(y) * rowBytes;
		scanlines.append(reinterpret_cast<const char*>(row), rowBytes);
	}
	return EncodePng(width, height, 8, 6, std::move(scanlines), pngBytes, error);
}


}
