#include "VansSceneWindow.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "ImGuizmo.h"
#include <cmath>
#include <algorithm>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "../VansEditorSelection.h"
#include "../VansEditorObjectReference.h"
#include "../VansSceneAssetPlacementService.h"
#include "../VansEditorWindow.h"
#include "../VansSceneEditService.h"
#include "../../RuntimeUI/Public/VansUISystem.h"
#include "../../VansTimer.h"
#include "../../Util/VansLog.h"
#include "VansMotionMatchingDebugWindow.h"

void VansGraphics::VansSceneWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
    // -------------------------------------------------------------------------
    // 3. Scene 窗口 (游戏视图)
    // -------------------------------------------------------------------------
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Scene");
		Vans::EditorAPI::FSRSettingsSnapshot fsrSettings = editorAPI.GetFSRSettings();

        // ImGuizmo must be told a new frame is starting once per ImGui frame.
        ImGuizmo::BeginFrame();

        // ── Gizmo mode toolbar ────────────────────────────────────────────────
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
            ImGui::Spacing();
            ImGui::Indent(4.0f);

            if (ImGui::RadioButton("T (Translate)##gizmo",
                m_Gizmos.m_Mode == GizmoMode::Translate))
                m_Gizmos.m_Mode = GizmoMode::Translate;
            ImGui::SameLine();

            if (ImGui::RadioButton("R (Rotate)##gizmo",
                m_Gizmos.m_Mode == GizmoMode::Rotate))
                m_Gizmos.m_Mode = GizmoMode::Rotate;
            ImGui::SameLine();

            if (ImGui::RadioButton("S (Scale)##gizmo",
                m_Gizmos.m_Mode == GizmoMode::Scale))
                m_Gizmos.m_Mode = GizmoMode::Scale;
            ImGui::SameLine();

            ImGui::Text("|");
            ImGui::SameLine();

            bool isWorld = (m_Gizmos.m_Space == GizmoSpace::World);
            if (ImGui::Checkbox("World##gizmo", &isWorld))
                m_Gizmos.m_Space = isWorld ? GizmoSpace::World : GizmoSpace::Local;

			ImGui::SameLine();
			ImGui::Text("|");
			ImGui::SameLine();
			const char* fsrModeNames[] = { "Viewport", "Native AA", "Quality 1.5x", "Balanced 1.7x", "Performance 2x" };
			int fsrMode = static_cast<int>(fsrSettings.mode);
			ImGui::SetNextItemWidth(125.0f);
			if (ImGui::Combo("##fsrMode", &fsrMode, fsrModeNames, IM_ARRAYSIZE(fsrModeNames)))
			{
				fsrSettings.mode = static_cast<Vans::EditorAPI::FSRUpscaleMode>(fsrMode);
				editorAPI.SetFSRSettings(fsrSettings.mode, fsrSettings.sharpness);
			}

			ImGui::SameLine();
			ImGui::SetNextItemWidth(90.0f);
			if (ImGui::SliderFloat("RCAS##fsr", &fsrSettings.sharpness, 0.0f, 1.0f, "%.2f"))
				editorAPI.SetFSRSettings(fsrSettings.mode, fsrSettings.sharpness);
			ImGui::SameLine();
			bool debugView = fsrSettings.debugViewEnabled;
			if (ImGui::Checkbox("FSR Debug##fsr", &debugView))
				editorAPI.SetFSRDebugViewEnabled(debugView);

			ImGui::SameLine();
			ImGui::TextDisabled("%ux%u -> %ux%u  bias %.2f",
				fsrSettings.renderWidth,
				fsrSettings.renderHeight,
				fsrSettings.outputWidth,
				fsrSettings.outputHeight,
				fsrSettings.mipBias);
			if (!fsrSettings.contextReady || !fsrSettings.lastError.empty())
				ImGui::TextDisabled("FSR status: %s (code %u)",
					fsrSettings.lastError.empty() ? "context not ready" : fsrSettings.lastError.c_str(),
					fsrSettings.lastDispatchReturnCode);

            ImGui::Unindent(4.0f);
            ImGui::Spacing();
            ImGui::PopStyleVar();
        }

        // 获取当前窗口可用区域大小
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();

		// Match the output to the aspect-fitted Scene image in framebuffer pixels.
		// Debouncing avoids rebuilding the FSR context for every pixel while a dock
		// splitter is actively dragged.
		if (viewportSize.x > 1.0f && viewportSize.y > 1.0f &&
			fsrSettings.renderWidth > 0 && fsrSettings.renderHeight > 0)
		{
			const float renderAspect = static_cast<float>(fsrSettings.renderWidth) /
				static_cast<float>(fsrSettings.renderHeight);
			ImVec2 desiredDrawSize = viewportSize;
			if (viewportSize.x / viewportSize.y > renderAspect)
				desiredDrawSize.x = viewportSize.y * renderAspect;
			else
				desiredDrawSize.y = viewportSize.x / renderAspect;

			const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
			const std::uint32_t candidateWidth = static_cast<std::uint32_t>(
				std::max(1.0f, std::round(desiredDrawSize.x * framebufferScale.x)));
			const std::uint32_t candidateHeight = static_cast<std::uint32_t>(
				std::max(1.0f, std::round(desiredDrawSize.y * framebufferScale.y)));

			if (candidateWidth != m_ViewportCandidateWidth || candidateHeight != m_ViewportCandidateHeight)
			{
				m_ViewportCandidateWidth = candidateWidth;
				m_ViewportCandidateHeight = candidateHeight;
				m_ViewportStableFrames = 0;
			}
			else if (m_ViewportStableFrames < 3)
			{
				++m_ViewportStableFrames;
			}

			if (m_ViewportStableFrames == 3 &&
				(candidateWidth != m_LastRequestedViewportWidth ||
				 candidateHeight != m_LastRequestedViewportHeight))
			{
				editorAPI.SetSceneViewportExtent(candidateWidth, candidateHeight);
				m_LastRequestedViewportWidth = candidateWidth;
				m_LastRequestedViewportHeight = candidateHeight;
			}
		}

        // ── No scene loaded: show an empty black region ───────────────────
        if (!editorAPI.IsRuntimeSceneReady())
        {
            ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(cursor, ImVec2(cursor.x + viewportSize.x, cursor.y + viewportSize.y), IM_COL32(0, 0, 0, 255));
            ImGui::Dummy(viewportSize);
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }

        Vans::EditorAPI::RenderTexturePreview scenePreview = editorAPI.GetViewportPreview(0);

        Vans::EditorAPI::EditorTextureHandle sceneTexture = scenePreview.texture;

        if (sceneTexture)
        {
            // Preserve aspect ratio: fit the texture into the available viewport without stretching.
            ImVec2 avail = viewportSize;
            float texW = (float)scenePreview.width;
            float texH = (float)scenePreview.height;

            // Compute aspect-fit size
            ImVec2 drawSize = avail;
            if (texW > 0.0f && texH > 0.0f)
            {
                float texAspect = texW / texH;
                float availAspect = avail.x / (avail.y > 0.0f ? avail.y : 1.0f);
                if (availAspect > texAspect)
                {
                    // Limit by height
                    drawSize.y = avail.y;
                    drawSize.x = drawSize.y * texAspect;
                }
                else
                {
                    // Limit by width
                    drawSize.x = avail.x;
                    drawSize.y = drawSize.x / texAspect;
                }
            }

            // Center the image within the window content region
            ImVec2 cursor = ImGui::GetCursorPos();
            ImVec2 offset = ImVec2((avail.x - drawSize.x) * 0.5f, (avail.y - drawSize.y) * 0.5f);
            ImGui::SetCursorPos(ImVec2(cursor.x + offset.x, cursor.y + offset.y));

            ImGui::Image(sceneTexture, drawSize);

            // ── Gizmo overlay (drawn on top of the scene image) ───────────────
            // Use the exact screen-space rect of the rendered texture, not the
            // whole panel window, so gizmo clipping and NDC unprojection are accurate.
            ImVec2 imageScreenPos = ImGui::GetItemRectMin();

            // ── Drag-drop target (紧贴 Image，被 ImGui 识别为 Image 的 drop zone) ──
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload(Vans::VansObjectReferenceDragPayloadType))
                {
                    if (payload->DataSize > 0 && payload->Data)
                    {
                        Vans::EditorObjectHandle droppedHandle;
                        const bool validModelDrop = Vans::TryDeserializeEditorObjectHandle(
                            payload->Data,
                            static_cast<std::size_t>(payload->DataSize),
                            droppedHandle) &&
                            droppedHandle.domain == Vans::EditorObjectDomain::ProjectAsset &&
                            droppedHandle.assetType == Vans::EditorAPI::AssetType::Model;
                        if (validModelDrop)
                        {
                            // Screen -> world: ray-ground plane intersection.
                            float ndcX = 2.0f * (ImGui::GetMousePos().x - imageScreenPos.x) / drawSize.x - 1.0f;
                            float ndcY = 1.0f - 2.0f * (ImGui::GetMousePos().y - imageScreenPos.y) / drawSize.y;

                            glm::vec3 rayOrigin(0.0f), rayDir(0.0f, -1.0f, 0.0f);
                            if (m_Camera)
                            {
                                glm::mat4 view = m_Camera->GetViewMatrix();
                                glm::mat4 proj = m_Camera->GetProjectiveMatrix();
                                glm::mat4 invPV = glm::inverse(proj * view);
                                glm::vec4 nearPt = invPV * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
                                glm::vec4 farPt = invPV * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
                                nearPt /= nearPt.w;
                                farPt /= farPt.w;
                                rayOrigin = glm::vec3(nearPt);
                                rayDir = glm::normalize(glm::vec3(farPt) - rayOrigin);
                            }

                            glm::vec3 worldPos(0.0f);
                            if (std::abs(rayDir.y) > 0.0001f)
                            {
                                float t = -rayOrigin.y / rayDir.y;
                                worldPos = (t > 0.0f) ? rayOrigin + rayDir * t
                                    : rayOrigin + rayDir * 5.0f;
                            }
                            else
                            {
                                glm::vec4 fwd4 = m_Camera ? m_Camera->GetForward() : glm::vec4(0, 0, -1, 0);
                                glm::vec3 fwd = glm::vec3(fwd4); fwd.y = 0.0f;
                                float len = glm::length(fwd);
                                worldPos = rayOrigin + (len > 0.0001f ? glm::normalize(fwd) : glm::vec3(0, 0, -1)) * 5.0f;
                            }

                            auto* editService = VansEditorWindow::GetSceneEditService();
                            if (editService)
                            {
                                const VansSceneAssetPlacementService::Result result =
                                    VansSceneAssetPlacementService::PlaceModelAsset(
                                        editorAPI,
                                        *editService,
                                        droppedHandle.guid,
                                        { worldPos.x, worldPos.y, worldPos.z });
                                if (!result)
                                    VANS_LOG_ERROR("[SceneWindow] Asset placement failed: " << result.message);
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
            const ImVec2 mainViewportPos = mainViewport ? mainViewport->Pos : ImVec2(0.0f, 0.0f);
            const ImVec2 sceneInputPos(
                imageScreenPos.x - mainViewportPos.x,
                imageScreenPos.y - mainViewportPos.y);

            // VansInputManager reports GLFW client-area coordinates. ImGui item
            // rects are absolute when multi-viewport is enabled, so normalize the
            // scene image rect back to the main GLFW window before routing to UI.
            VansRuntime::VansUISystem::Get().SetSceneViewport(
                sceneInputPos.x, sceneInputPos.y, drawSize.x, drawSize.y);

            // ── Coordinate diagnostic (one-time log per session) ──────────────
            {
                static bool s_LoggedOnce = false;
                if (!s_LoggedOnce)
                {
                    s_LoggedOnce = true;
                    // ImGui mouse position (same coordinate system as GetItemRectMin)
                    ImVec2 imguiMouse = ImGui::GetMousePos();
                    // GLFW cursor via ImGui's GLFW backend
                    ImGuiIO& io = ImGui::GetIO();
                    VANS_LOG("[SceneWindow] imageScreenPos=(" << (int)imageScreenPos.x << "," << (int)imageScreenPos.y
                        << ") sceneInputPos=(" << (int)sceneInputPos.x << "," << (int)sceneInputPos.y
                        << ") mainViewportPos=(" << (int)mainViewportPos.x << "," << (int)mainViewportPos.y
                        << ") drawSize=(" << (int)drawSize.x << "x" << (int)drawSize.y << ")"
                        << " imguiMouse=(" << (int)imguiMouse.x << "," << (int)imguiMouse.y << ")"
                        << " DisplaySize=(" << (int)io.DisplaySize.x << "x" << (int)io.DisplaySize.y << ")"
                        << " DisplayFramebufferScale=(" << io.DisplayFramebufferScale.x << "x" << io.DisplayFramebufferScale.y << ")");
                }
            }

            m_Gizmos.HandleHotkeys();
            m_Gizmos.Draw(editorAPI, m_Camera, imageScreenPos, drawSize);

            // ── Vehicle physics debug visualization ───────────────────────
            ImGui::Checkbox("Vehicle Debug", &VansEditorWindow::m_VehicleDebugGizmos);
            Vans::EditorAPI::VehicleDebugSnapshot vehicleDebug;
            if (VansEditorWindow::m_VehicleDebugGizmos)
                vehicleDebug = editorAPI.GetVehicleDebugSnapshot();

            if (VansEditorWindow::m_VehicleDebugGizmos && vehicleDebug.available)
            {
                const glm::mat4 view = m_Camera->GetViewMatrix();
                const glm::mat4 proj = m_Camera->GetProjectiveMatrix();
                const glm::mat4 viewProj = proj * view;
                ImDrawList* dl = ImGui::GetWindowDrawList();

                auto toGlm = [](const Vans::EditorAPI::Vec3& value) { return glm::vec3(value.x, value.y, value.z); };
                auto projectWorld = [&](const glm::vec3& world, ImVec2& screen) -> bool
                {
                    glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
                    if (clip.w <= 1e-5f)
                        return false;
                    glm::vec3 ndc = glm::vec3(clip) / clip.w;
                    screen = ImVec2(
                        imageScreenPos.x + (ndc.x * 0.5f + 0.5f) * drawSize.x,
                        imageScreenPos.y + (-ndc.y * 0.5f + 0.5f) * drawSize.y);
                    return ndc.z >= 0.0f && ndc.z <= 1.0f;
                };
                auto drawLineWorld = [&](const glm::vec3& a, const glm::vec3& b, ImU32 color, float thickness)
                {
                    ImVec2 sa, sb;
                    if (projectWorld(a, sa) && projectWorld(b, sb))
                        dl->AddLine(sa, sb, color, thickness);
                };
                auto drawPointWorld = [&](const glm::vec3& p, ImU32 color, float radius)
                {
                    ImVec2 sp;
                    if (projectWorld(p, sp))
                        dl->AddCircleFilled(sp, radius, color);
                };
                auto drawLabelWorld = [&](const glm::vec3& p, ImU32 color, const char* text)
                {
                    ImVec2 sp;
                    if (projectWorld(p, sp))
                        dl->AddText(ImVec2(sp.x + 6.0f, sp.y - 8.0f), color, text);
                };
                auto drawRingWorld = [&](const glm::vec3& center, const glm::vec3& axisA,
                                         const glm::vec3& axisB, float radius, ImU32 color)
                {
                    constexpr int kSegments = 40;
                    glm::vec3 prev = center + axisA * radius;
                    for (int i = 1; i <= kSegments; ++i)
                    {
                        const float angle = (float)i / (float)kSegments * 6.28318530718f;
                        glm::vec3 cur = center + (axisA * std::cos(angle) + axisB * std::sin(angle)) * radius;
                        drawLineWorld(prev, cur, color, 1.75f);
                        prev = cur;
                    }
                };
                auto drawOrientedBox = [&](
                    const glm::vec3& center,
                    const glm::vec3& axisX,
                    const glm::vec3& axisY,
                    const glm::vec3& axisZ,
                    const glm::vec3& halfExtents,
                    ImU32 color)
                {
                    const glm::vec3 corners[8] = {
                        center - axisX * halfExtents.x - axisY * halfExtents.y - axisZ * halfExtents.z,
                        center + axisX * halfExtents.x - axisY * halfExtents.y - axisZ * halfExtents.z,
                        center + axisX * halfExtents.x + axisY * halfExtents.y - axisZ * halfExtents.z,
                        center - axisX * halfExtents.x + axisY * halfExtents.y - axisZ * halfExtents.z,
                        center - axisX * halfExtents.x - axisY * halfExtents.y + axisZ * halfExtents.z,
                        center + axisX * halfExtents.x - axisY * halfExtents.y + axisZ * halfExtents.z,
                        center + axisX * halfExtents.x + axisY * halfExtents.y + axisZ * halfExtents.z,
                        center - axisX * halfExtents.x + axisY * halfExtents.y + axisZ * halfExtents.z
                    };
                    const int edges[12][2] = {
                        {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
                    };
                    for (const auto& edge : edges)
                        drawLineWorld(corners[edge[0]], corners[edge[1]], color, 2.0f);
                };

                const auto& chassis = vehicleDebug.chassis;
                drawOrientedBox(
                    toGlm(chassis.center),
                    glm::normalize(toGlm(chassis.axisX)),
                    glm::normalize(toGlm(chassis.axisY)),
                    glm::normalize(toGlm(chassis.axisZ)),
                    toGlm(chassis.halfExtents),
                    IM_COL32(70, 170, 255, 235));
                drawLabelWorld(toGlm(chassis.center), IM_COL32(120, 210, 255, 255), "chassis collider");

                for (uint32_t wi = 0; wi < vehicleDebug.wheels.size(); ++wi)
                {
                    const auto& wheel = vehicleDebug.wheels[wi];
                    const float radius = wheel.radius;
                    const float halfWidth = wheel.halfWidth;
                    const glm::vec3 center = toGlm(wheel.center);
                    const glm::vec3 lat = glm::normalize(toGlm(wheel.lateralAxis));
                    const glm::vec3 vrt = glm::normalize(toGlm(wheel.verticalAxis));
                    const glm::vec3 lng = glm::normalize(toGlm(wheel.longitudinalAxis));

                    drawRingWorld(center, vrt, lng, radius, IM_COL32(255, 220, 40, 245));
                    drawLineWorld(center - lat * halfWidth, center + lat * halfWidth, IM_COL32(50, 255, 255, 245), 2.0f);
                    drawPointWorld(center, IM_COL32(255, 255, 255, 245), 3.5f);

                    const glm::vec3 attach = toGlm(wheel.suspensionAttach);
                    const glm::vec3 rayEnd = toGlm(wheel.suspensionRayEnd);
                    drawPointWorld(attach, IM_COL32(255, 120, 40, 245), 4.0f);
                    drawLineWorld(attach, center, IM_COL32(255, 150, 40, 220), 2.0f);
                    drawLineWorld(attach, rayEnd, IM_COL32(255, 60, 80, 190), 1.5f);

                    char label[96];
                    snprintf(label, sizeof(label), "W%u r=%.2f hw=%.2f", wi, radius, halfWidth);
                    drawLabelWorld(center + vrt * (radius + 0.05f), IM_COL32(255, 245, 140, 255), label);
                }
            }

            // Motion Matching 的所有开关都由 Window > Animation > Motion Matching Debug
            // 统一拥有；Scene 只消费稳定 DTO 并负责绘制。
			if (VansMotionMatchingDebugWindow::SceneOverlayEnabled() && m_Camera)
			{
				const glm::mat4 viewProj = m_Camera->GetProjectiveMatrix() * m_Camera->GetViewMatrix();
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				const auto motionMatchingDebug = editorAPI.GetMotionMatchingDebugSnapshot();
				auto toGlm = [](const Vans::EditorAPI::Vec3& value)
				{
					return glm::vec3(value.x, value.y, value.z);
				};
				auto projectWorld = [&](const glm::vec3& world, ImVec2& screen) -> bool
				{
					const glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
					if (clip.w <= 1e-5f)
						return false;
					const glm::vec3 ndc = glm::vec3(clip) / clip.w;
					screen = ImVec2(
						imageScreenPos.x + (ndc.x * 0.5f + 0.5f) * drawSize.x,
						imageScreenPos.y + (-ndc.y * 0.5f + 0.5f) * drawSize.y);
					return ndc.z >= 0.0f && ndc.z <= 1.0f;
				};
				auto drawLine = [&](const glm::vec3& from, const glm::vec3& to,
				                    ImU32 color, float thickness)
				{
					ImVec2 a, b;
					if (projectWorld(from, a) && projectWorld(to, b))
						drawList->AddLine(a, b, color, thickness);
				};
				auto drawPoint = [&](const glm::vec3& world, ImU32 color, float radius)
				{
					ImVec2 point;
					if (projectWorld(world, point))
						drawList->AddCircleFilled(point, radius, color);
				};
				auto drawVelocity = [&](const glm::vec3& root, const Vans::EditorAPI::Vec3& velocity,
				                        ImU32 color)
				{
					const glm::vec3 value = toGlm(velocity);
					drawLine(root, root + value * 0.35f, color, 2.5f);
				};

				for (const auto& visual : motionMatchingDebug.visuals)
				{
					const glm::vec3 root = toGlm(visual.rootPosition);
					drawPoint(root, IM_COL32(255, 255, 255, 245), 4.5f);
					if (VansMotionMatchingDebugWindow::ShowHistory())
					{
						glm::vec3 previous = root;
						for (auto it = visual.historyPositions.rbegin(); it != visual.historyPositions.rend(); ++it)
						{
							const glm::vec3 point = toGlm(*it);
							drawLine(previous, point, IM_COL32(120, 120, 120, 190), 1.5f);
							drawPoint(point, IM_COL32(150, 150, 150, 210), 2.5f);
							previous = point;
						}
					}
					if (VansMotionMatchingDebugWindow::ShowFutureTrajectory())
					{
						glm::vec3 previous = root;
						for (const auto& future : visual.futurePositions)
						{
							const glm::vec3 point = toGlm(future);
							drawLine(previous, point, IM_COL32(255, 160, 32, 230), 2.0f);
							drawPoint(point, IM_COL32(255, 185, 70, 245), 3.0f);
							previous = point;
						}
					}
					if (VansMotionMatchingDebugWindow::ShowFutureVelocities())
					{
						const std::size_t sampleCount = (std::min)(
							visual.futurePositions.size(), visual.futureVelocities.size());
						for (std::size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
						{
							const glm::vec3 point = toGlm(visual.futurePositions[sampleIndex]);
							const glm::vec3 velocity = toGlm(visual.futureVelocities[sampleIndex]);
							drawLine(point, point + velocity * 0.20f,
								IM_COL32(255, 210, 80, 220), 1.5f);
						}
					}
					if (VansMotionMatchingDebugWindow::ShowActualVelocity())
						drawVelocity(root, visual.actualVelocity, IM_COL32(0, 220, 255, 245));
					if (VansMotionMatchingDebugWindow::ShowPlannedVelocity())
						drawVelocity(root, visual.plannedVelocity, IM_COL32(120, 180, 255, 220));
					if (VansMotionMatchingDebugWindow::ShowDesiredVelocity())
						drawVelocity(root, visual.desiredVelocity, IM_COL32(80, 255, 100, 245));
					if (VansMotionMatchingDebugWindow::ShowActiveClipVelocity())
						drawVelocity(root, visual.activeClipVelocity, IM_COL32(255, 80, 220, 245));
					if (VansMotionMatchingDebugWindow::ShowSelectedCandidateVelocity())
						drawVelocity(root, visual.selectedCandidateVelocity, IM_COL32(255, 184, 52, 235));
					if (VansMotionMatchingDebugWindow::ShowAppliedRootMotionVelocity())
						drawVelocity(root, visual.appliedRootMotionVelocity, IM_COL32(196, 110, 255, 245));
					if (VansMotionMatchingDebugWindow::ShowPivot() && visual.hasPredictedPivot)
					{
						const glm::vec3 pivot = toGlm(visual.predictedPivotPosition);
						drawPoint(pivot,
							visual.pivotDatabaseAvailable ? IM_COL32(255, 80, 40, 255) : IM_COL32(255, 210, 40, 255),
							6.0f);
					}
					if (VansMotionMatchingDebugWindow::ShowLabels())
					{
						ImVec2 screen;
						if (projectWorld(root, screen))
						{
							char label[320];
							snprintf(label, sizeof(label),
								"%s -> %s  motion %.1f  input %.1f  ref %.1f/s  %s",
								visual.activeClip.c_str(), visual.selectedClip.c_str(),
								visual.directionChangeDegrees,
								visual.inputDirectionChangeDegrees,
								visual.movementReferenceYawRate,
								visual.pivotRequested ? "PIVOT" : "MOVE");
							drawList->AddText(ImVec2(screen.x + 8.0f, screen.y - 18.0f),
								IM_COL32(255, 255, 210, 245), label);
						}
					}
				}
			}

            // ── Foot IK debug visualization ───────────────────────────────
            if (VansMotionMatchingDebugWindow::FootPlacementOverlayEnabled())
            {
                const glm::mat4& view = m_Camera->GetViewMatrix();
                const glm::mat4& proj = m_Camera->GetProjectiveMatrix();
                glm::vec4 vp(0, 0, drawSize.x, drawSize.y);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const Vans::EditorAPI::FootIKDebugSnapshot footIKDebug =
                    editorAPI.GetFootIKDebugSnapshot();

                auto projectWorld = [&](const glm::vec3& w) -> ImVec2
                {
                    glm::vec3 s = glm::project(w, view, proj, vp);
                    return ImVec2(imageScreenPos.x + s.x,
                                  imageScreenPos.y + (drawSize.y - s.y));
                };

                auto drawCross = [&](const glm::vec3& w, ImU32 color, float radius)
                {
                    ImVec2 p = projectWorld(w);
                    dl->AddLine(ImVec2(p.x - radius, p.y), ImVec2(p.x + radius, p.y), color, 1.5f);
                    dl->AddLine(ImVec2(p.x, p.y - radius), ImVec2(p.x, p.y + radius), color, 1.5f);
                };

                auto drawLeg = [&](const Vans::EditorAPI::FootIKDebugLegSnapshot& leg, const char* label, ImU32 chainColor)
                {
                    auto toGlm = [](const Vans::EditorAPI::Vec3& value) { return glm::vec3(value.x, value.y, value.z); };
                    ImVec2 hip = projectWorld(toGlm(leg.hip));
                    ImVec2 knee = projectWorld(toGlm(leg.knee));
                    ImVec2 foot = projectWorld(toGlm(leg.solvedFoot));
					const glm::vec3 animatedFootWorld = toGlm(leg.animatedFoot);
					const glm::vec3 solvedFootWorld = toGlm(leg.solvedFoot);
					const glm::vec2 horizontalCorrection(
						solvedFootWorld.x - animatedFootWorld.x,
						solvedFootWorld.z - animatedFootWorld.z);
					const float horizontalCorrectionLength = glm::length(horizontalCorrection);
                    dl->AddLine(hip, knee, chainColor, 2.0f);
                    dl->AddLine(knee, foot, chainColor, 2.0f);
                    dl->AddCircleFilled(hip, 3.5f, IM_COL32(80, 180, 255, 230));
                    dl->AddCircleFilled(knee, 3.5f, IM_COL32(80, 180, 255, 230));
                    dl->AddCircleFilled(foot, 4.0f, IM_COL32(255, 255, 255, 240));
					drawCross(animatedFootWorld, IM_COL32(120, 180, 255, 180), 5.0f);
					dl->AddLine(projectWorld(animatedFootWorld), foot,
						horizontalCorrectionLength <= 0.005f
							? IM_COL32(80, 220, 255, 130)
							: IM_COL32(255, 64, 64, 230), 2.0f);

                    int hits = 0;
                    int acceptedHits = 0;
                    const Vans::EditorAPI::FootIKDebugSampleSnapshot* firstInterestingSample = nullptr;
                    for (const auto& sample : leg.samples)
                    {
                        if (sample.hasHit)
							++hits;
                        if (sample.accepted)
                            ++acceptedHits;
                        if (!firstInterestingSample && !sample.status.empty())
                            firstInterestingSample = &sample;

                        const ImVec2 a = projectWorld(toGlm(sample.rayStart));
                        const ImVec2 b = projectWorld(toGlm(sample.rayEnd));
                        const ImU32 rayColor = sample.hasHit
                            ? (sample.accepted ? IM_COL32(0, 255, 128, 220) : IM_COL32(255, 210, 64, 150))
                            : IM_COL32(255, 70, 70, 120);
                        dl->AddLine(a, b, rayColor, sample.accepted ? 2.0f : 1.0f);
                        if (sample.hasHit)
                        {
                            ImVec2 h = projectWorld(toGlm(sample.hitPosition));
                            dl->AddCircleFilled(h, sample.accepted ? 4.0f : 2.5f, rayColor);
                        }
                    }

                    if (leg.hasContact)
                    {
                        const glm::vec3 contact = toGlm(leg.contact);
                        drawCross(contact, IM_COL32(0, 255, 128, 255), 6.0f);
                        dl->AddLine(projectWorld(contact),
                                    projectWorld(contact + toGlm(leg.normal) * 0.18f),
                                    IM_COL32(0, 255, 128, 220), 2.0f);
                    }
                    if (leg.hasTarget)
                    {
                        drawCross(toGlm(leg.target), IM_COL32(255, 64, 255, 255), 8.0f);
                        dl->AddLine(foot, projectWorld(toGlm(leg.target)), IM_COL32(255, 64, 255, 200), 2.0f);
                        ImVec2 t = projectWorld(toGlm(leg.target));
                        char targetLabel[64];
                        snprintf(targetLabel, sizeof(targetLabel), "%s %.2f", label, leg.targetWeight);
                        dl->AddText(ImVec2(t.x + 8, t.y - 10), IM_COL32(255, 220, 255, 240), targetLabel);
                    }

                    char statusLabel[192];
                    if (!leg.hasContact)
                    {
						snprintf(statusLabel, sizeof(statusLabel), "%s NO CONTACT hit:%d dxz:%.3f %s",
                                 label,
							 hits,
							 horizontalCorrectionLength,
                                 firstInterestingSample ? firstInterestingSample->status.c_str() : "");
                    }
                    else if (!leg.hasTarget)
                    {
						snprintf(statusLabel, sizeof(statusLabel), "%s NO TARGET hit:%d weight:%.2f dy:%.3f dxz:%.3f",
                                 label,
							 hits,
							 leg.targetWeight,
							 leg.verticalOffset,
							 horizontalCorrectionLength);
                    }
                    else
                    {
						snprintf(statusLabel, sizeof(statusLabel), "%s TARGET hit:%d accepted:%d weight:%.2f dy:%.3f dxz:%.3f plant:%.2f lock:%s err:%.3f",
                                 label,
							 hits,
                                 acceptedHits,
							 leg.targetWeight,
							 leg.verticalOffset,
							 horizontalCorrectionLength,
							 leg.plantWeight,
							 leg.planted ? "yes" : "no",
							 leg.horizontalLockError);
                    }
                    dl->AddText(ImVec2(foot.x + 8, foot.y + 8),
                                leg.hasTarget ? IM_COL32(210, 255, 210, 240) : IM_COL32(255, 110, 110, 240),
                                statusLabel);
                };

                for (const auto& leg : footIKDebug.leftLegs)
                    drawLeg(leg, "L IK", IM_COL32(64, 180, 255, 220));
                for (const auto& leg : footIKDebug.rightLegs)
                    drawLeg(leg, "R IK", IM_COL32(255, 150, 64, 220));
            }

            // ── Object picking: LMB click when gizmo is not being dragged ─────
            const ImVec2 mousePos = ImGui::GetMousePos();
            const bool mouseInsideSceneImage =
                mousePos.x >= imageScreenPos.x &&
                mousePos.y >= imageScreenPos.y &&
                mousePos.x <= imageScreenPos.x + drawSize.x &&
                mousePos.y <= imageScreenPos.y + drawSize.y;

            if (m_Camera)
            {
                const ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
                VansEditorCameraInputState cameraInput;
                cameraInput.editMode = editorAPI.GetPlayState() == Vans::EditorAPI::EnginePlayState::Edit;
                cameraInput.viewportHovered = mouseInsideSceneImage;
                cameraInput.rightMouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
                cameraInput.rightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
                cameraInput.mouseDeltaX = mouseDelta.x;
                cameraInput.mouseDeltaY = mouseDelta.y;
                cameraInput.forwardAxis =
                    (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) -
                    (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
                cameraInput.rightAxis =
                    (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) -
                    (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
                cameraInput.upAxis =
                    (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) -
                    (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f);
                cameraInput.deltaTime = static_cast<float>(VansTimer::GetEditorDeltaTime());
                m_CameraController.Update(m_Camera, cameraInput);
            }

            if (mouseInsideSceneImage
                && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                && !ImGuizmo::IsOver())
            {
                m_Gizmos.TryPickObject(editorAPI, m_Camera, mousePos, imageScreenPos, drawSize);
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }
}
