#include "VansPcgWindow.h"
#include "../../EngineAPILayer/Public/IPcgEditorAPI.h"
#include <imgui.h>
#include <algorithm>
#include <vector>

namespace VansGraphics
{
void VansPcgWindow::ShowInstances(Vans::EditorAPI::IPcgEditorAPI& api,const Vans::EditorAPI::PcgBrushSnapshot& brush)
{
    using namespace Vans::EditorAPI;
    if (!ImGui::CollapsingHeader("Individual instances")) return;
    if (m_InstanceTarget.recipeGuid!=brush.target.recipeGuid || m_InstanceTarget.regionId!=brush.target.regionId ||
        m_InstanceTarget.layerId!=brush.target.layerId) {
        m_InstanceTarget=brush.target;m_Instances={};m_InstanceDraft={};
    }
    const auto refresh=[&](uint64_t offset) {m_Instances=api.GetPcgInstances(brush.target,offset);};
    if (!m_Instances.success || ImGui::Button("Refresh instances")) refresh(m_Instances.offset);
    ImGui::SameLine();ImGui::Text("%llu entries",static_cast<unsigned long long>(m_Instances.total));
    if (!m_Instances.message.empty()) ImGui::TextWrapped("%s",m_Instances.message.c_str());
    ImGui::BeginDisabled(!m_Instances.offset);
    if (ImGui::SmallButton("Previous 128")) refresh(m_Instances.offset>=128?m_Instances.offset-128:0);
    ImGui::EndDisabled();ImGui::SameLine();
    ImGui::BeginDisabled(m_Instances.offset+128>=m_Instances.total);
    if (ImGui::SmallButton("Next 128")) refresh(m_Instances.offset+128);
    ImGui::EndDisabled();
    if (ImGui::BeginChild("InstanceList",ImVec2(0,180),true)) {
        for (const auto& item:m_Instances.items) {
            const char* kind=item.origin==PcgInstanceOrigin::Fixed?"Fixed":item.origin==PcgInstanceOrigin::Added?"Added":
                item.removed?"Removed":item.locked?"Locked":item.orphan?"Orphan override":"Generated";
            const std::string label=std::string(kind)+"  "+item.id+"  "+item.variant;
            if (ImGui::Selectable(label.c_str(),m_InstanceDraft.id==item.id)) m_InstanceDraft=item;
        }
    }
    ImGui::EndChild();
    ImGui::TextWrapped("Fixed and added instances keep their authored transforms. Lock retains a generated instance when its Mask or density changes. Reset override restores procedural control.");
    ImGui::BeginDisabled(!brush.canvasEditable || brush.strokeActive);
    if (ImGui::Button("New instance")) m_InstanceDraft={};
    if (!m_InstanceDraft.id.empty()) ImGui::TextDisabled("Selected: %s",m_InstanceDraft.id.c_str());
    const auto configuration=api.GetPcgLayerConfiguration(brush.target);
    const auto plant=api.GetPcgPlantConfiguration(configuration.plantGuid);
    if (ImGui::BeginCombo("Instance variant",m_InstanceDraft.variant.empty()?"Select variant":m_InstanceDraft.variant.c_str())) {
        for (const auto& variant:plant.variants)
            if (ImGui::Selectable((variant.name+"##"+variant.id).c_str(),m_InstanceDraft.variant==variant.id)) m_InstanceDraft.variant=variant.id;
        ImGui::EndCombo();
    }
    ImGui::DragFloat3("Instance position",m_InstanceDraft.position.data(),.05f);
    ImGui::DragFloat4("Instance rotation XYZW",m_InstanceDraft.rotation.data(),.01f);
    ImGui::DragFloat3("Instance scale",m_InstanceDraft.scale.data(),.01f,.0001f,10000);
    const auto apply=[&](PcgInstanceAction action) {
        PcgInstanceEditRequest request;request.target=brush.target;request.documentState=m_Instances.documentState;
        request.action=action;request.instance=m_InstanceDraft;
        const auto result=api.EditPcgInstance(request);m_Message=result.message;
        if (result.success) {refresh(m_Instances.offset);m_InstanceDraft={};}
    };
    if (m_InstanceDraft.id.empty()) {
        if (ImGui::Button("Add instance")) apply(PcgInstanceAction::Add);
        ImGui::SameLine();ImGui::BeginDisabled(configuration.source!=PcgSourceMode::Fixed);
        if (ImGui::Button("Add fixed instance")) apply(PcgInstanceAction::AddFixed);
        ImGui::EndDisabled();
    } else {
        if (ImGui::Button("Apply transform")) apply(PcgInstanceAction::Transform);
        ImGui::SameLine();if (ImGui::Button("Delete instance")) apply(PcgInstanceAction::Remove);
        const bool authored=m_InstanceDraft.origin==PcgInstanceOrigin::Fixed || m_InstanceDraft.origin==PcgInstanceOrigin::Added;
        ImGui::BeginDisabled(authored);
        if (ImGui::Button("Lock instance")) apply(PcgInstanceAction::Lock);
        ImGui::SameLine();if (ImGui::Button("Reset override / unlock")) apply(PcgInstanceAction::ResetOverride);
        ImGui::EndDisabled();
    }
    if (ImGui::Button("Undo instance / recipe")) {m_Message=api.EditPcgConfiguration(brush.target.recipeGuid,PcgConfigurationAction::Undo).message;refresh(m_Instances.offset);}
    ImGui::SameLine();if (ImGui::Button("Redo instance / recipe")) {m_Message=api.EditPcgConfiguration(brush.target.recipeGuid,PcgConfigurationAction::Redo).message;refresh(m_Instances.offset);}
    if (ImGui::Button("Save recipe")) m_Message=api.EditPcgConfiguration(brush.target.recipeGuid,PcgConfigurationAction::Save).message;
    ImGui::EndDisabled();
}
}
