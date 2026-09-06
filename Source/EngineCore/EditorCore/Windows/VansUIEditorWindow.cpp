#include "VansUIEditorWindow.h"

#include "../VansEditorWindow.h"
#include "../../Util/VansLog.h"

#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <string>

using namespace VansGraphics;

VansUIEditorWindow::VansUIEditorWindow()
{
    std::strncpy(m_XamlPathBuf, "UI/Screens/HUD_Main.vui.json", sizeof(m_XamlPathBuf) - 1);
}

void VansUIEditorWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& api)
{
    DrawUIEditorContents(api);
}

void VansUIEditorWindow::DrawUIEditorContents(Vans::EditorAPI::IEngineEditorAPI& api)
{
    if (!VansEditorWindow::m_UIEditorWindowOpen)
        return;

    ImGui::SetNextWindowSize(ImVec2(720.0f, 520.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("UI Editor");

    ImGui::Text("UI Document:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 180.0f);
    ImGui::InputText("##uipath", m_XamlPathBuf, sizeof(m_XamlPathBuf));

    ImGui::SameLine();
    if (ImGui::Button("Load", ImVec2(72.0f, 0.0f)))
        LoadPreview(api);

    ImGui::SameLine();
    if (ImGui::Button("Unload", ImVec2(72.0f, 0.0f)))
        UnloadPreview(api);

    const bool reloadKeyDown = ImGui::IsKeyDown(ImGuiKey_F5);
    if (reloadKeyDown && !m_ReloadKeyWasDown && m_PreviewDocumentId != 0)
    {
        const Vans::EditorAPI::UIDocumentSnapshot snapshot =
            api.GetUIDocumentSnapshot(m_PreviewDocumentId);
        const std::string reloadPath = snapshot.valid ? snapshot.sourcePath : std::string(m_XamlPathBuf);
        UnloadPreview(api);
        std::strncpy(m_XamlPathBuf, reloadPath.c_str(), sizeof(m_XamlPathBuf) - 1);
        m_XamlPathBuf[sizeof(m_XamlPathBuf) - 1] = '\0';
        LoadPreview(api);
    }
    m_ReloadKeyWasDown = reloadKeyDown;

    if (!m_LastStatus.empty())
    {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", m_LastStatus.c_str());
    }

    ImGui::Separator();

    ImGui::BeginChild("UIEdMeta", ImVec2(260.0f, 0.0f), true);
    DrawMetaPanel(api);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("UIEdPreview", ImVec2(0.0f, 0.0f), true);
    DrawPreviewViewport(api);
    ImGui::EndChild();

    ImGui::End();
}

void VansUIEditorWindow::LoadPreview(Vans::EditorAPI::IEngineEditorAPI& api)
{
    UnloadPreview(api);

    const std::string path(m_XamlPathBuf);
    Vans::EditorAPI::UIDocumentOpenResult result = api.OpenUIDocument(path);
    if (!result.success)
    {
        m_LastStatus = result.error.empty() ? "Failed to load UI document." : result.error;
        VANS_LOG_WARN("[UIEditor] " << m_LastStatus);
        return;
    }

    m_PreviewDocumentId = result.documentId;
    Vans::EditorAPI::UIPreviewRequest previewRequest{};
    previewRequest.documentId = m_PreviewDocumentId;
    const Vans::EditorAPI::UIPreviewResult preview = api.RequestUIPreview(previewRequest);
    m_PreviewId = preview.previewId;
    m_LastStatus = preview.message.empty()
        ? "UI document loaded."
        : "UI document loaded. " + preview.message;
}

void VansUIEditorWindow::UnloadPreview(Vans::EditorAPI::IEngineEditorAPI& api)
{
    if (m_PreviewDocumentId == 0)
        return;

    api.CloseUIDocument(m_PreviewDocumentId);
    m_PreviewDocumentId = 0;
    m_PreviewId = 0;
    m_LastStatus = "UI document unloaded.";
}

void VansUIEditorWindow::DrawMetaPanel(Vans::EditorAPI::IEngineEditorAPI& api)
{
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "Document");
    ImGui::Separator();

    const Vans::EditorAPI::UIDocumentSnapshot snapshot =
        api.GetUIDocumentSnapshot(m_PreviewDocumentId);
    if (!snapshot.valid)
    {
        ImGui::TextDisabled("No document loaded.");
        return;
    }

    ImGui::Text("Source:");
    ImGui::TextWrapped("%s", snapshot.sourcePath.c_str());
    ImGui::Spacing();
    ImGui::Text("Visible: %s", snapshot.visible ? "Yes" : "No");

    if (snapshot.visible)
    {
        if (ImGui::Button("Hide"))
            api.SetUIDocumentVisible(snapshot.documentId, false);
    }
    else
    {
        if (ImGui::Button("Show"))
            api.SetUIDocumentVisible(snapshot.documentId, true);
    }

    if (!snapshot.assetKind.empty())
    {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "Screen");
        ImGui::Text("Name: %s", snapshot.name.c_str());
        ImGui::TextWrapped("XAML asset: %s", snapshot.xamlAssetGuid.c_str());
        ImGui::Text("Layer: %s", snapshot.layer.c_str());
        ImGui::Text("Z Order: %d", snapshot.zOrder);

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.5f, 1.0f), "Events");
        if (snapshot.events.empty())
        {
            ImGui::TextDisabled("No declared events.");
        }
        else
        {
            for (const Vans::EditorAPI::UIScreenEventSummary& event : snapshot.events)
            {
                ImGui::BulletText("%s.%s -> %s",
                    event.source.c_str(),
                    event.eventName.c_str(),
                    event.action.c_str());
            }
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.8f, 0.7f, 1.0f, 1.0f), "Resources");
        for (const std::string& theme : snapshot.themeAssetGuids)
            ImGui::BulletText("Theme: %s", theme.c_str());
        for (const std::string& token : snapshot.tokenAssetGuids)
            ImGui::BulletText("Tokens: %s", token.c_str());
        for (const std::string& loc : snapshot.localizationAssetGuids)
            ImGui::BulletText("Localization: %s", loc.c_str());
        for (const std::string& dependency : snapshot.dependencies)
            ImGui::BulletText("Dependency: %s", dependency.c_str());
        if (snapshot.themeAssetGuids.empty() && snapshot.tokenAssetGuids.empty() &&
            snapshot.localizationAssetGuids.empty() && snapshot.dependencies.empty())
        {
            ImGui::TextDisabled("No resources declared.");
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "Budget");
        ImGui::Text("Draw Calls <= %u", snapshot.performanceBudget.maxDrawCalls);
        ImGui::Text("Texture MB <= %u", snapshot.performanceBudget.maxTextureMemoryMB);
        ImGui::Text("Layout <= %.2f ms", snapshot.performanceBudget.maxLayoutMs);
        ImGui::Text("Binding Updates <= %u", snapshot.performanceBudget.maxBindingUpdatesPerFrame);
        ImGui::Text("Animations <= %u", snapshot.performanceBudget.maxAnimations);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Diagnostics");
    const Vans::EditorAPI::UIDiagnosticsSnapshot diagnostics =
        api.GetUIDiagnostics(snapshot.documentId);
    for (const std::string& message : diagnostics.messages)
        ImGui::BulletText("%s", message.c_str());
}

void VansUIEditorWindow::DrawPreviewViewport(Vans::EditorAPI::IEngineEditorAPI& api)
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        cursor,
        ImVec2(cursor.x + avail.x, cursor.y + avail.y),
        IM_COL32(15, 15, 30, 255));

    Vans::EditorAPI::EditorTextureHandle texture = nullptr;
    if (m_PreviewId != 0)
        texture = api.GetUIPreviewTexture(m_PreviewId);

    ImGui::Dummy(avail);
    if (texture)
    {
        const float targetAspect = 1280.0f / 720.0f;
        ImVec2 imageSize = avail;
        if (imageSize.x / std::max(1.0f, imageSize.y) > targetAspect)
            imageSize.x = imageSize.y * targetAspect;
        else
            imageSize.y = imageSize.x / targetAspect;

        ImGui::SetCursorScreenPos(ImVec2(
            cursor.x + (avail.x - imageSize.x) * 0.5f,
            cursor.y + (avail.y - imageSize.y) * 0.5f));
        ImGui::Image(texture, imageSize);
        return;
    }

    const char* message = "Preview texture is not available.";
    const ImVec2 textSize = ImGui::CalcTextSize(message);
    ImGui::SetCursorScreenPos(ImVec2(
        cursor.x + (avail.x - textSize.x) * 0.5f,
        cursor.y + (avail.y - textSize.y) * 0.5f));
    ImGui::Text("%s", message);
}
