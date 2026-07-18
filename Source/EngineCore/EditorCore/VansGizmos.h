#pragma once

// imgui.h must be included before ImGuizmo.h so that ImDrawList / ImGuiContext
// are already declared when ImGuizmo's inline function signatures are parsed.
#include "imgui.h"
#include "ImGuizmo.h"
#include "../EngineAPILayer/Public/IEngineEditorAPI.h"
#include "../RenderCore/VansCamera.h"

#include <string>

// Requires External/GUI/ImGuizmo to be added to the project's include directories.
// In VS project properties: C/C++ -> General -> Additional Include Directories
//   $(SolutionDir)ForestEngine\External\GUI\ImGuizmo

namespace VansGraphics
{
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
    //  Thin wrapper around ImGuizmo. Runtime picking and transform writes go
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
                  VansCamera* camera, ImVec2 windowPos, ImVec2 windowSize);

        // Screen-space left-click picking via ray–sphere test.
        // Call when LMB is pressed inside the Scene window and ImGuizmo is not active.
        void TryPickObject(Vans::EditorAPI::IEngineEditorAPI& api,
                           VansCamera* camera, ImVec2 mousePos,
                           ImVec2 windowPos, ImVec2 windowSize);

        // Handle W / E / R / X / Escape hotkeys.
        // Call once per frame inside the Scene window.
        void HandleHotkeys();

    private:
        bool m_WasUsing = false;
        bool m_PendingDocumentSync = false;
        std::string m_PendingDocumentSyncEntityGuid;
        Vans::EditorAPI::RuntimeTransformSnapshot m_PendingDocumentSyncTransform;

        static ImGuizmo::OPERATION OperationFromMode(GizmoMode mode);
        static void SyncTransformToSceneDocument(const std::string& entityGuid,
                                                 const Vans::EditorAPI::RuntimeTransformSnapshot& transform);

        // Shoot a world-space ray from the camera through the given NDC pixel.
        // Returns origin (camera position) and normalised direction.
        static void UnprojectRay(VansCamera* camera,
                                 float ndcX, float ndcY,
                                 glm::vec3& outOrigin,
                                 glm::vec3& outDir);

        // Ray–sphere intersection.  Returns the smallest positive t, or -1 if no hit.
    };

} // namespace VansGraphics
