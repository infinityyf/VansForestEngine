#pragma once

// imgui.h must be included before ImGuizmo.h so that ImDrawList / ImGuiContext
// are already declared when ImGuizmo's inline function signatures are parsed.
#include "imgui.h"
#include "ImGuizmo.h"
#include "../EngineAPILayer/Public/IEngineEditorAPI.h"
#include "../EngineAPILayer/Public/ISceneInteractionEditorAPI.h"
#include "../RenderCore/VansCamera.h"

#include <string>

namespace Vans
{
    class VansSceneEditService;
}

// Requires External/GUI/ImGuizmo to be added to the project's include directories.
// In VS project properties: C/C++ -> General -> Additional Include Directories
//   $(SolutionDir)ForestEngine\External\GUI\ImGuizmo

namespace VansGraphics
{
	struct VansEditorDebugViewState;
    // ─────────────────────────────────────────────────────────────────────────
    //  Gizmo mode and space enumerations
    // ─────────────────────────────────────────────────────────────────────────
    enum class GizmoMode
    {
        Translate = 0,
        Rotate    = 1,
        Scale     = 2,
    };

    enum class GizmoSpace
    {
        World = 0,
        Local = 1,
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  VansGizmos
    //
    //  Thin wrapper around ImGuizmo. Transform writes go
    //  through IEngineEditorAPI; document sync remains in the editor layer.
    // ─────────────────────────────────────────────────────────────────────────
    class VansGizmos
    {
    public:
        GizmoMode  m_Mode  = GizmoMode::Translate;
        GizmoSpace m_Space = GizmoSpace::World;

        // Draw the 3-D manipulation handle for the entity selected in VansEditorSelection.
        // Must be called inside the ImGui "Scene" window AFTER ImGui::Image().
        // windowPos  – top-left of the Scene ImGui window in screen coords.
        // windowSize – size of the Scene ImGui window in screen coords.
        void Draw(Vans::EditorAPI::IEngineEditorAPI& api,
                  Vans::VansSceneEditService* sceneEdits,
                  VansCamera* camera, ImVec2 windowPos, ImVec2 windowSize,
				  const VansEditorDebugViewState& debugViewState);

        // Handle W / E / R / X / Escape hotkeys.
        // Call once per frame inside the Scene window.
        void HandleHotkeys();

    private:
        bool m_WasUsing = false;
        bool m_TransformEditPending = false;
        std::string m_TransformEditEntityGuid;
        Vans::EditorAPI::RuntimeTransformSnapshot m_BeforeLocalTransform;

        static ImGuizmo::OPERATION OperationFromMode(GizmoMode mode);
        void CancelPendingTransform(Vans::EditorAPI::ISceneInteractionEditorAPI& api);
        void FinishPendingTransform(Vans::EditorAPI::ISceneInteractionEditorAPI& api,
                                    Vans::VansSceneEditService& sceneEdits);
        void ResetPendingTransform();

    };

} // namespace VansGraphics
