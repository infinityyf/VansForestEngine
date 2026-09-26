#pragma once

#include "../EngineAPILayer/Public/IRuntimeSceneEditorAPI.h"

#include <string>

namespace Vans
{
class VansSceneEditService;
}

namespace VansGraphics
{
class VansSceneAssetPlacementService
{
public:
    struct Result
    {
        bool success = false;
        std::string message;
		std::string entityGuid;

        explicit operator bool() const { return success; }
    };

    static Result PlaceModelAsset(
        Vans::EditorAPI::IRuntimeSceneEditorAPI& editorAPI,
        Vans::VansSceneEditService& editService,
        const std::string& assetGuid,
        const Vans::EditorAPI::Vec3& worldPosition);
};
}
