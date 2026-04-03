#pragma once
#include "VansBaseWindowComponent.h"
#include <string>
#include <vector>
#include <filesystem>

namespace VansGraphics
{
    // -----------------------------------------------------------------------
    // Script browser window – lists all .py files found under ForestExporter
    // and allows selecting / viewing them.
    // -----------------------------------------------------------------------
    class VansScriptorWindow : public VansBaseWindowComponent
    {
    public:
        VansScriptorWindow();

        void ShowWindow(VansVKDevice& device) override;

        // The currently selected .py file (absolute path)
        static std::filesystem::path m_SelectedScript;

    private:
        void RefreshFileList();
        void LoadSelectedFile();
        void SaveCurrentFile();

        std::vector<std::filesystem::path> m_PythonFiles;
        bool m_NeedsRefresh = true;

        // ---- Editor buffer ----
        static constexpr size_t EDIT_BUF_SIZE = 1024 * 256; // 256 KB max
        std::string m_EditBuffer;          // in-memory editor content
        std::filesystem::path m_LoadedPath; // path that m_EditBuffer corresponds to
        bool m_Dirty = false;              // unsaved changes?

        // ---- Font scale for the text editor ----
        float m_EditorFontScale = 1.0f;
        static constexpr float FONT_SCALE_MIN = 0.5f;
        static constexpr float FONT_SCALE_MAX = 3.0f;
        static constexpr float FONT_SCALE_STEP = 0.1f;
    };
}
