#pragma once
#include "VansBaseWindowComponent.h"
#include "VansConsole.h"

namespace VansGraphics
{
    // -----------------------------------------------------------------------
    // Console output window – switchable between Engine / Python output.
    // -----------------------------------------------------------------------
    class VansConsoleWindow : public VansBaseWindowComponent
    {
    public:
        void ShowWindow(VansVKDevice& device) override;

    private:
        // 0 = All, 1 = Engine only, 2 = Python only
        int m_FilterMode = 0;

        // Auto-scroll
        bool m_AutoScroll = true;

        // Input buffer for optional command entry
        char m_InputBuf[256] = {};
    };
}
