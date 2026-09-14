#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Vans
{
struct VansTerrainHeightImage
{
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::vector<std::uint16_t> pixels;
};

struct VansTerrainWeightImage
{
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::vector<std::uint8_t> pixels;
};

class VansTerrainImageCodec
{
public:
	static bool DecodeHeight16(
		const std::string& pngBytes,
		VansTerrainHeightImage& image,
		std::string& error);
	static bool DecodeWeightsRGBA8(
		const std::string& pngBytes,
		VansTerrainWeightImage& image,
		std::string& error);
	static bool EncodeHeight16(
		const VansTerrainHeightImage& image,
		std::string& pngBytes,
		std::string& error);
	static bool EncodeWeightsRGBA8(
		const VansTerrainWeightImage& image,
		std::string& pngBytes,
		std::string& error);
	static bool LoadHeight16(
		const std::filesystem::path& path,
		VansTerrainHeightImage& image,
		std::string& error);
	static bool LoadWeightsRGBA8(
		const std::filesystem::path& path,
		VansTerrainWeightImage& image,
		std::string& error);
};
}
