#define IMGUI_DEFINE_MATH_OPERATORS
#include "VansGizmos.h"
#include "VansEditorDebugViewState.h"
#include "VansEditorWindow.h"
#include "Windows/VansParticleDebugWindow.h"
#include "VansEditorSelectionService.h"
#include "VansSceneEditService.h"
#include "../EngineAPILayer/Public/IAnimationEditorAPI.h"
#include "../EngineAPILayer/Public/IGIEditorAPI.h"
#include "../EngineAPILayer/Public/IReflectionProbeEditorAPI.h"
#include "../EngineAPILayer/Public/IRenderEditorAPI.h"
#include "imgui.h"
#include "../Util/VansInputManager.h"
#include "../Util/VansLog.h"

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

void VansGizmos::ResetPendingTransform()
{
    m_TransformEditPending = false;
    m_TransformEditEntityGuid.clear();
    m_BeforeLocalTransform = {};
}

void VansGizmos::CancelPendingTransform(Vans::EditorAPI::ISceneInteractionEditorAPI& api)
{
    if (m_TransformEditPending && m_BeforeLocalTransform.available)
    {
        Vans::EditorAPI::RuntimeEntityPreviewChange rollback;
        rollback.hasTransform = true;
        rollback.transform.entityGuid = m_TransformEditEntityGuid;
        rollback.transform.space = Vans::EditorAPI::RuntimeTransformSpace::Local;
        rollback.transform.position = m_BeforeLocalTransform.position;
        rollback.transform.rotationDegrees = m_BeforeLocalTransform.rotationDegrees;
        rollback.transform.scale = m_BeforeLocalTransform.scale;
        api.ApplyRuntimeEntityPreviewChange(rollback);
    }
    ResetPendingTransform();
    m_WasUsing = false;
}

void VansGizmos::FinishPendingTransform(
    Vans::EditorAPI::ISceneInteractionEditorAPI& api,
    Vans::VansSceneEditService& sceneEdits)
{
    if (!m_TransformEditPending)
        return;

    const Vans::EditorAPI::RuntimeTransformSnapshot local = api.GetRuntimeTransform(
        m_TransformEditEntityGuid,
        Vans::EditorAPI::RuntimeTransformSpace::Local);
    if (!local.available)
    {
        VANS_LOG_WARN("[Gizmo] Final local transform is unavailable; restoring the drag origin");
        CancelPendingTransform(api);
        return;
    }

    const Vans::SceneEditResult result = sceneEdits.SetEntityTransform(
        m_TransformEditEntityGuid,
        local);
    if (!result)
    {
        VANS_LOG_WARN("[Gizmo] Scene transform commit failed: " << result.message);
        CancelPendingTransform(api);
        return;
    }
    ResetPendingTransform();
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

//  VansGizmos::Draw


void VansGizmos::Draw(Vans::EditorAPI::IEngineEditorAPI& api,
                      Vans::VansSceneEditService* sceneEdits,
                      VansCamera* camera,
                      ImVec2      windowPos,
                      ImVec2      windowSize,
					  const VansEditorDebugViewState& debugViewState)
{
    if (!camera) return;
	Vans::EditorAPI::IGIEditorAPI& giAPI = api;
	Vans::EditorAPI::IReflectionProbeEditorAPI& probeAPI = api;
	Vans::EditorAPI::IRenderEditorAPI& renderAPI = api;
	Vans::EditorAPI::ISceneInteractionEditorAPI& sceneInteractionAPI = api;

    ImGuizmo::SetID(0x53434E45); // "SCNE": Scene Transform Gizmo
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(windowPos.x, windowPos.y, windowSize.x, windowSize.y);

    // 反射探针 gizmo 只由 Reflection Probe 窗口驱动，窗口关闭时不拉取整份探针 DTO。
    if (VansEditorWindow::IsWindowOpen(VansEditorWindowId::ReflectionProbe))
    {
        const auto probeSettings = probeAPI.GetReflectionProbeSettings();
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
    }

    // GI gizmo 同样由 GI Inspector 窗口驱动，避免 Scene 视口常驻复制 GI 调试数据。
    if (VansEditorWindow::IsWindowOpen(VansEditorWindowId::GI))
    {
        const auto giSettings = giAPI.GetGISettings();
		if (giSettings.available && !giSettings.regions.empty() &&
			(giSettings.showProbeGizmos || giSettings.showProbeVolume))
		{
			const auto& selectedRegion = giSettings.regions[std::min<std::uint32_t>(
				giSettings.selectedRegionIndex,
				static_cast<std::uint32_t>(giSettings.regions.size() - 1u))];
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
                    if (project(corners[edge[0]], a) && project(corners[edge[1]], b))
                        drawList->AddLine(a, b, color, thickness);
                }
            };

			const glm::vec3 volumeMin = ToGlm(selectedRegion.volumeMin);
			const glm::vec3 volumeMax = ToGlm(selectedRegion.volumeMax);
            if (giSettings.showProbeVolume)
                drawBox(volumeMin, volumeMax, IM_COL32(255, 180, 40, 220), 1.5f);

            std::shared_ptr<const Vans::EditorAPI::GIProbeDebugSnapshot> giProbeDebug;
            if (giSettings.showProbeGizmos)
                giProbeDebug = giAPI.GetGIProbeDebugSnapshot();
            if (giSettings.showProbeGizmos && giSettings.placement.enabled && giProbeDebug && giProbeDebug->available)
            {
                for (const auto& probe : giProbeDebug->probes)
                {
                    ImVec2 screen;
                    if (!project(ToGlm(probe.position), screen) || screen.x < windowPos.x || screen.y < windowPos.y ||
                        screen.x > windowPos.x + windowSize.x || screen.y > windowPos.y + windowSize.y)
                        continue;
                    drawList->AddCircleFilled(screen, 3.0f, IM_COL32(255, 210, 80, 230), 8);
                }
			}
			else if (giSettings.showProbeGizmos && !giSettings.placement.enabled &&
					 selectedRegion.gridDimensions.x > 0.0f && selectedRegion.gridDimensions.y > 0.0f && selectedRegion.gridDimensions.z > 0.0f &&
					 selectedRegion.probeSpacing > 0.0f)
            {
                const uint32_t stride = std::max(1u, giSettings.gizmoStride);
                const glm::uvec3 gridDimensions(
					static_cast<uint32_t>(selectedRegion.gridDimensions.x),
					static_cast<uint32_t>(selectedRegion.gridDimensions.y),
					static_cast<uint32_t>(selectedRegion.gridDimensions.z));
                const glm::vec3 spacing(selectedRegion.probeSpacing);
                const ImU32 baseColor = IM_COL32(255, 210, 80, 220);
                const ImU32 axisColor = IM_COL32(80, 220, 255, 240);

                for (uint32_t z = 0; z < gridDimensions.z; z += stride)
                for (uint32_t y = 0; y < gridDimensions.y; y += stride)
                for (uint32_t x = 0; x < gridDimensions.x; x += stride)
                {
                    const glm::vec3 probePos = volumeMin + (glm::vec3(x, y, z) + glm::vec3(0.5f)) * spacing;
                    ImVec2 screen;
                    if (!project(probePos, screen))
                        continue;

                    const bool axisProbe = x == 0 || y == 0 || z == 0;
                    const float radius = axisProbe ? 3.0f : 2.0f;
                    const ImU32 color = axisProbe ? axisColor : baseColor;
                    drawList->AddCircleFilled(screen, radius, color, 10);
                }
            }
        }
    }

    if (debugViewState.hiZCullDebugVisualization)
    {
        const auto hizSnapshot = renderAPI.GetMainCameraHiZCullDebugSnapshot();
        if (hizSnapshot.available && hizSnapshot.enabled && !hizSnapshot.culledNodes.empty())
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
            auto drawObb = [&](const Vans::EditorAPI::MainCameraHiZCulledNodeSnapshot& node)
            {
                const glm::vec3 center = ToGlm(node.center);
                const glm::vec3 axisX = ToGlm(node.axisXHalf);
                const glm::vec3 axisY = ToGlm(node.axisYHalf);
                const glm::vec3 axisZ = ToGlm(node.axisZHalf);
                const glm::vec3 corners[8] = {
                    center - axisX - axisY - axisZ,
                    center + axisX - axisY - axisZ,
                    center + axisX + axisY - axisZ,
                    center - axisX + axisY - axisZ,
                    center - axisX - axisY + axisZ,
                    center + axisX - axisY + axisZ,
                    center + axisX + axisY + axisZ,
                    center - axisX + axisY + axisZ,
                };
                const int edges[12][2] = {
                    {0,1},{1,2},{2,3},{3,0},
                    {4,5},{5,6},{6,7},{7,4},
                    {0,4},{1,5},{2,6},{3,7}
                };
                constexpr ImU32 kCulledColor = IM_COL32(255, 80, 40, 230);
                for (const auto& edge : edges)
                {
                    ImVec2 a, b;
                    if (project(corners[edge[0]], a) && project(corners[edge[1]], b))
                        drawList->AddLine(a, b, kCulledColor, 1.8f);
                }
            };

            for (const auto& node : hizSnapshot.culledNodes)
                drawObb(node);
        }
    }

	VansEditorWindow::DrawParticleDebugSceneOverlay(api,
		camera->GetProjectiveMatrix() * camera->GetViewMatrix(), windowPos, windowSize);

    if (debugViewState.skeletonDebugGizmos)
    {
        const std::string filterGuid = debugViewState.skeletonDebugSelectedOnly
            ? Vans::VansEditorSelectionService::Get().EntityGuid()
            : std::string();
		const auto skeletonSnapshot =
			static_cast<Vans::EditorAPI::IAnimationEditorAPI&>(api).GetSkeletonDebugSnapshot(filterGuid);
        if (skeletonSnapshot.available)
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

            constexpr ImU32 kBoneColor = IM_COL32(80, 220, 255, 230);
            constexpr ImU32 kJointColor = IM_COL32(255, 240, 120, 240);
            constexpr ImU32 kRootColor = IM_COL32(255, 120, 80, 250);
            constexpr ImU32 kSourceBoneColor = IM_COL32(220, 120, 255, 210);
            constexpr ImU32 kSourceJointColor = IM_COL32(255, 190, 255, 230);
            constexpr ImU32 kSourceRootColor = IM_COL32(255, 120, 210, 250);
            for (const auto& rig : skeletonSnapshot.rigs)
            {
                if (rig.retargetSource && !debugViewState.skeletonDebugShowRetargetSource)
                    continue;
                const ImU32 boneColor = rig.retargetSource ? kSourceBoneColor : kBoneColor;
                const ImU32 jointColor = rig.retargetSource ? kSourceJointColor : kJointColor;
                const ImU32 rootColor = rig.retargetSource ? kSourceRootColor : kRootColor;
                for (int i = 0; i < static_cast<int>(rig.bones.size()); ++i)
                {
                    const auto& bone = rig.bones[i];
                    const glm::vec3 boneWorld = ToGlm(bone.worldPosition);
                    ImVec2 boneScreen;
                    if (!project(boneWorld, boneScreen))
                        continue;

                    if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int>(rig.bones.size()))
                    {
                        ImVec2 parentScreen;
                        if (project(ToGlm(rig.bones[bone.parentIndex].worldPosition), parentScreen))
                            drawList->AddLine(parentScreen, boneScreen, boneColor, rig.retargetSource ? 1.4f : 1.8f);
                    }

                    drawList->AddCircleFilled(boneScreen, bone.parentIndex < 0 ? 4.0f : 2.4f,
                        bone.parentIndex < 0 ? rootColor : jointColor, 10);
                    if (debugViewState.skeletonDebugShowNames)
                        drawList->AddText(ImVec2(boneScreen.x + 5.0f, boneScreen.y - 5.0f),
                            rig.retargetSource ? IM_COL32(255, 220, 255, 230) : IM_COL32(235, 245, 255, 230),
                            bone.name.c_str());
                }
            }
        }
    }

    const std::string selectedGuid = Vans::VansEditorSelectionService::Get().EntityGuid();
    if (m_TransformEditPending &&
        (selectedGuid.empty() || selectedGuid != m_TransformEditEntityGuid || !sceneEdits))
    {
        CancelPendingTransform(sceneInteractionAPI);
    }
    auto transform = sceneInteractionAPI.GetRuntimeTransform(
        selectedGuid, Vans::EditorAPI::RuntimeTransformSpace::World);
    if (!transform.available || !sceneEdits) return;


    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(windowPos.x, windowPos.y,
                      windowSize.x, windowSize.y);


    glm::mat4 view = camera->GetViewMatrix();
    glm::mat4 proj = camera->GetProjectiveMatrix();

    // ImGuizmo renders through ImGui's screen-space draw list; pass the
    // projection matrix exactly as the engine stores it (Vulkan Y-flip included).
    // Do not negate proj[1][1] here; ImGuizmo's screen-space math already
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
        if (!m_TransformEditPending)
        {
            m_BeforeLocalTransform = sceneInteractionAPI.GetRuntimeTransform(
                selectedGuid,
                Vans::EditorAPI::RuntimeTransformSpace::Local);
            if (!m_BeforeLocalTransform.available)
                return;
            m_TransformEditPending = true;
            m_TransformEditEntityGuid = selectedGuid;
        }

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
        edit.space = Vans::EditorAPI::RuntimeTransformSpace::World;
        edit.position = transform.position;
        edit.rotationDegrees = transform.rotationDegrees;
        edit.scale = transform.scale;
        edit.writePosition = m_Mode == GizmoMode::Translate;
        edit.writeRotation = m_Mode == GizmoMode::Rotate;
        edit.writeScale = m_Mode == GizmoMode::Scale;
        Vans::EditorAPI::RuntimeEntityPreviewChange preview;
        preview.hasTransform = true;
        preview.transform = edit;
        if (!sceneInteractionAPI.ApplyRuntimeEntityPreviewChange(preview))
        {
            VANS_LOG_WARN("[Gizmo] Runtime transform preview failed; restoring the drag origin");
            CancelPendingTransform(sceneInteractionAPI);
            return;
        }
    }

    const bool isUsing = ImGuizmo::IsUsing();
    if ((m_WasUsing && !isUsing) || (changed && !isUsing))
    {
        if (m_TransformEditPending)
            FinishPendingTransform(sceneInteractionAPI, *sceneEdits);
    }
    m_WasUsing = isUsing;
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
        Vans::VansEditorSelectionService::Get().Clear("GizmoDelete");
}

} // namespace VansGraphics
