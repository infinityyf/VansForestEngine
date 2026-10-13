#pragma once
#include "../Public/EngineDTOs.h"
#include <filesystem>
#include <memory>
namespace VansGraphics {class VansVKDevice; class VansRenderSystem;}
namespace Vans::EditorAPI
{
class VansEditorPreviewRegistry;
// 一份 EngineAPI 拥有一份 Editor 会话；运行时包不构造它。
class VansRenderTexturePreviewSession final
{
public:
    VansRenderTexturePreviewSession(VansGraphics::VansVKDevice& device, VansGraphics::VansRenderSystem& renderSystem, std::shared_ptr<VansEditorPreviewRegistry> registry,
        std::filesystem::path shaderRoot,std::filesystem::path artifactRoot);
    ~VansRenderTexturePreviewSession();
    std::vector<RenderTextureSourceInfo> Sources(RenderTextureFilter filter) const;
    RenderTexturePreviewRegistrationResult Register(const std::string& key,const RenderTexturePreviewViewSpec& view);
    void Unregister(std::uint64_t registration);
    std::vector<RenderTexturePreview> Previews() const;
    void Clear();
private:
    struct State;
    std::unique_ptr<State> m_State;
};
}
