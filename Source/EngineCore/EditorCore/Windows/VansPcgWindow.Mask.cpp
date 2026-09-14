#include "VansPcgWindow.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace VansGraphics
{
void VansPcgWindow::ShowMaskCanvas(Vans::EditorAPI::IEngineEditorAPI& api,const Vans::EditorAPI::PcgBrushSnapshot& brush)
{
    using namespace Vans::EditorAPI;
    const auto preview=api.GetPcgMaskPreview(brush.target.maskGuid);
    if (!preview.available) {ImGui::TextWrapped("%s",preview.error.c_str());return;}
    if (!(m_CanvasTarget==brush.target)) {
        m_CanvasTarget=brush.target;m_CanvasDragging=false;m_CanvasZoom=1;m_CanvasPan={};
        m_MaskDataDraft={};m_MaskDataDraft.target=brush.target;
        m_MaskDataDraft.width=preview.width;m_MaskDataDraft.height=preview.height;
        m_MaskDataDraft.boundsMin=preview.boundsMin;m_MaskDataDraft.boundsMax=preview.boundsMax;
    }
    ImGui::TextDisabled("Mask canvas: LMB paint / Shift erase / MMB pan / wheel zoom");
    if (ImGui::SmallButton("Fit canvas")) {m_CanvasZoom=1;m_CanvasPan={};}
    ImGui::SameLine();ImGui::TextDisabled("%u x %u | +X right, +Z down",preview.width,preview.height);
    const ImVec2 origin=ImGui::GetCursorScreenPos();
    const float w=std::max(100.0f,std::min(420.0f,ImGui::GetContentRegionAvail().x));
    const float h=std::clamp(w*preview.height/preview.width,100.0f,320.0f);
    ImGui::InvisibleButton("MaskCanvas",ImVec2(w,h),ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered=ImGui::IsItemHovered();const auto& io=ImGui::GetIO();
    if (hovered && io.MouseWheel!=0 && !m_CanvasDragging) {
        const float previous=m_CanvasZoom;m_CanvasZoom=std::clamp(previous*std::pow(1.2f,io.MouseWheel),1.0f,16.0f);
        const float ratio=m_CanvasZoom/previous;
        m_CanvasPan[0]=(io.MousePos.x-origin.x)*(1-ratio)+m_CanvasPan[0]*ratio;
        m_CanvasPan[1]=(io.MousePos.y-origin.y)*(1-ratio)+m_CanvasPan[1]*ratio;
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle) && !m_CanvasDragging) {
        m_CanvasPan[0]+=io.MouseDelta.x;m_CanvasPan[1]+=io.MouseDelta.y;
    }
    m_CanvasPan[0]=std::clamp(m_CanvasPan[0],w*(1-m_CanvasZoom),0.0f);
    m_CanvasPan[1]=std::clamp(m_CanvasPan[1],h*(1-m_CanvasZoom),0.0f);
    const ImVec2 image(origin.x+m_CanvasPan[0],origin.y+m_CanvasPan[1]);
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(origin,ImVec2(origin.x+w,origin.y+h),true);
    for (uint32_t y=0;y<preview.previewHeight;++y) for (uint32_t x=0;x<preview.previewWidth;++x) {
        const auto value=preview.pixels[static_cast<size_t>(y)*preview.previewWidth+x];
        draw->AddRectFilled(ImVec2(image.x+w*m_CanvasZoom*x/preview.previewWidth,image.y+h*m_CanvasZoom*y/preview.previewHeight),
            ImVec2(image.x+w*m_CanvasZoom*(x+1)/preview.previewWidth,image.y+h*m_CanvasZoom*(y+1)/preview.previewHeight),IM_COL32(value,value,value,255));
    }
    if (hovered && brush.canvasEditable) {
        const float rx=brush.settings.radius*w*m_CanvasZoom/(preview.boundsMax[0]-preview.boundsMin[0]);
        const float ry=brush.settings.radius*h*m_CanvasZoom/(preview.boundsMax[1]-preview.boundsMin[1]);
        for (int i=0;i<=64;++i) {const float a=i*6.28318530718f/64;
            draw->PathLineTo(ImVec2(io.MousePos.x+rx*std::cos(a),io.MousePos.y+ry*std::sin(a)));}
        draw->PathStroke(IM_COL32(255,150,50,255),0,1.5f);
    }
    draw->PopClipRect();
    if (brush.canvasEditable && (hovered || m_CanvasDragging)) {
        PcgBrushInput input;input.target=brush.target;input.space=PcgBrushSpace::MaskCanvas;input.erase=io.KeyShift;
        input.maskUV={(io.MousePos.x-image.x)/(w*m_CanvasZoom),(io.MousePos.y-image.y)/(h*m_CanvasZoom)};
        bool send=false;
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !brush.strokeActive) {input.phase=PcgBrushPhase::Begin;send=true;}
        else if (m_CanvasDragging) {input.phase=hovered?PcgBrushPhase::Update:PcgBrushPhase::Break;send=true;}
        if (send) {const auto result=api.ApplyPcgBrushInput(input);m_CanvasDragging=result.strokeActive;
            if (!result.success) m_Message=result.message;}
    }
    if (ImGui::CollapsingHeader("Mask data and mapping")) {
        ImGui::BeginDisabled(!brush.canvasEditable || brush.strokeActive);
        const auto run=[&](PcgMaskDataAction action) {
            m_MaskDataDraft.action=action;m_Message=api.EditPcgMaskData(m_MaskDataDraft).message;
        };
        if (ImGui::Button("Clear")) {m_MaskDataDraft.fill=0;run(PcgMaskDataAction::Fill);}
        ImGui::SameLine();if (ImGui::Button("Fill white")) {m_MaskDataDraft.fill=1;run(PcgMaskDataAction::Fill);}
        ImGui::DragFloat("Fill value",&m_MaskDataDraft.fill,.01f,0,1);
        if (ImGui::Button("Fill value across Mask")) run(PcgMaskDataAction::Fill);
        int width=static_cast<int>(m_MaskDataDraft.width),height=static_cast<int>(m_MaskDataDraft.height);
        ImGui::InputInt("Width",&width);ImGui::InputInt("Height",&height);
        m_MaskDataDraft.width=std::max(0,width);m_MaskDataDraft.height=std::max(0,height);
        ImGui::DragFloat2("World minimum XZ",m_MaskDataDraft.boundsMin.data(),.1f);
        ImGui::DragFloat2("World maximum XZ",m_MaskDataDraft.boundsMax.data(),.1f);
        ImGui::Checkbox("Preserve world coverage",&m_MaskDataDraft.preserveWorld);
        ImGui::TextWrapped("Unchecked: stretch the image into the new bounds. Resampling is undoable.");
        if (ImGui::Button("Apply mapping / resolution")) run(PcgMaskDataAction::Remap);
        std::vector<char> path(std::max<size_t>(2048,m_MaskDataDraft.path.size()+1),0);
        std::copy(m_MaskDataDraft.path.begin(),m_MaskDataDraft.path.end(),path.begin());
        if (ImGui::InputText("Image path",path.data(),path.size())) m_MaskDataDraft.path=path.data();
        int channel=static_cast<int>(m_MaskDataDraft.channel);
        ImGui::Combo("Import channel",&channel,"R / gray\0G\0B\0A\0");m_MaskDataDraft.channel=channel;
        ImGui::TextWrapped("Import adopts the image resolution, keeps world bounds and copies the selected channel into this Mask.");
        if (ImGui::Button("Import into working Mask")) run(PcgMaskDataAction::Import);
        ImGui::SameLine();if (ImGui::Button("Export 16-bit PNG")) run(PcgMaskDataAction::Export);
        ImGui::EndDisabled();
    }
}
}
