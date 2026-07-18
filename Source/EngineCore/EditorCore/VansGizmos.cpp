#define IMGUI_DEFINE_MATH_OPERATORS
#include "VansGizmos.h"
#include "VansEditorWindow.h"
#include "VansEditorSelection.h"
#include "VansSceneEditService.h"
#include "imgui.h"
#include "../SceneCore/VansSceneDocument.h"
#include "../Util/VansInputManager.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <utility>

namespace VansGraphics
{


//  Helpers


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

void VansGizmos::SyncTransformToSceneDocument(const std::string& entityGuid,
                                              const Vans::EditorAPI::RuntimeTransformSnapshot& transform)
{
    if (entityGuid.empty()) return;

    Vans::VansSceneDocument* document = VansEditorWindow::GetSceneDocument();
    Vans::VansSceneEditService* editService = VansEditorWindow::GetSceneEditService();
    if (!document || !editService) return;

    const auto& root = document->Root();
    if (!root.contains("entities") || !root["entities"].is_array()) return;

    for (std::size_t entityIndex = 0; entityIndex < root["entities"].size(); ++entityIndex)
    {
        const auto& entity = root["entities"][entityIndex];
        if (entity.value("id", "") != entityGuid)
            continue;
        if (!entity.contains("components") || !entity["components"].is_array())
            return;

        for (std::size_t componentIndex = 0; componentIndex < entity["components"].size(); ++componentIndex)
        {
            const auto& component = entity["components"][componentIndex];
            if (component.value("type", "") != "Transform")
                continue;

            Vans::SceneJson data = {
                { "position", { transform.position.x, transform.position.y, transform.position.z } },
                { "rotation", { transform.rotationDegrees.x, transform.rotationDegrees.y, transform.rotationDegrees.z } },
                { "scale",    { transform.scale.x,    transform.scale.y,    transform.scale.z    } }
            };

            const std::string pointer = "/entities/" + std::to_string(entityIndex) +
                "/components/" + std::to_string(componentIndex) + "/data";
            editService->Set(pointer, std::move(data));
            return;
        }
        return;
    }
}

glm::vec3 ToGlm(const Vans::EditorAPI::Vec3& value)
{
    return glm::vec3(value.x, value.y, value.z);
}

Vans::EditorAPI::Vec3 ToEditorVec3(const glm::vec3& value)
{
    return { value.x, value.y, value.z };
}

glm::mat4 BuildModelMatrix(const Vans::EditorAPI::RuntimeTransformSnapshot& transform)
{
    glm::mat4 model(1.0f);
    const glm::vec3 position = ToGlm(transform.position);
    const glm::vec3 rotation = ToGlm(transform.rotationDegrees);
    const glm::vec3 scale = ToGlm(transform.scale);
    model = glm::translate(model, position);
    model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::scale(model, scale);
    return model;
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


//  VansGizmos::Draw


void VansGizmos::Draw(Vans::EditorAPI::IEngineEditorAPI& api,
                      VansCamera* camera,
                      ImVec2      windowPos,
                      ImVec2      windowSize)
{
    if (!camera) return;

    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(windowPos.x, windowPos.y, windowSize.x, windowSize.y);

    const auto probeSettings = api.GetReflectionProbeSettings();
    if (probeSettings.available && probeSettings.editor.showProbeGizmos)
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
        const auto& probes = probeSettings.probes;
        const auto& editor = probeSettings.editor;
        const glm::vec3 cameraRight = glm::normalize(glm::vec3(glm::inverse(camera->GetViewMatrix())[0]));
        for (int i = 0; i < (int)probes.size(); ++i)
        {
            const auto& probe = probes[i];
            if (!probe.enabled || probe.type == 2) continue;
            ImU32 color = probe.type == 1 ? IM_COL32(255,40,220,220) :
                (probe.shape == 1 ? IM_COL32(30,220,255,220) : IM_COL32(40,255,100,220));
            const bool selected = editor.selectedProbeIndex == i;
            if (selected) color = IM_COL32(255,255,255,255);
            const float thickness = selected ? 2.5f : 1.5f;
            const glm::vec3 boxMin = ToGlm(probe.boxMin);
            const glm::vec3 boxMax = ToGlm(probe.boxMax);
            const glm::vec3 position = ToGlm(probe.position);
            const glm::vec3 capturePosition = ToGlm(probe.capturePosition);
            if (probe.shape == 1)
            {
                if (editor.showInfluenceVolumes)
                    drawBox(boxMin, boxMax, color, thickness);
                if (editor.showBlendVolumes)
                    drawBox(boxMin + glm::vec3(probe.blendDistance), boxMax - glm::vec3(probe.blendDistance), color, 1.0f);
            }
            else
            {
                ImVec2 center, edge;
                if (project(position, center) && project(position + cameraRight * probe.radius, edge))
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
            if (project(capturePosition, capture))
            {
                drawList->AddCircleFilled(capture, selected ? 5.0f : 3.5f, color);
                drawList->AddLine(ImVec2(capture.x - 7, capture.y), ImVec2(capture.x + 7, capture.y), color, 1.0f);
                drawList->AddLine(ImVec2(capture.x, capture.y - 7), ImVec2(capture.x, capture.y + 7), color, 1.0f);
            }
        }
    }

    const std::string selectedGuid = Vans::VansEditorSelection::EntityGuid();
    auto transform = api.GetRuntimeTransform(selectedGuid);
    if (!transform.available)  return;


    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(windowPos.x, windowPos.y,
                      windowSize.x, windowSize.y);


    glm::mat4 view = camera->GetViewMatrix();
    glm::mat4 proj = camera->GetProjectiveMatrix();

    // ImGuizmo renders through ImGui's screen-space draw list; pass the
    // projection matrix exactly as the engine stores it (Vulkan Y-flip included).
    // Do NOT negate proj[1][1] here 鈥?ImGuizmo's screen-space math already
    // compensates, and negating it causes the visible Y-axis flip.


    glm::mat4 modelMatrix = BuildModelMatrix(transform);


    ImGuizmo::OPERATION op    = OperationFromMode(m_Mode);
    ImGuizmo::MODE      space = (m_Space == GizmoSpace::World)
                                ? ImGuizmo::WORLD
                                : ImGuizmo::LOCAL;


    float delta[16] = {};
    bool changed = ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(proj),
        op, space,
        glm::value_ptr(modelMatrix),
        delta
    );


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
            transform.position = ToEditorVec3(pos);
            break;
        case GizmoMode::Scale:
            transform.scale = ToEditorVec3(scale);
            break;
        case GizmoMode::Rotate:
            transform.rotationDegrees = ToEditorVec3(rotDeg);
            break;
        }

        Vans::EditorAPI::RuntimeTransformEdit edit;
        edit.entityGuid = selectedGuid;
        edit.position = transform.position;
        edit.rotationDegrees = transform.rotationDegrees;
        edit.scale = transform.scale;
        edit.writePosition = m_Mode == GizmoMode::Translate;
        edit.writeRotation = m_Mode == GizmoMode::Rotate;
        edit.writeScale = m_Mode == GizmoMode::Scale;
        api.ApplyRuntimeTransform(edit);

        m_PendingDocumentSync = true;
        m_PendingDocumentSyncEntityGuid = selectedGuid;
        m_PendingDocumentSyncTransform = transform;
    }

    const bool isUsing = ImGuizmo::IsUsing();
    if ((m_WasUsing && !isUsing) || (changed && !isUsing))
    {
        if (m_PendingDocumentSync)
        {
            SyncTransformToSceneDocument(m_PendingDocumentSyncEntityGuid, m_PendingDocumentSyncTransform);
            m_PendingDocumentSync = false;
            m_PendingDocumentSyncEntityGuid.clear();
            m_PendingDocumentSyncTransform = {};
        }
    }
    m_WasUsing = isUsing;
}


//  VansGizmos::TryPickObject


void VansGizmos::TryPickObject(Vans::EditorAPI::IEngineEditorAPI& api,
                                VansCamera* camera,
                                ImVec2      mousePos,
                                ImVec2      windowPos,
                                ImVec2      windowSize)
{
    if (!camera) return;
    if (windowSize.x <= 0.0f || windowSize.y <= 0.0f) return;

    // Mouse 鈫?NDC in [-1, 1]
    float ndcX = 2.0f * (mousePos.x - windowPos.x) / windowSize.x - 1.0f;
    float ndcY = 1.0f - 2.0f * (mousePos.y - windowPos.y) / windowSize.y;

    glm::vec3 rayOrigin, rayDir;
    UnprojectRay(camera, ndcX, ndcY, rayOrigin, rayDir);

    Vans::EditorAPI::Ray ray;
    ray.origin = ToEditorVec3(rayOrigin);
    ray.direction = ToEditorVec3(rayDir);
    const std::string entityGuid = api.PickRuntimeEntity(ray);
    if (!entityGuid.empty())
        Vans::VansEditorSelection::SelectEntity(entityGuid);
}



//  VansGizmos::HandleHotkeys


void VansGizmos::HandleHotkeys()
{
    // Only fire when no text widget is active (avoids conflicts with input fields)
    if (ImGui::GetIO().WantCaptureKeyboard) return;

    auto& input = Vans::VansInputManager::Get();

    if (input.IsKeyPressed(GLFW_KEY_W)) m_Mode  = GizmoMode::Translate;
    if (input.IsKeyPressed(GLFW_KEY_E)) m_Mode  = GizmoMode::Rotate;
    if (input.IsKeyPressed(GLFW_KEY_R)) m_Mode  = GizmoMode::Scale;

    if (input.IsKeyPressed(GLFW_KEY_X))
        m_Space = (m_Space == GizmoSpace::World) ? GizmoSpace::Local : GizmoSpace::World;

    if (input.IsKeyPressed(GLFW_KEY_ESCAPE))
        Vans::VansEditorSelection::Clear();
}

} // namespace VansGraphics

