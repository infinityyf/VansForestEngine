#pragma once
#include "VansBaseWindowComponent.h"

namespace VansGraphics
{
    class VansProfilerWindow : public VansBaseWindowComponent
    {
    public:
        void ShowWindow(VansVKDevice& device) override;

    private:
        // FPS history ring buffer for line graph
        static constexpr int FPS_HISTORY_SIZE = 256;
        float  m_FpsHistory[FPS_HISTORY_SIZE] = {};
        int    m_FpsHistoryOffset = 0;
        float  m_FpsMin = 0.0f;
        float  m_FpsMax = 144.0f;

        // Frame time history
        float  m_FrameTimeHistory[FPS_HISTORY_SIZE] = {};

        bool   m_PauseCapture = false;
        bool   m_ShowCpu = true;
        bool   m_ShowGpu = true;

        void DrawFpsGraph();
        void DrawFrameTimeline();
        void DrawRecordTree();
    };
}
