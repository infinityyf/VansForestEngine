#include "VansScriptorWindow.h"
#include "../../Configration/VansConfigration.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "VansConsole.h"
#include "imgui.h"

#include <fstream>
#include <sstream>
#include <algorithm>

std::filesystem::path VansGraphics::VansScriptorWindow::m_SelectedScript = "";

VansGraphics::VansScriptorWindow::VansScriptorWindow()
{
    RefreshFileList();
}

// Recursively collect every .py file under the ForestExporter folder.
void VansGraphics::VansScriptorWindow::RefreshFileList()
{
    m_PythonFiles.clear();

    auto* cfg = VansConfigration::GetInstance();
    // projectRoot points to  …/ForestEngine/ForestEngine/
    // EngineExported holds the .pyd and user .py scripts
    std::string exporterDir = cfg->GetProjectRootPath() + "../ForestExporter/EngineExported";

    if (!std::filesystem::exists(exporterDir))
        return;

    for (auto& entry : std::filesystem::recursive_directory_iterator(exporterDir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".py")
        {
            m_PythonFiles.push_back(std::filesystem::canonical(entry.path()));
        }
    }

    // Sort alphabetically by filename
    std::sort(m_PythonFiles.begin(), m_PythonFiles.end(),
        [](const std::filesystem::path& a, const std::filesystem::path& b) {
            return a.filename().string() < b.filename().string();
        });

    m_NeedsRefresh = false;
}

void VansGraphics::VansScriptorWindow::ShowWindow(VansVKDevice& device)
{
    ImGui::Begin("Scripts");

    // Toolbar
    if (ImGui::Button("Refresh"))
        m_NeedsRefresh = true;

    ImGui::SameLine();
    if (ImGui::Button("Reload .py"))
    {
        if (auto* ctx = VansScriptContext::GetInstance())
        {
            ctx->ReloadAllPyScripts();
            m_NeedsRefresh = true;
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Force-reload all tracked Python scripts");

    ImGui::SameLine();
    if (ImGui::Button("Reload .pyd"))
    {
        if (auto* ctx = VansScriptContext::GetInstance())
        {
            ctx->ReloadPydModule();
            m_NeedsRefresh = true;
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Rebuild vanscomponent, then click here to hot-reload the C++ .pyd module");

    if (m_NeedsRefresh)
        RefreshFileList();

    ImGui::Separator();

    // ---- Left: file list  ---- Right: preview ----
    float listWidth = 220.0f;

    // File list panel
    ImGui::BeginChild("ScriptList", ImVec2(listWidth, 0), true);
    for (size_t i = 0; i < m_PythonFiles.size(); ++i)
    {
        const auto& path = m_PythonFiles[i];
        std::string label = path.filename().string();

        // Mark dirty files with an asterisk
        if (m_Dirty && m_LoadedPath == path)
            label += " *";

        bool isSelected = (m_SelectedScript == path);
        if (ImGui::Selectable(label.c_str(), isSelected))
        {
            if (m_SelectedScript != path)
            {
                m_SelectedScript = path;
                LoadSelectedFile();
            }
        }

        // Tooltip with full path
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", path.string().c_str());
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Editor panel – editable text view of the selected file
    ImGui::BeginChild("ScriptEditor", ImVec2(0, 0), true);
    if (!m_SelectedScript.empty() && std::filesystem::exists(m_SelectedScript))
    {
        // Load the file into the buffer if we haven't yet
        if (m_LoadedPath != m_SelectedScript)
            LoadSelectedFile();

        // Filename header (show * if dirty)
        std::string header = m_SelectedScript.filename().string();
        if (m_Dirty) header += " *";
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", header.c_str());

        // Ctrl+S shortcut
        if (m_Dirty && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
            SaveCurrentFile();

        ImGui::Separator();

        // Reserve space at the bottom for the Save/Revert bar
        float bottomBarHeight = ImGui::GetFrameHeightWithSpacing() + 4.0f;
        ImVec2 avail = ImGui::GetContentRegionAvail();
        avail.y -= bottomBarHeight;

        // Editable text area
        if (ImGui::InputTextMultiline("##source", &m_EditBuffer[0], EDIT_BUF_SIZE,
            avail, ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackEdit,
            [](ImGuiInputTextCallbackData* data) -> int {
                auto* self = static_cast<VansScriptorWindow*>(data->UserData);
                self->m_Dirty = true;
                return 0;
            }, this))
        {
        }

        // Bottom bar: Save / Revert
        ImGui::Separator();
        if (m_Dirty)
        {
            if (ImGui::Button("Save"))
                SaveCurrentFile();
            ImGui::SameLine();
            if (ImGui::Button("Revert"))
                LoadSelectedFile();
        }
        else
        {
            ImGui::TextDisabled("No unsaved changes");
        }
    }
    else
    {
        ImGui::TextDisabled("Select a script to edit.");
    }
    ImGui::EndChild();

    ImGui::End();
}

void VansGraphics::VansScriptorWindow::LoadSelectedFile()
{
    m_EditBuffer.clear();
    m_Dirty = false;
    m_LoadedPath = m_SelectedScript;

    std::ifstream ifs(m_SelectedScript, std::ios::binary);
    if (ifs.is_open())
    {
        std::ostringstream ss;
        ss << ifs.rdbuf();
        m_EditBuffer = ss.str();
    }

    // Resize to fit EDIT_BUF_SIZE so ImGui has room for edits
    m_EditBuffer.resize(EDIT_BUF_SIZE, '\0');
}

void VansGraphics::VansScriptorWindow::SaveCurrentFile()
{
    if (m_LoadedPath.empty()) return;

    // Find the actual string length (buffer is zero-padded)
    size_t len = strlen(m_EditBuffer.c_str());

    std::ofstream ofs(m_LoadedPath, std::ios::binary | std::ios::trunc);
    if (ofs.is_open())
    {
        ofs.write(m_EditBuffer.c_str(), len);
        ofs.close();
        m_Dirty = false;
        VansConsole::Get().LogPython("[Script] Saved " + m_LoadedPath.filename().string());

        // Auto-reload the saved script in the Python interpreter
        if (auto* ctx = VansScriptContext::GetInstance())
            ctx->ReloadAllPyScripts();
    }
    else
    {
        VansConsole::Get().LogPython("[Script] Failed to save " + m_LoadedPath.string());
    }
}
