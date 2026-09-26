#pragma once

#include "EngineCommandContext.h"
#include "../Public/EngineDTOs.h"

namespace Vans::EditorAPI
{
class EngineAPIImpl;

struct ProjectMeshLoadRequest
{
    std::string meshName;
    std::string sourcePath;
};

struct ProjectMeshLoadResult
{
    bool loaded = false;
    bool available = false;
};

struct ProjectSubmeshSnapshot
{
    std::string sourceNodeName;
    std::string materialName;
    std::string diffuseTexturePath;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
};

struct ProjectMeshSnapshot
{
    bool available = false;
    bool isMultiMesh = false;
    std::vector<ProjectSubmeshSnapshot> submeshes;
};

class ModelAssetPlacementPreparationService
{
public:
    static ModelAssetPlacementPayload Prepare(
        const ModelAssetPlacementRequest& request,
        EngineAPIImpl& editorAPI,
        RuntimeSceneHandle scene);
};
}
