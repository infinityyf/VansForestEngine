#include "VansPcgWindow.h"
#include "../../EngineAPILayer/Public/IAssetEditorAPI.h"
#include "../../EngineAPILayer/Public/IPcgEditorAPI.h"
#include "imgui.h"
#include <algorithm>
#include <limits>
#include <vector>
#include <unordered_set>

namespace VansGraphics
{
namespace
{
using namespace Vans::EditorAPI;
bool TextField(const char* label,std::string& value)
{
    std::vector<char> buffer(std::max<size_t>(1024,value.size()+256),0);
    std::copy(value.begin(),value.end(),buffer.begin());
    if (!ImGui::InputText(label,buffer.data(),buffer.size())) return false;
    value=buffer.data();return true;
}
void AssetField(IAssetEditorAPI& assetAPI,const char* label,std::string& guid,AssetType type)
{
	const auto selected=guid.empty()?AssetGuidResolution{}:assetAPI.ResolveAssetGuid(guid);
    const std::string name=guid.empty()?"Unassigned":selected.found?selected.asset.name:guid;
    if (ImGui::BeginCombo(label,name.c_str())) {
        if (ImGui::Selectable("Unassigned",guid.empty())) guid.clear();
        for (const auto& asset : assetAPI.QueryAssets({type})) {
            ImGui::PushID(asset.guid.c_str());
            if (ImGui::Selectable(asset.name.c_str(),asset.guid==guid)) guid=asset.guid;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",asset.relativePath.c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
}
void Number(const char* label,float& value) {ImGui::DragFloat(label,&value,.01f,0,0,"%.3f");}
void Unsigned(const char* label,uint32_t& value) {ImGui::InputScalar(label,ImGuiDataType_U32,&value);}
void Budget(const char* label,uint64_t& value) {ImGui::InputScalar(label,ImGuiDataType_U64,&value);}
void ConfigurationFields(std::vector<PcgConfigurationField>& fields,bool densitySource,PcgPlantConfiguration* plant=nullptr,
    uint32_t firstOrder=0,uint32_t lastOrder=(std::numeric_limits<uint32_t>::max)())
{
    std::vector<PcgConfigurationField*> ordered;
    ordered.reserve(fields.size());
    for(auto& field:fields) if(field.editorOrder>=firstOrder && field.editorOrder<=lastOrder &&
        (field.visibility!=PcgConfigurationFieldVisibility::DensitySourceOnly || densitySource))
        ordered.push_back(&field);
    std::stable_sort(ordered.begin(),ordered.end(),[](const auto* a,const auto* b){return a->editorOrder<b->editorOrder;});
    for(auto* field:ordered) {
        const float minimum=field->editorConstrained && field->hasMinimum?field->minimum:0;
        const float maximum=field->editorConstrained && field->hasMaximum?field->maximum:0;
        switch(field->kind) {
        case PcgConfigurationFieldKind::Float:
            if(field->values.size()==1) ImGui::DragFloat(field->label.c_str(),field->values.data(),field->editorSpeed,minimum,maximum,"%.3f");
            break;
        case PcgConfigurationFieldKind::Unsigned:Unsigned(field->label.c_str(),field->unsignedValue);break;
        case PcgConfigurationFieldKind::Boolean:ImGui::Checkbox(field->label.c_str(),&field->boolValue);break;
        case PcgConfigurationFieldKind::Float2:
            if(field->values.size()==2) ImGui::DragFloat2(field->label.c_str(),field->values.data(),field->editorSpeed,minimum,maximum);
            break;
        case PcgConfigurationFieldKind::Float3:
            if(field->values.size()==3) ImGui::DragFloat3(field->label.c_str(),field->values.data(),field->editorSpeed,minimum,maximum);
            break;
        case PcgConfigurationFieldKind::FloatList:
            if(plant) {
                int count=static_cast<int>(field->values.size());
                if(ImGui::SliderInt("Simplified LOD levels",&count,
                    static_cast<int>(field->minimumCount),static_cast<int>(field->maximumCount))) {
                    const auto oldDistanceCount=field->values.size();
                    field->values.resize(static_cast<size_t>(count));
                    for(size_t level=oldDistanceCount;level<field->values.size();++level) {
                        const float previous=level?field->values[level-1]:60.f;
                        field->values[level]=previous+std::max(1.f,previous*2.f);
                    }
                    for(auto& variant:plant->variants) {
                        const auto oldRatioCount=variant.lodRatios.size();
                        variant.lodRatios.resize(static_cast<size_t>(count));
                        for(size_t level=oldRatioCount;level<variant.lodRatios.size();++level) {
                            const float previous=level?variant.lodRatios[level-1]:.5f;
                            variant.lodRatios[level]=std::max(.01f,previous*.36f);
                        }
                    }
                }
            }
            for(size_t level=0;level<field->values.size();++level) {
                ImGui::PushID(static_cast<int>(level));
                ImGui::DragFloat(field->label.c_str(),&field->values[level],field->editorSpeed,minimum,maximum);
                ImGui::PopID();
            }
            break;
        }
    }
}
}
void VansPcgWindow::ShowLayerActions(Vans::EditorAPI::IPcgEditorAPI& api,
	Vans::EditorAPI::IAssetEditorAPI& assetAPI,
    const Vans::EditorAPI::PcgEditorSnapshot& snapshot,int category)
{
    using namespace Vans::EditorAPI;
    const auto selected=api.GetPcgBrushSnapshot().target;
    const auto beginCreate=[&](bool copy) {
        m_CreateDraft={};m_CreateDraft.tree=category==1;
        m_CreateDraft.recipeGuid=selected.recipeGuid.empty()?snapshot.sceneRecipeGuid:selected.recipeGuid;
        m_CreateDraft.regionId=selected.regionId;
        if (copy) m_CreateDraft.copyFrom=selected;
        ImGui::OpenPopup("Create plant layer");
    };
    if (ImGui::Button("New plant layer")) beginCreate(false);
    ImGui::SameLine();ImGui::BeginDisabled(selected.layerId.empty());
    if (ImGui::Button("Copy selected layer")) beginCreate(true);
    ImGui::SameLine();
    if (ImGui::Button("Remove layer")) {m_Message=api.RemovePcgLayer(selected).message;m_ConfigurationTarget={};}
    ImGui::EndDisabled();
    if (!selected.recipeGuid.empty() && selected.recipeGuid!=snapshot.sceneRecipeGuid &&
        ImGui::Button("Use this recipe in the current scene"))
        m_Message=api.BindPcgRecipeToScene(selected.recipeGuid).message;
    if (ImGui::BeginPopupModal("Create plant layer",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        auto& d=m_CreateDraft;
        ImGui::TextUnformatted(d.tree?"Trees":"Grass");
        TextField("Plant / layer name",d.name);
        const auto oldRecipe=d.recipeGuid;
		AssetField(assetAPI,"Recipe (unassigned creates a new one)",d.recipeGuid,AssetType::VegetationConfig);
        if (oldRecipe!=d.recipeGuid) d.regionId.clear();
        if (d.recipeGuid.empty()) TextField("New recipe name",d.recipeName);
        const auto region=std::find_if(snapshot.layers.begin(),snapshot.layers.end(),[&](const auto& layer){
            return layer.recipeGuid==d.recipeGuid && layer.regionId==d.regionId;
        });
        if (ImGui::BeginCombo("Region",region==snapshot.layers.end()?"Create a region":region->regionName.c_str())) {
            if (ImGui::Selectable("Create a region",d.regionId.empty())) d.regionId.clear();
            std::unordered_set<std::string> shown;
            for (const auto& layer:snapshot.layers) {
                if (layer.recipeGuid!=d.recipeGuid || !shown.insert(layer.regionId).second) continue;
                ImGui::PushID(layer.regionId.c_str());
                if (ImGui::Selectable(layer.regionName.c_str(),d.regionId==layer.regionId)) d.regionId=layer.regionId;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (d.regionId.empty()) {
            TextField("Region name",d.regionName);
            ImGui::InputFloat2("World XZ minimum",d.boundsMin.data());
            ImGui::InputFloat2("World XZ maximum",d.boundsMax.data());
            Number("Cell size (m)",d.cellSize);
        }
        if (d.copyFrom.layerId.empty()) {
            Unsigned("Mask width",d.maskWidth);Unsigned("Mask height",d.maskHeight);
            ImGui::TextWrapped("Creates an empty plant, a black 16-bit Mask and zero density. Configure the surface, model, material and density before enabling the layer.");
        } else ImGui::TextWrapped("Copies the plant and Mask pixels to independent assets. Instance overrides must be removed before copying.");
        if (ImGui::Button("Create assets")) {
            const auto created=api.CreatePcgLayer(d);m_Message=created.message;
            if (created.success) {m_ConfigurationTarget={};ImGui::CloseCurrentPopup();}
        }
        ImGui::SameLine();if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        if (!m_Message.empty()) ImGui::TextWrapped("%s",m_Message.c_str());
        ImGui::EndPopup();
    }
}
void VansPcgWindow::ShowConfiguration(Vans::EditorAPI::IPcgEditorAPI& api,
	Vans::EditorAPI::IAssetEditorAPI& assetAPI,const Vans::EditorAPI::PcgLayerSnapshot& layer)
{
    using namespace Vans::EditorAPI;
    const PcgBrushTarget target{layer.recipeGuid,layer.regionId,layer.layerId,{}};
    const auto reload=[&]() {
        m_ConfigurationTarget=target;
        m_LayerDraft=api.GetPcgLayerConfiguration(target);
        m_PlantDraft=api.GetPcgPlantConfiguration(m_LayerDraft.plantGuid);
    };
    if (!(m_ConfigurationTarget==target)) reload();
    const auto report=[&](const PcgEditorOperationResult& result) {m_Message=result.message;return result.success;};
    const auto history=[&](const std::string& guid,bool undo,bool redo) {
        ImGui::PushID(guid.c_str());
        ImGui::BeginDisabled(!undo);
        if (ImGui::Button("Undo configuration")) {report(api.EditPcgConfiguration(guid,PcgConfigurationAction::Undo));reload();}
        ImGui::EndDisabled();ImGui::SameLine();
        ImGui::BeginDisabled(!redo);
        if (ImGui::Button("Redo configuration")) {report(api.EditPcgConfiguration(guid,PcgConfigurationAction::Redo));reload();}
        ImGui::EndDisabled();
        ImGui::PopID();
    };
    if (ImGui::Button("Reload configuration drafts")) reload();
    if (ImGui::CollapsingHeader("Distribution configuration")) {
        auto& d=m_LayerDraft;
        if (!d.available) ImGui::TextWrapped("%s",d.message.c_str());
        else {
            TextField("Layer name",d.name);
            ImGui::Checkbox("Enabled",&d.enabled);ImGui::SameLine();ImGui::Checkbox("Locked",&d.locked);
			AssetField(assetAPI,"Plant asset",d.plantGuid,AssetType::PlantType);
            if (layer.tree) {
                int source=static_cast<int>(d.source);
                if (ImGui::Combo("Distribution mode",&source,"Density\0Target count\0Fixed instances\0")) d.source=static_cast<PcgSourceMode>(source);
                if (d.source==PcgSourceMode::Count) Unsigned("Target trees",d.treeTargetCount);
            } else {
                int source=d.source==PcgSourceMode::Fixed?1:0;
                if (ImGui::Combo("Distribution mode",&source,"Density\0Fixed instances\0"))
                    d.source=source==1?PcgSourceMode::Fixed:PcgSourceMode::Density;
            }
            ConfigurationFields(d.placementFields,d.source==PcgSourceMode::Density,nullptr,0,0);
            Unsigned("Layer seed",d.seed);
            ConfigurationFields(d.placementFields,d.source==PcgSourceMode::Density,nullptr,1);
            if (ImGui::TreeNode("Generation safety budgets")) {
                Budget("Candidate limit",d.maxCandidates);Budget("Instance limit",d.maxInstances);
                ImGui::TextWrapped("Exceeding a safety budget reports an error; it never thins or refills the distribution.");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Region and surface")) {
                TextField("Region name",d.regionName);ImGui::Checkbox("Region enabled",&d.regionEnabled);
                ImGui::DragFloat2("Region minimum XZ",d.boundsMin.data(),.1f);ImGui::DragFloat2("Region maximum XZ",d.boundsMax.data(),.1f);
                Number("Cell size (m)",d.cellSize);Unsigned("Region seed",d.regionSeed);
                int surface=static_cast<int>(d.surface);
                if (ImGui::Combo("Surface",&surface,"Unassigned\0Plane\0Terrain\0")) {
                    d.surface=static_cast<PcgSurfaceKind>(surface);
                    if (d.surface!=PcgSurfaceKind::Terrain) d.terrainGuid.clear();
                }
                if (d.surface==PcgSurfaceKind::Plane) Number("Plane height",d.planeHeight);
				if (d.surface==PcgSurfaceKind::Terrain) AssetField(assetAPI,"Terrain asset",d.terrainGuid,AssetType::Terrain);
                ImGui::TextDisabled("Region settings affect every layer in this region.");
                ImGui::TreePop();
            }
            if (ImGui::Button("Apply distribution")) if (report(api.ApplyPcgLayerConfiguration(target,d))) reload();
            ImGui::SameLine();
            if (ImGui::Button("Save distribution")) if (report(api.ApplyPcgLayerConfiguration(target,d))) {
                report(api.EditPcgConfiguration(target.recipeGuid,PcgConfigurationAction::Save));reload();
            }
            history(target.recipeGuid,d.canUndo,d.canRedo);
        }
    }
    if (ImGui::CollapsingHeader("Models and materials")) {
        auto& p=m_PlantDraft;
        if (!p.available) ImGui::TextWrapped("%s",p.message.c_str());
        else {
            TextField("Plant name",p.name);
            ImGui::TextDisabled("This plant asset can be referenced by several independent distribution layers.");
            if (ImGui::Button("Add model variant")) p.variants.emplace_back();
            for (size_t index=0;index<p.variants.size();) {
                ImGui::PushID(static_cast<int>(index));auto& variant=p.variants[index];
                bool remove=false;
                const std::string title=variant.name.empty()?"Unnamed variant":variant.name;
                if (ImGui::TreeNode("Variant","%s",title.c_str())) {
                    TextField("Variant name",variant.name);
                    int geometry=static_cast<int>(variant.geometry);
                    if (ImGui::Combo("Geometry",&geometry,p.tree?"Unassigned\0Mesh\0":"Unassigned\0Mesh\0Procedural blade\0")) {
                        variant.geometry=static_cast<PcgGeometry>(geometry);
                        if (variant.geometry!=PcgGeometry::Mesh) for (auto& part : variant.parts) {
                            part.mesh.clear();part.submesh=-1;
                        }
                    }
                    Number("Selection weight",variant.weight);Number("Footprint radius (m)",variant.footprintRadius);
                    Number("Culling radius (m)",variant.cullingRadius);
                    if (variant.geometry==PcgGeometry::ProceduralBlade) Number("Blade width (m)",variant.bladeWidth);
                    ImGui::DragFloat3("Model offset",variant.offset.data(),.01f);
                    ImGui::DragFloat3("Model scale",variant.scale.data(),.01f);
                    ImGui::DragFloat4("Model rotation XYZW",variant.rotation.data(),.01f);
                    if (ImGui::Button("Add material part")) variant.parts.emplace_back();
                    for (size_t partIndex=0;partIndex<variant.parts.size();) {
                        ImGui::PushID(static_cast<int>(partIndex));auto& part=variant.parts[partIndex];
                        bool removePart=false;
                        if (ImGui::TreeNode("Part","Part %zu",partIndex+1)) {
                            int kind=static_cast<int>(part.kind);
                            if (ImGui::Combo("Part kind",&kind,"Surface\0Trunk\0Leaves\0")) part.kind=static_cast<PcgPartKind>(kind);
                            if (variant.geometry==PcgGeometry::Mesh) {
								AssetField(assetAPI,"Model",part.mesh,AssetType::Model);
                                ImGui::InputInt("Submesh (-1 = all)",&part.submesh);
                            }
							AssetField(assetAPI,"Material",part.material,AssetType::Material);
                            removePart=ImGui::Button("Remove part");
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                        if (removePart) variant.parts.erase(variant.parts.begin()+partIndex);else ++partIndex;
                    }
                    if(p.tree && ImGui::TreeNode("Automatic mesh LOD")) {
                        for(size_t level=0;level<variant.lodRatios.size();++level) {
                            ImGui::PushID(static_cast<int>(level));
                            ImGui::DragFloat("Triangle ratio",&variant.lodRatios[level],.01f,.01f,.99f);
                            ImGui::PopID();
                        }
                        Number("Maximum simplification error",variant.lodMaximumError);
                        ImGui::TextUnformatted(variant.lodBuildKey.empty()?"Not built. Save plant builds LODs automatically.":"Built model LODs are referenced by this plant.");
                        for(size_t level=0;level<variant.lodLevels.size();++level){unsigned triangles=0;for(const auto& part:variant.lodLevels[level].parts)triangles+=part.triangleCount;
                            ImGui::Text("LOD %zu: %u triangles",level+1,triangles);}
                        ImGui::TreePop();
                    }
                    remove=ImGui::Button("Remove model variant");ImGui::TreePop();
                }
                ImGui::PopID();
                if (remove) p.variants.erase(p.variants.begin()+index);else ++index;
            }
            if (!p.tree && ImGui::TreeNode("Grass geometry and wind")) {
                ConfigurationFields(p.grassFields,false);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Render settings")) {
                ConfigurationFields(p.renderFields,false,p.tree?&p:nullptr);
                if(p.tree) ImGui::TextUnformatted("Shared by every tree variant. Distant trees retain their lowest mesh LOD.");
                ImGui::TreePop();
            }
            if (ImGui::Button("Apply plant")) if (report(api.ApplyPcgPlantConfiguration(p))) reload();
            ImGui::SameLine();
            if (ImGui::Button("Save plant")) if (report(api.ApplyPcgPlantConfiguration(p))) {
                report(api.EditPcgConfiguration(p.guid,PcgConfigurationAction::Save));reload();
            }
            if(p.tree && ImGui::Button("Build model LODs")) if(report(api.ApplyPcgPlantConfiguration(p))) {
                report(api.BuildPcgPlantLods(p.guid));reload();
            }
            history(p.guid,p.canUndo,p.canRedo);
        }
    }
}
}
