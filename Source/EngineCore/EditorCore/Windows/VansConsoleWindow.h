#pragma once
#include "VansBaseWindowComponent.h"
#include "VansConsole.h"
#include <vector>

namespace VansGraphics
{
    // -----------------------------------------------------------------------
    // Console output window ?switchable between Engine / Script output.
    // -----------------------------------------------------------------------
    class VansConsoleWindow : public VansBaseWindowComponent
    {
    public:
        void ShowWindow(Vans::EditorAPI::IEngineEditorAPI&) override;

    private:
        void DrawConsoleContents();

        // 0 = All, 1 = Engine only, 2 = Script only
        int m_FilterMode = 0;

        // Auto-scroll
        bool m_AutoScroll = true;
        std::vector<std::size_t> m_FilteredEntries;
        std::vector<float> m_RowOffsets;
        std::uint64_t m_LayoutRevision = 0;
        int m_LayoutFilter = -1;
        const void* m_LayoutFont = nullptr;
        float m_LayoutFontSize = 0, m_LayoutSpacingX = 0, m_LayoutSpacingY = 0;
        float m_ContentWidth = 0;
    };
}
