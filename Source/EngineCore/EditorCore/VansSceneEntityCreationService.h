#pragma once

#include "../EngineAPILayer/Public/IEngineEditorAPI.h"
#include "../SceneCore/VansSceneLocalVolumetricFogComponentConfig.h"
#include "../SceneCore/VansSceneParentReference.h"

#include <array>
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

    struct LocalVolumetricFogRequest
    {
        std::string baseName = "Local Volumetric Fog";
        std::array<float, 3> position = { 0.0f, 1.0f, 0.0f };
        std::array<float, 3> dimensions = { 20.0f, 2.0f, 20.0f };
        Vans::VansSceneLocalVolumetricFogComponentConfig settings;
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

    static Result CreateLocalVolumetricFog(
        Vans::EditorAPI::IEngineEditorAPI& editorAPI,
        const Vans::VansSceneDocument& document,
        Vans::VansSceneEditService& editService,
        const LocalVolumetricFogRequest& request);
};
}
