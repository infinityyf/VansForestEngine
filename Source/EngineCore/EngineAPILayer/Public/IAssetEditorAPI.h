#pragma once

#include "EngineDTOs.h"

#include <string>
#include <vector>

namespace Vans::EditorAPI
{
	class IAssetEditorAPI
	{
	public:
		virtual ~IAssetEditorAPI() = default;
		virtual std::vector<AssetEntry> QueryAssets(AssetTypeFilter filter) const = 0;
		virtual AssetDragPayload CreateAssetDragPayload(const std::string& assetPath) = 0;
		virtual AssetGuidResolution ResolveAssetGuid(const std::string& assetGuid) const = 0;
	};
}
