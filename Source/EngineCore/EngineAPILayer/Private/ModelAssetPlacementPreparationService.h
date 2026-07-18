#pragma once

#include "EngineCommandContext.h"
#include "../Public/EngineDTOs.h"

namespace Vans::EditorAPI
{
class ModelAssetPlacementPreparationService
{
public:
    static ModelAssetPlacementPayload Prepare(
        const ModelAssetPlacementRequest& request,
        RuntimeSceneHandle scene,
        RuntimeRenderDeviceHandle device);
};
}
