#include "VansPcgWindow.h"
#include "../../EngineAPILayer/Public/IAssetEditorAPI.h"
#include "../../EngineAPILayer/Public/IPcgEditorAPI.h"
#include "../../EngineAPILayer/Public/IRenderEditorAPI.h"
#include "imgui.h"
#include <algorithm>
#include <cstring>

namespace VansGraphics
{
void VansPcgWindow::ShowSplines(Vans::EditorAPI::IEngineEditorAPI& api,Vans::EditorAPI::PcgSplineKind kind)
{
	using namespace Vans::EditorAPI;
	IAssetEditorAPI& assetAPI=api;
	IPcgEditorAPI& pcgAPI=api;
	IRenderEditorAPI& renderAPI=api;
	auto snapshot=pcgAPI.GetPcgSplineSnapshot();
    const auto report=[&](const PcgEditorOperationResult& result){m_Message=result.message;};
    const auto command=[&](PcgSplineCommand action) {
        PcgSplineCommandRequest request;request.command=action;request.splineId=snapshot.selectedSpline;
        request.pointId=snapshot.selectedPoint;request.materialGuid=m_RoadMaterial;request.position=m_NewSplinePosition;
		report(pcgAPI.ExecutePcgSplineCommand(request));
    };
    if (ImGui::BeginCombo("Spline asset",snapshot.assetGuid.empty()?"Choose asset":snapshot.assetGuid.c_str()))
    {
        for (const auto& asset:assetAPI.QueryAssets({AssetType::PcgSpline}))
			if (ImGui::Selectable((asset.name+"##"+asset.guid).c_str(),asset.guid==snapshot.assetGuid)) report(pcgAPI.BindPcgSplineAsset(asset.guid));
        ImGui::EndCombo();
    }
	if (ImGui::Button("Create scene spline asset")) report(pcgAPI.CreatePcgSplineAsset("Scene splines"));
    if (!snapshot.available) {ImGui::TextWrapped("Bind a spline asset to the scene terrain to start editing.");return;}
    ImGui::SameLine();ImGui::BeginDisabled(!snapshot.editable);
    if (ImGui::Button("Save splines")) command(PcgSplineCommand::Save);
    ImGui::SameLine();if (ImGui::Button("Bake fields")) command(PcgSplineCommand::Bake);
    ImGui::EndDisabled();
    ImGui::Text("%s | %zu field tiles | rebuilt %zu",snapshot.building?"Updating":"Ready",
        snapshot.activeTiles,snapshot.rebuiltTiles);
    if (snapshot.dirty) {ImGui::SameLine();ImGui::TextUnformatted("*");}
    if (!snapshot.message.empty()) ImGui::TextWrapped("%s",snapshot.message.c_str());
    for (const auto& warning:snapshot.warnings) ImGui::TextWrapped("%s",warning.c_str());
    if (ImGui::CollapsingHeader("Field quality"))
    {
        if (m_SplineFieldSettingsState!=snapshot.documentState)
        {
            m_SplineFieldSettings={snapshot.fieldTexelSize,snapshot.sampleSpacing,snapshot.curveTolerance,snapshot.heightConflictThreshold};
            m_SplineFieldSettingsState=snapshot.documentState;
        }
        ImGui::InputFloat("Field texel (m)",&m_SplineFieldSettings[0]);
        ImGui::InputFloat("Curve spacing (m)",&m_SplineFieldSettings[1]);
        ImGui::InputFloat("Curve error (m)",&m_SplineFieldSettings[2]);
        ImGui::InputFloat("Water height warning (m)",&m_SplineFieldSettings[3]);
        ImGui::BeginDisabled(!snapshot.editable);
        if (ImGui::Button("Apply field quality"))
		{PcgSplineCommandRequest request;request.command=PcgSplineCommand::ConfigureFields;request.fieldSettings=m_SplineFieldSettings;report(pcgAPI.ExecutePcgSplineCommand(request));}
        ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("Runtime control textures"))
    {
        RenderTextureFilter filter;filter.category="pcg_splines";
        for(const auto& preview:renderAPI.QueryRenderTexturePreviews(filter)) if(preview.texture&&preview.width&&preview.height)
        {
            ImGui::TextUnformatted(preview.name.c_str());
            const float width=std::min(ImGui::GetContentRegionAvail().x,600.f);
            ImGui::Image(preview.texture,{width,width*preview.height/preview.width});
        }
    }
    ImGui::BeginDisabled(!snapshot.editable);
    ImGui::BeginDisabled(!snapshot.canUndo);if (ImGui::Button("Undo spline edit")) command(PcgSplineCommand::Undo);ImGui::EndDisabled();
    ImGui::SameLine();ImGui::BeginDisabled(!snapshot.canRedo);if (ImGui::Button("Redo spline edit")) command(PcgSplineCommand::Redo);ImGui::EndDisabled();
    bool tool=snapshot.toolEnabled;
	if (ImGui::Checkbox("Edit splines in Scene",&tool)) report(pcgAPI.SelectPcgSpline(snapshot.selectedSpline,snapshot.selectedPoint,tool));
    ImGui::TextDisabled("Select points or handles in Scene. Drag the gizmo to move. Esc cancels the drag.");
    if (kind==PcgSplineKind::River) ImGui::TextDisabled("Extend an endpoint: Ctrl+click terrain. New points keep the endpoint water level.");
    else ImGui::TextDisabled("Extend an endpoint: Ctrl+click terrain. New road points take the ground height.");
    if (kind==PcgSplineKind::Road)
    {
        const auto materials=assetAPI.QueryAssets({AssetType::Material});
        if (m_RoadMaterial.empty() && !materials.empty()) m_RoadMaterial=materials.front().guid;
        if (ImGui::BeginCombo("New road material",m_RoadMaterial.c_str()))
        {
            for (const auto& asset:materials)
                if (ImGui::Selectable((asset.name+"##"+asset.guid).c_str(),asset.guid==m_RoadMaterial)) m_RoadMaterial=asset.guid;
            ImGui::EndCombo();
        }
    }
    ImGui::InputFloat3("New spline start (world)",m_NewSplinePosition.data());
    if (ImGui::Button(kind==PcgSplineKind::Road?"Add road":"Add river"))
        command(kind==PcgSplineKind::Road?PcgSplineCommand::AddRoad:PcgSplineCommand::AddRiver);
    if (ImGui::BeginTable("SplineEditor",2,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV))
    {
        ImGui::TableSetupColumn("Splines",ImGuiTableColumnFlags_WidthFixed,190);
        ImGui::TableSetupColumn("Properties",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();
        for (const auto& spline:snapshot.splines)
        {
            if (spline.kind!=kind) continue;
            if (ImGui::Selectable((spline.name+"##"+spline.id).c_str(),spline.id==snapshot.selectedSpline))
				report(pcgAPI.SelectPcgSpline(spline.id,{},snapshot.toolEnabled));
        }
        ImGui::TableNextColumn();
        const auto selected=std::find_if(snapshot.splines.begin(),snapshot.splines.end(),[&](const auto& item){return item.id==snapshot.selectedSpline&&item.kind==kind;});
        if (selected!=snapshot.splines.end())
        {
            if (ImGui::Button("Frame spline in Scene")) command(PcgSplineCommand::Focus);
            if (!m_SplinePropertyDrag) {m_SplineDraft=*selected;m_SplineDraftState=snapshot.documentState;}
            const auto submit=[&](PcgSplineEditPhase phase) {
                PcgSplineEditRequest request;request.phase=phase;request.documentState=m_SplineDraftState;request.spline=m_SplineDraft;
				const auto result=pcgAPI.ApplyPcgSplineEdit(request);report(result);return result.success;
            };
            const auto live=[&](bool changed) {
                if (changed)
                {
                    if (!m_SplinePropertyDrag) m_SplinePropertyDrag=submit(PcgSplineEditPhase::Begin);
                    if (m_SplinePropertyDrag) submit(PcgSplineEditPhase::Update);
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && m_SplinePropertyDrag)
                {submit(PcgSplineEditPhase::Commit);m_SplinePropertyDrag=false;}
            };
            if (m_SplinePropertyDrag && (ImGui::IsKeyPressed(ImGuiKey_Escape)||ImGui::GetIO().AppFocusLost))
            {submit(PcgSplineEditPhase::Cancel);m_SplinePropertyDrag=false;}
            if (ImGui::Checkbox("Locked",&m_SplineDraft.locked)) submit(PcgSplineEditPhase::Apply);
            ImGui::BeginDisabled(m_SplineDraft.locked);
            char name[256]{};std::strncpy(name,m_SplineDraft.name.c_str(),sizeof(name)-1);
            const bool renamed=ImGui::InputText("Name",name,sizeof(name));
            if (renamed) m_SplineDraft.name=name;live(renamed);
            if (ImGui::Checkbox("Enabled",&m_SplineDraft.enabled)) submit(PcgSplineEditPhase::Apply);
            if (ImGui::Checkbox("Exclude grass and trees",&m_SplineDraft.excludeVegetation)) submit(PcgSplineEditPhase::Apply);
            if (m_SplineDraft.excludeVegetation) live(ImGui::DragFloat("Vegetation edge fade (m)",&m_SplineDraft.vegetationFade,.1f,.05f,1000));
            live(ImGui::DragInt("Terrain order",&m_SplineDraft.priority,1,-10000,10000));
            live(ImGui::DragFloat("Terrain shoulder (m)",&m_SplineDraft.shoulder,.1f,0,1000));
            live(ImGui::DragFloat("Coverage blend (m)",&m_SplineDraft.blendWidth,.05f,.01f,1000));
            if (kind==PcgSplineKind::Road)
            {
                if (ImGui::BeginCombo("PBR material",m_SplineDraft.materialGuid.c_str()))
                {
                    for (const auto& asset:assetAPI.QueryAssets({AssetType::Material}))
                        if (ImGui::Selectable((asset.name+"##"+asset.guid).c_str(),asset.guid==m_SplineDraft.materialGuid))
                        {m_SplineDraft.materialGuid=asset.guid;submit(PcgSplineEditPhase::Apply);}
                    ImGui::EndCombo();
                }
                // 切换模式前也可设置独立材质，避免未赋材质的投影模式无法提交。
                if (ImGui::BeginCombo("Road decal material",m_SplineDraft.roadDecalMaterialGuid.empty()?"Choose PBR material":m_SplineDraft.roadDecalMaterialGuid.c_str()))
                {
                    for (const auto& asset:assetAPI.QueryAssets({AssetType::Material}))
                        if (ImGui::Selectable((asset.name+"##road-decal-"+asset.guid).c_str(),asset.guid==m_SplineDraft.roadDecalMaterialGuid))
                        {m_SplineDraft.roadDecalMaterialGuid=asset.guid;submit(PcgSplineEditPhase::Apply);}
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Independent PBR surface material for Road Decal. Uses road UV projection, not the ordinary Decal shader.");
                int roadRenderMode=static_cast<int>(m_SplineDraft.roadRenderMode);
                if (ImGui::Combo("Road render",&roadRenderMode,"Road mesh\0Projected decal\0"))
                {
                    m_SplineDraft.roadRenderMode=static_cast<PcgRoadRenderMode>(roadRenderMode);
                    submit(PcgSplineEditPhase::Apply);
                }
                if (m_SplineDraft.roadRenderMode==PcgRoadRenderMode::ProjectedDecal)
                {
                    live(ImGui::DragFloat("Projection depth (m)",&m_SplineDraft.projectedDepth,.1f,.05f,100));
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The proxy volume extends downward only to rasterize the road onto terrain.");
                }
                live(ImGui::DragFloat("Road surface offset (m)",&m_SplineDraft.surfaceOffset,.005f,0,1));
                live(ImGui::DragFloat("Along-road repeat (m)",&m_SplineDraft.textureRepeat,.1f,.01f,1000));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("One texture across the full road width; repeat along the spline. U runs across, V runs along the road.");
            }
            else
            {
                bool reverse=m_SplineDraft.flowSign<0;
                live(ImGui::DragFloat("Water surface drop (m)",&m_SplineDraft.waterSurfaceDrop,.01f,0,100));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Water level = spline height - drop. Bed depth is measured below this water level.");
                ImGui::TextUnformatted("Water Level transition");
                live(ImGui::DragFloat("Boundary blend (m)",&m_SplineDraft.waterBlendWidthMeters,.25f,0,1000));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Blend water height, waves and normals inward from the buried mask edge. Minimum effective width: four field texels. Does not change terrain.");
                live(ImGui::DragFloat("Start blend (m)",&m_SplineDraft.waterBlendStartMeters,.5f,0,1000000));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Distance from the first point to full river waves. 0 keeps only the buried boundary blend. Independent of flow direction.");
                live(ImGui::DragFloat("End blend (m)",&m_SplineDraft.waterBlendEndMeters,.5f,0,1000000));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Distance from the last point to full river waves. Increase at a lake/sea outlet; use 0 at tributary junctions. Nonzero distances span at least four field texels.");
                if (ImGui::Checkbox("Carve riverbed",&m_SplineDraft.carveRiverbed)) submit(PcgSplineEditPhase::Apply);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Disable when the riverbed is sculpted directly into the terrain.");
                live(ImGui::DragFloat("Wet bank width (m)",&m_SplineDraft.wetBankWidthMeters,.1f,
                    std::max(snapshot.minimumRiverTransitionWidth,.2f),1000));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Terrain stays fully wet below the river and fades to dry across this distance beyond each bank.");
                live(ImGui::SliderFloat("Wetness strength",&m_SplineDraft.wetnessStrength,0,1,"%.2f"));
                if (ImGui::Checkbox("Reverse water flow",&reverse)) {m_SplineDraft.flowSign=reverse?-1.f:1.f;submit(PcgSplineEditPhase::Apply);}
                live(ImGui::DragFloat("Upstream fade (m)",&m_SplineDraft.fadeInDistance,.1f,0,10000));
                live(ImGui::DragFloat("Downstream fade (m)",&m_SplineDraft.fadeOutDistance,.1f,0,10000));
                if (ImGui::Checkbox("Normal flow",&m_SplineDraft.normalFlowEnabled)) submit(PcgSplineEditPhase::Apply);
            }
			if (ImGui::CollapsingHeader("Continuity diagnostics"))
			{
				ImGui::Text("Coordinate: offset %.3f m, sign %+.0f",
					selected->coordinateOffset,selected->coordinateSign);
				ImGui::Text("Continuation: %s",selected->continuation?"yes":"no");
				ImGui::Text("Envelope: offset %.3f m, length %.3f m",
					selected->envelopeOffset,selected->envelopeLength);
				ImGui::TextDisabled("Read-only values maintained by spline split/import continuity.");
			}
            if (ImGui::Button("Duplicate")) command(PcgSplineCommand::Duplicate);
            ImGui::SameLine();if (ImGui::Button("Reverse point order")) command(PcgSplineCommand::Reverse);
            ImGui::SameLine();if (ImGui::Button("Delete spline")) command(PcgSplineCommand::Remove);
            ImGui::Separator();
            for (std::size_t i=0;i<m_SplineDraft.points.size();++i)
            {
                auto& point=m_SplineDraft.points[i];
                if (ImGui::Selectable(("Point "+std::to_string(i+1)+"##"+point.id).c_str(),point.id==snapshot.selectedPoint))
					report(pcgAPI.SelectPcgSpline(m_SplineDraft.id,point.id,snapshot.toolEnabled));
                if (point.id!=snapshot.selectedPoint) continue;
                ImGui::PushID(point.id.c_str());
                live(ImGui::DragFloat3("Position (m)",point.position.data(),.05f));
                int tangent=static_cast<int>(point.tangentMode);
                if (ImGui::Combo("Tangents",&tangent,"Auto\0Aligned\0Mirrored\0Broken\0"))
                {point.tangentMode=static_cast<PcgSplineTangentMode>(tangent);submit(PcgSplineEditPhase::Apply);}
                ImGui::BeginDisabled(point.tangentMode==PcgSplineTangentMode::Auto);
                live(ImGui::DragFloat3("Incoming handle",point.arrive.data(),.05f));
                live(ImGui::DragFloat3("Outgoing handle",point.leave.data(),.05f));ImGui::EndDisabled();
                int segment=static_cast<int>(point.outgoing);
                if (ImGui::Combo("Next segment",&segment,"Curve\0Line\0"))
                {point.outgoing=static_cast<PcgSplineSegmentMode>(segment);submit(PcgSplineEditPhase::Apply);}
                if (ImGui::Checkbox("Link widths",&point.linkedWidth)) {if(point.linkedWidth)point.rightWidth=point.leftWidth;submit(PcgSplineEditPhase::Apply);}
                float totalWidth=point.leftWidth+point.rightWidth;
                const bool totalChanged=ImGui::DragFloat("Total width (m)",&totalWidth,.1f,.1f,2000);
                if (totalChanged)
                {const float ratio=point.leftWidth/(point.leftWidth+point.rightWidth);point.leftWidth=std::max(.05f,totalWidth*ratio);point.rightWidth=std::max(.05f,totalWidth*(1-ratio));}
                live(totalChanged);
                const bool width=ImGui::DragFloat(point.linkedWidth?"Half width (m)":"Left width (m)",&point.leftWidth,.05f,.05f,1000);
                if (point.linkedWidth) point.rightWidth=point.leftWidth;live(width);
                if (!point.linkedWidth) live(ImGui::DragFloat("Right width (m)",&point.rightWidth,.05f,.05f,1000));
                if (kind==PcgSplineKind::Road) live(ImGui::DragFloat("Bank (degrees)",&point.bankAngleDegrees,.1f,-45,45));
                else
                {
                    live(ImGui::DragFloat("River depth (m)",&point.depth,.05f,.05f,1000));
                    live(ImGui::SliderFloat("Bank steepness",&point.bankSteepness,0,.9f,"%.2f"));
                    ImGui::TextDisabled("0: gentle bowl; higher: wider deep bed, steeper banks. Ground is only lowered.");
                    live(ImGui::DragFloat("Flow speed (m/s)",&point.speed,.05f,0,100));
                    ImGui::Text("After endpoint fade: %.2f m/s",point.effectiveFlowSpeed);
                }
                if (i+1<m_SplineDraft.points.size() && ImGui::Button("Insert midpoint after"))
                {
                    PcgSplineCommandRequest request;request.command=PcgSplineCommand::InsertPoint;request.splineId=m_SplineDraft.id;
					request.segment=static_cast<std::uint32_t>(i);report(pcgAPI.ExecutePcgSplineCommand(request));
                }
                ImGui::SameLine();if (ImGui::Button("Delete point")) command(PcgSplineCommand::RemovePoint);
                ImGui::PopID();
            }
            ImGui::EndDisabled();
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
}
}
