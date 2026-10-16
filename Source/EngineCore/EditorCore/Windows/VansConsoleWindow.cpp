#include "VansConsoleWindow.h"
#include "../VansEditorWindow.h"
#include "imgui.h"
#include <algorithm>

void VansGraphics::VansConsoleWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI&)
{
    DrawConsoleContents();
}

void VansGraphics::VansConsoleWindow::DrawConsoleContents()
{
    if (!VansEditorWindow::IsWindowOpen(VansEditorWindowId::Console))
        return;

    if (!ImGui::Begin("Console"))
    {
        ImGui::End();
        return;
    }

    // ---- Toolbar row ----
    {
        // Filter combo
        const char* filterLabels[] = { "All", "Engine", "Script" };
        ImGui::SetNextItemWidth(120.0f);
        ImGui::Combo("##ConsoleFilter", &m_FilterMode, filterLabels, IM_ARRAYSIZE(filterLabels));
        ImGui::SameLine();

        // Auto-scroll toggle
        ImGui::Checkbox("Auto-scroll", &m_AutoScroll);
        ImGui::SameLine();

        // Clear button
        if (ImGui::Button("Clear"))
        {
            VansConsole::Get().Clear();
        }
    }

    ImGui::Separator();

    // ---- Log area ----
    ImGui::BeginChild("ConsoleScrollRegion", ImVec2(0, 0), false,
        ImGuiWindowFlags_HorizontalScrollbar);

    VansConsole& console = VansConsole::Get();
    const auto& entries = console.GetEntries();
    const auto& style = ImGui::GetStyle();
    if (m_LayoutFilter != m_FilterMode || m_LayoutRevision != console.Revision() ||
        m_LayoutFont != ImGui::GetFont() || m_LayoutFontSize != ImGui::GetFontSize() ||
        m_LayoutSpacingX != style.ItemSpacing.x || m_LayoutSpacingY != style.ItemSpacing.y)
    {
        m_LayoutRevision = console.Revision();
        m_LayoutFilter = m_FilterMode;
        m_LayoutFont = ImGui::GetFont();
        m_LayoutFontSize = ImGui::GetFontSize();
        m_LayoutSpacingX = style.ItemSpacing.x;
        m_LayoutSpacingY = style.ItemSpacing.y;
        m_FilteredEntries.clear();
        m_RowOffsets.assign(1, 0.0f);
        m_ContentWidth = 0;
        for (std::size_t index = 0; index < entries.size(); ++index)
        {
            const auto& entry = entries[index];
            if ((m_FilterMode == 1 && entry.type != VansConsoleLogType::Engine) ||
                (m_FilterMode == 2 && entry.type != VansConsoleLogType::Script))
                continue;
            const auto messageSize = ImGui::CalcTextSize(entry.message.c_str());
            const std::string timestamp = "[" + entry.timestamp + "]";
            const char* tag = entry.type == VansConsoleLogType::Engine ? "[Engine]" : "[Script]";
            m_FilteredEntries.push_back(index);
            m_RowOffsets.push_back(m_RowOffsets.back() +
                std::max(ImGui::GetFontSize(), messageSize.y) + style.ItemSpacing.y);
            m_ContentWidth = std::max(m_ContentWidth, ImGui::CalcTextSize(timestamp.c_str()).x +
                ImGui::CalcTextSize(tag).x + messageSize.x + 2 * style.ItemSpacing.x);
        }
    }

    // 多行日志使用真实行高的前缀和，保留完整滚动范围及不可见行的水平宽度。
    const float originY = ImGui::GetCursorPosY();
    std::size_t firstRow = 0, lastRow = m_FilteredEntries.size();
    if (!m_FilteredEntries.empty())
    {
        const float visibleTop = std::max(0.0f, ImGui::GetScrollY() - originY);
        const float visibleBottom = visibleTop + ImGui::GetWindowHeight();
        firstRow = static_cast<std::size_t>(std::upper_bound(
            m_RowOffsets.begin(), m_RowOffsets.end(), visibleTop) - m_RowOffsets.begin() - 1);
        lastRow = std::min(lastRow, static_cast<std::size_t>(std::lower_bound(
            m_RowOffsets.begin(), m_RowOffsets.end(), visibleBottom) - m_RowOffsets.begin()) + 1);
        ImGui::Dummy(ImVec2(m_ContentWidth, m_RowOffsets.back() - style.ItemSpacing.y));
        ImGui::SetCursorPosY(originY + m_RowOffsets[firstRow]);
    }

    for (std::size_t row = firstRow; row < lastRow; ++row)
    {
        const auto& entry = entries[m_FilteredEntries[row]];

        // Tag color  (source: Engine / Script)
        ImVec4 tagColor;
        const char* tag;
        switch (entry.type)
        {
        case VansConsoleLogType::Engine:
            tagColor = ImVec4(0.4f, 0.8f, 0.4f, 1.0f);   // green
            tag = "[Engine]";
            break;
        case VansConsoleLogType::Script:
            tagColor = ImVec4(0.3f, 0.6f, 1.0f, 1.0f);    // blue
            tag = "[Script]";
            break;
        default:
            tagColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
            tag = "[???]";
            break;
        }

        // Message color based on severity level
        ImVec4 msgColor;
        switch (entry.severity)
        {
        case VansConsoleSeverity::Warning:
            msgColor = ImVec4(1.0f, 0.85f, 0.2f, 1.0f);   // yellow
            break;
        case VansConsoleSeverity::Error:
            msgColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);    // red
            break;
        case VansConsoleSeverity::Info:
        default:
            msgColor = ImVec4(0.86f, 0.86f, 0.88f, 1.0f);  // white/light gray
            break;
        }

        // Timestamp
        ImGui::TextDisabled("[%s]", entry.timestamp.c_str());
        ImGui::SameLine();

        // Tag
        ImGui::TextColored(tagColor, "%s", tag);
        ImGui::SameLine();

        // Message (colored by severity)
        ImGui::TextColored(msgColor, "%s", entry.message.c_str());
    }

    if (!m_FilteredEntries.empty())
    {
        ImGui::SetCursorPosY(originY + m_RowOffsets.back() - style.ItemSpacing.y);
        ImGui::Dummy(ImVec2(0, 0));
    }

    // Auto-scroll to bottom when new entries arrive
    if (m_AutoScroll && console.ScrollToBottom)
    {
        ImGui::SetScrollHereY(1.0f);
        console.ScrollToBottom = false;
    }

    ImGui::EndChild();

    ImGui::End();
}
