#pragma once

#include "EngineCommandContext.h"
#include "../Public/EngineDTOs.h"

namespace Vans::EditorAPI
{
class IEngineEditorAPI;

class ModelAssetPlacementPreparationService
{
public:
    static ModelAssetPlacementPayload Prepare(
        const ModelAssetPlacementRequest& request,
        IEngineEditorAPI& editorAPI,
        RuntimeSceneHandle scene);
};
}
