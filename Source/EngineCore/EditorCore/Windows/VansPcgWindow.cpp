#include "VansPcgWindow.h"
#include "../VansEditorWindow.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
namespace VansGraphics
{
void VansPcgWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& api)
{
    using namespace Vans::EditorAPI;
    const auto report=[&](const PcgEditorOperationResult& result) {m_Message=result.message;};
    if (m_SplinePropertyDrag && (!VansEditorWindow::m_PcgWindowOpen || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive())))
    {
        PcgSplineEditRequest request;request.spline=m_SplineDraft;request.documentState=m_SplineDraftState;
        request.phase=ImGui::IsKeyPressed(ImGuiKey_Escape)?PcgSplineEditPhase::Cancel:PcgSplineEditPhase::Commit;
        report(api.ApplyPcgSplineEdit(request));m_SplinePropertyDrag=false;
    }
    if (m_CanvasDragging && (!VansEditorWindow::m_PcgWindowOpen || !ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Escape))) {
        PcgBrushInput input;input.target=m_CanvasTarget;input.space=PcgBrushSpace::MaskCanvas;
        input.phase=ImGui::IsKeyPressed(ImGuiKey_Escape)?PcgBrushPhase::Cancel:PcgBrushPhase::End;
        report(api.ApplyPcgBrushInput(input));m_CanvasDragging=false;
    }
    if (!VansEditorWindow::m_PcgWindowOpen)
    {
        if (api.GetPcgBrushSnapshot().enabled) report(api.SelectPcgBrushTarget({},false));
        if (api.GetPcgSplineSnapshot().toolEnabled) report(api.SelectPcgSpline({},{},false));
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(960,720),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(640,420),ImVec2(FLT_MAX,FLT_MAX));
    if (!ImGui::Begin("PCG",&VansEditorWindow::m_PcgWindowOpen)) {ImGui::End();return;}
    const auto snapshot=api.GetPcgEditorSnapshot();
    for (const auto& error : snapshot.errors) ImGui::TextWrapped("%s",error.c_str());
    if (!m_Message.empty()) ImGui::TextWrapped("%s",m_Message.c_str());
    if (ImGui::BeginTabBar("PlantCategories"))
    {
        for (int category=0;category<2;++category)
        {
            if (!ImGui::BeginTabItem(category==0?"Grass":"Trees")) continue;
            if (m_Category!=category) {report(api.SelectPcgBrushTarget({},false));api.SelectPcgSpline({},{},false);m_Category=category;}
            ShowLayerActions(api,snapshot,category);
            auto brush=api.GetPcgBrushSnapshot();
            if (ImGui::BeginTable("PlantLayers",2,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV))
            {
                ImGui::TableSetupColumn("Layers",ImGuiTableColumnFlags_WidthFixed,200);
                ImGui::TableSetupColumn("Configuration",ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("Distribution layers");
                bool any=false;
                for (const auto& layer : snapshot.layers)
                {
                    if (layer.tree!=(category==1)) continue;
                    any=true;
                    PcgBrushTarget target{layer.recipeGuid,layer.regionId,layer.layerId,layer.densityMaskGuid};
                    const bool selected=brush.target.recipeGuid==layer.recipeGuid &&
                        brush.target.regionId==layer.regionId && brush.target.layerId==layer.layerId;
                    ImGui::PushID((layer.recipeGuid+"/"+layer.regionId+"/"+layer.layerId).c_str());
                    if (ImGui::Selectable(layer.name.c_str(),selected))
                    {report(api.SelectPcgBrushTarget(target,false));brush=api.GetPcgBrushSnapshot();}
                    ImGui::TextDisabled("%s / %s",layer.recipeName.c_str(),layer.regionName.c_str());
                    ImGui::PopID();
                }
                if (!any) ImGui::TextWrapped("No distribution layers have been created.");
                ImGui::TableNextColumn();
                const auto selected=std::find_if(snapshot.layers.begin(),snapshot.layers.end(),[&](const auto& layer) {
                    return layer.tree==(category==1) && layer.recipeGuid==brush.target.recipeGuid &&
                        layer.regionId==brush.target.regionId && layer.layerId==brush.target.layerId;
                });
                if (selected!=snapshot.layers.end())
                {
                    const auto& layer=*selected;
                    ImGui::TextUnformatted(layer.name.c_str());
                    ImGui::Text("Plant: %s (%zu variants)",layer.plantName.c_str(),layer.variantCount);
                    ImGui::Text("Source: %s  Density: %.3f / m2",layer.source.c_str(),layer.density);
                    if (layer.tree && layer.source=="count") ImGui::Text("Target trees: %u",layer.treeTargetCount);
                    ImGui::Text("Fixed: %zu  Added: %zu",layer.fixedCount,layer.addedCount);
                    ImGui::Text("Bounds: %.2f, %.2f to %.2f, %.2f",layer.boundsMin[0],layer.boundsMin[1],layer.boundsMax[0],layer.boundsMax[1]);
                    ShowConfiguration(api,layer);
                    ImGui::Separator();
                    const auto chooseMask=[&](const char* label,const std::string& guid) {
                        if (guid.empty()) return;
                        if (ImGui::RadioButton(label,brush.target.maskGuid==guid))
                        {
                            report(api.SelectPcgBrushTarget({layer.recipeGuid,layer.regionId,layer.layerId,guid},false));
                            brush=api.GetPcgBrushSnapshot();
                        }
                    };
                    chooseMask("Density Mask",layer.densityMaskGuid);
                    if (!layer.exclusionMaskGuid.empty()) {ImGui::SameLine();chooseMask("Exclusion Mask",layer.exclusionMaskGuid);}
                    else {
                        ImGui::SameLine();ImGui::BeginDisabled(!brush.canvasEditable || brush.strokeActive);
                        if (ImGui::Button("Create exclusion Mask")) report(api.CreatePcgExclusionMask(brush.target));
                        ImGui::EndDisabled();
                    }
                    bool enabled=brush.enabled;
                    ImGui::BeginDisabled(!brush.editable || layer.locked);
                    if (ImGui::Checkbox("Paint in Scene",&enabled))
                    {report(api.SelectPcgBrushTarget(brush.target,enabled));brush=api.GetPcgBrushSnapshot();}
                    ImGui::EndDisabled();
                    ImGui::TextDisabled("LMB: paint   Shift: erase   Esc: cancel stroke");
                    if (layer.source=="count") ImGui::TextWrapped("Count mode refills the target across the region. Density mode controls local coverage.");
                    if (layer.source=="fixed") ImGui::TextWrapped(layer.tree
                        ? "Mask filters existing fixed trees at their roots. Erase hides them; paint restores them in place. Density does not add new trees."
                        : "Fixed instances retain their authored transforms. This Mask affects generated distribution modes.");
                    ImGui::BeginDisabled(brush.strokeActive || !brush.canvasEditable);
                    if (!(m_BrushTarget==brush.target) || (!ImGui::IsAnyItemActive() && !m_BrushDraftEditing))
                    {m_BrushTarget=brush.target;m_BrushDraft=brush.settings;}
                    int operation=static_cast<int>(m_BrushDraft.operation);
                    bool commit=ImGui::Combo("Operation",&operation,"Add\0Subtract\0Set value\0Smooth\0Erase\0");
                    m_BrushDraft.operation=static_cast<PcgBrushOperation>(operation);
                    const auto slider=[&](const char* label,float& value,float step,float minimum,float maximum) {
                        ImGui::DragFloat(label,&value,step,minimum,maximum);
                        if (ImGui::IsItemActive()) m_BrushDraftEditing=true;
                        commit=ImGui::IsItemDeactivatedAfterEdit()||commit;
                    };
                    slider("Radius (m)",m_BrushDraft.radius,.05f,.01f,10000);
                    slider("Strength",m_BrushDraft.strength,.01f,0,1);
                    slider("Hardness",m_BrushDraft.hardness,.01f,0,1);
                    slider("Target value",m_BrushDraft.targetValue,.01f,0,1);
                    slider("Dab spacing / radius",m_BrushDraft.spacingFraction,.01f,.01f,1);
                    if (commit) {report(api.ConfigurePcgBrush(m_BrushDraft));m_BrushDraftEditing=false;}
                    ImGui::BeginDisabled(!brush.canUndo);
                    if (ImGui::Button("Undo")) report(api.EditPcgMaskDocument(PcgMaskDocumentAction::Undo));
                    ImGui::EndDisabled();ImGui::SameLine();
                    ImGui::BeginDisabled(!brush.canRedo);
                    if (ImGui::Button("Redo")) report(api.EditPcgMaskDocument(PcgMaskDocumentAction::Redo));
                    ImGui::EndDisabled();ImGui::SameLine();
                    if (ImGui::Button("Save Mask")) report(api.EditPcgMaskDocument(PcgMaskDocumentAction::Save));
                    ImGui::EndDisabled();
                    if (brush.dirty) {ImGui::SameLine();ImGui::TextUnformatted("*");}
                    if (!brush.message.empty()) ImGui::TextWrapped("%s",brush.message.c_str());
                    ShowMaskCanvas(api,api.GetPcgBrushSnapshot());
                    ShowInstances(api,api.GetPcgBrushSnapshot());
                }
                else ImGui::TextDisabled("Select a distribution layer.");
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        for (int category=2;category<4;++category)
        {
            if (!ImGui::BeginTabItem(category==2?"Roads":"Rivers")) continue;
            if (m_Category!=category) {report(api.SelectPcgBrushTarget({},false));m_Category=category;}
            ShowSplines(api,category==2?PcgSplineKind::Road:PcgSplineKind::River);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
}
