#define IMGUI_DEFINE_MATH_OPERATORS
#include "VansGizmos.h"
#include "imgui.h"
#include "../RenderCore/VulkanCore/VansMesh.h"
#include "../Util/VansInputManager.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

namespace VansGraphics
{

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────

ImGuizmo::OPERATION VansGizmos::OperationFromMode(GizmoMode mode)
{
    switch (mode)
    {
    case GizmoMode::Translate: return ImGuizmo::TRANSLATE;
    case GizmoMode::Rotate:    return ImGuizmo::ROTATE;
    case GizmoMode::Scale:     return ImGuizmo::SCALE;
    default:                   return ImGuizmo::TRANSLATE;
    }
}

void VansGizmos::UnprojectRay(VansCamera*  camera,
                               float        ndcX,
                               float        ndcY,
                               glm::vec3&   outOrigin,
                               glm::vec3&   outDir)
{
    glm::mat4 view = camera->GetViewMatrix();
    glm::mat4 proj = camera->GetProjectiveMatrix();

    // GetProjectiveMatrix returns GLM clip coordinates with +Y up.
    glm::mat4 invPV = glm::inverse(proj * view);

    // Vulkan depth range is [0, 1]
    glm::vec4 nearPt = invPV * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 farPt  = invPV * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    nearPt /= nearPt.w;
    farPt  /= farPt.w;

    outOrigin = glm::vec3(nearPt);
    outDir    = glm::normalize(glm::vec3(farPt) - glm::vec3(nearPt));
}

float VansGizmos::RaySphereIntersect(const glm::vec3& ro,
                                      const glm::vec3& rd,
                                      const glm::vec3& center,
                                      float            radius)
{
    glm::vec3 oc = ro - center;
    float b = glm::dot(oc, rd);
    float c = glm::dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0f) return -1.0f;
    float sqrtDisc = glm::sqrt(disc);
    float t0 = -b - sqrtDisc;
    float t1 = -b + sqrtDisc;
    if (t0 > 0.0f) return t0;
    if (t1 > 0.0f) return t1;
    return -1.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
//  VansGizmos::Draw
// ─────────────────────────────────────────────────────────────────────────────

void VansGizmos::Draw(VansScene*  scene,
                      VansCamera* camera,
                      ImVec2      windowPos,
                      ImVec2      windowSize)
{
    if (!scene || !camera) return;

    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(windowPos.x, windowPos.y, windowSize.x, windowSize.y);

    auto* probeSystem = scene->GetReflectionProbeSystem();
    if (probeSystem && probeSystem->GetEditorState().showProbeGizmos)
    {
        const glm::mat4 viewProjection = camera->GetProjectiveMatrix() * camera->GetViewMatrix();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        auto project = [&](const glm::vec3& world, ImVec2& screen) -> bool
        {
            glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
            if (clip.w <= 1e-4f) return false;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            screen = ImVec2(windowPos.x + (ndc.x * 0.5f + 0.5f) * windowSize.x,
                windowPos.y + (-ndc.y * 0.5f + 0.5f) * windowSize.y);
            return ndc.z >= 0.0f && ndc.z <= 1.0f;
        };
        auto drawBox = [&](const glm::vec3& bmin, const glm::vec3& bmax, ImU32 color, float thickness)
        {
            glm::vec3 corners[8] = {
                {bmin.x,bmin.y,bmin.z},{bmax.x,bmin.y,bmin.z},{bmax.x,bmax.y,bmin.z},{bmin.x,bmax.y,bmin.z},
                {bmin.x,bmin.y,bmax.z},{bmax.x,bmin.y,bmax.z},{bmax.x,bmax.y,bmax.z},{bmin.x,bmax.y,bmax.z} };
            const int edges[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
            for (const auto& edge : edges)
            {
                ImVec2 a, b;
                if (project(corners[edge[0]], a) && project(corners[edge[1]], b)) drawList->AddLine(a, b, color, thickness);
            }
        };
        const auto& probes = probeSystem->GetProbes();
        const auto& editor = probeSystem->GetEditorState();
        const glm::vec3 cameraRight = glm::normalize(glm::vec3(glm::inverse(camera->GetViewMatrix())[0]));
        for (int i = 0; i < (int)probes.size(); ++i)
        {
            const auto& probe = probes[i];
            if (!probe.enabled || probe.type == ReflectionProbeType::Sky) continue;
            ImU32 color = probe.portal ? IM_COL32(255,220,40,220) :
                (probe.type == ReflectionProbeType::Realtime ? IM_COL32(255,40,220,220) :
                (probe.shape == ReflectionProbeShape::Box ? IM_COL32(30,220,255,220) : IM_COL32(40,255,100,220)));
            const bool selected = editor.selectedProbeIndex == i;
            if (selected) color = IM_COL32(255,255,255,255);
            const float thickness = selected ? 2.5f : 1.5f;
            if (probe.shape == ReflectionProbeShape::Box)
            {
                if (editor.showInfluenceVolumes)
                    drawBox(probe.boxMin, probe.boxMax, color, thickness);
                if (editor.showBlendVolumes)
                    drawBox(probe.boxMin + glm::vec3(probe.blendDistance), probe.boxMax - glm::vec3(probe.blendDistance), color, 1.0f);
            }
            else
            {
                ImVec2 center, edge;
                if (project(probe.position, center) && project(probe.position + cameraRight * probe.radius, edge))
                {
                    const float dx = edge.x - center.x;
                    const float dy = edge.y - center.y;
                    const float radius = std::sqrt(dx * dx + dy * dy);
                    if (editor.showInfluenceVolumes)
                        drawList->AddCircle(center, radius, color, 48, thickness);
                    if (editor.showBlendVolumes)
                        drawList->AddCircle(center, radius * std::max(0.0f, 1.0f - probe.blendDistance / std::max(probe.radius, 0.001f)), color, 48, 1.0f);
                }
            }
            ImVec2 capture;
            if (project(probe.capturePosition, capture))
            {
                drawList->AddCircleFilled(capture, selected ? 5.0f : 3.5f, color);
                drawList->AddLine(ImVec2(capture.x - 7, capture.y), ImVec2(capture.x + 7, capture.y), color, 1.0f);
                drawList->AddLine(ImVec2(capture.x, capture.y - 7), ImVec2(capture.x, capture.y + 7), color, 1.0f);
            }
        }
        if (editor.showRegions)
        {
            for (const auto& region : probeSystem->GetRegions())
            {
                const ImU32 color = region.type == ProbeRegionType::Exterior ? IM_COL32(40,180,80,100) :
                    (region.type == ProbeRegionType::Corridor ? IM_COL32(255,170,30,150) : IM_COL32(40,170,255,150));
                drawBox(region.boundsMin, region.boundsMax, color, 1.0f);
                ImVec2 label;
                if (project(region.centroid, label)) drawList->AddText(label, color, ("Region " + std::to_string(region.id)).c_str());
            }
        }
        if (editor.showPlacementGrid)
        {
            const auto& grid = probeSystem->GetPlacementGrid();
            const uint32_t stride = std::max(1u, (uint32_t)(grid.cells.size() / 2048u));
            for (uint32_t i = 0; i < grid.cells.size(); i += stride)
            {
                const auto& cell = grid.cells[i];
                if (cell.cellClass == ProbeCellClass::Empty || cell.cellClass == ProbeCellClass::Unknown) continue;
                const uint32_t x = i % grid.dimensions.x;
                const uint32_t yz = i / grid.dimensions.x;
                const uint32_t y = yz % grid.dimensions.y;
                const uint32_t z = yz / grid.dimensions.y;
                const glm::vec3 bmin = grid.origin + glm::vec3(x,y,z) * grid.cellSize;
                ImU32 color = cell.cellClass == ProbeCellClass::Solid ? IM_COL32(255,50,50,90) :
                    (cell.cellClass == ProbeCellClass::Boundary ? IM_COL32(255,220,50,90) : IM_COL32(50,180,90,45));
                drawBox(bmin, bmin + glm::vec3(grid.cellSize), color, 0.75f);
            }
        }
    }

    VansRenderNode* node = scene->m_SelectedNode;
    if (!node)  return;

    // ── 1. Bind ImGuizmo to the current ImGui window ─────────────────────────
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(windowPos.x, windowPos.y,
                      windowSize.x, windowSize.y);

    // ── 2. Build camera matrices ──────────────────────────────────────────────
    glm::mat4 view = camera->GetViewMatrix();
    glm::mat4 proj = camera->GetProjectiveMatrix();

    // ImGuizmo renders through ImGui's screen-space draw list; pass the
    // projection matrix exactly as the engine stores it (Vulkan Y-flip included).
    // Do NOT negate proj[1][1] here — ImGuizmo's screen-space math already
    // compensates, and negating it causes the visible Y-axis flip.

    // ── 3. Current model matrix ───────────────────────────────────────────────
    VansTransform& tf = VansTransformStore::GetTransform(node->m_TransformID);
    glm::mat4 modelMatrix = tf.GetModelMatrix();

    // ── 4. Mode and space ─────────────────────────────────────────────────────
    ImGuizmo::OPERATION op    = OperationFromMode(m_Mode);
    ImGuizmo::MODE      space = (m_Space == GizmoSpace::World)
                                ? ImGuizmo::WORLD
                                : ImGuizmo::LOCAL;

    // ── 5. Draw + interact ────────────────────────────────────────────────────
    float delta[16] = {};
    bool changed = ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(proj),
        op, space,
        glm::value_ptr(modelMatrix),
        delta
    );

    // ── 6. Write back decomposed T / R / S ────────────────────────────────────
    if (changed)
    {
        glm::vec3 pos, rotDeg, scale;
        ImGuizmo::DecomposeMatrixToComponents(
            glm::value_ptr(modelMatrix),
            glm::value_ptr(pos),
            glm::value_ptr(rotDeg),   // degrees, ZYX convention (matches engine GetModelMatrix)
            glm::value_ptr(scale)
        );

        // Only write back the components that the active mode actually changed.
        // Writing all three every frame causes the other components to be
        // re-derived from the (potentially singular) Euler decomposition and
        // produces cascading jitter when only translating or scaling.
        switch (m_Mode)
        {
        case GizmoMode::Translate:
            tf.m_Position = pos;
            break;
        case GizmoMode::Scale:
            tf.m_Scale = scale;
            break;
        case GizmoMode::Rotate:
            tf.m_Rotation = rotDeg;
            break;
        }

        // Mark dirty so UpdateTransformRenderData() re-uploads the GPU SSBO.
        VansTransformStore::TransformIDToTransformDirty[node->m_TransformID] = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  VansGizmos::TryPickObject
// ─────────────────────────────────────────────────────────────────────────────

void VansGizmos::TryPickObject(VansScene*  scene,
                                VansCamera* camera,
                                ImVec2      mousePos,
                                ImVec2      windowPos,
                                ImVec2      windowSize)
{
    if (!scene || !camera) return;
    if (windowSize.x <= 0.0f || windowSize.y <= 0.0f) return;

    // Mouse → NDC in [-1, 1]
    float ndcX = 2.0f * (mousePos.x - windowPos.x) / windowSize.x - 1.0f;
    float ndcY = 1.0f - 2.0f * (mousePos.y - windowPos.y) / windowSize.y;

    glm::vec3 rayOrigin, rayDir;
    UnprojectRay(camera, ndcX, ndcY, rayOrigin, rayDir);

    // Collect all candidate render nodes
    struct Candidate
    {
        VansRenderNode* node;
        float           t;
    };

    float         bestT    = FLT_MAX;
    VansRenderNode* bestNode = nullptr;

    auto testNode = [&](VansRenderNode* node)
    {
        if (!node || !node->m_Mesh) return;

        VansTransform& tf   = VansTransformStore::GetTransform(node->m_TransformID);
        glm::vec3      pos  = tf.m_Position;

        // ── Prefer exact bounds from CPU mesh data when available ─────────────
        const std::vector<float>& rawPos = node->m_Mesh->GetMeshRawPositionData();
        float radius = 1.0f;

        if (rawPos.size() >= 3)
        {
            // Compute local AABB from raw XYZ triples and convert to world-space sphere
            glm::vec3 localMin( FLT_MAX), localMax(-FLT_MAX);
            for (size_t i = 0; i + 2 < rawPos.size(); i += 3)
            {
                glm::vec3 v(rawPos[i], rawPos[i + 1], rawPos[i + 2]);
                localMin = glm::min(localMin, v);
                localMax = glm::max(localMax, v);
            }
            glm::vec3 halfExt = (localMax - localMin) * 0.5f * tf.m_Scale;
            // World-space AABB center (approximate – ignores rotation)
            glm::vec3 localCenter = (localMin + localMax) * 0.5f;
            pos += localCenter * tf.m_Scale;
            radius = glm::length(halfExt);
        }
        else
        {
            // Fallback: sphere whose radius approximates the mesh extent via scale
            radius = glm::max(glm::length(tf.m_Scale) * 0.5f, 0.25f);
        }

        float t = RaySphereIntersect(rayOrigin, rayDir, pos, radius);
        if (t > 0.0f && t < bestT)
        {
            bestT    = t;
            bestNode = node;
        }
    };

    for (VansRenderNode* n : scene->m_OpaqueRenderNodes)      testNode(n);
    for (VansRenderNode* n : scene->m_TransParentRenderNodes) testNode(n);

    if (bestNode)
        scene->m_SelectedNode = bestNode;
}

// ─────────────────────────────────────────────────────────────────────────────
//  VansGizmos::HandleHotkeys
// ─────────────────────────────────────────────────────────────────────────────

void VansGizmos::HandleHotkeys(VansScene* scene)
{
    // Only fire when no text widget is active (avoids conflicts with input fields)
    if (ImGui::GetIO().WantCaptureKeyboard) return;

    auto& input = Vans::VansInputManager::Get();

    if (input.IsKeyPressed(GLFW_KEY_W)) m_Mode  = GizmoMode::Translate;
    if (input.IsKeyPressed(GLFW_KEY_E)) m_Mode  = GizmoMode::Rotate;
    if (input.IsKeyPressed(GLFW_KEY_R)) m_Mode  = GizmoMode::Scale;

    if (input.IsKeyPressed(GLFW_KEY_X))
        m_Space = (m_Space == GizmoSpace::World) ? GizmoSpace::Local : GizmoSpace::World;

    if (input.IsKeyPressed(GLFW_KEY_ESCAPE) && scene)
        scene->m_SelectedNode = nullptr;
}

} // namespace VansGraphics
