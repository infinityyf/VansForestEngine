#include "VansTerrainAssetStorage.h"

#include "../Serialization/VansTerrainAssetCodec.h"
#include "../Serialization/VansTerrainImageCodec.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../AssetCore/Storage/VansFileStorage.h"
#include "../../AssetCore/Serialization/VansJsonDocumentCodec.h"

#include <nlohmann/json.hpp>

namespace Vans
{
bool VansTerrainAssetStorage::Load(
	const std::filesystem::path& sourcePath,
	const TexturePathResolver& resolveTexturePath,
	VansTerrainAsset& asset,
	std::string& error)
{
	error.clear();
	asset = {};
	if (!resolveTexturePath)
	{
		error = "Terrain asset storage requires a texture path resolver";
		return false;
	}
	VansScopedIOContext io(VansIODomain::SourceResource, "TerrainAsset.Load", false);
	std::string sourceBytes;
	if (!VansFileStorage::ReadAllBytes(sourcePath, sourceBytes, error))
		return false;
	nlohmann::ordered_json json;
	if (!VansJsonDocumentCodec::Parse(sourceBytes, json, error) ||
		!VansTerrainAssetCodec::DecodeDefinition(DecodeSerializedValueJson(json), asset, error))
		return false;
	asset.sourcePath = std::filesystem::absolute(sourcePath).lexically_normal();

	const auto heightPath = resolveTexturePath(asset.heightmap);
	const auto splat0Path = resolveTexturePath(asset.splatmaps[0]);
	const auto splat1Path = resolveTexturePath(asset.splatmaps[1]);
	if (!heightPath || !splat0Path || !splat1Path)
	{
		error = "Terrain heightmap or splatmap reference cannot be resolved to a texture asset";
		asset = {};
		return false;
	}
	VansTerrainHeightImage heightImage;
	VansTerrainWeightImage splat0;
	VansTerrainWeightImage splat1;
	if (!VansTerrainImageCodec::LoadHeight16(*heightPath, heightImage, error) ||
		!VansTerrainImageCodec::LoadWeightsRGBA8(*splat0Path, splat0, error) ||
		!VansTerrainImageCodec::LoadWeightsRGBA8(*splat1Path, splat1, error))
	{
		asset = {};
		return false;
	}
	if (heightImage.width != splat0.width || heightImage.height != splat0.height ||
		heightImage.width != splat1.width || heightImage.height != splat1.height)
	{
		error = "Terrain heightmap and both splatmaps must have identical dimensions";
		asset = {};
		return false;
	}
	asset.width = heightImage.width;
	asset.height = heightImage.height;
	asset.heights = std::move(heightImage.pixels);
	asset.splatPixels[0] = std::move(splat0.pixels);
	asset.splatPixels[1] = std::move(splat1.pixels);
	const auto diagnostics = ValidateTerrainAsset(asset, true);
	if (!diagnostics.empty())
	{
		error = diagnostics.front();
		asset = {};
		return false;
	}
	return true;
}
}
