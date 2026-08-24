#pragma once

#include "../EngineAPILayer/Public/IEngineEditorAPI.h"
#include "../SceneCore/VansSceneParentReference.h"

#include <optional>
#include <string>

namespace Vans
{
class VansSceneDocument;
class VansSceneEditService;
}

namespace VansGraphics
{
class VansSceneEntityCreationService
{
public:
    struct EmptyObjectRequest
    {
        std::optional<Vans::VansSceneParentReference> parent;
        std::string baseName = "Empty Object";
    };

    struct Result
    {
        bool success = false;
        bool runtimeChangeApplied = false;
        std::string entityGuid;
        std::string message;

        explicit operator bool() const { return success; }
    };

    static Result CreateEmptyObject(
        Vans::EditorAPI::IEngineEditorAPI& editorAPI,
        const Vans::VansSceneDocument& document,
        Vans::VansSceneEditService& editService,
        const EmptyObjectRequest& request);
};
}
