#include "VansUIEditorWindow.h"
#include "../../../EngineCore/RuntimeUI/Public/VansUISystem.h"
#include "../../../EngineCore/RuntimeUI/Public/VansUIDocument.h"
#include "../../Util/VansInputManager.h"
#include "../../Util/VansLog.h"
#include "imgui.h"
#include <cstring>

using namespace VansGraphics;

VansUIEditorWindow::VansUIEditorWindow()
{
    std::strncpy(m_XamlPathBuf, "UI/Views/HUD.xaml", sizeof(m_XamlPathBuf) - 1);
}

// ─── ShowWindow ─────────────────────────────────────────────────────────────

void VansUIEditorWindow::ShowWindow(VansVKDevice& device)
{
    ImGui::SetNextWindowSize(ImVec2(620.0f, 480.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("UI Editor");

    // ─── Toolbar row ────────────────────────────────────────────────────────
    ImGui::Text("XAML Path:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 160.0f);
    ImGui::InputText("##xamlpath", m_XamlPathBuf, sizeof(m_XamlPathBuf));

    ImGui::SameLine();
    if (ImGui::Button("Load Preview", ImVec2(100.0f, 0.0f)))
    {
        LoadPreview(device);
    }
    ImGui::SameLine();
    if (ImGui::Button("Unload", ImVec2(50.0f, 0.0f)))
    {
        UnloadPreview();
    }

    // ── F5 热重载 ───────────────────────────────────────────────────────────
    // 仅在 ImGui frame 内通过 ImGui::IsKeyPressed 检测，不走 VansInputManager，
    // 符合编辑器窗口输入规范。
    {
        bool reloadKeyDown = ImGui::IsKeyDown(ImGuiKey_F5);
        if (reloadKeyDown && !m_ReloadKeyWasDown && m_PreviewDocument)
        {
            const std::string prevPath = m_PreviewDocument->GetSourcePath();
            UnloadPreview();
            std::strncpy(m_XamlPathBuf, prevPath.c_str(), sizeof(m_XamlPathBuf) - 1);
            LoadPreview(device);
        }
        m_ReloadKeyWasDown = reloadKeyDown;
    }

    ImGui::Separator();

    // ─── Left: Metadata panel ───────────────────────────────────────────────
    float leftW = 220.0f;
    ImGui::BeginChild("UIEdMeta", ImVec2(leftW, 0.0f), true);
    DrawMetaPanel();
    ImGui::EndChild();

    ImGui::SameLine();

    // ─── Right: Preview viewport ────────────────────────────────────────────
    ImGui::BeginChild("UIEdPreview", ImVec2(0.0f, 0.0f), true);
    DrawPreviewViewport();
    ImGui::EndChild();

    ImGui::End();
}

// ─── LoadPreview ────────────────────────────────────────────────────────────

void VansUIEditorWindow::LoadPreview(VansVKDevice& /*device*/)
{
    UnloadPreview();

    if (m_XamlPathBuf[0] == '\0')
    {
        VANS_LOG_WARN("[UIEditor] XAML path is empty, skipping load.");
        return;
    }

    if (!VansRuntime::VansUISystem::Get().IsInitialized())
    {
        VANS_LOG_WARN("[UIEditor] VansUISystem is not initialized. Cannot load XAML preview.");
        return;
    }

    const std::string path(m_XamlPathBuf);
    m_PreviewDocument = VansRuntime::VansUISystem::Get().LoadDocument(path);

    if (!m_PreviewDocument)
    {
        VANS_LOG_ERROR("[UIEditor] Failed to load XAML: " << path);
        return;
    }

    m_PreviewDocument->Show();
    VANS_LOG("[UIEditor] Loaded XAML preview: " << path);
}

// ─── UnloadPreview ──────────────────────────────────────────────────────────

void VansUIEditorWindow::UnloadPreview()
{
    if (!m_PreviewDocument)
        return;

    VansRuntime::VansUISystem::Get().UnloadDocument(m_PreviewDocument);
    m_PreviewDocument.reset();
}

// ─── DrawMetaPanel ──────────────────────────────────────────────────────────

void VansUIEditorWindow::DrawMetaPanel()
{
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "Document Info");
    ImGui::Separator();

    if (!m_PreviewDocument)
    {
        ImGui::TextDisabled("No document loaded.");
        return;
    }

    const std::string& srcPath = m_PreviewDocument->GetSourcePath();
    ImGui::Text("Source:");
    ImGui::TextWrapped("%s", srcPath.c_str());
    ImGui::Spacing();

    const bool visible = m_PreviewDocument->IsVisible();
    ImGui::Text("Visible: %s", visible ? "Yes" : "No");
    ImGui::Spacing();

    // Visibility toggle
    if (visible)
    {
        if (ImGui::Button("Hide"))
            m_PreviewDocument->Hide();
    }
    else
    {
        if (ImGui::Button("Show"))
            m_PreviewDocument->Show();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("F5 — Hot Reload");
}

// ─── DrawPreviewViewport ─────────────────────────────────────────────────────

void VansUIEditorWindow::DrawPreviewViewport()
{
    if (!m_PreviewDocument)
    {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(cursor,
                          ImVec2(cursor.x + avail.x, cursor.y + avail.y),
                          IM_COL32(20, 20, 20, 255));
        ImGui::Dummy(avail);

        // Centered hint text
        ImVec2 textSize = ImGui::CalcTextSize("No XAML loaded");
        ImGui::SetCursorScreenPos(ImVec2(
            cursor.x + (avail.x - textSize.x) * 0.5f,
            cursor.y + (avail.y - textSize.y) * 0.5f));
        ImGui::TextDisabled("No XAML loaded");
        return;
    }

    // NOTE: Noesis IView rendering results are currently not exposed as a
    // Vulkan image that can be sampled as an ImGui texture (the Noesis
    // Vulkan render device draws directly into the active swapchain render
    // pass, not into an off-screen texture).
    // The runtime document IS active and rendered each frame via the
    // normal VansUISystem render pipeline. This viewport shows metadata
    // only; a full off-screen preview requires a dedicated render target
    // integration (future work, see EngineDoc/NoesisGUI_Integration_Guide.md).
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(cursor,
                      ImVec2(cursor.x + avail.x, cursor.y + avail.y),
                      IM_COL32(15, 15, 30, 255));
    ImGui::Dummy(avail);

    // Centered status text
    const char* msg = "Document active — see game viewport for live preview";
    ImVec2 textSize = ImGui::CalcTextSize(msg);
    ImGui::SetCursorScreenPos(ImVec2(
        cursor.x + (avail.x - textSize.x) * 0.5f,
        cursor.y + (avail.y - textSize.y) * 0.5f));
    ImGui::Text("%s", msg);
}
