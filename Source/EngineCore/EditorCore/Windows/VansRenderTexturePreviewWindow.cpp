#include "VansRenderTexturePreviewWindow.h"
#include "../VansEditorWindow.h"
#include "../../EngineAPILayer/Public/IRenderEditorAPI.h"
#include "imgui.h"
#include <algorithm>

void VansGraphics::VansRenderTexturePreviewWindow::DrawBrowser(Vans::EditorAPI::IRenderEditorAPI& api)
{
    ImGui::InputText("Search resource / owner",m_TextureSearch,sizeof(m_TextureSearch));
    Vans::EditorAPI::RenderTextureFilter filter;filter.name=m_TextureSearch;
    const auto sources=api.QueryAvailableRenderTextures(filter);
    if(ImGui::BeginChild("RenderTextureSources",ImVec2(0,180),true))
    {
        for(const auto& source:sources)
        {
            ImGui::PushID(source.resourceKey.c_str());
            if(ImGui::Selectable(source.name.c_str(),m_SelectedTexture==source.resourceKey))
            {
                m_SelectedTexture=source.resourceKey;m_TextureView={};
                if(source.format.find("D32")!=std::string::npos)m_TextureView.channel=Vans::EditorAPI::RenderTexturePreviewChannel::R;
            }
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\nOwner: %s\n%s %u x %u, %u slots\n%s",source.resourceKey.c_str(),source.owner.c_str(),
                source.format.c_str(),source.width,source.height,source.frameSlots,source.status.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    if(!m_SelectedTexture.empty())
    {
        ImGui::TextWrapped("%s",m_SelectedTexture.c_str());
        int layer=static_cast<int>(m_TextureView.layer),mip=static_cast<int>(m_TextureView.mipLevel);
        if(ImGui::InputInt("Layer",&layer))m_TextureView.layer=static_cast<unsigned>(std::max(layer,0));
        if(ImGui::InputInt("Mip",&mip))m_TextureView.mipLevel=static_cast<unsigned>(std::max(mip,0));
        const char* channels[]={"RGBA","R","G","B","A"};int channel=static_cast<int>(m_TextureView.channel);
        if(ImGui::Combo("Channel",&channel,channels,5))m_TextureView.channel=static_cast<Vans::EditorAPI::RenderTexturePreviewChannel>(channel);
        ImGui::InputFloat("Minimum",&m_TextureView.minimum);ImGui::InputFloat("Maximum",&m_TextureView.maximum);
        ImGui::SliderFloat("Exposure",&m_TextureView.exposure,-16,16);ImGui::SliderFloat("Gamma",&m_TextureView.gamma,0.1f,4);
        ImGui::Checkbox("Opaque visualization",&m_TextureView.opaque);ImGui::Checkbox("Previous produced frame",&m_TextureView.previousFrame);
        if(ImGui::Button("Pin preview"))
        {
            const auto result=api.RegisterRenderTexturePreview(m_SelectedTexture,m_TextureView);
            m_TexturePreviewError=result.error;
            if (result.accepted) m_OwnedRegistrations.push_back(result.registration);
        }
        if(!m_TexturePreviewError.empty())ImGui::TextWrapped("%s",m_TexturePreviewError.c_str());
    }
    ImGui::Checkbox("Display at 1:1 scale", &m_OneToOne);
    for(const auto& preview:api.QueryRegisteredRenderTexturePreviews())
    {
        ImGui::PushID(std::to_string(preview.registration).c_str());
        ImGui::Separator();ImGui::Text("%s",preview.name.c_str());ImGui::SameLine();
        if(ImGui::SmallButton("Unpin")){api.UnregisterRenderTexturePreview(preview.registration);m_OwnedRegistrations.erase(std::remove(m_OwnedRegistrations.begin(),m_OwnedRegistrations.end(),preview.registration),m_OwnedRegistrations.end());ImGui::PopID();continue;}
        ImGui::TextWrapped("%s",preview.resourceKey.c_str());
        ImGui::Text("Generation %llu | frame %llu | %s",static_cast<unsigned long long>(preview.generation),static_cast<unsigned long long>(preview.frameSerial),preview.status.c_str());
        if(preview.texture&&preview.width&&preview.height)
        {
            const auto width=m_OneToOne ? static_cast<float>(preview.width) : std::max(1.0f,std::min(512.0f,ImGui::GetContentRegionAvail().x));
            // 新资源代际使用独立的图像子窗口状态，重建尺寸与滚动区域。
            ImGui::PushID(std::to_string(preview.generation).c_str());
            if (ImGui::BeginChild("PreviewImage",ImVec2(0,std::min(512.0f,width*preview.height/preview.width)+20),false,ImGuiWindowFlags_HorizontalScrollbar))
                ImGui::Image(preview.texture,ImVec2(width,width*preview.height/preview.width));
            ImGui::EndChild();
            ImGui::PopID();
        }
        ImGui::PopID();
    }
}

void VansGraphics::VansRenderTexturePreviewWindow::ReleaseRegistrations(Vans::EditorAPI::IRenderEditorAPI& api)
{
    for (const auto id : m_OwnedRegistrations) api.UnregisterRenderTexturePreview(id);
    m_OwnedRegistrations.clear();
}
void VansGraphics::VansRenderTexturePreviewWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
    Vans::EditorAPI::IRenderEditorAPI& api=editorAPI;
    if (!VansEditorWindow::IsWindowOpen(VansEditorWindowId::RenderTexturePreview))
    {
        ReleaseRegistrations(api);
        return;
    }
    if (ImGui::Begin("RenderTexture Preview",VansEditorWindow::WindowOpenState(VansEditorWindowId::RenderTexturePreview))) DrawBrowser(api);
    ImGui::End();
}
