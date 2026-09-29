#include "VansBuiltInAssetCatalog.h"

namespace Vans
{
const std::vector<VansBuiltInAssetEntry>& VansBuiltInAssetCatalog::Entries()
{
	static const std::vector<VansBuiltInAssetEntry> entries = {
        {
            "particleWhite",
            "34a4e372-93c5-44a1-a985-768a099c3c95",
            "EngineAssets/Textures/Default/particleWhite.png",
            VansAssetType::Texture
        },
		{
			"fullScreenQuad",
			"2c86c128-f3f0-4dbd-9e4e-0f0f0a61c9d1",
			"EngineAssets/Models/fullscreen.obj",
			VansAssetType::Model
		},
		{
			"plane",
			"92eb85ad-49ac-4f90-a8f9-9e30251c584b",
			"EngineAssets/Models/plane.obj",
			VansAssetType::Model
		},
		{
			"waterDetailWaveNormal",
			"6ba76755-170f-4915-8054-54699138937c",
			"EngineAssets/Textures/Water/DetailWaveTexture.png",
			VansAssetType::Texture
		},
		{
			"rainLayerTexture",
			"2ae873c5-332c-47ee-9806-c0b0ef2c5b18",
			"EngineAssets/Textures/Weather/RainLayerTexture.png",
			VansAssetType::Texture
		},
		{
			"rainSplashAtlas",
			"24ccbb4e-637d-4850-8c25-dbf67c8297f0",
			"EngineAssets/Textures/Weather/RainSplashAtlas.png",
			VansAssetType::Texture
		},
		{
			"rainRippleSeed",
			"15af6efd-d4a4-4d2b-93db-1157b889f691",
			"EngineAssets/Textures/Weather/RainRippleSeed.png",
			VansAssetType::Texture
		},
		{
			"defaultSkinCavity",
			"5f6d5237-1a6b-4b98-8b22-7cb738d88e09",
			"EngineAssets/Textures/Default/defaultSkinCavity.png",
			VansAssetType::Texture
		},
		{
			"defaultSkinMask",
			"b4b0a914-7e03-4b9a-92e9-fd6e73f32842",
			"EngineAssets/Textures/Default/defaultSkinMask.png",
			VansAssetType::Texture
		},
		{
			nullptr,
			BaseThemeGuid.data(),
			"EngineAssets/ui/Themes/BaseTheme.xaml",
			VansAssetType::UIXaml
		}
	};
	return entries;
}

bool VansBuiltInAssetCatalog::IsReservedRuntimeAlias(const std::string& alias)
{
	for (const VansBuiltInAssetEntry& entry : Entries())
	{
		if (entry.runtimeAlias != nullptr && alias == entry.runtimeAlias)
			return true;
	}
	return false;
}

bool VansBuiltInAssetCatalog::RegisterAssets(
	VansAssetDatabase& database,
	const std::filesystem::path& engineRoot,
	const VansAssetOperationPolicy& policy,
	std::vector<std::string>& errors)
{
	bool success = true;
	for (const VansBuiltInAssetEntry& entry : Entries())
	{
		std::string error;
		if (!database.RegisterOrRefresh(engineRoot / entry.sourcePath, policy, error))
		{
			success = false;
			errors.push_back(error.empty()
				? std::string("Cannot register built-in asset: ") + entry.sourcePath
				: std::move(error));
		}
	}
	return success;
}
}
