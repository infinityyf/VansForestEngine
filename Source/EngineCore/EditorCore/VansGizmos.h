#pragma once

// imgui.h must be included before ImGuizmo.h so that ImDrawList / ImGuiContext
// are already declared when ImGuizmo's inline function signatures are parsed.
#include "imgui.h"
#include "ImGuizmo.h"
#include "../RenderCore/VansScene.h"
#include "../RenderCore/VansCamera.h"

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
    //  Thin wrapper around ImGuizmo that integrates with VansScene selection
    //  and VansTransformStore.  All methods are called from VansSceneWindow.
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
        void Draw(VansScene* scene, VansCamera* camera,
                  ImVec2 windowPos, ImVec2 windowSize);

        // Screen-space left-click picking via ray–sphere test.
        // Call when LMB is pressed inside the Scene window and ImGuizmo is not active.
        void TryPickObject(VansScene* scene, VansCamera* camera,
                           ImVec2 mousePos,
                           ImVec2 windowPos, ImVec2 windowSize);

        // Handle W / E / R / X / Escape hotkeys.
        // Call once per frame inside the Scene window.
        void HandleHotkeys(VansScene* scene);

    private:
        static ImGuizmo::OPERATION OperationFromMode(GizmoMode mode);

        // Shoot a world-space ray from the camera through the given NDC pixel.
        // Returns origin (camera position) and normalised direction.
        static void UnprojectRay(VansCamera* camera,
                                 float ndcX, float ndcY,
                                 glm::vec3& outOrigin,
                                 glm::vec3& outDir);

        // Ray–sphere intersection.  Returns the smallest positive t, or -1 if no hit.
        static float RaySphereIntersect(const glm::vec3& ro, const glm::vec3& rd,
                                        const glm::vec3& center, float radius);
    };

} // namespace VansGraphics
