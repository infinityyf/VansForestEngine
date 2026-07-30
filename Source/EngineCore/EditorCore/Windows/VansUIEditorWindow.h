#pragma once

#include "VansBaseWindowComponent.h"

#include "../../EngineAPILayer/Public/EngineDTOs.h"

#include <string>

namespace VansGraphics
{
    class VansUIEditorWindow : public VansBaseWindowComponent
    {
    public:
        VansUIEditorWindow();
        void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& api) override;

    private:
        void DrawUIEditorContents(Vans::EditorAPI::IEngineEditorAPI& api);
        void LoadPreview(Vans::EditorAPI::IEngineEditorAPI& api);
        void UnloadPreview(Vans::EditorAPI::IEngineEditorAPI& api);
        void DrawMetaPanel(Vans::EditorAPI::IEngineEditorAPI& api);
        void DrawPreviewViewport(Vans::EditorAPI::IEngineEditorAPI& api);

        char m_XamlPathBuf[512] = {};
        Vans::EditorAPI::UIDocumentId m_PreviewDocumentId = 0;
        Vans::EditorAPI::UIPreviewId m_PreviewId = 0;
        std::string m_LastStatus;
        bool m_ReloadKeyWasDown = false;
    };
}
