#pragma once
#include "VansBaseWindowComponent.h"
#include "../../Util/VansProfiler.h"
#include "imgui.h"

#include <cstdint>

namespace VansGraphics
{
    class VansProfilerWindow : public VansBaseWindowComponent
    {
    public:
        void ShowWindow(VansVKDevice& device) override;

    private:
        // ── 帧历史 ─────────────────────────────────────────────────────
        static constexpr int FPS_HISTORY_SIZE = 256;
        float  m_FpsHistory[FPS_HISTORY_SIZE] = {};
        float  m_FrameTimeHistory[FPS_HISTORY_SIZE] = {};
        int    m_FpsHistoryOffset = 0;

        // ── Timeline 状态 ──────────────────────────────────────────────
        double m_ViewStartUs = 0.0;
        double m_ViewEndUs = 16667.0;
        int    m_SelectedEventId = -1;
        uint32_t m_FilterMask = 0xffffffffu;
        char   m_SearchText[64] = {};

        bool   m_PauseCapture = false;
        bool   m_ShowCpu = true;
        bool   m_ShowGpu = true;

        void DrawTimeline();
        void DrawTimelineToolbar(const Vans::ProfileFrame& frame);
        void DrawTimeRuler(const Vans::ProfileFrame& frame, const ImVec2& origin, float width);
        void DrawTrackLane(const Vans::ProfileFrame& frame, const Vans::ProfileTrack& track, const ImVec2& origin, float width, float& cursorY);
        void DrawHierarchy();
        void DrawFrameGraph();
        void DrawRawEvents();

        bool IsCategoryVisible(Vans::ProfileCategory category) const;
        bool PassSearch(const Vans::ProfileEvent& event) const;
        uint32_t GetCategoryColor(Vans::ProfileCategory category, uint16_t flags) const;
        const Vans::ProfileTrack* FindTrack(const Vans::ProfileFrame& frame, uint32_t trackId) const;
    };
}
