#pragma once
#include "VansBaseWindowComponent.h"
#include <string>
#include <vector>
namespace Vans::EditorAPI { class IRenderEditorAPI; }
namespace VansGraphics
{
class VansRenderTexturePreviewWindow final : public VansBaseWindowComponent
{
public:
    void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;
private:
    char m_TextureSearch[256]{};
    std::string m_SelectedTexture;
    Vans::EditorAPI::RenderTexturePreviewViewSpec m_TextureView;
    std::string m_TexturePreviewError;
    std::vector<std::uint64_t> m_OwnedRegistrations;
    bool m_OneToOne = false;
    void DrawBrowser(Vans::EditorAPI::IRenderEditorAPI& api);
    void ReleaseRegistrations(Vans::EditorAPI::IRenderEditorAPI& api);
};
}
