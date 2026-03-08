#include "VansConsoleWindow.h"
#include "imgui.h"

void VansGraphics::VansConsoleWindow::ShowWindow(VansVKDevice& device)
{
    ImGui::Begin("Console");

    // ---- Toolbar row ----
    {
        // Filter combo
        const char* filterLabels[] = { "All", "Engine", "Python" };
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

    for (const auto& entry : entries)
    {
        // Filter
        if (m_FilterMode == 1 && entry.type != VansConsoleLogType::Engine)
            continue;
        if (m_FilterMode == 2 && entry.type != VansConsoleLogType::Python)
            continue;

        // Tag color  (source: Engine / Python)
        ImVec4 tagColor;
        const char* tag;
        switch (entry.type)
        {
        case VansConsoleLogType::Engine:
            tagColor = ImVec4(0.4f, 0.8f, 0.4f, 1.0f);   // green
            tag = "[Engine]";
            break;
        case VansConsoleLogType::Python:
            tagColor = ImVec4(0.3f, 0.6f, 1.0f, 1.0f);    // blue
            tag = "[Python]";
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

    // Auto-scroll to bottom when new entries arrive
    if (m_AutoScroll && console.ScrollToBottom)
    {
        ImGui::SetScrollHereY(1.0f);
        console.ScrollToBottom = false;
    }

    ImGui::EndChild();

    ImGui::End();
}
