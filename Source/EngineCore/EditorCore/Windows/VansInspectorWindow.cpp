#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#elif defined __linux

#endif
#include "vulkan/vulkan.h"

#include "VansInspectorWindow.h" 
#include "VansProjectWindow.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include "../../RenderCore/VulkanCore/VansTexture.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../RenderCore/VulkanCore/VansVKCommandBuffer.h"
#include "../../RenderCore/VansScene.h"
#include "../../RenderCore/ReflectionProbeCore/VansReflectionProbeSystem.h"

#include "../../../Graphics/Vulkan/VansVKFunctions.h"

#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

VansGraphics::VansInspectorWindow::~VansInspectorWindow()
{
    ResetProbePreview();
}

void VansGraphics::VansInspectorWindow::ResetProbePreview()
{
    for (size_t i = 0; i < m_ProbePreviewSets.size(); ++i)
    {
        if (m_ProbePreviewSets[i] != VK_NULL_HANDLE)
            ImGui_ImplVulkan_RemoveTexture(m_ProbePreviewSets[i]);
        m_ProbePreviewSets[i] = VK_NULL_HANDLE;
        m_ProbePreviewViews[i] = VK_NULL_HANDLE;
    }
    m_ProbePreviewIndex = -1;
    m_ProbePreviewMip = -1;
}

void VansGraphics::VansInspectorWindow::ShowWindow(VansVKDevice& device)
{
    auto currentFile = VansProjectWindow::m_CurrentSelectedFile;
    ImGui::Begin("Inspector");

    if (m_Scene && m_Scene->IsSceneReady())
    {
        ShowReflectionProbeEditor(device);
        if (!currentFile.empty()) ImGui::Separator();
    }

    // Inspector Window for Selected File
    if (!currentFile.empty()) {
        ImGui::Text("Selected File: %s", currentFile.filename().string().c_str());
        ImGui::Separator();

        std::string extension = currentFile.extension().string();
        InspectResourceType type = InspectResourceType::None;

        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" || extension == ".tga")
        {
            type = InspectResourceType::TextureAsset;
        }
        else if (extension == ".txt" || extension == ".cpp" || extension == ".h" || extension == ".hpp" || extension == ".c" ||
            extension == ".json" || extension == ".xml" || extension == ".lua" || extension == ".py" ||
            extension == ".shader" || extension == ".vert" || extension == ".frag" || extension == ".comp" || extension == ".glsl" ||
            extension == ".cmake" || extension == ".ini" || extension == ".log")
        {
            type = InspectResourceType::TextAsset;
        }
        else if (extension == ".fbx" || extension == ".obj" || extension == ".gltf" || extension == ".glb")
        {
            type = InspectResourceType::ModelAsset;
        }
        else if (extension == ".vclip")
        {
            type = InspectResourceType::AnimationClipAsset;
        }

        switch (type)
        {
        case InspectResourceType::TextAsset:
            ShowTextAsset();
            break;
        case InspectResourceType::TextureAsset:
            ShowTextureAsset(device);
            break;
        case InspectResourceType::ModelAsset:
            ShowModelTextureAsset(device);
            break;
        case InspectResourceType::AnimationClipAsset:
            ShowAnimationClipAsset();
            break;
        default:
            ImGui::Text("File Name: %s", currentFile.filename().string().c_str());
            break;
        }

    }
    ImGui::End();
}

void VansGraphics::VansInspectorWindow::ShowReflectionProbeEditor(VansVKDevice& device)
{
    auto* system = m_Scene ? m_Scene->GetReflectionProbeSystem() : nullptr;
    if (!system || !ImGui::CollapsingHeader("Reflection Probes", ImGuiTreeNodeFlags_DefaultOpen)) return;

    auto& probes = system->GetProbes();
    auto& results = system->GetBakeResults();
    auto& state = system->GetEditorState();
    ImGui::Text("%d probes (%u active)", (int)probes.size(),
        (unsigned)std::count_if(results.begin(), results.end(), [](const auto& r) { return r.valid; }));

    ImGui::BeginChild("##probeList", ImVec2(0.0f, 125.0f), true);
    for (int i = 0; i < (int)probes.size(); ++i)
    {
        const auto& p = probes[i];
        const bool valid = i < (int)results.size() && results[i].valid;
        ImGui::PushID(i);
        if (!valid && p.type != ReflectionProbeType::Sky)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.35f, 1.0f));
        if (ImGui::Selectable(p.name.c_str(), state.selectedProbeIndex == i))
            state.selectedProbeIndex = i;
        if (!valid && p.type != ReflectionProbeType::Sky) ImGui::PopStyleColor();
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (ImGui::Button("Add Probe"))
    {
        VansReflectionProbeDesc p;
        p.name = "Reflection Probe " + std::to_string(probes.size());
        probes.insert(probes.end() - (probes.empty() ? 0 : 1), p);
        results.insert(results.end() - (results.empty() ? 0 : 1), ReflectionProbeBakeResult{});
        state.selectedProbeIndex = std::max(0, (int)probes.size() - 2);
        ResetProbePreview(); device.WaitForDevice(); system->CreateGPUResources(device, device.GetEditorCommandBuffer());
        system->BakeQueuedProbesNow(*m_Scene, device, device.GetEditorCommandBuffer());
        system->UpdateGlobalDescriptors(m_Scene->m_GlobalDescriptorSet);
    }
    ImGui::SameLine();
    if (ImGui::Button("Rebake All"))
    {
        device.WaitForDevice();
        system->RequestBakeAll();
        system->BakeQueuedProbesNow(*m_Scene, device, device.GetEditorCommandBuffer());
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Config")) system->SaveConfiguration();

    ImGui::Checkbox("Show Probe Gizmos", &state.showProbeGizmos);
    ImGui::SameLine();
    ImGui::Checkbox("Influence Volumes", &state.showInfluenceVolumes);
    ImGui::SameLine();
    ImGui::Checkbox("Blend Volumes", &state.showBlendVolumes);

    int debugView = (int)state.debugView;
    const char* debugViews[] = { "None", "Influence", "Probe Color", "SSR Confidence",
        "Region ID", "Parallax", "Fallback Only", "SSR Only" };
    if (ImGui::Combo("Fullscreen Debug", &debugView, debugViews, 8))
    {
        state.debugView = (ReflectionProbeDebugView)debugView;
        system->UploadMetadata();
    }
    if (ImGui::TreeNode("Reflection Lighting"))
    {
        auto& lighting = system->GetLightingSettings();
        bool lightingChanged = false;
        int maxBlend = (int)lighting.maxBlendCount;
        if (ImGui::SliderInt("Max Blended Probes", &maxBlend, 1, 4)) { lighting.maxBlendCount = (uint32_t)maxBlend; lightingChanged = true; }
        lightingChanged |= ImGui::SliderFloat("SSR Roughness Fade Start", &lighting.ssrRoughnessFadeStart, 0.0f, 1.0f);
        lightingChanged |= ImGui::SliderFloat("SSR Roughness Fade End", &lighting.ssrRoughnessFadeEnd, 0.0f, 1.0f);
        lightingChanged |= ImGui::DragFloat("Sky Fallback Intensity", &lighting.skyIntensity, 0.02f, 0.0f, 100.0f);
        if (lightingChanged) system->UploadMetadata();
        ImGui::TreePop();
    }

    if (state.selectedProbeIndex < 0 || state.selectedProbeIndex >= (int)probes.size()) return;
    const int selected = state.selectedProbeIndex;
    auto& probe = probes[selected];
    auto& bake = results[selected];
    bool changed = false;
    bool resourceChanged = false;

    ImGui::SeparatorText("Probe Parameters");
    char name[256]{};
    std::strncpy(name, probe.name.c_str(), sizeof(name) - 1);
    if (ImGui::InputText("Name", name, sizeof(name))) { probe.name = name; changed = true; }
    int type = (int)probe.type;
    const char* types[] = { "Baked", "Realtime", "Sky" };
    if (probe.type == ReflectionProbeType::Sky) ImGui::Text("Type: Sky (global fallback)");
    else if (ImGui::Combo("Type", &type, types, 2)) { probe.type = (ReflectionProbeType)type; changed = true; resourceChanged = true; }
    int shape = (int)probe.shape;
    const char* shapes[] = { "Sphere", "Box" };
    if (ImGui::Combo("Shape", &shape, shapes, 2)) { probe.shape = (ReflectionProbeShape)shape; changed = true; }
    int refresh = (int)probe.refreshMode;
    const char* refreshModes[] = { "On Load", "On Demand", "Every Frame", "Time Sliced" };
    if (ImGui::Combo("Refresh", &refresh, refreshModes, 4)) { probe.refreshMode = (ReflectionProbeRefreshMode)refresh; changed = true; }
    changed |= ImGui::Checkbox("Enabled", &probe.enabled);
    changed |= ImGui::DragFloat3("Position", &probe.position.x, 0.05f);
    changed |= ImGui::DragFloat3("Capture Position", &probe.capturePosition.x, 0.05f);
    if (probe.shape == ReflectionProbeShape::Box)
    {
        changed |= ImGui::DragFloat3("Box Min", &probe.boxMin.x, 0.05f);
        changed |= ImGui::DragFloat3("Box Max", &probe.boxMax.x, 0.05f);
        changed |= ImGui::Checkbox("Box Projection", &probe.boxProjection);
    }
    else changed |= ImGui::DragFloat("Radius", &probe.radius, 0.05f, 0.01f, 10000.0f);
    changed |= ImGui::DragFloat("Blend Distance", &probe.blendDistance, 0.02f, 0.001f, 1000.0f);
    changed |= ImGui::DragFloat("Priority", &probe.priority, 0.05f, -8.0f, 8.0f);
    changed |= ImGui::DragFloat("Intensity", &probe.intensity, 0.02f, 0.0f, 100.0f);
    changed |= ImGui::DragFloat("Specular Intensity", &probe.specularIntensity, 0.02f, 0.0f, 100.0f);
    changed |= ImGui::DragFloat("Near Plane", &probe.nearPlane, 0.01f, 0.001f, probe.farPlane);
    changed |= ImGui::DragFloat("Far Plane", &probe.farPlane, 1.0f, probe.nearPlane + 0.01f, 100000.0f);
    int cullingMask = (int)probe.cullingMask;
    if (ImGui::InputInt("Culling Mask", &cullingMask)) { probe.cullingMask = (uint32_t)cullingMask; changed = true; }
    if (probe.type == ReflectionProbeType::Realtime)
    {
        int facesPerFrame = (int)probe.realtimeFacesPerFrame;
        if (ImGui::SliderInt("Faces Per Frame", &facesPerFrame, 1, 6)) { probe.realtimeFacesPerFrame = (uint32_t)facesPerFrame; changed = true; }
    }
    int resolution = probe.resolution <= 32 ? 0 : probe.resolution <= 64 ? 1 :
        probe.resolution <= 128 ? 2 : probe.resolution <= 256 ? 3 : 4;
    if (ImGui::Combo("Resolution", &resolution, "32\0 64\0 128\0 256\0 512\0"))
    {
        static const uint32_t values[] = { 32, 64, 128, 256, 512 };
        probe.resolution = values[std::clamp(resolution, 0, 4)]; changed = true; resourceChanged = true;
    }
    int region = probe.regionId == 0xffffffffu ? -1 : (int)probe.regionId;
    if (ImGui::InputInt("Region ID", &region)) { probe.regionId = region < 0 ? 0xffffffffu : (uint32_t)region; changed = true; }
    changed |= ImGui::Checkbox("Portal Bridge", &probe.portal);
    ImGui::Text("Source: %s", probe.autoGenerated ? "Auto generated" : "Manual");
    ImGui::TextWrapped("Cache: %s", bake.cachePath.empty() ? "Not assigned" : bake.cachePath.c_str());
    ImGui::Text("Bake status: %s", bake.status.c_str());

    if (changed) system->MarkDirty(selected);
    if (resourceChanged)
    {
        ResetProbePreview(); device.WaitForDevice(); system->CreateGPUResources(device, device.GetEditorCommandBuffer());
        system->BakeQueuedProbesNow(*m_Scene, device, device.GetEditorCommandBuffer());
        system->UpdateGlobalDescriptors(m_Scene->m_GlobalDescriptorSet); return;
    }
    if (probe.type != ReflectionProbeType::Sky)
    {
        if (ImGui::Button("Rebake Selected"))
        {
            device.WaitForDevice();
            system->RequestBake(selected);
            system->BakeQueuedProbesNow(*m_Scene, device, device.GetEditorCommandBuffer());
        }
        ImGui::SameLine();
        if (probe.autoGenerated && ImGui::Button("Convert To Manual")) system->ConvertToManual(selected);
        if (!probe.autoGenerated)
        {
            ImGui::SameLine();
            if (ImGui::Button("Delete Probe"))
            {
                probes.erase(probes.begin() + selected); results.erase(results.begin() + selected);
                state.selectedProbeIndex = std::min(selected, (int)probes.size() - 1);
                ResetProbePreview(); device.WaitForDevice(); system->CreateGPUResources(device, device.GetEditorCommandBuffer());
                system->BakeQueuedProbesNow(*m_Scene, device, device.GetEditorCommandBuffer());
                system->UpdateGlobalDescriptors(m_Scene->m_GlobalDescriptorSet); return;
            }
        }
    }

    if (ImGui::TreeNode("Auto Placement"))
    {
        auto& settings = system->GetPlacementSettings();
        ImGui::Checkbox("Enabled##auto", &settings.enabled);
        ImGui::DragFloat3("Volume Min", &settings.volumeMin.x, 0.25f);
        ImGui::DragFloat3("Volume Max", &settings.volumeMax.x, 0.25f);
        ImGui::DragFloat("Uniform Spacing", &settings.uniformSpacing, 0.25f, 0.5f, 100.0f);
        ImGui::DragFloat("Cell Size##legacy", &settings.cellSize, 0.1f, 0.25f, 100.0f);
        ImGui::DragFloat("Indoor Spacing##legacy", &settings.indoorSpacing, 0.1f, 1.0f, 100.0f);
        ImGui::DragFloat("Outdoor Spacing##legacy", &settings.outdoorSpacing, 0.1f, 1.0f, 1000.0f);
        ImGui::SliderFloat("Solid Threshold##legacy", &settings.solidThreshold, 0.01f, 1.0f);
        ImGui::SliderFloat("Refinement Threshold##legacy", &settings.refinementThreshold, 0.0f, 0.5f);
        ImGui::Checkbox("Show Placement Grid##legacy", &state.showPlacementGrid);
        ImGui::Checkbox("Show Regions##legacy", &state.showRegions);
        int maxCount = (int)settings.maxProbeCount;
        if (ImGui::InputInt("Max Probe Count", &maxCount)) settings.maxProbeCount = (uint32_t)std::clamp(maxCount, 1, 1024);
        if (ImGui::Button("Regenerate Auto Probes"))
        {
            system->GenerateAutoProbes(*m_Scene, true); ResetProbePreview(); device.WaitForDevice();
            system->CreateGPUResources(device, device.GetEditorCommandBuffer());
            system->BakeQueuedProbesNow(*m_Scene, device, device.GetEditorCommandBuffer());
            system->UpdateGlobalDescriptors(m_Scene->m_GlobalDescriptorSet);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Auto Probes"))
        {
            system->ClearAutoProbes(); ResetProbePreview(); device.WaitForDevice();
            system->CreateGPUResources(device, device.GetEditorCommandBuffer());
            system->BakeQueuedProbesNow(*m_Scene, device, device.GetEditorCommandBuffer());
            system->UpdateGlobalDescriptors(m_Scene->m_GlobalDescriptorSet);
        }
        if (ImGui::Button("Validate Placement"))
        {
            const auto errors = system->ValidatePlacement();
            bake.status = errors.empty() ? "Placement valid" : errors.front();
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply Auto Probes")) system->SaveConfiguration();
        ImGui::TreePop();
    }

    if (!bake.valid || !system->GetSpecularArray()) return;
    ImGui::SeparatorText("Baked Cubemap Preview");
    state.previewCubemap = true;
    ImGui::SliderFloat("Roughness / Mip", &state.previewRoughness, 0.0f, 1.0f);
    const int mip = std::clamp((int)std::round(state.previewRoughness * float(system->GetMipCount() - 1)), 0, (int)system->GetMipCount() - 1);
    ImGui::Text("Resolution %u, layer %u, mip %d", system->GetArrayResolution(), bake.arrayLayer, mip);
    if (m_ProbePreviewIndex != selected || m_ProbePreviewMip != mip)
    {
        ResetProbePreview();
        m_ProbePreviewDevice = device.GetLogicDevice();
        auto& image = system->GetSpecularArray()->GetImage();
        for (uint32_t face = 0; face < 6; ++face)
        {
            m_ProbePreviewViews[face] = image.CreateLayerMipView(m_ProbePreviewDevice, bake.arrayLayer * 6u + face, (uint32_t)mip);
            if (m_ProbePreviewViews[face] != VK_NULL_HANDLE)
                m_ProbePreviewSets[face] = ImGui_ImplVulkan_AddTexture(image.GetSampler(), m_ProbePreviewViews[face], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        m_ProbePreviewIndex = selected; m_ProbePreviewMip = mip;
    }
    const char* faceNames[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
    const float width = std::max(64.0f, (ImGui::GetContentRegionAvail().x - 8.0f) / 3.0f);
    for (int face = 0; face < 6; ++face)
    {
        ImGui::BeginGroup(); ImGui::Text("%s", faceNames[face]);
        if (m_ProbePreviewSets[face] != VK_NULL_HANDLE) ImGui::Image((ImTextureID)m_ProbePreviewSets[face], ImVec2(width, width));
        ImGui::EndGroup();
        if (face % 3 != 2) ImGui::SameLine();
    }
}

void VansGraphics::VansInspectorWindow::ShowTextAsset()
{
    auto currentFile = VansProjectWindow::m_CurrentSelectedFile;
    static std::filesystem::path lastFile = "";
    static std::vector<char> textBuffer;

    if (currentFile != lastFile)
    {
        textBuffer.clear();
        std::ifstream file(currentFile, std::ios::ate | std::ios::binary);
        if (file.is_open()) {
            size_t fileSize = (size_t)file.tellg();
            textBuffer.resize(fileSize + 1024 * 10);
            file.seekg(0);
            file.read(textBuffer.data(), fileSize);
            textBuffer[fileSize] = '\0';
            file.close();
        }
        else {
            textBuffer.assign(1024, '\0');
        }
        lastFile = currentFile;
    }

    ImGui::Text("Text Editor:");
    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    contentSize.y -= 30.0f;

    ImGui::InputTextMultiline("##texteditor", textBuffer.data(), textBuffer.size(), contentSize, ImGuiInputTextFlags_AllowTabInput);

    if (ImGui::Button("Save")) {
        std::ofstream file(currentFile, std::ios::trunc | std::ios::binary);
        if (file.is_open()) {
            file << textBuffer.data();
            file.close();
        }
    }
}

void VansGraphics::VansInspectorWindow::ShowTextureAsset(VansVKDevice& device)
{
    auto currentFile = VansProjectWindow::m_CurrentSelectedFile;
    static std::filesystem::path lastFile = "";
    static VkDescriptorSet cachedImageDS = VK_NULL_HANDLE;
    static VkImageView cachedImageView = VK_NULL_HANDLE;
    static VkSampler cachedSampler = VK_NULL_HANDLE;
    static VansGraphics::VansTexture* previewTexture = nullptr;

    if (currentFile != lastFile)
    {
        if (previewTexture)
        {
            delete previewTexture;
            previewTexture = nullptr;
        }
        cachedImageDS = VK_NULL_HANDLE;
        cachedImageView = VK_NULL_HANDLE;
        cachedSampler = VK_NULL_HANDLE;

        previewTexture = new VansGraphics::VansTexture();

        device.WaitForDevice();

        VansGraphics::VansVKCommandBuffer editorCommandbuffer = device.GetEditorCommandBuffer();
        
        previewTexture->LoadTexture(editorCommandbuffer, currentFile.string(), false,true,false);

        lastFile = currentFile;
    }

    ImGui::Text("Image Preview:");
    if (previewTexture)
    {
        VansGraphics::VansVKImage& image = previewTexture->GetImage();
        if (cachedImageView != image.GetImageView() || cachedSampler != image.GetSampler() || cachedImageDS == VK_NULL_HANDLE)
        {
            cachedImageView = image.GetImageView();
            cachedSampler = image.GetSampler();
            cachedImageDS = ImGui_ImplVulkan_AddTexture(cachedSampler, cachedImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        if (cachedImageDS != VK_NULL_HANDLE)
        {
            float width = (float)previewTexture->GetWidth();
            float height = (float)previewTexture->GetHeight();
            float aspect = width / height;
            float displayWidth = ImGui::GetContentRegionAvail().x;
            float displayHeight = displayWidth / aspect;
            ImGui::Image((ImTextureID)cachedImageDS, ImVec2(displayWidth, displayHeight));
        }
    }
    ImGui::Text("Path: %s", currentFile.string().c_str());
    ImGui::Text("Size: %llu bytes", std::filesystem::file_size(currentFile));
}

void VansGraphics::VansInspectorWindow::ShowModelTextureAsset(VansVKDevice& device)
{
    auto currentFile = VansProjectWindow::m_CurrentSelectedFile;
    ImGui::Text("Model Asset: %s", currentFile.filename().string().c_str());
}

// ════════════════════════════════════════════════════════════════
//  Animation Clip (.vclip) Inspector
// ════════════════════════════════════════════════════════════════

// Recursive helper: draw a bone and its children as an ImGui tree
static void DrawBoneTree(
    const VansGraphics::Skeleton& skeleton,
    const VansGraphics::VansAnimationClip& clip,
    int boneIndex,
    float time,
    int& selectedBone)
{
    if (boneIndex < 0 || boneIndex >= (int)skeleton.bones.size())
        return;

    const auto& bone = skeleton.bones[boneIndex];
    bool isLeaf = bone.children.empty();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (isLeaf)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (boneIndex == selectedBone)
        flags |= ImGuiTreeNodeFlags_Selected;

    // Label: "boneName (id)"
    char label[256];
    snprintf(label, sizeof(label), "%s  [%d]", bone.name.c_str(), bone.id);

    bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)boneIndex, flags, "%s", label);

    if (ImGui::IsItemClicked())
        selectedBone = boneIndex;

    // If this bone is selected, show its keyframe data inline
    if (boneIndex == selectedBone)
    {
        ImGui::Indent(20.0f);

        // Find interpolated TRS at current scrub time
        if (boneIndex < (int)clip.boneKeyframes.size() && !clip.boneKeyframes[boneIndex].empty())
        {
            const auto& keyframes = clip.boneKeyframes[boneIndex];
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Keyframes: %d", (int)keyframes.size());

            // Find surrounding keyframes for interpolation
            glm::vec3 pos(0.0f);
            glm::quat rot(1.0f, 0.0f, 0.0f, 0.0f);
            glm::vec3 scl(1.0f);

            if (keyframes.size() == 1 || time <= keyframes.front().time)
            {
                pos = keyframes.front().position;
                rot = keyframes.front().rotation;
                scl = keyframes.front().scale;
            }
            else if (time >= keyframes.back().time)
            {
                pos = keyframes.back().position;
                rot = keyframes.back().rotation;
                scl = keyframes.back().scale;
            }
            else
            {
                // Linear search for surrounding pair
                for (size_t k = 0; k + 1 < keyframes.size(); k++)
                {
                    if (time >= keyframes[k].time && time <= keyframes[k + 1].time)
                    {
                        float seg = keyframes[k + 1].time - keyframes[k].time;
                        float alpha = (seg > 0.0001f) ? (time - keyframes[k].time) / seg : 0.0f;
                        pos = glm::mix(keyframes[k].position, keyframes[k + 1].position, alpha);
                        rot = glm::slerp(keyframes[k].rotation, keyframes[k + 1].rotation, alpha);
                        scl = glm::mix(keyframes[k].scale, keyframes[k + 1].scale, alpha);
                        break;
                    }
                }
            }

            ImGui::Text("Position: (%.3f, %.3f, %.3f)", pos.x, pos.y, pos.z);

            // Show rotation as both quaternion and euler
            ImGui::Text("Rotation (quat): (%.3f, %.3f, %.3f, %.3f)", rot.w, rot.x, rot.y, rot.z);
            glm::vec3 euler = glm::degrees(glm::eulerAngles(rot));
            ImGui::Text("Rotation (euler): (%.1f, %.1f, %.1f)", euler.x, euler.y, euler.z);

            ImGui::Text("Scale:    (%.3f, %.3f, %.3f)", scl.x, scl.y, scl.z);

            // Show keyframe time range
            ImGui::TextDisabled("Time range: %.3f - %.3f s", keyframes.front().time, keyframes.back().time);
        }
        else
        {
            ImGui::TextDisabled("No keyframes for this bone");
        }

        // Show offset matrix
        if (ImGui::TreeNode("Offset Matrix"))
        {
            const glm::mat4& m = bone.offsetMatrix;
            for (int row = 0; row < 4; row++)
                ImGui::Text("  [%.3f  %.3f  %.3f  %.3f]", m[0][row], m[1][row], m[2][row], m[3][row]);
            ImGui::TreePop();
        }

        ImGui::Unindent(20.0f);
    }

    if (nodeOpen && !isLeaf)
    {
        for (int childIdx : bone.children)
            DrawBoneTree(skeleton, clip, childIdx, time, selectedBone);
        ImGui::TreePop();
    }
}

void VansGraphics::VansInspectorWindow::ShowAnimationClipAsset()
{
    auto currentFile = VansProjectWindow::m_CurrentSelectedFile;

    // Reload if selected file changed
    if (currentFile != m_VClipCache.loadedPath)
    {
        m_VClipCache = CachedVClipData{};
        m_VClipCache.loadedPath = currentFile;
        m_VClipCache.valid = VansAnimationClipIO::Load(
            currentFile.string(), m_VClipCache.clip, m_VClipCache.skeleton);
    }

    if (!m_VClipCache.valid)
    {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Failed to load .vclip file");
        return;
    }

    const auto& clip     = m_VClipCache.clip;
    const auto& skeleton = m_VClipCache.skeleton;

    // ── Header info ──────────────────────────────────────────────
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "Animation Clip");
    ImGui::Separator();

    ImGui::Text("Clip Name:       %s", clip.clipName.c_str());
    ImGui::Text("Duration:        %.3f s", clip.duration);
    ImGui::Text("Ticks/Second:    %.1f", clip.ticksPerSecond);
    ImGui::Text("Bone Count:      %d", (int)skeleton.bones.size());

    // Count total keyframes
    uint32_t totalKF = 0;
    for (const auto& boneKFs : clip.boneKeyframes)
        totalKF += (uint32_t)boneKFs.size();
    ImGui::Text("Total Keyframes: %u", totalKF);

    ImGui::Spacing();
    ImGui::Separator();

    // ── Time scrubber ────────────────────────────────────────────
    ImGui::Text("Scrub Time:");
    float duration = clip.duration > 0.0f ? clip.duration : 1.0f;
    ImGui::SliderFloat("##scrubTime", &m_VClipCache.scrubTime, 0.0f, duration, "%.3f s");

    // Show as frame number (approximate)
    float fps = clip.ticksPerSecond > 0.0f ? clip.ticksPerSecond : 30.0f;
    int frameNum = (int)(m_VClipCache.scrubTime * fps);
    int totalFrames = (int)(duration * fps);
    ImGui::SameLine();
    ImGui::TextDisabled("Frame %d / %d", frameNum, totalFrames);

    // Also allow frame-based slider
    if (ImGui::SliderInt("##scrubFrame", &frameNum, 0, totalFrames, "Frame %d"))
    {
        m_VClipCache.scrubTime = (float)frameNum / fps;
    }

    ImGui::Spacing();
    ImGui::Separator();

    // ── Bone hierarchy tree ──────────────────────────────────────
    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.7f, 1.0f), "Bone Hierarchy");
    ImGui::Spacing();

    // Find root bones (parentIndex == -1) and draw recursively
    ImGui::BeginChild("##boneTree", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (int b = 0; b < (int)skeleton.bones.size(); b++)
    {
        if (skeleton.bones[b].parentIndex < 0)
        {
            DrawBoneTree(skeleton, clip, b, m_VClipCache.scrubTime, m_VClipCache.selectedBone);
        }
    }
    ImGui::EndChild();
}
