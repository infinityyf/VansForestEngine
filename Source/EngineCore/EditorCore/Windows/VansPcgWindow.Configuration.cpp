#include "VansPcgWindow.h"
#include "imgui.h"
#include <algorithm>
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
void AssetField(IEngineEditorAPI& api,const char* label,std::string& guid,AssetType type)
{
    const auto selected=guid.empty()?AssetGuidResolution{}:api.ResolveAssetGuid(guid);
    const std::string name=guid.empty()?"Unassigned":selected.found?selected.asset.name:guid;
    if (ImGui::BeginCombo(label,name.c_str())) {
        if (ImGui::Selectable("Unassigned",guid.empty())) guid.clear();
        for (const auto& asset : api.QueryAssets({type})) {
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
}
void VansPcgWindow::ShowLayerActions(Vans::EditorAPI::IEngineEditorAPI& api,
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
        AssetField(api,"Recipe (unassigned creates a new one)",d.recipeGuid,AssetType::VegetationConfig);
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
void VansPcgWindow::ShowConfiguration(Vans::EditorAPI::IEngineEditorAPI& api,const Vans::EditorAPI::PcgLayerSnapshot& layer)
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
            AssetField(api,"Plant asset",d.plantGuid,AssetType::PlantType);
            if (layer.tree) {
                int source=static_cast<int>(d.source);
                if (ImGui::Combo("Distribution mode",&source,"Density\0Target count\0Fixed instances\0")) d.source=static_cast<PcgSourceMode>(source);
                if (d.source==PcgSourceMode::Count) Unsigned("Target trees",d.treeTargetCount);
            } else {
                int source=d.source==PcgSourceMode::Fixed?1:0;
                if (ImGui::Combo("Distribution mode",&source,"Density\0Fixed instances\0"))
                    d.source=source==1?PcgSourceMode::Fixed:PcgSourceMode::Density;
            }
            if (d.source==PcgSourceMode::Density) Number("Density / m2",d.placement.density);
            Unsigned("Layer seed",d.seed);
            Number("Position jitter",d.placement.positionJitter);Number("Minimum spacing (m)",d.placement.minimumSpacing);
            ImGui::Checkbox("Uniform scale",&d.placement.uniformScale);
            ImGui::DragFloat3("Scale minimum",d.placement.scaleMin.data(),.01f);
            ImGui::DragFloat3("Scale maximum",d.placement.scaleMax.data(),.01f);
            Number("Yaw minimum (degrees)",d.placement.yawMinDegrees);Number("Yaw maximum (degrees)",d.placement.yawMaxDegrees);
            Number("Normal alignment",d.placement.normalAlignment);Number("Maximum tilt (degrees)",d.placement.maximumTiltDegrees);
            Number("Root offset (m)",d.placement.rootOffset);
            Number("Mask threshold",d.placement.maskThreshold);Number("Mask multiplier",d.placement.maskMultiplier);
            ImGui::Checkbox("Invert Mask",&d.placement.invertMask);
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
                if (d.surface==PcgSurfaceKind::Terrain) AssetField(api,"Terrain asset",d.terrainGuid,AssetType::Terrain);
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
                                AssetField(api,"Model",part.mesh,AssetType::Model);
                                ImGui::InputInt("Submesh (-1 = all)",&part.submesh);
                            }
                            AssetField(api,"Material",part.material,AssetType::Material);
                            removePart=ImGui::Button("Remove part");
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                        if (removePart) variant.parts.erase(variant.parts.begin()+partIndex);else ++partIndex;
                    }
                    if(p.tree && ImGui::TreeNode("Automatic mesh LOD")) {
                        ImGui::DragFloat2("Triangle ratios",variant.lodRatios.data(),.01f,.01f,.99f);
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
                Unsigned("Bones",p.grass.boneCount);Unsigned("Blades per instance",p.grass.subBladeCount);
                ImGui::DragFloat2("Wind direction",p.grass.windDirection.data(),.01f);
                Number("Blade height (m)",p.grass.bladeHeight);
                Number("Lean deviation (degrees)",p.grass.leanDeviation);
                Number("Rest tip bend (degrees)",p.grass.restTipBendDegrees);
                Number("Rest root bend (degrees)",p.grass.restRootBendDegrees);
                Unsigned("Sub-blade scatter seed",p.grass.scatterSeed);
                Number("Scatter radius minimum (m)",p.grass.scatterRadiusMin);
                Number("Scatter radius maximum (m)",p.grass.scatterRadiusMax);
                Number("Wind strength",p.grass.windStrength);
                Number("Wind frequency",p.grass.windFrequency);
                Number("Wind speed",p.grass.windSpeed);
                Number("Wind bend multiplier",p.grass.windBendMultiplier);
                Number("Stiffness",p.grass.stiffness);
                Number("Damping",p.grass.damping);
                Number("Softness",p.grass.softness);
                Number("Full simulation distance",p.grass.simulationFullDistance);
                Number("Simulation fade distance",p.grass.simulationFadeDistance);
                Number("Sub-blade middle LOD distance",p.grass.subBladeLodMidDistance);
                Number("Sub-blade far LOD distance",p.grass.subBladeLodFarDistance);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Render settings")) {
                ImGui::Checkbox("Culling",&p.render.cullingEnabled);Number(p.tree?"Shadow distance":"Cull distance",p.render.cullDistance);
                if(p.tree) {
                    ImGui::DragFloat2("Tree LOD distances (m)",p.render.lodDistances.data(),1.f,1.f,100000.f);
                    Number("Tree LOD hysteresis",p.render.lodHysteresis);
                    ImGui::TextUnformatted("Shared by every tree variant. Distant trees retain their lowest mesh LOD.");
                }
                ImGui::Checkbox("Hi-Z",&p.render.hizEnabled);Number("Hi-Z bias",p.render.hizBias);
                ImGui::Checkbox("Cast directional shadows (first 2 cascades)",&p.render.castShadows);
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
