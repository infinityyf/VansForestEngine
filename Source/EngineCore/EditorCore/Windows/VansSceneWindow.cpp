#include "VansSceneWindow.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "ImGuizmo.h"
#include <filesystem>
#include <fstream>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "../../RenderCore/VulkanCore/VansTexture.h"
#include "../VansEditorSelection.h"
#include "../VansEditorWindow.h"
#include "../VansSceneEditService.h"
#include "../../AssetCore/VansAssetGuid.h"
#include "../../SceneCore/VansSceneDocument.h"
#include "../../RenderCore/VansScene.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../RenderCore/VulkanCore/VansMesh.h"
#include "../../RenderCore/VulkanCore/VansVKCommandBuffer.h"
#include "../../RenderCore/VulkanCore/VansRenderPass.h"
#include "../../VansTimer.h"
#include "../../RuntimeUI/Public/VansUISystem.h"
#include "../../Util/VansLog.h"
#include "../../AnimationCore/VansAnimationNode.h"
#include "../../AnimationCore/VansAnimationController.h"
#include "../../AnimationCore/MotionMatching/VansMotionMatching.h"
#include "../../ScriptCore/VansTransform.h"
#include "VansHierachyWindow.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../AssetCore/VansAssetDatabase.h"
#include "../../AssetCore/VansAssetGuid.h"

void VansGraphics::VansSceneWindow::ShowWindow(VansVKDevice& device)
{
    // -------------------------------------------------------------------------
    // 3. Scene 窗口 (游戏视图)
    // -------------------------------------------------------------------------
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Scene");

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

            ImGui::Unindent(4.0f);
            ImGui::Spacing();
            ImGui::PopStyleVar();
        }

        // 获取当前窗口可用区域大小
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();

        // ── No scene loaded: show an empty black region ───────────────────
        if (!m_Scene || !m_Scene->IsSceneReady())
        {
            ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(cursor, ImVec2(cursor.x + viewportSize.x, cursor.y + viewportSize.y), IM_COL32(0, 0, 0, 255));
            ImGui::Dummy(viewportSize);
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }

        // TODO: 检测 viewportSize 是否变化，如果变化则通知渲染器调整 RenderTarget (Framebuffer) 的大小
        // if (viewportSize.x != m_LastWidth || viewportSize.y != m_LastHeight) { ResizeRenderTarget(...); }

       // 获取 FSR 上采样后的渲染结果图像 (显示分辨率)
        VansVKImage& sceneImage = device.GetFSROutputImage();

        // 静态缓存，防止每帧重复创建 DescriptorSet
        static VkDescriptorSet cachedSceneDS = VK_NULL_HANDLE;
        static VkImageView cachedImageView = VK_NULL_HANDLE;
        static VkSampler cachedSampler = VK_NULL_HANDLE;

        // 检测图像资源是否发生变化 (例如窗口大小改变导致 ImageView 重建)
        if (cachedImageView != sceneImage.GetImageView() || cachedSampler != sceneImage.GetSampler() || cachedSceneDS == VK_NULL_HANDLE)
        {
            cachedImageView = sceneImage.GetImageView();
            cachedSampler = sceneImage.GetSampler();

            // 使用 ImGui Vulkan 后端提供的辅助函数创建 DescriptorSet
            // 注意：ImGui 期望图像处于 SHADER_READ_ONLY_OPTIMAL 布局
            cachedSceneDS = ImGui_ImplVulkan_AddTexture(
                cachedSampler,
                cachedImageView,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
        }

        VkDescriptorSet sceneTextureDS = cachedSceneDS;

        if (sceneTextureDS != VK_NULL_HANDLE)
        {
            // Preserve aspect ratio: fit the texture into the available viewport without stretching.
            // Query the actual image extent (fallback to swapchain extent if needed).
            ImVec2 avail = viewportSize;
            // Assume VansVKImage exposes image extent; if not, adapt to your API (e.g., swapchain extent).
            VkExtent3D imgExtent = sceneImage.GetImageDimension(); // width/height/depth
            float texW = (float)imgExtent.width;
            float texH = (float)imgExtent.height;

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

            ImGui::Image((ImTextureID)sceneTextureDS, drawSize);

            // ── Gizmo overlay (drawn on top of the scene image) ───────────────
            // Use the exact screen-space rect of the rendered texture, not the
            // whole panel window, so gizmo clipping and NDC unprojection are accurate.
            ImVec2 imageScreenPos = ImGui::GetItemRectMin();

            // ── Drag-drop target (紧贴 Image，被 ImGui 识别为 Image 的 drop zone) ──
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload("VANS_ASSET_GUID"))
                {
                    if (payload->DataSize > 0 && payload->Data)
                    {
                        std::string guidStr(static_cast<const char*>(payload->Data));

                        auto& projectMgr = Vans::VansProjectManager::Get();
                        if (auto* database = projectMgr.GetAssetDatabase())
                        {
                            Vans::VansAssetGuid guid;
                            if (Vans::VansAssetGuid::TryParse(guidStr, guid))
                            {
                                auto record = database->Find(guid);
                                if (record.has_value() && record->type == Vans::VansAssetType::Model)
                                {
                                    // 使用资产文件名作为 mesh 名称
                                    std::string meshName = record->sourcePath.stem().string();

                                    // ── 按需加载 Mesh 到场景 m_Meshes ──────
                                    // GetMeshAsset 在 CreateEntity 内部调用，需在此之前将 mesh 加载到 m_Meshes
                                    VansVKDevice* vkDev = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
                                    if (vkDev && m_Scene && m_Scene->m_ProjectMeshAliases.find(meshName) == m_Scene->m_ProjectMeshAliases.end())
                                    {
                                        auto* mesh = new VansMesh(false, false);
                                        mesh->LoadMesh(vkDev->GetLogicDevice(),
                                            vkDev->GetGraphicsQueue(),
                                            &vkDev->GetCommandBuffer(),
                                            record->sourcePath.string(), false);
                                        mesh->SetName(meshName);
                                        m_Scene->m_Meshes.push_back(mesh);
                                        m_Scene->m_ProjectMeshAliases[meshName] = mesh;
                                        VANS_LOG("[SceneWindow] Mesh loaded: "
                                            << record->sourcePath.string() << " as '" << meshName << "'");
                                    }

                                    // 生成唯一实体名称
                                    std::string uniqueName = meshName;
                                    int suffix = 1;
                                    while (m_Scene && m_Scene->FindObjectByName(uniqueName))
                                        uniqueName = meshName + "_" + std::to_string(suffix++);

                                    // ── Screen → World（ray-ground 平面求交）──
                                    float ndcX = 2.0f * (ImGui::GetMousePos().x - imageScreenPos.x) / drawSize.x - 1.0f;
                                    float ndcY = 1.0f - 2.0f * (ImGui::GetMousePos().y - imageScreenPos.y) / drawSize.y;

                                    glm::vec3 rayOrigin(0.0f), rayDir(0.0f, -1.0f, 0.0f);
                                    if (m_Camera)
                                    {
                                        glm::mat4 view = m_Camera->GetViewMatrix();
                                        glm::mat4 proj = m_Camera->GetProjectiveMatrix();
                                        glm::mat4 invPV = glm::inverse(proj * view);
                                        glm::vec4 nearPt = invPV * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
                                        glm::vec4 farPt  = invPV * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
                                        nearPt /= nearPt.w;
                                        farPt  /= farPt.w;
                                        rayOrigin = glm::vec3(nearPt);
                                        rayDir    = glm::normalize(glm::vec3(farPt) - rayOrigin);
                                    }

                                    // 与 y=0 地平面求交
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

                                    if (m_Scene && vkDev)
                                    {
                                        VansScriptObject* obj = m_Scene->CreateEntity(
                                            vkDev->GetLogicDevice(), uniqueName,
                                            meshName, "DefaultPBR", worldPos);

                                        // ── 同步写入 JSON 文档（SchemaV2 格式）──
                                        // 注意：必须以 "entities/-" JSON Pointer 追加，否则 schema 验证失败
                                        if (obj && !guidStr.empty())
                                        {
                                            auto* doc = VansEditorWindow::GetSceneDocument();
                                            auto* editService = VansEditorWindow::GetSceneEditService();
                                            if (doc && editService)
                                            {
                                                const std::string modelGuid = guidStr;

                                                // 同时注册 mesh 的 GUID 别名（场景重载时 BuildRuntimeSceneFromV2 按 GUID 查找 mesh）
                                                if (m_Scene)
                                                    m_Scene->m_ProjectMeshAliases[modelGuid] = m_Scene->m_ProjectMeshAliases[meshName];

                                                Vans::SceneJson transformComp;
                                                transformComp["id"]      = Vans::VansAssetGuid::New().ToString();
                                                transformComp["type"]    = "Transform";
                                                transformComp["version"] = 1u;
                                                transformComp["enabled"] = true;
                                                transformComp["data"]    = {
                                                    {"position", {worldPos.x, worldPos.y, worldPos.z}},
                                                    {"rotation", {0.0f, 0.0f, 0.0f, 1.0f}},
                                                    {"scale",    {1.0f, 1.0f, 1.0f}}
                                                };

                                                // ── 获取实际使用的 Material GUID（与 CreateEntity fallback 逻辑一致）──
                                                std::string materialGuid;
                                                if (!m_Scene->m_Materials.empty())
                                                    materialGuid = m_Scene->m_Materials[0]->m_AssetName;

                                                Vans::SceneJson modelData;
                                                modelData["model"] = { {"guid", modelGuid} };
                                                if (!materialGuid.empty())
                                                    modelData["materialOverrides"] = {
                                                        {"0", {{"guid", materialGuid}}}
                                                    };

                                                Vans::SceneJson rendererComp;
                                                rendererComp["id"]      = Vans::VansAssetGuid::New().ToString();
                                                rendererComp["type"]    = "ModelRenderer";
                                                rendererComp["version"] = 1u;
                                                rendererComp["enabled"] = true;
                                                rendererComp["data"]    = std::move(modelData);

                                                Vans::SceneJson newEntity;
                                                newEntity["id"]         = obj->m_EntityGuid;
                                                newEntity["name"]       = uniqueName;
                                                newEntity["parent"]     = nullptr;
                                                newEntity["components"] = Vans::SceneJson::array(
                                                    {std::move(transformComp), std::move(rendererComp)});

                                                // nlohmann json_pointer 支持 "-" 作为数组追加 token
                                                editService->Set("/entities/-", newEntity);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // Inform the Noesis input adapter of the scene image's screen rect so
            // that raw GLFW cursor coordinates are mapped to Noesis view space.
            VansRuntime::VansUISystem::Get().SetSceneViewport(
                imageScreenPos.x, imageScreenPos.y, drawSize.x, drawSize.y);

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
                        << ") drawSize=(" << (int)drawSize.x << "x" << (int)drawSize.y << ")"
                        << " imguiMouse=(" << (int)imguiMouse.x << "," << (int)imguiMouse.y << ")"
                        << " DisplaySize=(" << (int)io.DisplaySize.x << "x" << (int)io.DisplaySize.y << ")"
                        << " DisplayFramebufferScale=(" << io.DisplayFramebufferScale.x << "x" << io.DisplayFramebufferScale.y << ")");
                }
            }

            m_Gizmos.HandleHotkeys(m_Scene);
            m_Gizmos.Draw(m_Scene, m_Camera, imageScreenPos, drawSize);

            // ── Motion Matching 轨迹可视化 ────────────────────────────────
			if (m_Scene && !m_Scene->m_AnimationNodes.empty())
				ImGui::Checkbox("Motion Matching Debug", &VansHierachuWindow::m_ShowMMViz);
            if (VansHierachuWindow::m_ShowMMViz && m_Scene)
            {
                const glm::mat4& view = m_Camera->GetViewMatrix();
                const glm::mat4& proj = m_Camera->GetProjectiveMatrix();
                ImDrawList* dl = ImGui::GetWindowDrawList();

                for (auto* animNode : m_Scene->m_AnimationNodes)
                {
                    auto* ctrl = animNode ? animNode->GetController() : nullptr;
                    if (!ctrl || !ctrl->IsMotionMatchingConfigured())
                        continue;
                    const auto* mm = ctrl->GetMotionMatchingDebugData();
                    if (!mm || !mm->enabled)
                        continue;

                    // 世界空间根位置
                    uint32_t tid = animNode->GetTransformID();
                    glm::mat4 worldMat = VansTransformStore::GetTransform(tid).GetModelMatrix();
                    const auto& globals = ctrl->GetCachedGlobalTransforms();
                    glm::vec3 rootWS = glm::vec3(worldMat[3]);
                    const Skeleton& skeleton = animNode->GetSkeleton();
                    int rootBoneIndex = -1;
                    auto rootIt = skeleton.boneNameToIndex.find("root");
                    if (rootIt != skeleton.boneNameToIndex.end())
                        rootBoneIndex = rootIt->second;
                    else
                    {
                        rootIt = skeleton.boneNameToIndex.find("Root");
                        if (rootIt != skeleton.boneNameToIndex.end())
                            rootBoneIndex = rootIt->second;
                    }
                    if (rootBoneIndex >= 0 && rootBoneIndex < static_cast<int>(globals.size()))
                    {
                        glm::mat4 boneWorld = worldMat * globals[rootBoneIndex];
                        rootWS = glm::vec3(boneWorld[3]);
                    }

                    // 预测轨迹点 (0.25s 间隔, 共 1s, 4 个点)
                    float dir = mm->queryDirection;
                    float spd = mm->querySpeed * 0.01f; // cm/s → m/s
                    glm::vec3 velLocal(std::sin(dir) * spd, -std::cos(dir) * spd, 0.0f);
                    glm::vec3 velWS = glm::vec3(worldMat * glm::vec4(velLocal, 0.0f));
                    velWS.y = 0.0f;
                    if (glm::length(velWS) > 0.0001f)
                        velWS = glm::normalize(velWS) * spd;
                    const int numSteps = 4;
                    const float stepT = 0.25f;

                    // 收集轨迹点世界矩阵
                    std::vector<glm::mat4> trajMats;
                    trajMats.reserve(numSteps + 1);
                    trajMats.push_back(glm::translate(glm::mat4(1.0f), rootWS));
                    for (int i = 1; i <= numSteps; ++i)
                    {
                        glm::vec3 pt = rootWS + velWS * (stepT * (float)i);
                        trajMats.push_back(glm::translate(glm::mat4(1.0f), pt));
                    }

                    // ImGuizmo: 在轨迹点绘制小方块
                    std::vector<float> matFloats;
                    matFloats.reserve(trajMats.size() * 16);
                    for (auto& m : trajMats)
                    {
                        // 缩放到小尺寸
                        glm::mat4 cubeM = glm::scale(m, glm::vec3(0.02f));
                        for (int c = 0; c < 4; ++c)
                            for (int r = 0; r < 4; ++r)
                                matFloats.push_back(cubeM[c][r]);
                    }
                    ImGuizmo::DrawCubes(
                        glm::value_ptr(view), glm::value_ptr(proj),
                        matFloats.data(), (int)trajMats.size());

                    // ImDrawList: 连线 + 速度箭头
                    glm::vec4 vp(0, 0, drawSize.x, drawSize.y);
                    auto projPt = [&](const glm::vec3& w) -> ImVec2 {
                        glm::vec3 s = glm::project(w, view, proj, vp);
                        return ImVec2(imageScreenPos.x + s.x,
                                      imageScreenPos.y + (drawSize.y - s.y));
                    };

                    ImVec2 prev = projPt(rootWS);
                    // 根位置圆
                    dl->AddCircleFilled(prev, 4.0f, IM_COL32(0, 255, 128, 220));
                    dl->AddCircle(prev, 5.0f, IM_COL32(0, 255, 128, 255), 0, 2.5f);

                    // 轨迹线
                    for (int i = 1; i <= numSteps; ++i)
                    {
                        glm::vec3 pt = rootWS + velWS * (stepT * (float)i);
                        ImVec2 cur = projPt(pt);
                        dl->AddLine(prev, cur, IM_COL32(255, 160, 32, 200), 1.5f);
                        dl->AddCircleFilled(cur, 2.5f, IM_COL32(255, 160, 32, 220));
                        prev = cur;
                    }

                    // 速度箭头（从根位置出发）
                    ImVec2 rootScr = projPt(rootWS);
                    glm::vec3 velEnd = rootWS + velWS * 0.5f;
                    ImVec2 ve = projPt(velEnd);
                    dl->AddLine(rootScr, ve, IM_COL32(0, 200, 255, 220), 2.5f);
                    float dx = ve.x - rootScr.x, dy = ve.y - rootScr.y;
                    float len = std::sqrt(dx*dx + dy*dy);
                    if (len > 1.0f)
                    {
                        dx /= len; dy /= len;
                        float al = 8.0f;
                        ImVec2 tip(ve.x + dx*3, ve.y + dy*3);
                        ImVec2 L(ve.x - dx*al + dy*al*0.5f, ve.y - dy*al - dx*al*0.5f);
                        ImVec2 R(ve.x - dx*al - dy*al*0.5f, ve.y - dy*al + dx*al*0.5f);
                        dl->AddTriangleFilled(tip, L, R, IM_COL32(0, 200, 255, 220));
                    }

                    // clip 名标签
                    char lbl[64];
                    snprintf(lbl, sizeof(lbl), "%s", mm->activeClip.c_str());
                    dl->AddText(ImVec2(rootScr.x + 8, rootScr.y - 16),
                                IM_COL32(255, 255, 180, 230), lbl);
                }
            }

            // ── Object picking: LMB click when gizmo is not being dragged ─────
            if (ImGui::IsWindowHovered()
                && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                && !ImGuizmo::IsOver())
            {
                m_Gizmos.TryPickObject(m_Scene, m_Camera,
                    ImGui::GetMousePos(), imageScreenPos, drawSize);
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }
}
