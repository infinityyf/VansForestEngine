#pragma once

#include "VansEditorSelectionService.h"

#include <filesystem>
#include <string>
#include <utility>

namespace Vans
{
class VansEditorSelection
{
public:
    static void SelectEntity(std::string entityGuid)
    {
        VansEditorSelectionService::Get().SelectEntity(std::move(entityGuid), "LegacyFacade");
    }

	static void SelectScene()
	{
		VansEditorSelectionService::Get().SelectScene("LegacyFacade");
	}

    static void SelectAsset(std::filesystem::path assetPath)
    {
        VansEditorSelectionService::Get().SelectAsset(std::move(assetPath), "LegacyFacade");
    }

    static void Clear()
    {
        VansEditorSelectionService::Get().Clear("LegacyFacade");
    }

    static const std::string& EntityGuid() { return VansEditorSelectionService::Get().EntityGuid(); }
    static const std::filesystem::path& AssetPath() { return VansEditorSelectionService::Get().AssetPath(); }
	static bool IsSceneSelected() { return VansEditorSelectionService::Get().IsSceneSelected(); }
};
}
