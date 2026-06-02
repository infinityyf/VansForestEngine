#include "VansClothProfileEditorWindow.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansInputManager.h"
#include "../../ProjectSystem/VansProjectManager.h"

#include <imgui.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/matrix4x4.h>
#include <GLM/glm.hpp>
#include <GLM/gtc/matrix_transform.hpp>
#include <GLM/gtc/type_ptr.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <tuple>
#include <cmath>
#include <filesystem>
#include <limits>

namespace fs = std::filesystem;

namespace VansGraphics
{
    namespace
    {
        struct ClothEditorRawMeshData
        {
            std::vector<glm::vec3> m_Positions;
            std::vector<uint32_t>  m_Indices;
        };

        glm::vec3 TransformAiPosition(const aiMatrix4x4& transform, const aiVector3D& position)
        {
            aiVector3D transformed = transform * position;
            return glm::vec3(transformed.x, transformed.y, transformed.z);
        }

        void AppendAiNodeMeshes(aiNode* node,
                                const aiScene* scene,
                                const aiMatrix4x4& parentTransform,
                                ClothEditorRawMeshData& rawData)
        {
            if (!node || !scene)
                return;

            aiMatrix4x4 accumulatedTransform = parentTransform * node->mTransformation;
            for (uint32_t meshSlot = 0; meshSlot < node->mNumMeshes; ++meshSlot)
            {
                const aiMesh* mesh = scene->mMeshes[node->mMeshes[meshSlot]];
                if (!mesh)
                    continue;

                uint32_t baseVertex = static_cast<uint32_t>(rawData.m_Positions.size());
                rawData.m_Positions.reserve(rawData.m_Positions.size() + mesh->mNumVertices);
                for (uint32_t v = 0; v < mesh->mNumVertices; ++v)
                {
                    rawData.m_Positions.push_back(
                        TransformAiPosition(accumulatedTransform, mesh->mVertices[v]));
                }

                rawData.m_Indices.reserve(rawData.m_Indices.size() + static_cast<size_t>(mesh->mNumFaces) * 3);
                for (uint32_t f = 0; f < mesh->mNumFaces; ++f)
                {
                    const aiFace& face = mesh->mFaces[f];
                    if (face.mNumIndices != 3)
                        continue;

                    rawData.m_Indices.push_back(baseVertex + face.mIndices[0]);
                    rawData.m_Indices.push_back(baseVertex + face.mIndices[1]);
                    rawData.m_Indices.push_back(baseVertex + face.mIndices[2]);
                }
            }

            for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
                AppendAiNodeMeshes(node->mChildren[childIndex], scene, accumulatedTransform, rawData);
        }
    }

    // =========================================================================
    // ShowWindow — 每帧主循环调用
    // =========================================================================

    void VansClothProfileEditorWindow::ShowWindow(VansVKDevice& /*device*/)
    {
        if (!m_IsOpen)
            return;

        ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(600.0f, 400.0f), ImVec2(FLT_MAX, FLT_MAX));

        std::string title = "Cloth Profile Editor";
        if (!m_CurrentProfilePath.empty())
        {
            // 只显示文件名部分
            title += " — " + fs::path(m_CurrentProfilePath).filename().string();
        }
        if (m_IsDirty)
            title += " *";
        title += "##ClothProfileEditor";

        if (!ImGui::Begin(title.c_str(), &m_IsOpen))
        {
            ImGui::End();
            return;
        }

        // ── 新建 Profile 对话框 ───────────────────────────────────────────
        if (m_ShowNewProfileDialog)
        {
            ImGui::OpenPopup("新建 Cloth Profile");
            m_ShowNewProfileDialog = false;
        }

        if (ImGui::BeginPopupModal("新建 Cloth Profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("保存路径（相对于引擎根目录）：");
            ImGui::InputText("##newProfilePath", m_NewProfilePathBuf, sizeof(m_NewProfilePathBuf));

            if (ImGui::Button("创建", ImVec2(100, 0)))
            {
                if (m_NewProfilePathBuf[0] != '\0')
                {
                    m_CurrentProfilePath = m_NewProfilePathBuf;
                    m_Profile            = VansEngine::VansClothProfile{};
                    m_Profile.m_Name     = fs::path(m_CurrentProfilePath).stem().string();
                    m_MeshLoaded         = false;
                    m_WeldedParticles.clear();
                    m_WeldedTriangles.clear();
                    m_IsDirty = true;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("取消", ImVec2(100, 0)))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }

        // ── 顶部操作栏 ───────────────────────────────────────────────────
        DrawProfileInfoPanel();

        ImGui::Separator();

        // ── 主体布局：左侧视口 + 右侧 Inspector ─────────────────────────
        float leftWidth  = ImGui::GetContentRegionAvail().x * 0.55f;
        float rightWidth = ImGui::GetContentRegionAvail().x * 0.45f - 8.0f;
        float panelHeight = ImGui::GetContentRegionAvail().y;

        // 左侧：3D 网格视口
        if (ImGui::BeginChild("##clothLeft", ImVec2(leftWidth, panelHeight), false))
        {
            DrawMeshViewport();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // 右侧：参数面板 + 骨骼配置面板 + 固定点列表
        if (ImGui::BeginChild("##clothRight", ImVec2(rightWidth, panelHeight), false))
        {
            DrawParametersPanel();
            ImGui::Spacing();
            DrawBoneConfigPanel();
            ImGui::Spacing();
            DrawPinnedParticleList();
        }
        ImGui::EndChild();

        ImGui::End();
    }

    // =========================================================================
    // OpenProfile — 外部入口：通过路径打开已有配置
    // =========================================================================

    void VansClothProfileEditorWindow::OpenProfile(const std::string& profilePath)
    {
        if (profilePath.empty())
        {
            // 路径为空时直接打开空窗口，等用户设置路径后加载
            m_CurrentProfilePath = "";
            m_Profile            = VansEngine::VansClothProfile{};
            m_MeshLoaded         = false;
            m_WeldedParticles.clear();
            m_WeldedTriangles.clear();
            m_EditorBones.clear();
            m_SkeletonLoaded  = false;
            m_SkeletonLoadError.clear();
            m_IsDirty = false;
            m_IsOpen  = true;
            return;
        }

        m_CurrentProfilePath = profilePath;
        m_Profile            = VansEngine::VansClothProfile{};
        m_MeshLoaded         = false;
        m_WeldedParticles.clear();
        m_WeldedTriangles.clear();
        m_EditorBones.clear();
        m_SkeletonLoaded  = false;
        m_SkeletonLoadError.clear();
        m_IsDirty = false;

        if (m_Profile.LoadFromFile(profilePath))
        {
            if (!m_Profile.m_ModelPath.empty())
                LoadModelFromProfile();
            // V2：若配置了参考骨架路径则自动加载骨骼
            if (m_Profile.m_FollowBones && !m_Profile.m_ReferenceSkeletonPath.empty())
                LoadReferenceSkeleton();
        }
        else
        {
            VANS_LOG_WARN("[VansClothProfileEditor] 加载 Profile 失败: " << profilePath);
        }

        m_IsOpen = true;
    }

    // =========================================================================
    // NewProfile — 外部入口：新建空 Profile
    // =========================================================================

    void VansClothProfileEditorWindow::NewProfile()
    {
        m_ShowNewProfileDialog = true;
        m_NewProfilePathBuf[0] = '\0';
        m_IsOpen = true;
    }

    // =========================================================================
    // DrawProfileInfoPanel — 顶部元信息与操作按钮
    // =========================================================================

    void VansClothProfileEditorWindow::DrawProfileInfoPanel()
    {
        // 文件路径显示
        ImGui::Text("当前文件: %s",
                    m_CurrentProfilePath.empty() ? "（未保存）" : m_CurrentProfilePath.c_str());

        ImGui::SameLine();
        if (ImGui::Button("新建"))  NewProfile();
        ImGui::SameLine();
        if (ImGui::Button("保存"))  SaveProfile();
        ImGui::SameLine();
        if (ImGui::Button("还原"))  RevertProfile();

        // 名称与描述
        char nameBuf[256];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", m_Profile.m_Name.c_str());
        if (ImGui::InputText("名称", nameBuf, sizeof(nameBuf)))
        {
            m_Profile.m_Name = nameBuf;
            m_IsDirty = true;
        }

        char descBuf[512];
        std::snprintf(descBuf, sizeof(descBuf), "%s", m_Profile.m_Description.c_str());
        if (ImGui::InputText("描述", descBuf, sizeof(descBuf)))
        {
            m_Profile.m_Description = descBuf;
            m_IsDirty = true;
        }

        // 模型路径 + 重新加载按钮
        char modelPathBuf[512];
        std::snprintf(modelPathBuf, sizeof(modelPathBuf), "%s", m_Profile.m_ModelPath.c_str());
        if (ImGui::InputText("模型路径", modelPathBuf, sizeof(modelPathBuf)))
        {
            m_Profile.m_ModelPath = modelPathBuf;
            m_IsDirty  = true;
            m_MeshLoaded = false; // 路径变更，旧网格数据失效
        }
        ImGui::SameLine();
        if (ImGui::Button("重新加载模型"))
        {
            if (!m_Profile.m_ModelPath.empty())
                LoadModelFromProfile();
        }
    }

    // =========================================================================
    // DrawMeshViewport — ImGui DrawList CPU 投影 3D 视口
    // =========================================================================

    void VansClothProfileEditorWindow::DrawMeshViewport()
    {
        ImVec2 viewportMin  = ImGui::GetCursorScreenPos();
        ImVec2 contentSize  = ImGui::GetContentRegionAvail();
        ImVec2 viewportSize = { contentSize.x, contentSize.y };

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(viewportMin,
            { viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y },
            IM_COL32(35, 35, 40, 255));

        if (!m_MeshLoaded)
        {
            const char* msg = m_MeshLoadError.empty()
                ? "未加载模型。请设置 [模型路径] 后点击「重新加载模型」。"
                : m_MeshLoadError.c_str();
            dl->AddText({ viewportMin.x + 10.0f, viewportMin.y + 10.0f },
                        IM_COL32(180, 180, 180, 255), msg);
            ImGui::Dummy(viewportSize);
            return;
        }

        // ── 构建轨道相机 MVP ───────────────────────────────────────────────
        float az = glm::radians(m_CameraAzimuth);
        float el = glm::radians(m_CameraElevation);
        glm::vec3 eye = m_CameraTarget + m_CameraDistance *
                        glm::vec3(std::cos(el) * std::sin(az),
                                  std::sin(el),
                                  std::cos(el) * std::cos(az));
        glm::mat4 view = glm::lookAt(eye, m_CameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
        float aspect = (viewportSize.y > 1.0f) ? (viewportSize.x / viewportSize.y) : 1.0f;
        float nearPlane = glm::clamp(m_CameraDistance * 0.001f, 0.001f, 1.0f);
        float farPlane  = glm::max(200.0f, m_CameraDistance * 4.0f);
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, nearPlane, farPlane);
        glm::mat4 mvp = proj * view;

        // 辅助 lambda：局部空间坐标 → 屏幕像素坐标
        auto Project = [&](glm::vec3 p) -> ImVec2
        {
            glm::vec4 clip = mvp * glm::vec4(p, 1.0f);
            if (clip.w <= 0.001f) return { -9999.0f, -9999.0f };
            glm::vec2 ndc = glm::vec2(clip.x, clip.y) / clip.w;
            return {
                viewportMin.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x,
                viewportMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y
            };
        };

        // ① 绘制线框
        for (size_t t = 0; t + 2 < m_WeldedTriangles.size(); t += 3)
        {
            ImVec2 p0 = Project(m_WeldedParticles[m_WeldedTriangles[t + 0]].m_LocalPosition);
            ImVec2 p1 = Project(m_WeldedParticles[m_WeldedTriangles[t + 1]].m_LocalPosition);
            ImVec2 p2 = Project(m_WeldedParticles[m_WeldedTriangles[t + 2]].m_LocalPosition);
            dl->AddTriangle(p0, p1, p2, IM_COL32(70, 70, 80, 160), 0.7f);
        }

        // ② 按 view-z 排序粒子（由远及近，近处覆盖远处）
        std::vector<std::pair<float, int>> sorted;
        sorted.reserve(m_WeldedParticles.size());
        for (int i = 0; i < static_cast<int>(m_WeldedParticles.size()); ++i)
        {
            glm::vec4 vp = view * glm::vec4(m_WeldedParticles[i].m_LocalPosition, 1.0f);
            sorted.emplace_back(vp.z, i);
        }
        std::sort(sorted.begin(), sorted.end());

        for (auto& [z, i] : sorted)
        {
            ImVec2 sp  = Project(m_WeldedParticles[i].m_LocalPosition);
            ImU32  col = m_WeldedParticles[i].m_IsPinned
                         ? IM_COL32(220, 60,  60,  255)
                         : IM_COL32(80,  200, 80,  200);
            dl->AddCircleFilled(sp, 5.0f, col);
            dl->AddCircle(sp, 5.8f, IM_COL32(255, 255, 255, 100), 0, 1.0f);
        }

        // ③ 若已加载骨架，叠加骨骼线框
        if (m_SkeletonLoaded && m_Profile.m_FollowBones)
            DrawSkeletonOverlay(dl, mvp, viewportMin, viewportSize);

        ImGui::Dummy(viewportSize);
        bool isViewportHovered = ImGui::IsItemHovered();
        HandleOrbitalCamera(isViewportHovered);
        HandleVertexPicking(viewportMin, viewportSize, mvp, view);

        // 更新绑定结果倒计时
        if (m_BindResultTimer > 0.0f)
        {
            m_BindResultTimer -= ImGui::GetIO().DeltaTime;
            if (m_BindResultTimer < 0.0f)
                m_BindResultTimer = 0.0f;
        }
    }

    // =========================================================================
    // DrawParametersPanel — 右侧物理参数 Inspector
    // =========================================================================

    void VansClothProfileEditorWindow::DrawParametersPanel()
    {
        if (!ImGui::CollapsingHeader("物理参数", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (ImGui::SliderFloat("刚度 (Stiffness)", &m_Profile.m_Stiffness, 0.0f, 1.0f))
            m_IsDirty = true;

        if (ImGui::SliderFloat("阻尼 (Damping)", &m_Profile.m_Damping, 0.0f, 1.0f))
            m_IsDirty = true;

        if (ImGui::SliderFloat("摩擦 (Friction)", &m_Profile.m_Friction, 0.0f, 1.0f))
            m_IsDirty = true;

        if (ImGui::SliderFloat("重力 Y (Gravity)", &m_Profile.m_Gravity, -30.0f, 0.0f))
            m_IsDirty = true;

        if (ImGui::Checkbox("自碰撞 (SelfCollision)", &m_Profile.m_SelfCollision))
            m_IsDirty = true;

        if (ImGui::SliderFloat("匹配容差 (MatchTol)", &m_Profile.m_PinnedMatchTolerance, 0.001f, 0.5f))
            m_IsDirty = true;
    }

    // =========================================================================
    // DrawPinnedParticleList — 右侧已固定粒子列表
    // =========================================================================

    void VansClothProfileEditorWindow::DrawPinnedParticleList()
    {
        int pinnedCount = static_cast<int>(m_Profile.m_PinnedLocalPositions.size());
        std::string header = "固定顶点 (" + std::to_string(pinnedCount) + ")";

        if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (ImGui::Button("全部清除"))
        {
            m_Profile.m_PinnedLocalPositions.clear();
            // 同步回 EditorParticle 状态
            for (auto& p : m_WeldedParticles)
                p.m_IsPinned = false;
            m_IsDirty = true;
        }

        ImGui::Separator();

        // 逐项列表，带 [✕] 删除按钮
        for (int i = 0; i < static_cast<int>(m_Profile.m_PinnedLocalPositions.size()); )
        {
            const glm::vec3& pos = m_Profile.m_PinnedLocalPositions[i];
            ImGui::Text("[%d] (%.3f, %.3f, %.3f)", i, pos.x, pos.y, pos.z);

            // V2：若启用骨骼跟随则显示绑定信息
            if (m_Profile.m_FollowBones
                && i < static_cast<int>(m_Profile.m_PinnedBoneBindings.size()))
            {
                const auto& bd = m_Profile.m_PinnedBoneBindings[i];
                ImGui::Indent(16.0f);
                if (!bd.m_BoneNames.empty())
                {
                    for (size_t b = 0; b < bd.m_BoneNames.size(); ++b)
                    {
                        float w = (b < bd.m_Weights.size()) ? bd.m_Weights[b] : 0.0f;
                        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f),
                            "-> %s (%.2f)", bd.m_BoneNames[b].c_str(), w);
                    }
                }
                ImGui::Unindent(16.0f);
            }

            ImGui::SameLine();
            std::string btnId = "[X]##pin" + std::to_string(i);
            if (ImGui::SmallButton(btnId.c_str()))
            {
                m_Profile.m_PinnedLocalPositions.erase(
                    m_Profile.m_PinnedLocalPositions.begin() + i);
                if (i < static_cast<int>(m_Profile.m_PinnedBoneBindings.size()))
                    m_Profile.m_PinnedBoneBindings.erase(
                        m_Profile.m_PinnedBoneBindings.begin() + i);

                // 同步 EditorParticle 的 m_IsPinned 状态
                for (auto& ep : m_WeldedParticles)
                {
                    float dx = ep.m_LocalPosition.x - pos.x;
                    float dy = ep.m_LocalPosition.y - pos.y;
                    float dz = ep.m_LocalPosition.z - pos.z;
                    if (std::sqrt(dx*dx + dy*dy + dz*dz) < m_Profile.m_PinnedMatchTolerance)
                        ep.m_IsPinned = false;
                }
                m_IsDirty = true;
                continue; // 不递增 i，因为已经删除了一个元素
            }
            ++i;
        }
    }

    // =========================================================================
    // HandleOrbitalCamera — 轨道相机控制
    // =========================================================================

    void VansClothProfileEditorWindow::HandleOrbitalCamera(bool isViewportHovered)
    {
        auto& input = Vans::VansInputManager::Get();

        if (isViewportHovered && input.IsMouseButtonPressed(Vans::MouseButton::Right))
            m_IsOrbitingViewport = true;
        if (input.IsMouseButtonReleased(Vans::MouseButton::Right))
            m_IsOrbitingViewport = false;

        if (isViewportHovered && input.IsMouseButtonPressed(Vans::MouseButton::Middle))
            m_IsPanningViewport = true;
        if (input.IsMouseButtonReleased(Vans::MouseButton::Middle))
            m_IsPanningViewport = false;

        double mouseDeltaX = 0.0;
        double mouseDeltaY = 0.0;
        input.GetMouseDelta(mouseDeltaX, mouseDeltaY);

        // 右键拖拽 → 旋转视角
        if (m_IsOrbitingViewport && input.IsMouseButtonDown(Vans::MouseButton::Right))
        {
            m_CameraAzimuth   += static_cast<float>(mouseDeltaX) * 0.5f;
            m_CameraElevation -= static_cast<float>(mouseDeltaY) * 0.5f;
            m_CameraElevation  = glm::clamp(m_CameraElevation, -89.0f, 89.0f);
        }

        // 中键拖拽 → 平移目标点
        if (m_IsPanningViewport && input.IsMouseButtonDown(Vans::MouseButton::Middle))
        {
            m_CameraTarget.x -= static_cast<float>(mouseDeltaX) * 0.005f * m_CameraDistance;
            m_CameraTarget.y += static_cast<float>(mouseDeltaY) * 0.005f * m_CameraDistance;
        }

        // 滚轮 → 缩放
        if (isViewportHovered)
        {
            double scrollX = 0.0;
            double scrollY = 0.0;
            input.GetScrollDelta(scrollX, scrollY);
            m_CameraDistance = glm::clamp(
                m_CameraDistance - static_cast<float>(scrollY) * 0.2f, 0.1f, 2000.0f);
        }
    }

    // =========================================================================
    // HandleVertexPicking — 左键点击切换固定状态
    // =========================================================================

    void VansClothProfileEditorWindow::HandleVertexPicking(
        ImVec2 viewportMin, ImVec2 viewportSize, const glm::mat4& mvp, const glm::mat4& view)
    {
        constexpr float PICK_RADIUS = 8.0f;
        ImVec2 restoreCursorPos = ImGui::GetCursorScreenPos();

        // 使用 ImGui 小部件命中检测创建每个投影顶点的点击热区。
        // 这样点击坐标与 DrawList 的屏幕坐标保持同一套 ImGui 坐标系，避免 GLFW 原始鼠标坐标
        // 在多视口 / DPI 缩放场景下与 ImGui 屏幕坐标不一致导致无法选中。
        std::vector<std::pair<float, int>> sorted;
        sorted.reserve(m_WeldedParticles.size());
        for (int i = 0; i < static_cast<int>(m_WeldedParticles.size()); ++i)
        {
            glm::vec4 viewPos = view * glm::vec4(m_WeldedParticles[i].m_LocalPosition, 1.0f);
            sorted.emplace_back(viewPos.z, i);
        }
        std::sort(sorted.begin(), sorted.end());

        for (auto it = sorted.rbegin(); it != sorted.rend(); ++it)
        {
            int i = it->second;
            const glm::vec3& localPos = m_WeldedParticles[i].m_LocalPosition;
            glm::vec4 clip = mvp * glm::vec4(localPos, 1.0f);
            if (clip.w <= 0.001f)
                continue;

            glm::vec2 ndc = glm::vec2(clip.x, clip.y) / clip.w;
            if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f)
                continue;

            float sx = viewportMin.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
            float sy = viewportMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y;

            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(ImVec2(sx - PICK_RADIUS, sy - PICK_RADIUS));
            if (ImGui::InvisibleButton("##clothVertexPick", ImVec2(PICK_RADIUS * 2.0f, PICK_RADIUS * 2.0f)))
            {
                m_WeldedParticles[i].m_IsPinned ^= true;
                SyncProfilePinnedVertices(); // 实时同步回 m_Profile
                m_IsDirty = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }

        ImGui::SetCursorScreenPos(restoreCursorPos);
    }

    // =========================================================================
    // SyncProfilePinnedVertices — 将 EditorParticle 状态同步回 Profile
    // =========================================================================

    void VansClothProfileEditorWindow::SyncProfilePinnedVertices()
    {
        m_Profile.m_PinnedLocalPositions.clear();
        for (const EditorParticle& ep : m_WeldedParticles)
        {
            if (ep.m_IsPinned)
                m_Profile.m_PinnedLocalPositions.push_back(ep.m_LocalPosition);
        }
    }

    // =========================================================================
    // SaveProfile — 保存到文件
    // =========================================================================

    void VansClothProfileEditorWindow::SaveProfile()
    {
        if (m_CurrentProfilePath.empty())
        {
            VANS_LOG_WARN("[VansClothProfileEditor] 保存路径为空，请先设置文件路径。");
            return;
        }
        if (m_Profile.SaveToFile(m_CurrentProfilePath))
        {
            m_IsDirty = false;
            VANS_LOG("[VansClothProfileEditor] Profile 已保存: " << m_CurrentProfilePath);
        }
        else
        {
            VANS_LOG_ERROR("[VansClothProfileEditor] Profile 保存失败: " << m_CurrentProfilePath);
        }
    }

    // =========================================================================
    // RevertProfile — 从文件重新加载，丢弃改动
    // =========================================================================

    void VansClothProfileEditorWindow::RevertProfile()
    {
        if (m_CurrentProfilePath.empty()) return;
        OpenProfile(m_CurrentProfilePath);
        m_IsDirty = false;
    }

    // =========================================================================
    // DrawBoneConfigPanel — 骨骼跟随配置面板
    // =========================================================================

    void VansClothProfileEditorWindow::DrawBoneConfigPanel()
    {
        if (!ImGui::CollapsingHeader("骨骼跟随"))
            return;

        // Follow Bones 总开关
        bool prevFollow = m_Profile.m_FollowBones;
        if (ImGui::Checkbox("启用骨骼跟随", &m_Profile.m_FollowBones))
        {
            m_IsDirty = true;
            if (!prevFollow && m_Profile.m_FollowBones)
            {
                // 刚启用时清空旧绑定数据
                m_Profile.m_PinnedBoneBindings.clear();
            }
        }

        if (!m_Profile.m_FollowBones)
            return;

        ImGui::Separator();

        // 参考骨架路径输入
        char skelBuf[512]{};
        strncpy_s(skelBuf, sizeof(skelBuf), m_Profile.m_ReferenceSkeletonPath.c_str(), _TRUNCATE);
        if (ImGui::InputText("骨架路径", skelBuf, sizeof(skelBuf)))
        {
            m_Profile.m_ReferenceSkeletonPath = skelBuf;
            m_IsDirty = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("加载骨架"))
        {
            LoadReferenceSkeleton();
        }

        // 加载状态提示
        if (m_SkeletonLoaded)
        {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f),
                "已加载 %d 根骨骼", static_cast<int>(m_EditorBones.size()));
        }
        else if (!m_SkeletonLoadError.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                "错误: %s", m_SkeletonLoadError.c_str());
        }

        ImGui::Separator();
        ImGui::Text("骨架偏移（相对模型坐标系）");

        // 位置偏移
        float pos[3] = { m_Profile.m_SkeletonOffset.m_Position.x,
                         m_Profile.m_SkeletonOffset.m_Position.y,
                         m_Profile.m_SkeletonOffset.m_Position.z };
        if (ImGui::DragFloat3("偏移位置", pos, 0.01f))
        {
            m_Profile.m_SkeletonOffset.m_Position = glm::vec3(pos[0], pos[1], pos[2]);
            m_IsDirty = true;
            if (m_SkeletonLoaded)
                RebuildEditorBonePositions();
        }

        // 旋转偏移（Euler 角，度）
        float rot[3] = { m_Profile.m_SkeletonOffset.m_Rotation.x,
                         m_Profile.m_SkeletonOffset.m_Rotation.y,
                         m_Profile.m_SkeletonOffset.m_Rotation.z };
        if (ImGui::DragFloat3("偏移旋转", rot, 0.5f))
        {
            m_Profile.m_SkeletonOffset.m_Rotation = glm::vec3(rot[0], rot[1], rot[2]);
            m_IsDirty = true;
            if (m_SkeletonLoaded)
                RebuildEditorBonePositions();
        }

        // 缩放偏移
        float scl[3] = { m_Profile.m_SkeletonOffset.m_Scale.x,
                         m_Profile.m_SkeletonOffset.m_Scale.y,
                         m_Profile.m_SkeletonOffset.m_Scale.z };
        if (ImGui::DragFloat3("偏移缩放", scl, 0.01f))
        {
            m_Profile.m_SkeletonOffset.m_Scale = glm::vec3(scl[0], scl[1], scl[2]);
            m_IsDirty = true;
            if (m_SkeletonLoaded)
                RebuildEditorBonePositions();
        }

        ImGui::Separator();

        // 绑定模式选择
        const char* bindModeItems[] = { "单骨骼（最近骨骼）", "多骨骼（混合权重）" };
        int currentMode = static_cast<int>(m_BindMode);
        if (ImGui::Combo("绑定模式", &currentMode, bindModeItems, 2))
            m_BindMode = static_cast<BindMode>(currentMode);

        // 自动计算绑定按钮
        if (ImGui::Button("自动计算绑定"))
            AutoBindPinnedVertices();

        // 绑定结果提示（计时器控制显示时长）
        if (m_BindResultTimer > 0.0f && !m_BindResultMessage.empty())
        {
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.2f, 1.0f),
                "%s", m_BindResultMessage.c_str());
        }
    }

    // =========================================================================
    // LoadReferenceSkeleton — 用 Assimp 解析骨架，构建 EditorBone 列表
    // =========================================================================

    void VansClothProfileEditorWindow::LoadReferenceSkeleton()
    {
        m_SkeletonLoaded = false;
        m_SkeletonLoadError.clear();
        m_EditorBones.clear();

        if (m_Profile.m_ReferenceSkeletonPath.empty())
        {
            m_SkeletonLoadError = "骨架路径为空";
            return;
        }

        // 将相对路径拼接为绝对路径（与 LoadModelFromProfile 保持一致）
        std::string absSkeletonPath = m_Profile.m_ReferenceSkeletonPath;
        const std::string& projectRoot = Vans::VansProjectManager::Get().GetProjectRootPath();
        if (!projectRoot.empty() && !fs::path(absSkeletonPath).is_absolute())
            absSkeletonPath = projectRoot + absSkeletonPath;

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(
            absSkeletonPath,
            aiProcess_GlobalScale | aiProcess_FlipUVs);

        if (!scene || !scene->mRootNode)
        {
            m_SkeletonLoadError = importer.GetErrorString();
            return;
        }

        // 递归遍历骨骼节点，构建 EditorBone 列表
        std::function<void(const aiNode*, int, const glm::mat4&)> traverseNode =
            [&](const aiNode* node, int parentIdx, const glm::mat4& parentGlobal)
        {
            // 将 aiMatrix4x4 转为 glm::mat4（行主序 → 列主序）
            const aiMatrix4x4& aiLocal = node->mTransformation;
            glm::mat4 localMat = glm::mat4(
                aiLocal.a1, aiLocal.b1, aiLocal.c1, aiLocal.d1,
                aiLocal.a2, aiLocal.b2, aiLocal.c2, aiLocal.d2,
                aiLocal.a3, aiLocal.b3, aiLocal.c3, aiLocal.d3,
                aiLocal.a4, aiLocal.b4, aiLocal.c4, aiLocal.d4);

            glm::mat4 globalMat = parentGlobal * localMat;

            EditorBone bone;
            bone.m_Name         = node->mName.C_Str();
            bone.m_ParentIndex  = parentIdx;
            bone.m_GlobalTransform = globalMat;
            // 头部位置暂存为全局平移分量，后续由 RebuildEditorBonePositions 调整
            bone.m_HeadPos = glm::vec3(globalMat[3]);
            bone.m_TailPos = bone.m_HeadPos; // 先填相同值，后续更新

            int myIdx = static_cast<int>(m_EditorBones.size());
            m_EditorBones.push_back(bone);

            for (unsigned int c = 0; c < node->mNumChildren; ++c)
                traverseNode(node->mChildren[c], myIdx, globalMat);
        };

        traverseNode(scene->mRootNode, -1, glm::mat4(1.0f));

        // 计算尾部位置：子骨骼头部 = 父骨骼尾部；叶节点外推
        for (int i = 0; i < static_cast<int>(m_EditorBones.size()); ++i)
        {
            // 找第一个子节点
            int firstChild = -1;
            for (int j = i + 1; j < static_cast<int>(m_EditorBones.size()); ++j)
            {
                if (m_EditorBones[j].m_ParentIndex == i)
                {
                    firstChild = j;
                    break;
                }
            }

            if (firstChild >= 0)
            {
                m_EditorBones[i].m_TailPos = m_EditorBones[firstChild].m_HeadPos;
            }
            else
            {
                // 叶节点：沿父骨骼方向外推 0.1 个单位
                if (m_EditorBones[i].m_ParentIndex >= 0)
                {
                    glm::vec3 parentHead =
                        m_EditorBones[m_EditorBones[i].m_ParentIndex].m_HeadPos;
                    glm::vec3 dir = m_EditorBones[i].m_HeadPos - parentHead;
                    float len = glm::length(dir);
                    if (len > 1e-5f)
                        m_EditorBones[i].m_TailPos =
                            m_EditorBones[i].m_HeadPos + (dir / len) * 0.1f;
                    else
                        m_EditorBones[i].m_TailPos =
                            m_EditorBones[i].m_HeadPos + glm::vec3(0.0f, 0.1f, 0.0f);
                }
                else
                {
                    m_EditorBones[i].m_TailPos =
                        m_EditorBones[i].m_HeadPos + glm::vec3(0.0f, 0.1f, 0.0f);
                }
            }
        }

        m_SkeletonLoaded = true;
        RebuildEditorBonePositions();
    }

    // =========================================================================
    // RebuildEditorBonePositions — 将骨骼全局变换乘以 skeletonOffset 矩阵，
    //                              重新计算编辑器中每根骨骼的头/尾世界坐标
    // =========================================================================

    void VansClothProfileEditorWindow::RebuildEditorBonePositions()
    {
        if (m_EditorBones.empty())
            return;

        glm::mat4 offsetMat = m_Profile.GetSkeletonOffsetMatrix();

        // 第一遍：更新所有骨骼头部位置
        for (auto& bone : m_EditorBones)
        {
            glm::vec4 h = offsetMat * glm::vec4(glm::vec3(bone.m_GlobalTransform[3]), 1.0f);
            bone.m_HeadPos = glm::vec3(h);
        }

        // 第二遍：更新尾部位置（与 LoadReferenceSkeleton 中逻辑一致）
        for (int i = 0; i < static_cast<int>(m_EditorBones.size()); ++i)
        {
            int firstChild = -1;
            for (int j = i + 1; j < static_cast<int>(m_EditorBones.size()); ++j)
            {
                if (m_EditorBones[j].m_ParentIndex == i)
                {
                    firstChild = j;
                    break;
                }
            }

            if (firstChild >= 0)
            {
                m_EditorBones[i].m_TailPos = m_EditorBones[firstChild].m_HeadPos;
            }
            else
            {
                if (m_EditorBones[i].m_ParentIndex >= 0)
                {
                    glm::vec3 parentHead =
                        m_EditorBones[m_EditorBones[i].m_ParentIndex].m_HeadPos;
                    glm::vec3 dir = m_EditorBones[i].m_HeadPos - parentHead;
                    float len = glm::length(dir);
                    if (len > 1e-5f)
                        m_EditorBones[i].m_TailPos =
                            m_EditorBones[i].m_HeadPos + (dir / len) * 0.1f;
                    else
                        m_EditorBones[i].m_TailPos =
                            m_EditorBones[i].m_HeadPos + glm::vec3(0.0f, 0.1f, 0.0f);
                }
                else
                {
                    m_EditorBones[i].m_TailPos =
                        m_EditorBones[i].m_HeadPos + glm::vec3(0.0f, 0.1f, 0.0f);
                }
            }
        }
    }

    // =========================================================================
    // DrawSkeletonOverlay — 在网格视口上叠加绘制骨架（蓝色线段 + 圆点）
    // =========================================================================

    void VansClothProfileEditorWindow::DrawSkeletonOverlay(
        ImDrawList* drawList,
        const glm::mat4& mvp,
        ImVec2 viewportMin,
        ImVec2 viewportSize)
    {
        // 辅助 lambda：局部空间坐标 → 屏幕像素坐标
        auto Project = [&](const glm::vec3& p) -> ImVec2
        {
            glm::vec4 clip = mvp * glm::vec4(p, 1.0f);
            if (clip.w <= 0.001f) return { -9999.0f, -9999.0f };
            glm::vec2 ndc = glm::vec2(clip.x, clip.y) / clip.w;
            return {
                viewportMin.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x,
                viewportMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y
            };
        };

        constexpr ImU32 kBoneLineColor   = IM_COL32(80,  140, 255, 180);
        constexpr ImU32 kBoneHeadColor   = IM_COL32(120, 180, 255, 220);
        constexpr float kBoneHeadRadius  = 4.0f;

        for (const auto& bone : m_EditorBones)
        {
            // 绘制骨骼连线（从父骨骼头部到本骨骼头部）
            if (bone.m_ParentIndex >= 0
                && bone.m_ParentIndex < static_cast<int>(m_EditorBones.size()))
            {
                ImVec2 p0 = Project(m_EditorBones[bone.m_ParentIndex].m_HeadPos);
                ImVec2 p1 = Project(bone.m_HeadPos);
                drawList->AddLine(p0, p1, kBoneLineColor, 1.5f);
            }

            // 绘制骨骼头部圆点
            ImVec2 h = Project(bone.m_HeadPos);
            drawList->AddCircleFilled(h, kBoneHeadRadius, kBoneHeadColor);
        }
    }

    // =========================================================================
    // DistanceToBoneSegment — 点到骨骼线段（头→尾）的最短距离
    // =========================================================================

    float VansClothProfileEditorWindow::DistanceToBoneSegment(
        const glm::vec3& point,
        const EditorBone& bone) const
    {
        glm::vec3 ab = bone.m_TailPos - bone.m_HeadPos;
        float abLen2 = glm::dot(ab, ab);

        if (abLen2 < 1e-10f)
            return glm::length(point - bone.m_HeadPos);

        float t = glm::clamp(glm::dot(point - bone.m_HeadPos, ab) / abLen2, 0.0f, 1.0f);
        glm::vec3 closest = bone.m_HeadPos + t * ab;
        return glm::length(point - closest);
    }

    // =========================================================================
    // AutoBindPinnedVertices — 自动计算固定点骨骼绑定关系
    // =========================================================================

    void VansClothProfileEditorWindow::AutoBindPinnedVertices()
    {
        if (m_Profile.m_PinnedLocalPositions.empty())
        {
            m_BindResultMessage = "没有固定点，请先在网格上拾取";
            m_BindResultTimer   = 3.0f;
            return;
        }
        if (!m_SkeletonLoaded || m_EditorBones.empty())
        {
            m_BindResultMessage = "骨架未加载，请先指定骨架路径并加载";
            m_BindResultTimer   = 3.0f;
            return;
        }

        m_Profile.m_PinnedBoneBindings.clear();
        m_Profile.m_PinnedBoneBindings.reserve(
            m_Profile.m_PinnedLocalPositions.size());

        constexpr int   kMaxBones      = 4;
        constexpr float kMinWeightTh   = 0.05f;    // 低于此权重的骨骼影响将被丢弃

        for (const glm::vec3& pinPos : m_Profile.m_PinnedLocalPositions)
        {
            VansEngine::VansClothProfile::PinBoneBinding binding;

            if (m_BindMode == BindMode::SingleBone)
            {
                // 单骨骼：找距离最近的骨骼
                float    minDist  = std::numeric_limits<float>::max();
                int      bestBone = -1;

                for (int b = 0; b < static_cast<int>(m_EditorBones.size()); ++b)
                {
                    float d = DistanceToBoneSegment(pinPos, m_EditorBones[b]);
                    if (d < minDist)
                    {
                        minDist  = d;
                        bestBone = b;
                    }
                }

                if (bestBone >= 0)
                {
                    binding.m_BoneNames.push_back(m_EditorBones[bestBone].m_Name);
                    binding.m_Weights.push_back(1.0f);
                }
            }
            else
            {
                // 多骨骼：取最近 K 根，按距离倒数平方计算权重
                struct BoneDistPair { int idx; float dist; };
                std::vector<BoneDistPair> pairs;
                pairs.reserve(m_EditorBones.size());

                for (int b = 0; b < static_cast<int>(m_EditorBones.size()); ++b)
                {
                    float d = DistanceToBoneSegment(pinPos, m_EditorBones[b]);
                    pairs.push_back({ b, d });
                }

                // 升序排序，取前 kMaxBones 根
                std::sort(pairs.begin(), pairs.end(),
                    [](const BoneDistPair& a, const BoneDistPair& b)
                    { return a.dist < b.dist; });

                int count = std::min(static_cast<int>(pairs.size()), kMaxBones);
                float weightSum = 0.0f;
                std::vector<float> rawWeights(count);

                for (int k = 0; k < count; ++k)
                {
                    float d   = pairs[k].dist;
                    float inv = (d > 1e-5f) ? (1.0f / (d * d)) : 1e10f;
                    rawWeights[k] = inv;
                    weightSum    += inv;
                }

                // 归一化 + 过滤低权重骨骼
                for (int k = 0; k < count; ++k)
                {
                    float w = (weightSum > 1e-10f) ? rawWeights[k] / weightSum : 0.0f;
                    if (w < kMinWeightTh)
                        continue;
                    binding.m_BoneNames.push_back(m_EditorBones[pairs[k].idx].m_Name);
                    binding.m_Weights.push_back(w);
                }

                // 若过滤后为空则回退到最近骨骼
                if (binding.m_BoneNames.empty() && !pairs.empty())
                {
                    binding.m_BoneNames.push_back(m_EditorBones[pairs[0].idx].m_Name);
                    binding.m_Weights.push_back(1.0f);
                }
                else
                {
                    // 再次归一化（过滤后权重之和不为 1）
                    float sum2 = 0.0f;
                    for (float w : binding.m_Weights) sum2 += w;
                    if (sum2 > 1e-10f)
                        for (float& w : binding.m_Weights) w /= sum2;
                }
            }

            m_Profile.m_PinnedBoneBindings.push_back(std::move(binding));
        }

        m_IsDirty = true;
        m_BindResultMessage = "绑定完成，共处理 "
            + std::to_string(m_Profile.m_PinnedBoneBindings.size())
            + " 个固定点";
        m_BindResultTimer = 3.0f;
    }

    // =========================================================================
    // LoadModelFromProfile — 通过 Assimp CPU-only 读取网格数据
    // =========================================================================

    void VansClothProfileEditorWindow::LoadModelFromProfile()
    {
        m_MeshLoaded  = false;
        m_MeshLoadError.clear();
        m_WeldedParticles.clear();
        m_WeldedTriangles.clear();

        if (m_Profile.m_ModelPath.empty())
        {
            m_MeshLoadError = "Profile 中未设置 modelPath。";
            return;
        }

        // 将 modelPath（相对路径）拼接为绝对路径后再传给 Assimp
        std::string absModelPath = m_Profile.m_ModelPath;
        const std::string& projectRoot = Vans::VansProjectManager::Get().GetProjectRootPath();
        if (!projectRoot.empty() && !fs::path(absModelPath).is_absolute())
            absModelPath = projectRoot + absModelPath;

        RawEditorMesh raw = LoadRawMeshFromFile(absModelPath);
        if (!raw.m_Ok)
        {
            m_MeshLoadError = raw.m_Error;
            VANS_LOG_ERROR("[VansClothProfileEditor] 网格加载失败: " << raw.m_Error);
            return;
        }

        BuildWeldedParticles(raw.m_Positions, raw.m_Indices);
        m_MeshLoaded = true;

        // 根据 Profile 的 m_PinnedLocalPositions 标记初始固定点
        // 对每个保存的固定点坐标，找距离最近的唯一粒子并标记，
        // 避免半径范围匹配（原逻辑）在顶点密集模型上将一大片邻近粒子误标为固定。
        for (const glm::vec3& pinPos : m_Profile.m_PinnedLocalPositions)
        {
            float minDistSq = std::numeric_limits<float>::max();
            int   bestIdx   = -1;
            for (int i = 0; i < static_cast<int>(m_WeldedParticles.size()); ++i)
            {
                const glm::vec3& lp = m_WeldedParticles[i].m_LocalPosition;
                float dx = lp.x - pinPos.x;
                float dy = lp.y - pinPos.y;
                float dz = lp.z - pinPos.z;
                float distSq = dx*dx + dy*dy + dz*dz;
                if (distSq < minDistSq)
                {
                    minDistSq = distSq;
                    bestIdx   = i;
                }
            }
            // 仅当最近粒子仍在容差范围内时才标记（防止坐标系不匹配导致误固定）
            float tol = m_Profile.m_PinnedMatchTolerance;
            if (bestIdx >= 0 && std::sqrt(minDistSq) < tol)
            {
                m_WeldedParticles[bestIdx].m_IsPinned = true;
            }
        }

        // 自动调整相机距离以适应模型包围盒
        if (!m_WeldedParticles.empty())
        {
            glm::vec3 minP = m_WeldedParticles[0].m_LocalPosition;
            glm::vec3 maxP = minP;
            for (const EditorParticle& ep : m_WeldedParticles)
            {
                minP = glm::min(minP, ep.m_LocalPosition);
                maxP = glm::max(maxP, ep.m_LocalPosition);
            }
            m_CameraTarget   = (minP + maxP) * 0.5f;
            m_CameraDistance = glm::length(maxP - minP) * 1.2f;
            if (m_CameraDistance < 0.1f) m_CameraDistance = 1.0f;
        }

        VANS_LOG("[VansClothProfileEditor] 网格加载完成，焊接粒子数=" << m_WeldedParticles.size());
    }

    // =========================================================================
    // BuildWeldedParticles — 顶点位置焊接（与 VansClothNode 保持一致）
    // =========================================================================

    void VansClothProfileEditorWindow::BuildWeldedParticles(
        const std::vector<glm::vec3>& rawPositions,
        const std::vector<uint32_t>&  rawIndices)
    {
        const float WELD_GRID = 1e5f; // 量化精度与 VansClothNode 保持一致
        std::map<std::tuple<int,int,int>, uint32_t> posToWelded;
        std::vector<uint32_t> origToWelded(rawPositions.size());
        int weldedCount = 0;

        for (int v = 0; v < static_cast<int>(rawPositions.size()); ++v)
        {
            float x = rawPositions[v].x;
            float y = rawPositions[v].y;
            float z = rawPositions[v].z;
            auto key = std::make_tuple(
                static_cast<int>(std::round(x * WELD_GRID)),
                static_cast<int>(std::round(y * WELD_GRID)),
                static_cast<int>(std::round(z * WELD_GRID)));

            auto it = posToWelded.find(key);
            if (it != posToWelded.end())
            {
                origToWelded[v] = it->second;
            }
            else
            {
                uint32_t wIdx     = static_cast<uint32_t>(weldedCount++);
                posToWelded[key]  = wIdx;
                origToWelded[v]   = wIdx;

                EditorParticle ep;
                ep.m_LocalPosition = rawPositions[v];
                ep.m_IsPinned      = false;
                m_WeldedParticles.push_back(ep);
            }
        }

        // 构建焊接后的三角形索引，跳过退化三角形
        m_WeldedTriangles.clear();
        m_WeldedTriangles.reserve(rawIndices.size());
        for (size_t t = 0; t + 2 < rawIndices.size(); t += 3)
        {
            uint32_t w0 = origToWelded[rawIndices[t + 0]];
            uint32_t w1 = origToWelded[rawIndices[t + 1]];
            uint32_t w2 = origToWelded[rawIndices[t + 2]];
            if (w0 == w1 || w1 == w2 || w0 == w2) continue;
            m_WeldedTriangles.push_back(w0);
            m_WeldedTriangles.push_back(w1);
            m_WeldedTriangles.push_back(w2);
        }
    }

    // =========================================================================
    // LoadRawMeshFromFile — 静态辅助：Assimp CPU-only 读取
    // 不创建任何 VkBuffer，不需要 VkDevice
    // =========================================================================

    VansClothProfileEditorWindow::RawEditorMesh
    VansClothProfileEditorWindow::LoadRawMeshFromFile(const std::string& modelPath)
    {
        RawEditorMesh result;

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(
            modelPath,
            aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);

        if (!scene || !scene->HasMeshes())
        {
            result.m_Error = "Assimp 加载失败: " + std::string(importer.GetErrorString())
                             + "（路径: " + modelPath + "）";
            return result;
        }

        aiMatrix4x4 identityTransform;
        ClothEditorRawMeshData rawData;
        AppendAiNodeMeshes(scene->mRootNode, scene, identityTransform, rawData);
        if (rawData.m_Positions.empty() || rawData.m_Indices.empty())
        {
            result.m_Error = "Assimp 加载成功，但没有可用的三角形网格（路径: " + modelPath + "）";
            return result;
        }

        result.m_Positions = std::move(rawData.m_Positions);
        result.m_Indices   = std::move(rawData.m_Indices);

        result.m_Ok = true;
        return result;
    }
}
