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

#include "../../../Graphics/Vulkan/VansVKFunctions.h"

#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>

void VansGraphics::VansInspectorWindow::ShowWindow(VansVKDevice& device)
{
    auto currentFile = VansProjectWindow::m_CurrentSelectedFile;
    // Inspector Window for Selected File
    if (!currentFile.empty()) {
        ImGui::Begin("Inspector");
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

        ImGui::End();
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
