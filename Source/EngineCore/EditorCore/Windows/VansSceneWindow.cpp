#include "VansSceneWindow.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "ImGuizmo.h"
#include <filesystem>
#include <fstream>
#include <cctype>
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
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
#include "../../PhysicsCore/VansPhysicsVehicle.h"
#include "../../ScriptCore/VansTransform.h"
#include "VansHierachyWindow.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../AssetCore/VansAssetDatabase.h"
#include "../../AssetCore/VansAssetGuid.h"

namespace
{
std::string LowerAscii(std::string value)
{
    for (char& c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::string TextureFileKey(const std::string& path)
{
    if (path.empty())
        return {};
    return LowerAscii(std::filesystem::path(path).filename().string());
}

struct GeneratedMaterialLookup
{
    std::vector<std::string> byIndex;
    std::unordered_map<std::string, std::string> byTextureName;
};

GeneratedMaterialLookup BuildGeneratedMaterialLookup(const std::string& modelGuid)
{
    GeneratedMaterialLookup lookup;
    auto* database = Vans::VansProjectManager::Get().GetAssetDatabase();
    if (database == nullptr || modelGuid.empty())
        return lookup;

    Vans::VansAssetGuid parsedGuid;
    if (!Vans::VansAssetGuid::TryParse(modelGuid, parsedGuid))
        return lookup;

    const auto record = database->Find(parsedGuid);
    if (!record || record->metaPath.empty())
        return lookup;

    std::ifstream input(record->metaPath);
    if (!input)
        return lookup;

    const auto meta = Vans::SceneJson::parse(input, nullptr, false);
    if (meta.is_discarded() || !meta.is_object())
        return lookup;

    const auto settingsIt = meta.find("settings");
    if (settingsIt == meta.end() || !settingsIt->is_object())
        return lookup;
    const auto reportIt = settingsIt->find("importReport");
    if (reportIt == settingsIt->end() || !reportIt->is_object())
        return lookup;

    std::unordered_map<std::string, std::string> textureGuidToName;
    const auto texturesIt = reportIt->find("textures");
    if (texturesIt != reportIt->end() && texturesIt->is_array())
    {
        for (const auto& texture : *texturesIt)
        {
            if (!texture.is_object())
                continue;
            const std::string guid = texture.value("guid", "");
            std::string name = texture.value("name", "");
            if (name.empty())
                name = TextureFileKey(texture.value("path", ""));
            else
                name = TextureFileKey(name);
            if (!guid.empty() && !name.empty())
                textureGuidToName[guid] = name;
        }
    }

    const auto matsIt = reportIt->find("generatedMaterials");
    if (matsIt != reportIt->end() && matsIt->is_array())
    {
        for (const auto& material : *matsIt)
        {
            if (!material.is_object())
                continue;
            const std::string matGuid = material.value("guid", "");
            if (matGuid.empty())
                continue;

            lookup.byIndex.push_back(matGuid);
            const std::string textureGuid = material.value("texture", "");
            const auto texIt = textureGuidToName.find(textureGuid);
            if (texIt != textureGuidToName.end())
                lookup.byTextureName[texIt->second] = matGuid;
        }
    }

    return lookup;
}

std::string ResolveGeneratedMaterialOverride(const GeneratedMaterialLookup& lookup,
    const VansGraphics::FBXSubmeshMaterialInfo& fbxInfo,
    uint32_t submeshIndex)
{
    const std::string textureKey = TextureFileKey(fbxInfo.diffuseTexPath);
    if (!textureKey.empty())
    {
        const auto found = lookup.byTextureName.find(textureKey);
        if (found != lookup.byTextureName.end())
            return found->second;
    }
    if (submeshIndex < lookup.byIndex.size())
        return lookup.byIndex[submeshIndex];
    return {};
}
}

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
                                    VansMesh* droppedMesh = nullptr;
                                    if (vkDev && m_Scene && !m_Scene->HasProjectMeshAlias(meshName))
                                    {
                                        auto* mesh = new VansMesh(false, false);
                                        mesh->LoadMesh(vkDev->GetLogicDevice(),
                                            vkDev->GetGraphicsQueue(),
                                            &vkDev->GetCommandBuffer(),
                                            record->sourcePath.string(), false);
                                        mesh->SetName(meshName);
                                        m_Scene->AddMeshAsset(mesh);
                                        m_Scene->SetProjectMeshAlias(meshName, mesh);
                                        VANS_LOG("[SceneWindow] Mesh loaded: "
                                            << record->sourcePath.string() << " as '" << meshName << "'");
                                    }
                                    if (m_Scene)
                                        droppedMesh = static_cast<VansMesh*>(m_Scene->FindMeshAsset(meshName));

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
                                        auto* doc = VansEditorWindow::GetSceneDocument();
                                        auto* editService = VansEditorWindow::GetSceneEditService();
                                        if (droppedMesh && droppedMesh->m_IsMultiMesh && doc && editService && !guidStr.empty())
                                        {
                                            const std::string modelGuid = guidStr;
                                            m_Scene->SetProjectMeshAlias(modelGuid, m_Scene->FindMeshAsset(meshName));

                                            const std::string parentId = Vans::VansAssetGuid::New().ToString();
                                            glm::quat rotQuat = glm::quat(glm::radians(glm::vec3(0.0f)));

                                            Vans::SceneJson transformComp;
                                            transformComp["id"]      = Vans::VansAssetGuid::New().ToString();
                                            transformComp["type"]    = "Transform";
                                            transformComp["version"] = 1u;
                                            transformComp["enabled"] = true;
                                            transformComp["data"]    = {
                                                {"position", {worldPos.x, worldPos.y, worldPos.z}},
                                                {"rotation", {rotQuat.x, rotQuat.y, rotQuat.z, rotQuat.w}},
                                                {"scale",    {1.0f, 1.0f, 1.0f}}
                                            };

                                            Vans::SceneJson rootComp;
                                            rootComp["id"]      = Vans::VansAssetGuid::New().ToString();
                                            rootComp["type"]    = "MultiMeshRoot";
                                            rootComp["version"] = 1u;
                                            rootComp["enabled"] = true;
                                            rootComp["data"]    = {
                                                {"model", {{"guid", modelGuid}}},
                                                {"submeshCount", static_cast<uint32_t>(droppedMesh->m_SubMeshes.size())},
                                                {"generation", "object-hierarchy"}
                                            };

                                            Vans::SceneJson parentEntity;
                                            parentEntity["id"] = parentId;
                                            parentEntity["name"] = uniqueName;
                                            parentEntity["parent"] = nullptr;
                                            parentEntity["components"] = Vans::SceneJson::array(
                                                { std::move(transformComp), std::move(rootComp) });
                                            editService->Set("/entities/-", parentEntity);

                                            std::unordered_set<std::string> usedSlots;
                                            const GeneratedMaterialLookup generatedMaterials =
                                                BuildGeneratedMaterialLookup(modelGuid);
                                            auto makeSlotName = [&](const std::string& nodeName,
                                                                    const std::string& materialName,
                                                                    uint32_t index)
                                            {
                                                std::string base = (!nodeName.empty() || !materialName.empty())
                                                    ? nodeName + "/" + materialName
                                                    : "Submesh_" + std::to_string(index);
                                                if (base == "/")
                                                    base = "Submesh_" + std::to_string(index);
                                                std::string candidate = base;
                                                uint32_t suffix = 1;
                                                while (!usedSlots.insert(candidate).second)
                                                    candidate = base + "_" + std::to_string(suffix++);
                                                return candidate;
                                            };

                                            for (uint32_t i = 0; i < droppedMesh->m_SubMeshes.size(); ++i)
                                            {
                                                VansMesh* subMesh = droppedMesh->m_SubMeshes[i];
                                                if (!subMesh || subMesh->GetMeshVertexCount() == 0 || subMesh->GetIndexCount() == 0)
                                                    continue;

                                                const auto& matInfos = droppedMesh->m_SubmeshMaterialInfos;
                                                const VansGraphics::FBXSubmeshMaterialInfo fbxInfo = matInfos.empty()
                                                    ? VansGraphics::FBXSubmeshMaterialInfo{}
                                                    : (i < matInfos.size() ? matInfos[i] : matInfos[0]);
                                                const std::string sourceNode = subMesh->m_SourceNodeName;
                                                const std::string sourceMaterial = fbxInfo.materialName;
                                                const std::string slotName = makeSlotName(sourceNode, sourceMaterial, i);

                                                Vans::SceneJson childTransform;
                                                childTransform["id"]      = Vans::VansAssetGuid::New().ToString();
                                                childTransform["type"]    = "Transform";
                                                childTransform["version"] = 1u;
                                                childTransform["enabled"] = true;
                                                childTransform["data"] = {
                                                    {"position", {0.0f, 0.0f, 0.0f}},
                                                    {"rotation", {0.0f, 0.0f, 0.0f, 1.0f}},
                                                    {"scale",    {1.0f, 1.0f, 1.0f}}
                                                };

                                                Vans::SceneJson modelData;
                                                modelData["model"] = { {"guid", modelGuid} };
                                                modelData["submesh"] = {
                                                    {"index", i},
                                                    {"sourceNode", sourceNode},
                                                    {"sourceMaterial", sourceMaterial},
                                                    {"slotName", slotName}
                                                };
                                                modelData["castShadows"] = true;
                                                modelData["receiveShadows"] = true;
                                                modelData["rayTracingMode"] = "auto";
                                                modelData["visibilityMask"] = 0xffffffffu;
                                                const std::string materialGuid =
                                                    ResolveGeneratedMaterialOverride(generatedMaterials, fbxInfo, i);
                                                if (!materialGuid.empty())
                                                    modelData["materialOverrides"] = {
                                                        {"default", {{"guid", materialGuid}}}
                                                    };
                                                else
                                                    modelData["materialOverrides"] = Vans::SceneJson::object();
                                                modelData["orphanOverrides"] = Vans::SceneJson::object();

                                                Vans::SceneJson rendererComp;
                                                rendererComp["id"]      = Vans::VansAssetGuid::New().ToString();
                                                rendererComp["type"]    = "ModelRenderer";
                                                rendererComp["version"] = 1u;
                                                rendererComp["enabled"] = true;
                                                rendererComp["data"]    = std::move(modelData);

                                                std::string childName = uniqueName + "_" +
                                                    (!sourceNode.empty() ? sourceNode : ("Submesh_" + std::to_string(i)));
                                                Vans::SceneJson childEntity;
                                                childEntity["id"] = Vans::VansAssetGuid::New().ToString();
                                                childEntity["name"] = childName;
                                                childEntity["parent"] = parentId;
                                                childEntity["components"] = Vans::SceneJson::array(
                                                    { std::move(childTransform), std::move(rendererComp) });
                                                editService->Set("/entities/-", childEntity);
                                            }
                                        }
                                        else
                                        {
                                            VansScriptObject* obj = m_Scene->CreateEntity(
                                                vkDev->GetLogicDevice(), uniqueName,
                                                meshName, "DefaultPBR", worldPos);

                                            // ── 同步写入 JSON 文档（SchemaV2 格式）──
                                            // 注意：必须以 "entities/-" JSON Pointer 追加，否则 schema 验证失败
                                            if (obj && !guidStr.empty() && doc && editService)
                                            {
                                                const std::string modelGuid = guidStr;

                                                // 同时注册 mesh 的 GUID 别名（场景重载时 BuildRuntimeSceneFromV2 按 GUID 查找 mesh）
                                                if (m_Scene)
                                                    m_Scene->SetProjectMeshAlias(modelGuid, m_Scene->FindMeshAsset(meshName));

                                                Vans::SceneJson transformComp;
                                                transformComp["id"]      = Vans::VansAssetGuid::New().ToString();
                                                transformComp["type"]    = "Transform";
                                                transformComp["version"] = 1u;
                                                transformComp["enabled"] = true;
                                                glm::quat rotQuat = glm::quat(glm::radians(glm::vec3(0.0f)));
                                                transformComp["data"]    = {
                                                    {"position", {worldPos.x, worldPos.y, worldPos.z}},
                                                    {"rotation", {rotQuat.x, rotQuat.y, rotQuat.z, rotQuat.w}},
                                                    {"scale",    {1.0f, 1.0f, 1.0f}}
                                                };

                                                std::string materialGuid;
                                                const auto& materials = m_Scene->GetMaterialAssets();
                                                if (!materials.empty())
                                                    materialGuid = materials[0]->m_AssetName;

                                                Vans::SceneJson modelData;
                                                modelData["model"] = { {"guid", modelGuid} };
                                                if (!materialGuid.empty())
                                                    modelData["materialOverrides"] = {
                                                        {"default", {{"guid", materialGuid}}}
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

            // ── Vehicle physics debug visualization ───────────────────────
            if (m_Scene && m_Scene->GetVehicle())
            {
                ImGui::Checkbox("Vehicle Debug", &VansEditorWindow::m_VehicleDebugGizmos);
            }
            if (VansEditorWindow::m_VehicleDebugGizmos && m_Scene && m_Scene->GetVehicle())
            {
                VansEngine::VansPhysicsVehicle* vehicle = m_Scene->GetVehicle();
                const glm::mat4 view = m_Camera->GetViewMatrix();
                const glm::mat4 proj = m_Camera->GetProjectiveMatrix();
                const glm::mat4 viewProj = proj * view;
                ImDrawList* dl = ImGui::GetWindowDrawList();

                auto toGlm = [](const PxVec3& v) { return glm::vec3(v.x, v.y, v.z); };
                auto axisToPx = [](PxVehicleAxes::Enum axis) -> PxVec3
                {
                    switch (axis)
                    {
                    case PxVehicleAxes::ePosX: return PxVec3(1.0f, 0.0f, 0.0f);
                    case PxVehicleAxes::eNegX: return PxVec3(-1.0f, 0.0f, 0.0f);
                    case PxVehicleAxes::ePosY: return PxVec3(0.0f, 1.0f, 0.0f);
                    case PxVehicleAxes::eNegY: return PxVec3(0.0f, -1.0f, 0.0f);
                    case PxVehicleAxes::ePosZ: return PxVec3(0.0f, 0.0f, 1.0f);
                    case PxVehicleAxes::eNegZ: return PxVec3(0.0f, 0.0f, -1.0f);
                    default: return PxVec3(0.0f, 1.0f, 0.0f);
                    }
                };
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
                auto drawOrientedBox = [&](const PxTransform& pose, const PxVec3& halfExtents, ImU32 color)
                {
                    const PxVec3 he = halfExtents;
                    const PxVec3 localCorners[8] = {
                        PxVec3(-he.x, -he.y, -he.z), PxVec3( he.x, -he.y, -he.z),
                        PxVec3( he.x,  he.y, -he.z), PxVec3(-he.x,  he.y, -he.z),
                        PxVec3(-he.x, -he.y,  he.z), PxVec3( he.x, -he.y,  he.z),
                        PxVec3( he.x,  he.y,  he.z), PxVec3(-he.x,  he.y,  he.z)
                    };
                    glm::vec3 corners[8];
                    for (int i = 0; i < 8; ++i)
                        corners[i] = toGlm(pose.transform(localCorners[i]));
                    const int edges[12][2] = {
                        {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
                    };
                    for (const auto& edge : edges)
                        drawLineWorld(corners[edge[0]], corners[edge[1]], color, 2.0f);
                };

                const PxTransform bodyPose = vehicle->GetTransform();
                const VansEngine::VansVehicleTuning& tuning = vehicle->GetTuning();
                const PxTransform chassisPose = bodyPose * vehicle->GetBodyBoxLocalPose();
                drawOrientedBox(chassisPose, vehicle->GetBodyBoxHalfExtents(), IM_COL32(70, 170, 255, 235));
                drawLabelWorld(toGlm(chassisPose.p), IM_COL32(120, 210, 255, 255), "chassis collider");

                const PxVec3 localLat = axisToPx(tuning.lateralAxis);
                const PxVec3 localVrt = axisToPx(tuning.verticalAxis);
                const PxVec3 localLng = axisToPx(tuning.longitudinalAxis);
                const uint32_t wheelCount = std::min<uint32_t>(vehicle->GetNumWheels(), 4u);
                for (uint32_t wi = 0; wi < wheelCount; ++wi)
                {
                    const PxTransform wheelPose = vehicle->GetWheelWorldPose(wi);
                    const float radius = vehicle->GetWheelRadius(wi);
                    const float halfWidth = vehicle->GetWheelHalfWidth(wi);
                    const glm::vec3 center = toGlm(wheelPose.p);
                    const glm::vec3 lat = glm::normalize(toGlm(wheelPose.q.rotate(localLat)));
                    const glm::vec3 vrt = glm::normalize(toGlm(wheelPose.q.rotate(localVrt)));
                    const glm::vec3 lng = glm::normalize(toGlm(wheelPose.q.rotate(localLng)));

                    drawRingWorld(center, vrt, lng, radius, IM_COL32(255, 220, 40, 245));
                    drawLineWorld(center - lat * halfWidth, center + lat * halfWidth, IM_COL32(50, 255, 255, 245), 2.0f);
                    drawPointWorld(center, IM_COL32(255, 255, 255, 245), 3.5f);

                    const PxVec3 attachLocal = vehicle->GetSuspensionAttachmentLocal(wi);
                    const glm::vec3 attach = toGlm(bodyPose.transform(attachLocal));
                    const PxVec3 travelDir = vehicle->GetSuspensionTravelDir(wi);
                    const float travel = vehicle->GetSuspensionTravelDist(wi);
                    const glm::vec3 rayEnd = toGlm(bodyPose.transform(attachLocal + travelDir * (travel + radius)));
                    drawPointWorld(attach, IM_COL32(255, 120, 40, 245), 4.0f);
                    drawLineWorld(attach, center, IM_COL32(255, 150, 40, 220), 2.0f);
                    drawLineWorld(attach, rayEnd, IM_COL32(255, 60, 80, 190), 1.5f);

                    char label[96];
                    snprintf(label, sizeof(label), "W%u r=%.2f hw=%.2f", wi, radius, halfWidth);
                    drawLabelWorld(center + vrt * (radius + 0.05f), IM_COL32(255, 245, 140, 255), label);
                }
            }

            // ── Motion Matching 轨迹可视化 ────────────────────────────────
			if (m_Scene && !m_Scene->GetAnimationNodes().empty())
				ImGui::Checkbox("Motion Matching Debug", &VansHierachuWindow::m_ShowMMViz);
            if (VansHierachuWindow::m_ShowMMViz && m_Scene)
            {
                const glm::mat4& view = m_Camera->GetViewMatrix();
                const glm::mat4& proj = m_Camera->GetProjectiveMatrix();
                ImDrawList* dl = ImGui::GetWindowDrawList();

                for (auto* animNode : m_Scene->GetAnimationNodes())
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

            // ── Foot IK debug visualization ───────────────────────────────
            static bool s_ShowFootIKViz = false;
            if (m_Scene && !m_Scene->GetAnimationNodes().empty())
            {
                ImGui::SameLine();
                ImGui::Checkbox("Foot IK Debug", &s_ShowFootIKViz);
            }
            if (m_Scene)
            {
                const glm::mat4& view = m_Camera->GetViewMatrix();
                const glm::mat4& proj = m_Camera->GetProjectiveMatrix();
                glm::vec4 vp(0, 0, drawSize.x, drawSize.y);
                ImDrawList* dl = ImGui::GetWindowDrawList();

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

                auto drawLeg = [&](const FootPlacementDebugLeg& leg, const char* label, ImU32 chainColor)
                {
                    ImVec2 hip = projectWorld(leg.hip);
                    ImVec2 knee = projectWorld(leg.knee);
                    ImVec2 foot = projectWorld(leg.foot);
                    dl->AddLine(hip, knee, chainColor, 2.0f);
                    dl->AddLine(knee, foot, chainColor, 2.0f);
                    dl->AddCircleFilled(hip, 3.5f, IM_COL32(80, 180, 255, 230));
                    dl->AddCircleFilled(knee, 3.5f, IM_COL32(80, 180, 255, 230));
                    dl->AddCircleFilled(foot, 4.0f, IM_COL32(255, 255, 255, 240));
                    if (leg.hasOverlap)
                    {
                        drawCross(leg.overlapCenter, IM_COL32(80, 255, 255, 255), 9.0f);
                        ImVec2 oc = projectWorld(leg.overlapCenter);
                        char overlapLabel[96];
                        snprintf(overlapLabel, sizeof(overlapLabel), "OVERLAP L%u %s",
                                 leg.overlapLayer,
                                 leg.overlapActorName.c_str());
                        dl->AddText(ImVec2(oc.x + 8, oc.y - 24), IM_COL32(160, 255, 255, 240), overlapLabel);
                    }

                    int filteredHits = 0;
                    int rawHits = 0;
                    int acceptedHits = 0;
                    const FootPlacementDebugSample* firstInterestingSample = nullptr;
                    for (const auto& sample : leg.samples)
                    {
                        if (sample.hasHit)
                            ++filteredHits;
                        if (sample.hasRawHit)
                            ++rawHits;
                        if (sample.accepted)
                            ++acceptedHits;
                        if (!firstInterestingSample && !sample.status.empty())
                            firstInterestingSample = &sample;

                        const ImVec2 a = projectWorld(sample.rayStart);
                        const ImVec2 b = projectWorld(sample.rayEnd);
                        const ImU32 rayColor = sample.hasHit
                            ? (sample.accepted ? IM_COL32(0, 255, 128, 220) : IM_COL32(255, 210, 64, 150))
                            : IM_COL32(255, 70, 70, 120);
                        dl->AddLine(a, b, rayColor, sample.accepted ? 2.0f : 1.0f);
                        if (sample.hasHit)
                        {
                            ImVec2 h = projectWorld(sample.hitPosition);
                            dl->AddCircleFilled(h, sample.accepted ? 4.0f : 2.5f, rayColor);
                        }
                        else if (sample.hasRawHit)
                        {
                            ImVec2 h = projectWorld(sample.rawHitPosition);
                            dl->AddCircleFilled(h, 3.0f, IM_COL32(160, 160, 160, 180));
                        }
                    }

                    if (leg.hasContact)
                    {
                        drawCross(leg.contact, IM_COL32(0, 255, 128, 255), 6.0f);
                        dl->AddLine(projectWorld(leg.contact),
                                    projectWorld(leg.contact + leg.normal * 0.18f),
                                    IM_COL32(0, 255, 128, 220), 2.0f);
                    }
                    if (leg.hasTarget)
                    {
                        drawCross(leg.target, IM_COL32(255, 64, 255, 255), 8.0f);
                        dl->AddLine(foot, projectWorld(leg.target), IM_COL32(255, 64, 255, 200), 2.0f);
                        ImVec2 t = projectWorld(leg.target);
                        char targetLabel[64];
                        snprintf(targetLabel, sizeof(targetLabel), "%s %.2f", label, leg.targetWeight);
                        dl->AddText(ImVec2(t.x + 8, t.y - 10), IM_COL32(255, 220, 255, 240), targetLabel);
                    }

                    char statusLabel[192];
                    if (!leg.hasContact)
                    {
                        snprintf(statusLabel, sizeof(statusLabel), "%s NO CONTACT hit:%d raw:%d overlap:%d %s",
                                 label,
                                 filteredHits,
                                 rawHits,
                                 leg.hasOverlap ? 1 : 0,
                                 firstInterestingSample ? firstInterestingSample->status.c_str() : "");
                    }
                    else if (!leg.hasTarget)
                    {
                        snprintf(statusLabel, sizeof(statusLabel), "%s NO TARGET hit:%d overlap:%d weight:%.2f",
                                 label,
                                 filteredHits,
                                 leg.hasOverlap ? 1 : 0,
                                 leg.targetWeight);
                    }
                    else
                    {
                        snprintf(statusLabel, sizeof(statusLabel), "%s TARGET hit:%d accepted:%d overlap:%d weight:%.2f",
                                 label,
                                 filteredHits,
                                 acceptedHits,
                                 leg.hasOverlap ? 1 : 0,
                                 leg.targetWeight);
                    }
                    dl->AddText(ImVec2(foot.x + 8, foot.y + 8),
                                leg.hasTarget ? IM_COL32(210, 255, 210, 240) : IM_COL32(255, 110, 110, 240),
                                statusLabel);
                };

                for (auto* animNode : m_Scene->GetAnimationNodes())
                {
                    auto* ctrl = animNode ? animNode->GetController() : nullptr;
                    if (!ctrl || !ctrl->IsFootPlacementConfigured())
                        continue;
                    ctrl->SetFootPlacementDebugVisualization(s_ShowFootIKViz);
                    const auto* debug = ctrl->GetFootPlacementDebugData();
                    if (!s_ShowFootIKViz || !debug || !debug->enabled)
                        continue;
                    drawLeg(debug->left, "L IK", IM_COL32(64, 180, 255, 220));
                    drawLeg(debug->right, "R IK", IM_COL32(255, 150, 64, 220));
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
