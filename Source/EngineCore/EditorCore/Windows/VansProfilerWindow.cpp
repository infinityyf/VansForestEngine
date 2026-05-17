#include "VansProfilerWindow.h"
#include "../../Util/VansProfiler.h"
#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace
{
    const char* CategoryName(Vans::ProfileCategory category)
    {
        switch (category)
        {
        case Vans::ProfileCategory::Frame:         return "Frame";
        case Vans::ProfileCategory::Editor:        return "Editor";
        case Vans::ProfileCategory::Script:        return "Script";
        case Vans::ProfileCategory::Physics:       return "Physics";
        case Vans::ProfileCategory::Animation:     return "Animation";
        case Vans::ProfileCategory::Particles:     return "Particles";
        case Vans::ProfileCategory::Audio:         return "Audio";
        case Vans::ProfileCategory::Video:         return "Video";
        case Vans::ProfileCategory::RuntimeUI:     return "RuntimeUI";
        case Vans::ProfileCategory::RenderPrepare: return "RenderPrepare";
        case Vans::ProfileCategory::CommandRecord: return "CommandRecord";
        case Vans::ProfileCategory::VulkanSubmit:  return "VulkanSubmit";
        case Vans::ProfileCategory::GPU:           return "GPU";
        case Vans::ProfileCategory::JobSystem:     return "JobSystem";
        case Vans::ProfileCategory::Wait:          return "Wait";
        case Vans::ProfileCategory::IO:            return "IO";
        default:                                   return "Other";
        }
    }

    uint32_t CategoryBit(Vans::ProfileCategory category)
    {
        return 1u << static_cast<uint32_t>(category);
    }

    float EventDurationMs(const Vans::ProfileEvent& event)
    {
        return static_cast<float>((event.endUs - event.startUs) * 0.001);
    }
}

void VansGraphics::VansProfilerWindow::ShowWindow(VansVKDevice& /*device*/)
{
#if VANS_PROFILER_ENABLED
    VANS_PROFILE_SCOPE("Editor::ProfilerWindow", Vans::ProfileCategory::Editor);

    ImGui::Begin("Profiler", nullptr, ImGuiWindowFlags_NoScrollbar);

    const Vans::ProfileFrame& frame = Vans::VansProfiler::Get().GetTimeline();
    if (!m_PauseCapture)
    {
        m_FpsHistory[m_FpsHistoryOffset] = static_cast<float>(frame.fps);
        m_FrameTimeHistory[m_FpsHistoryOffset] = static_cast<float>(frame.frameDurationUs * 0.001);
        m_FpsHistoryOffset = (m_FpsHistoryOffset + 1) % FPS_HISTORY_SIZE;
        m_ViewStartUs = 0.0;
        m_ViewEndUs = std::max(16667.0, frame.frameDurationUs);
    }

    DrawTimelineToolbar(frame);

    if (ImGui::BeginTabBar("ProfilerTabs"))
    {
        if (ImGui::BeginTabItem("Timeline"))
        {
            DrawTimeline();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Hierarchy"))
        {
            DrawHierarchy();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Frame Graph"))
        {
            DrawFrameGraph();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Raw Events"))
        {
            DrawRawEvents();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
#endif
}

void VansGraphics::VansProfilerWindow::DrawTimelineToolbar(const Vans::ProfileFrame& frame)
{
#if VANS_PROFILER_ENABLED
    ImGui::Text("Frame #%llu  %.3f ms  %.1f FPS  Events:%u Tracks:%u%s",
        static_cast<unsigned long long>(frame.frameIndex),
        frame.frameDurationUs * 0.001,
        frame.fps,
        frame.eventCount,
        frame.trackCount,
        frame.overflow ? "  OVERFLOW" : "");

    ImGui::SameLine();
    ImGui::Checkbox("Pause", &m_PauseCapture);
    ImGui::SameLine();
    ImGui::Checkbox("CPU", &m_ShowCpu);
    ImGui::SameLine();
    ImGui::Checkbox("GPU", &m_ShowGpu);
    ImGui::SameLine();
    if (ImGui::Button("Dump JSON"))
        Vans::VansProfiler::Get().DumpFrameJson();

    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputText("Search", m_SearchText, sizeof(m_SearchText));
    ImGui::SameLine();
    if (ImGui::Button("Reset View"))
    {
        m_ViewStartUs = 0.0;
        m_ViewEndUs = std::max(16667.0, frame.frameDurationUs);
    }

    const struct FilterItem
    {
        const char* name;
        Vans::ProfileCategory category;
    } items[] = {
        { "Frame", Vans::ProfileCategory::Frame },
        { "Editor", Vans::ProfileCategory::Editor },
        { "Script", Vans::ProfileCategory::Script },
        { "Physics", Vans::ProfileCategory::Physics },
        { "Anim", Vans::ProfileCategory::Animation },
        { "Particles", Vans::ProfileCategory::Particles },
        { "Audio", Vans::ProfileCategory::Audio },
        { "Video", Vans::ProfileCategory::Video },
        { "UI", Vans::ProfileCategory::RuntimeUI },
        { "Record", Vans::ProfileCategory::CommandRecord },
        { "Submit", Vans::ProfileCategory::VulkanSubmit },
        { "GPU", Vans::ProfileCategory::GPU },
        { "Wait", Vans::ProfileCategory::Wait },
        { "Job", Vans::ProfileCategory::JobSystem },
        { "Other", Vans::ProfileCategory::Other },
    };

    for (const auto& item : items)
    {
        bool enabled = (m_FilterMask & CategoryBit(item.category)) != 0;
        if (ImGui::Checkbox(item.name, &enabled))
        {
            if (enabled)
                m_FilterMask |= CategoryBit(item.category);
            else
                m_FilterMask &= ~CategoryBit(item.category);
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::Separator();
#endif
}

void VansGraphics::VansProfilerWindow::DrawTimeline()
{
#if VANS_PROFILER_ENABLED
    const Vans::ProfileFrame& frame = Vans::VansProfiler::Get().GetTimeline();
    if (frame.eventCount == 0)
    {
        ImGui::Text("No profiling data");
        return;
    }

    double viewDurationUs = std::max(100.0, m_ViewEndUs - m_ViewStartUs);
    float zoom = static_cast<float>(frame.frameDurationUs / viewDurationUs);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::SliderFloat("Zoom", &zoom, 1.0f, 40.0f, "%.1fx"))
    {
        double center = (m_ViewStartUs + m_ViewEndUs) * 0.5;
        double newDuration = std::max(100.0, frame.frameDurationUs / static_cast<double>(zoom));
        m_ViewStartUs = std::max(0.0, center - newDuration * 0.5);
        m_ViewEndUs = m_ViewStartUs + newDuration;
    }

    ImGui::SameLine();
    float startMs = static_cast<float>(m_ViewStartUs * 0.001);
    float endMs = static_cast<float>(m_ViewEndUs * 0.001);
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::DragFloatRange2("Visible ms", &startMs, &endMs, 0.05f, 0.0f, static_cast<float>(frame.frameDurationUs * 0.001), "%.2f", "%.2f"))
    {
        m_ViewStartUs = startMs * 1000.0;
        m_ViewEndUs = std::max(m_ViewStartUs + 100.0, endMs * 1000.0);
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float rulerHeight = 28.0f;
    DrawTimeRuler(frame, origin, avail.x);
    ImGui::Dummy(ImVec2(avail.x, rulerHeight));

    ImGui::BeginChild("TimelineTracks", ImVec2(0, 0), ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar);
    float cursorY = ImGui::GetCursorScreenPos().y;
    ImVec2 childOrigin = ImGui::GetCursorScreenPos();
    for (uint32_t i = 0; i < frame.trackCount; ++i)
    {
        const Vans::ProfileTrack& track = frame.tracks[i];
        if (!m_ShowCpu && track.type == Vans::ProfileTrackType::CpuThread)
            continue;
        if (!m_ShowGpu && track.type == Vans::ProfileTrackType::GpuQueue)
            continue;
        DrawTrackLane(frame, track, ImVec2(childOrigin.x, cursorY), avail.x, cursorY);
    }
    ImGui::EndChild();
#endif
}

void VansGraphics::VansProfilerWindow::DrawTimeRuler(const Vans::ProfileFrame& frame, const ImVec2& origin, float width)
{
#if VANS_PROFILER_ENABLED
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float height = 26.0f;
    drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(24, 24, 28, 255));

    double visibleUs = std::max(100.0, m_ViewEndUs - m_ViewStartUs);
    double pixelsPerUs = static_cast<double>(width) / visibleUs;
    const double stepUs = visibleUs > 50000.0 ? 10000.0 : (visibleUs > 20000.0 ? 5000.0 : 1000.0);

    for (double t = 0.0; t <= frame.frameDurationUs + stepUs; t += stepUs)
    {
        if (t < m_ViewStartUs || t > m_ViewEndUs)
            continue;
        float x = origin.x + static_cast<float>((t - m_ViewStartUs) * pixelsPerUs);
        drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + height), IM_COL32(80, 80, 88, 180));
        char label[32];
        std::snprintf(label, sizeof(label), "%.1f", t * 0.001);
        drawList->AddText(ImVec2(x + 3.0f, origin.y + 4.0f), IM_COL32(210, 210, 210, 255), label);
    }

    const double budget60 = 16667.0;
    if (budget60 >= m_ViewStartUs && budget60 <= m_ViewEndUs)
    {
        float x = origin.x + static_cast<float>((budget60 - m_ViewStartUs) * pixelsPerUs);
        drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + height), IM_COL32(255, 180, 40, 255), 2.0f);
    }
#endif
}

void VansGraphics::VansProfilerWindow::DrawTrackLane(const Vans::ProfileFrame& frame, const Vans::ProfileTrack& track, const ImVec2& origin, float width, float& cursorY)
{
#if VANS_PROFILER_ENABLED
    uint16_t maxDepth = 0;
    for (uint32_t i = 0; i < frame.eventCount; ++i)
    {
        const Vans::ProfileEvent& event = frame.events[i];
        if (event.trackId == track.trackId && IsCategoryVisible(event.category) && PassSearch(event))
            maxDepth = std::max<uint16_t>(maxDepth, event.depth);
    }

    const float headerHeight = 22.0f;
    const float barHeight = 18.0f;
    const float laneHeight = headerHeight + (static_cast<float>(maxDepth) + 1.0f) * barHeight + 8.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + laneHeight), IM_COL32(16, 16, 20, 255));
    drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + headerHeight), IM_COL32(32, 32, 38, 255));
    drawList->AddText(ImVec2(origin.x + 6.0f, origin.y + 3.0f), IM_COL32(235, 235, 235, 255), track.name);

    double visibleUs = std::max(100.0, m_ViewEndUs - m_ViewStartUs);
    double pixelsPerUs = static_cast<double>(width) / visibleUs;

    for (uint32_t i = 0; i < frame.eventCount; ++i)
    {
        const Vans::ProfileEvent& event = frame.events[i];
        if (event.trackId != track.trackId)
            continue;
        if (!IsCategoryVisible(event.category) || !PassSearch(event))
            continue;
        if (event.endUs < m_ViewStartUs || event.startUs > m_ViewEndUs)
            continue;

        float x0 = origin.x + static_cast<float>((event.startUs - m_ViewStartUs) * pixelsPerUs);
        float x1 = origin.x + static_cast<float>((event.endUs - m_ViewStartUs) * pixelsPerUs);
        x0 = std::clamp(x0, origin.x, origin.x + width);
        x1 = std::clamp(x1, origin.x, origin.x + width);
        if (x1 - x0 < 2.0f)
            x1 = x0 + 2.0f;

        float y = origin.y + headerHeight + static_cast<float>(event.depth) * barHeight + 2.0f;
        ImU32 color = GetCategoryColor(event.category, event.flags);
        drawList->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + barHeight - 3.0f), color, 3.0f);
        drawList->AddRect(ImVec2(x0, y), ImVec2(x1, y + barHeight - 3.0f), IM_COL32(0, 0, 0, 120), 3.0f);

        if (x1 - x0 > 48.0f)
        {
            char label[128];
            std::snprintf(label, sizeof(label), "%s %.2f", event.name, EventDurationMs(event));
            drawList->AddText(ImVec2(x0 + 4.0f, y + 1.0f), IM_COL32(255, 255, 255, 230), label);
        }

        ImGui::SetCursorScreenPos(ImVec2(x0, y));
        char id[64];
        std::snprintf(id, sizeof(id), "##event_%u_%u", track.trackId, event.eventId);
        ImGui::InvisibleButton(id, ImVec2(std::max(2.0f, x1 - x0), barHeight - 3.0f));
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("%s", event.name);
            ImGui::Text("Track: %s", track.name);
            ImGui::Text("Category: %s", CategoryName(event.category));
            ImGui::Text("Start: %.3f ms", event.startUs * 0.001);
            ImGui::Text("Duration: %.3f ms", EventDurationMs(event));
            ImGui::Text("Depth: %u", event.depth);
            ImGui::EndTooltip();
        }
        if (ImGui::IsItemClicked())
            m_SelectedEventId = static_cast<int>(event.eventId);
    }

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + laneHeight));
    ImGui::Dummy(ImVec2(width, laneHeight));
    cursorY = origin.y + laneHeight + 2.0f;
#endif
}

void VansGraphics::VansProfilerWindow::DrawHierarchy()
{
#if VANS_PROFILER_ENABLED
    const Vans::ProfileFrame& frame = Vans::VansProfiler::Get().GetTimeline();
    struct Row
    {
        double totalUs = 0.0;
        double maxUs = 0.0;
        uint32_t calls = 0;
        Vans::ProfileCategory category = Vans::ProfileCategory::Other;
        char name[96] = {};
    };

    Row rows[512] = {};
    uint32_t rowCount = 0;
    for (uint32_t i = 0; i < frame.eventCount; ++i)
    {
        const Vans::ProfileEvent& event = frame.events[i];
        if (!IsCategoryVisible(event.category) || !PassSearch(event))
            continue;

        uint32_t rowIndex = rowCount;
        for (uint32_t r = 0; r < rowCount; ++r)
        {
            if (rows[r].category == event.category && std::strcmp(rows[r].name, event.name) == 0)
            {
                rowIndex = r;
                break;
            }
        }

        if (rowIndex == rowCount && rowCount < 512)
        {
            rows[rowIndex].category = event.category;
            std::snprintf(rows[rowIndex].name, sizeof(rows[rowIndex].name), "%s", event.name);
            ++rowCount;
        }

        double durationUs = std::max(0.0, event.endUs - event.startUs);
        rows[rowIndex].totalUs += durationUs;
        rows[rowIndex].maxUs = std::max(rows[rowIndex].maxUs, durationUs);
        rows[rowIndex].calls++;
    }

    std::sort(rows, rows + rowCount, [](const Row& a, const Row& b) { return a.totalUs > b.totalUs; });

    if (ImGui::BeginTable("ProfilerHierarchy", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, 0)))
    {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Total ms", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Avg ms", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Max ms", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        for (uint32_t i = 0; i < rowCount; ++i)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%s", rows[i].name);
            ImGui::TableSetColumnIndex(1); ImGui::Text("%s", CategoryName(rows[i].category));
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", rows[i].totalUs * 0.001);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%u", rows[i].calls);
            ImGui::TableSetColumnIndex(4); ImGui::Text("%.3f", rows[i].calls ? rows[i].totalUs * 0.001 / rows[i].calls : 0.0);
            ImGui::TableSetColumnIndex(5); ImGui::Text("%.3f", rows[i].maxUs * 0.001);
        }
        ImGui::EndTable();
    }
#endif
}

void VansGraphics::VansProfilerWindow::DrawFrameGraph()
{
#if VANS_PROFILER_ENABLED
    const Vans::ProfileFrame& frame = Vans::VansProfiler::Get().GetTimeline();
    ImGui::Text("FPS: %.1f", frame.fps);
    ImGui::SameLine();
    ImGui::Text("Frame Time: %.3f ms", frame.frameDurationUs * 0.001);

    float ordered[FPS_HISTORY_SIZE];
    float maxFrameMs = 1.0f;
    for (int i = 0; i < FPS_HISTORY_SIZE; ++i)
    {
        ordered[i] = m_FrameTimeHistory[(m_FpsHistoryOffset + i) % FPS_HISTORY_SIZE];
        maxFrameMs = std::max(maxFrameMs, ordered[i]);
    }
    ImGui::PlotHistogram("Frame Time ms", ordered, FPS_HISTORY_SIZE, 0, nullptr, 0.0f, maxFrameMs * 1.2f, ImVec2(ImGui::GetContentRegionAvail().x, 160.0f));

    for (int i = 0; i < FPS_HISTORY_SIZE; ++i)
        ordered[i] = m_FpsHistory[(m_FpsHistoryOffset + i) % FPS_HISTORY_SIZE];
    ImGui::PlotLines("FPS", ordered, FPS_HISTORY_SIZE, 0, nullptr, 0.0f, 180.0f, ImVec2(ImGui::GetContentRegionAvail().x, 100.0f));
#endif
}

void VansGraphics::VansProfilerWindow::DrawRawEvents()
{
#if VANS_PROFILER_ENABLED
    const Vans::ProfileFrame& frame = Vans::VansProfiler::Get().GetTimeline();
    if (ImGui::BeginTable("RawEvents", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, 0)))
    {
        ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Start", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Depth", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        for (uint32_t i = 0; i < frame.eventCount; ++i)
        {
            const Vans::ProfileEvent& event = frame.events[i];
            if (!IsCategoryVisible(event.category) || !PassSearch(event))
                continue;
            const Vans::ProfileTrack* track = FindTrack(frame, event.trackId);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%s", track ? track->name : "?");
            ImGui::TableSetColumnIndex(1); ImGui::Text("%s", event.name);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%s", CategoryName(event.category));
            ImGui::TableSetColumnIndex(3); ImGui::Text("%.3f", event.startUs * 0.001);
            ImGui::TableSetColumnIndex(4); ImGui::Text("%.3f", EventDurationMs(event));
            ImGui::TableSetColumnIndex(5); ImGui::Text("%u", event.depth);
            ImGui::TableSetColumnIndex(6); ImGui::Text("0x%04x", event.flags);
        }
        ImGui::EndTable();
    }
#endif
}

bool VansGraphics::VansProfilerWindow::IsCategoryVisible(Vans::ProfileCategory category) const
{
    return (m_FilterMask & CategoryBit(category)) != 0;
}

bool VansGraphics::VansProfilerWindow::PassSearch(const Vans::ProfileEvent& event) const
{
    if (m_SearchText[0] == '\0')
        return true;
    return std::strstr(event.name, m_SearchText) != nullptr;
}

uint32_t VansGraphics::VansProfilerWindow::GetCategoryColor(Vans::ProfileCategory category, uint16_t flags) const
{
    if ((flags & Vans::ProfileEventFlagWait) != 0)
        return IM_COL32(220, 70, 70, 220);
    if ((flags & Vans::ProfileEventFlagGpu) != 0 || category == Vans::ProfileCategory::GPU)
        return IM_COL32(105, 125, 255, 230);

    switch (category)
    {
    case Vans::ProfileCategory::Frame:         return IM_COL32(180, 180, 180, 230);
    case Vans::ProfileCategory::Editor:        return IM_COL32(190, 120, 255, 230);
    case Vans::ProfileCategory::Script:        return IM_COL32(240, 190, 70, 230);
    case Vans::ProfileCategory::Physics:       return IM_COL32(90, 180, 255, 230);
    case Vans::ProfileCategory::Animation:     return IM_COL32(255, 150, 80, 230);
    case Vans::ProfileCategory::Particles:     return IM_COL32(255, 100, 170, 230);
    case Vans::ProfileCategory::Audio:         return IM_COL32(80, 210, 170, 230);
    case Vans::ProfileCategory::Video:         return IM_COL32(80, 180, 100, 230);
    case Vans::ProfileCategory::RuntimeUI:     return IM_COL32(130, 220, 240, 230);
    case Vans::ProfileCategory::RenderPrepare: return IM_COL32(100, 210, 120, 230);
    case Vans::ProfileCategory::CommandRecord: return IM_COL32(110, 170, 255, 230);
    case Vans::ProfileCategory::VulkanSubmit:  return IM_COL32(120, 130, 255, 230);
    case Vans::ProfileCategory::JobSystem:     return IM_COL32(170, 170, 90, 230);
    case Vans::ProfileCategory::IO:            return IM_COL32(150, 150, 220, 230);
    default:                                   return IM_COL32(120, 190, 120, 230);
    }
}

const Vans::ProfileTrack* VansGraphics::VansProfilerWindow::FindTrack(const Vans::ProfileFrame& frame, uint32_t trackId) const
{
    for (uint32_t i = 0; i < frame.trackCount; ++i)
    {
        if (frame.tracks[i].trackId == trackId)
            return &frame.tracks[i];
    }
    return nullptr;
}
