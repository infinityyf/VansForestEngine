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
