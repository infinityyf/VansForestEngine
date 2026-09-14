#include "VansTerrainImageCodec.h"

#include "../../AssetCore/Storage/VansFileStorage.h"
#include "../../AssetCore/Serialization/VansDataPngEncoder.h"

#include <stb_image.h>


namespace Vans
{

bool VansTerrainImageCodec::DecodeHeight16(
	const std::string& pngBytes,
	VansTerrainHeightImage& image,
	std::string& error)
{
	image = {};
	error.clear();
	if (pngBytes.empty() || !stbi_is_16_bit_from_memory(
		reinterpret_cast<const stbi_uc*>(pngBytes.data()), static_cast<int>(pngBytes.size())))
	{
		error = "Terrain heightmap must be a 16-bit image";
		return false;
	}
	int width = 0;
	int height = 0;
	int channels = 0;
	stbi_us* pixels = stbi_load_16_from_memory(
		reinterpret_cast<const stbi_uc*>(pngBytes.data()), static_cast<int>(pngBytes.size()),
		&width, &height, &channels, 1);
	if (!pixels || width < 2 || height < 2)
	{
		if (pixels) stbi_image_free(pixels);
		error = stbi_failure_reason() ? stbi_failure_reason() : "Cannot decode terrain heightmap";
		return false;
	}
	image.width = static_cast<std::uint32_t>(width);
	image.height = static_cast<std::uint32_t>(height);
	image.pixels.assign(pixels, pixels + static_cast<std::size_t>(width) * height);
	stbi_image_free(pixels);
	return true;
}

bool VansTerrainImageCodec::DecodeWeightsRGBA8(
	const std::string& pngBytes,
	VansTerrainWeightImage& image,
	std::string& error)
{
	image = {};
	error.clear();
	if (pngBytes.empty() || stbi_is_16_bit_from_memory(
		reinterpret_cast<const stbi_uc*>(pngBytes.data()), static_cast<int>(pngBytes.size())))
	{
		error = "Terrain splatmap must be an 8-bit RGBA image";
		return false;
	}
	int width = 0;
	int height = 0;
	int channels = 0;
	stbi_uc* pixels = stbi_load_from_memory(
		reinterpret_cast<const stbi_uc*>(pngBytes.data()), static_cast<int>(pngBytes.size()),
		&width, &height, &channels, 4);
	if (!pixels || width < 2 || height < 2)
	{
		if (pixels) stbi_image_free(pixels);
		error = stbi_failure_reason() ? stbi_failure_reason() : "Cannot decode terrain splatmap";
		return false;
	}
	image.width = static_cast<std::uint32_t>(width);
	image.height = static_cast<std::uint32_t>(height);
	image.pixels.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4u);
	stbi_image_free(pixels);
	return true;
}

bool VansTerrainImageCodec::EncodeHeight16(
	const VansTerrainHeightImage& image, std::string& pngBytes, std::string& error)
{
	return VansDataPngEncoder::EncodeGray16(image.width, image.height, image.pixels, pngBytes, error);
}

bool VansTerrainImageCodec::EncodeWeightsRGBA8(
	const VansTerrainWeightImage& image, std::string& pngBytes, std::string& error)
{
	return VansDataPngEncoder::EncodeRGBA8(image.width, image.height, image.pixels, pngBytes, error);
}

bool VansTerrainImageCodec::LoadHeight16(
	const std::filesystem::path& path,
	VansTerrainHeightImage& image,
	std::string& error)
{
	VansScopedIOContext io(VansIODomain::SourceResource, "TerrainImage.LoadHeight", false);
	std::string bytes;
	return VansFileStorage::ReadAllBytes(path, bytes, error) && DecodeHeight16(bytes, image, error);
}

bool VansTerrainImageCodec::LoadWeightsRGBA8(
	const std::filesystem::path& path,
	VansTerrainWeightImage& image,
	std::string& error)
{
	VansScopedIOContext io(VansIODomain::SourceResource, "TerrainImage.LoadWeights", false);
	std::string bytes;
	return VansFileStorage::ReadAllBytes(path, bytes, error) && DecodeWeightsRGBA8(bytes, image, error);
}
}
