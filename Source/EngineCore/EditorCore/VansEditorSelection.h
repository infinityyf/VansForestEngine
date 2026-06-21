#pragma once

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
        s_EntityGuid = std::move(entityGuid);
        s_AssetPath.clear();
		s_SceneSelected = false;
    }

	static void SelectScene()
	{
		s_EntityGuid.clear();
		s_AssetPath.clear();
		s_SceneSelected = true;
	}

    static void SelectAsset(std::filesystem::path assetPath)
    {
        s_AssetPath = std::move(assetPath);
        s_EntityGuid.clear();
		s_SceneSelected = false;
    }

    static void Clear()
    {
        s_EntityGuid.clear();
        s_AssetPath.clear();
		s_SceneSelected = false;
    }

    static const std::string& EntityGuid() { return s_EntityGuid; }
    static const std::filesystem::path& AssetPath() { return s_AssetPath; }
	static bool IsSceneSelected() { return s_SceneSelected; }

private:
    inline static std::string s_EntityGuid;
    inline static std::filesystem::path s_AssetPath;
	inline static bool s_SceneSelected = false;
};
}
