#include "VansProfilerWindow.h"
#include "../../Util/VansProfiler.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

// -----------------------------------------------------------------------
// Color palette
// -----------------------------------------------------------------------
namespace
{
    const ImVec4 kCpuColor     = ImVec4(0.35f, 0.75f, 0.35f, 1.0f);   // green
    const ImVec4 kGpuColor     = ImVec4(0.40f, 0.55f, 0.90f, 1.0f);   // blue
    const ImVec4 kFpsLineColor = ImVec4(0.90f, 0.70f, 0.20f, 1.0f);   // amber
    const ImVec4 kHeaderBg     = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
}

// -----------------------------------------------------------------------
// ShowWindow  –  main entry point called from EditorWindow loop
// -----------------------------------------------------------------------
void VansGraphics::VansProfilerWindow::ShowWindow(VansVKDevice& device)
{
#if VANS_PROFILER_ENABLED

    ImGui::Begin("Profiler", nullptr, ImGuiWindowFlags_NoScrollbar);

    // -- Controls bar --
    ImGui::Checkbox("Pause", &m_PauseCapture);
    ImGui::SameLine();
    ImGui::Checkbox("CPU", &m_ShowCpu);
    ImGui::SameLine();
    ImGui::Checkbox("GPU", &m_ShowGpu);
    ImGui::SameLine();

    if (ImGui::Button("Dump JSON"))
    {
        Vans::VansProfiler::Get().DumpFrameJson();
    }

    ImGui::Separator();

    // -- Update histories --
    if (!m_PauseCapture)
    {
        const Vans::ProfileFrame& frame = Vans::VansProfiler::Get().GetTimeline();
        m_FpsHistory[m_FpsHistoryOffset]       = static_cast<float>(frame.fps);
        m_FrameTimeHistory[m_FpsHistoryOffset]  = static_cast<float>(frame.frameDurationMs);
        m_FpsHistoryOffset = (m_FpsHistoryOffset + 1) % FPS_HISTORY_SIZE;
    }

    // -- Tabs --
    if (ImGui::BeginTabBar("ProfilerTabs"))
    {
        if (ImGui::BeginTabItem("FPS"))
        {
            DrawFpsGraph();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Timeline"))
        {
            DrawFrameTimeline();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Details"))
        {
            DrawRecordTree();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();

#endif // VANS_PROFILER_ENABLED
}

// -----------------------------------------------------------------------
// FPS line graph + frame time overlay
// -----------------------------------------------------------------------
void VansGraphics::VansProfilerWindow::DrawFpsGraph()
{
#if VANS_PROFILER_ENABLED
    const Vans::ProfileFrame& frame = Vans::VansProfiler::Get().GetTimeline();

    // Summary text
    ImGui::TextColored(kFpsLineColor, "FPS: %.1f", frame.fps);
    ImGui::SameLine(200);
    ImGui::Text("Frame: %.2f ms", frame.frameDurationMs);
    ImGui::SameLine(400);
    ImGui::Text("Frame #%u", frame.frameIndex);

    // Compute min/max for adaptive scaling
    float fpsMin = 9999.0f, fpsMax = 0.0f;
    float ftMin  = 9999.0f, ftMax  = 0.0f;
    for (int i = 0; i < FPS_HISTORY_SIZE; i++)
    {
        if (m_FpsHistory[i] > 0.0f)
        {
            fpsMin = std::min(fpsMin, m_FpsHistory[i]);
            fpsMax = std::max(fpsMax, m_FpsHistory[i]);
        }
        if (m_FrameTimeHistory[i] > 0.0f)
        {
            ftMin = std::min(ftMin, m_FrameTimeHistory[i]);
            ftMax = std::max(ftMax, m_FrameTimeHistory[i]);
        }
    }
    if (fpsMax < 1.0f) fpsMax = 144.0f;
    // Add some padding
    fpsMin = std::max(0.0f, fpsMin - 5.0f);
    fpsMax += 10.0f;

    // --- Custom FPS line graph using ImDrawList ---
    ImVec2 graphSize = ImVec2(ImGui::GetContentRegionAvail().x, 180.0f);
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();

    // Background rect
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(cursorPos,
        ImVec2(cursorPos.x + graphSize.x, cursorPos.y + graphSize.y),
        IM_COL32(15, 15, 18, 255));

    // Reference lines at 30, 60, 120 fps
    float refLines[] = { 30.0f, 60.0f, 120.0f };
    for (float ref : refLines)
    {
        if (ref >= fpsMin && ref <= fpsMax)
        {
            float y = cursorPos.y + graphSize.y - ((ref - fpsMin) / (fpsMax - fpsMin)) * graphSize.y;
            drawList->AddLine(
                ImVec2(cursorPos.x, y),
                ImVec2(cursorPos.x + graphSize.x, y),
                IM_COL32(80, 80, 80, 120), 1.0f);

            char label[16];
            snprintf(label, sizeof(label), "%.0f", ref);
            drawList->AddText(ImVec2(cursorPos.x + 4, y - 14), IM_COL32(160, 160, 160, 200), label);
        }
    }

    // Plot FPS line
    float dx = graphSize.x / static_cast<float>(FPS_HISTORY_SIZE - 1);
    ImU32 lineCol = ImGui::ColorConvertFloat4ToU32(kFpsLineColor);

    for (int i = 1; i < FPS_HISTORY_SIZE; i++)
    {
        int idx0 = (m_FpsHistoryOffset + i - 1) % FPS_HISTORY_SIZE;
        int idx1 = (m_FpsHistoryOffset + i)     % FPS_HISTORY_SIZE;

        float v0 = m_FpsHistory[idx0];
        float v1 = m_FpsHistory[idx1];

        if (v0 <= 0.0f && v1 <= 0.0f) continue;

        float x0 = cursorPos.x + static_cast<float>(i - 1) * dx;
        float x1 = cursorPos.x + static_cast<float>(i)     * dx;
        float y0 = cursorPos.y + graphSize.y - ((v0 - fpsMin) / (fpsMax - fpsMin)) * graphSize.y;
        float y1 = cursorPos.y + graphSize.y - ((v1 - fpsMin) / (fpsMax - fpsMin)) * graphSize.y;

        y0 = std::clamp(y0, cursorPos.y, cursorPos.y + graphSize.y);
        y1 = std::clamp(y1, cursorPos.y, cursorPos.y + graphSize.y);

        drawList->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), lineCol, 2.0f);
    }

    // Border
    drawList->AddRect(cursorPos,
        ImVec2(cursorPos.x + graphSize.x, cursorPos.y + graphSize.y),
        IM_COL32(60, 60, 60, 200));

    // Reserve ImGui space
    ImGui::Dummy(graphSize);

    // --- Frame time plot (smaller, below) ---
    ImGui::Text("Frame Time (ms):");
    char overlay[64];
    snprintf(overlay, sizeof(overlay), "%.2f ms", frame.frameDurationMs);

    // Reorder the ring buffer for ImGui::PlotLines
    float ordered[FPS_HISTORY_SIZE];
    for (int i = 0; i < FPS_HISTORY_SIZE; i++)
        ordered[i] = m_FrameTimeHistory[(m_FpsHistoryOffset + i) % FPS_HISTORY_SIZE];

    ImGui::PlotLines("##FrameTime", ordered, FPS_HISTORY_SIZE, 0, overlay,
        0.0f, ftMax * 1.2f, ImVec2(ImGui::GetContentRegionAvail().x, 80.0f));

#endif
}

// -----------------------------------------------------------------------
// Visual timeline bars (CPU green, GPU blue)
// -----------------------------------------------------------------------
void VansGraphics::VansProfilerWindow::DrawFrameTimeline()
{
#if VANS_PROFILER_ENABLED
    const Vans::ProfileFrame& frame = Vans::VansProfiler::Get().GetTimeline();

    if (frame.recordCount == 0)
    {
        ImGui::Text("No profiling data");
        return;
    }

    ImGui::Text("Frame #%u  |  %.2f ms  |  %.1f FPS", frame.frameIndex, frame.frameDurationMs, frame.fps);
    ImGui::Separator();

    float totalMs = static_cast<float>(frame.frameDurationMs);
    if (totalMs <= 0.0f) totalMs = 1.0f;

    ImVec2 regionAvail = ImGui::GetContentRegionAvail();
    float barAreaWidth = regionAvail.x - 20.0f;
    float barHeight    = 20.0f;
    float depthIndent  = 16.0f;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Section headers
    auto DrawSection = [&](const char* label, bool isGpu)
    {
        ImGui::TextColored(isGpu ? kGpuColor : kCpuColor, "%s", label);
        ImGui::Separator();

        for (uint32_t i = 0; i < frame.recordCount; i++)
        {
            const Vans::ProfileRecord& r = frame.records[i];
            if (r.isGpu != isGpu) continue;
            if (isGpu && !m_ShowGpu) continue;
            if (!isGpu && !m_ShowCpu) continue;

            float indent = static_cast<float>(r.depth) * depthIndent;
            float startFrac = static_cast<float>(r.startMs) / totalMs;
            float widthFrac = static_cast<float>(r.durationMs) / totalMs;

            // Clamp
            startFrac = std::clamp(startFrac, 0.0f, 1.0f);
            widthFrac = std::clamp(widthFrac, 0.001f, 1.0f - startFrac);

            ImVec2 cursor = ImGui::GetCursorScreenPos();
            cursor.x += indent;

            float barW = widthFrac * (barAreaWidth - indent);
            if (barW < 2.0f) barW = 2.0f;

            ImVec4 col = isGpu ? kGpuColor : kCpuColor;
            // Darken by depth
            float darken = 1.0f - static_cast<float>(r.depth) * 0.12f;
            col.x *= darken; col.y *= darken; col.z *= darken;

            ImU32 barCol = ImGui::ColorConvertFloat4ToU32(col);

            drawList->AddRectFilled(
                cursor,
                ImVec2(cursor.x + barW, cursor.y + barHeight),
                barCol, 3.0f);

            // Label on bar
            char barLabel[128];
            snprintf(barLabel, sizeof(barLabel), "%s (%.2f ms)", r.name ? r.name : "?", r.durationMs);

            ImVec2 textSize = ImGui::CalcTextSize(barLabel);
            if (textSize.x < barW - 4.0f)
            {
                drawList->AddText(
                    ImVec2(cursor.x + 4.0f, cursor.y + 2.0f),
                    IM_COL32(255, 255, 255, 220), barLabel);
            }
            else
            {
                // Short label
                char shortLabel[64];
                snprintf(shortLabel, sizeof(shortLabel), "%.2f ms", r.durationMs);
                drawList->AddText(
                    ImVec2(cursor.x + 4.0f, cursor.y + 2.0f),
                    IM_COL32(255, 255, 255, 220), shortLabel);
            }

            // Tooltip on hover
            ImGui::SetCursorScreenPos(cursor);
            ImGui::InvisibleButton(("##prof_" + std::to_string(i)).c_str(), ImVec2(barW, barHeight));
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("%s", r.name);
                ImGui::Text("Start: %.3f ms", r.startMs);
                ImGui::Text("Duration: %.3f ms", r.durationMs);
                ImGui::Text("Depth: %d", r.depth);
                ImGui::Text("Type: %s", r.isGpu ? "GPU" : "CPU");
                ImGui::EndTooltip();
            }

            ImGui::SetCursorScreenPos(ImVec2(cursor.x - indent, cursor.y + barHeight + 2.0f));
        }
    };

    // Scrollable child region
    ImGui::BeginChild("TimelineBars", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);

    if (m_ShowCpu) DrawSection("CPU", false);
    if (m_ShowGpu) DrawSection("GPU", true);

    ImGui::EndChild();

#endif
}

// -----------------------------------------------------------------------
// Tree-view detail table
// -----------------------------------------------------------------------
void VansGraphics::VansProfilerWindow::DrawRecordTree()
{
#if VANS_PROFILER_ENABLED
    const Vans::ProfileFrame& frame = Vans::VansProfiler::Get().GetTimeline();

    if (frame.recordCount == 0)
    {
        ImGui::Text("No profiling data");
        return;
    }

    // Summary
    double cpuTotal = 0.0, gpuTotal = 0.0;
    int cpuCount = 0, gpuCount = 0;
    for (uint32_t i = 0; i < frame.recordCount; i++)
    {
        if (frame.records[i].depth == 0)
        {
            if (frame.records[i].isGpu)
            { gpuTotal += frame.records[i].durationMs; gpuCount++; }
            else
            { cpuTotal += frame.records[i].durationMs; cpuCount++; }
        }
    }

    ImGui::Text("CPU scopes: %d (top-level total: %.2f ms)  |  GPU scopes: %d (top-level total: %.2f ms)",
        cpuCount, cpuTotal, gpuCount, gpuTotal);
    ImGui::Separator();

    // Table
    if (ImGui::BeginTable("ProfileRecords", 5,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
        ImVec2(0, ImGui::GetContentRegionAvail().y)))
    {
        ImGui::TableSetupColumn("Name",     ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Type",     ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Start",    ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Depth",    ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (uint32_t i = 0; i < frame.recordCount; i++)
        {
            const Vans::ProfileRecord& r = frame.records[i];
            if (r.isGpu && !m_ShowGpu) continue;
            if (!r.isGpu && !m_ShowCpu) continue;

            ImGui::TableNextRow();

            // Name with indent
            ImGui::TableSetColumnIndex(0);
            for (uint8_t d = 0; d < r.depth; d++) ImGui::Indent(10.0f);
            ImGui::TextColored(r.isGpu ? kGpuColor : kCpuColor, "%s", r.name ? r.name : "?");
            for (uint8_t d = 0; d < r.depth; d++) ImGui::Unindent(10.0f);

            // Type
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(r.isGpu ? kGpuColor : kCpuColor, "%s", r.isGpu ? "GPU" : "CPU");

            // Start
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f ms", r.startMs);

            // Duration
            ImGui::TableSetColumnIndex(3);
            // Color-code high durations
            if (r.durationMs > 8.0)
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%.3f ms", r.durationMs);
            else if (r.durationMs > 4.0)
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%.3f ms", r.durationMs);
            else
                ImGui::Text("%.3f ms", r.durationMs);

            // Depth
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", r.depth);
        }

        ImGui::EndTable();
    }

#endif
}
