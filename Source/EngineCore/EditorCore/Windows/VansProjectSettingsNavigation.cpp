#include "VansProjectSettingsWindow.h"
#include "../../EngineAPILayer/Public/INavigationEditorAPI.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

namespace VansGraphics
{
namespace
{
using Value=Vans::VansSerializedValue;
using Snapshot=Vans::EditorAPI::NavigationSettingsSnapshot;

std::string FieldLabel(const std::string& name)
{
    std::string result;
    for(const unsigned char ch:name)
    {
        if(!result.empty() && std::isupper(ch)) result+=' ';
        result+=static_cast<char>(result.empty()?std::toupper(ch):ch);
    }
    return result;
}

const Vans::EditorAPI::NavigationSettingsArrayTemplate* ArrayTemplate(const Snapshot& schema,const std::string& path)
{
    const auto found=std::find_if(schema.arrays.begin(),schema.arrays.end(),[&](const auto& item){return item.path==path;});
    return found==schema.arrays.end()?nullptr:&*found;
}

std::vector<std::string> DirectoryNames(const Value& root,const std::string& path)
{
    const Value* directory=nullptr;
    if(path=="maps/profiles") directory=Vans::FindObjectField(root,"profiles");
    else if(const auto* areas=Vans::FindObjectField(root,"areas")) directory=Vans::FindObjectField(*areas,"definitions");
    std::vector<std::string> result;
    if(directory) for(const auto& entry:directory->arrayItems)
        result.push_back(Vans::ReadSerializedStringField(entry,path=="maps/profiles"?"id":"name"));
    return result;
}

void AssignNewIdentity(Value& element,const Value& collection)
{
    auto* id=Vans::FindObjectField(element,"id");
    auto* name=Vans::FindObjectField(element,"name");
    auto* key=id && id->kind==Value::Kind::String?id:name;
    if(key && key->kind==Value::Kind::String)
    {
        const auto base=key->stringValue;
        for(std::size_t suffix=1;suffix<=collection.arrayItems.size()+1;++suffix)
        {
            const auto candidate=base+"_"+std::to_string(suffix);
            const bool exists=std::any_of(collection.arrayItems.begin(),collection.arrayItems.end(),[&](const auto& current)
            { return Vans::ReadSerializedStringField(current,key==id?"id":"name")==candidate; });
            if(!exists) { key->stringValue=candidate; break; }
        }
    }
    if(id && id->kind==Value::Kind::Int)
        for(std::int64_t candidate=0;candidate<=static_cast<std::int64_t>(collection.arrayItems.size());++candidate)
        {
            const bool exists=std::any_of(collection.arrayItems.begin(),collection.arrayItems.end(),[&](const auto& current)
            { const auto* currentId=Vans::FindObjectField(current,"id"); return currentId && currentId->intValue==candidate; });
            if(!exists) { id->intValue=candidate; break; }
        }
}

bool DrawSettingsValue(const std::string& name,Value& value,const std::string& path,
    const Snapshot& schema,const Value& root)
{
    bool changed=false;
    ImGui::PushID(name.c_str());
    const auto label=FieldLabel(name);
    if(value.kind==Value::Kind::Object || value.kind==Value::Kind::Array)
    {
        if(ImGui::TreeNodeEx(label.c_str(),ImGuiTreeNodeFlags_DefaultOpen))
        {
            if(path=="queryFilters/areaCosts")
            {
                for(std::size_t index=0;index<value.objectFields.size();)
                {
                    auto& field=value.objectFields[index];
                    ImGui::PushID(static_cast<int>(index));
                    changed|=DrawSettingsValue(field.first,field.second,path+"/"+field.first,schema,root);
                    ImGui::SameLine();
                    if(ImGui::SmallButton("Remove"))
                    { value.objectFields.erase(value.objectFields.begin()+index); changed=true; }
                    else ++index;
                    ImGui::PopID();
                }
                if(ImGui::BeginMenu("Add Area Cost"))
                {
                    for(const auto& area:DirectoryNames(root,path))
                        if(ImGui::MenuItem(area.c_str(),nullptr,false,!Vans::FindObjectField(value,area)))
                        { value.objectFields.emplace_back(area,Value::Float(1)); changed=true; }
                    ImGui::EndMenu();
                }
            }
            else if(value.kind==Value::Kind::Object)
                for(auto& [field,child]:value.objectFields)
                    changed|=DrawSettingsValue(field,child,path.empty()?field:path+"/"+field,schema,root);
            else
            {
                const auto* prototype=ArrayTemplate(schema,path);
                for(std::size_t index=0;index<value.arrayItems.size();)
                {
                    ImGui::PushID(static_cast<int>(index));
                    auto& element=value.arrayItems[index];
                    const auto id=Vans::ReadSerializedStringField(element,"id",Vans::ReadSerializedStringField(element,"name"));
                    const auto title=id.empty()?"Item "+std::to_string(index+1):id;
                    changed|=DrawSettingsValue(title,element,path,schema,root);
                    if(prototype)
                    {
                        ImGui::SameLine();
                        if(ImGui::SmallButton("Remove"))
                        { value.arrayItems.erase(value.arrayItems.begin()+index); changed=true; ImGui::PopID(); continue; }
                    }
                    ++index; ImGui::PopID();
                }
                if(prototype)
                {
                    ImGui::BeginDisabled(value.arrayItems.size()>=prototype->maximumItems);
                    if(prototype->element.kind==Value::Kind::String)
                    {
                        if(ImGui::BeginMenu("Add Reference"))
                        {
                            for(const auto& reference:DirectoryNames(root,path))
                            {
                                const bool exists=std::any_of(value.arrayItems.begin(),value.arrayItems.end(),[&](const auto& current)
                                { return current.stringValue==reference; });
                                if(ImGui::MenuItem(reference.c_str(),nullptr,false,!exists))
                                { value.arrayItems.push_back(Value::String(reference)); changed=true; }
                            }
                            ImGui::EndMenu();
                        }
                    }
                    else if(ImGui::SmallButton("Add Item"))
                    {
                        auto element=prototype->element; AssignNewIdentity(element,value);
                        value.arrayItems.push_back(std::move(element)); changed=true;
                    }
                    ImGui::EndDisabled();
                }
            }
            ImGui::TreePop();
        }
    }
    else
    {
        ImGui::SetNextItemWidth(220);
        switch(value.kind)
        {
        case Value::Kind::Bool: changed=ImGui::Checkbox(label.c_str(),&value.boolValue); break;
        case Value::Kind::Int: changed=ImGui::InputScalar(label.c_str(),ImGuiDataType_S64,&value.intValue); break;
        case Value::Kind::Float: changed=ImGui::InputScalar(label.c_str(),ImGuiDataType_Double,&value.floatValue); break;
        case Value::Kind::String:
        {
            const auto choices=std::find_if(schema.choiceFields.begin(),schema.choiceFields.end(),
                [&](const auto& field){return field.path==path;});
            if(choices!=schema.choiceFields.end())
            {
                if(ImGui::BeginCombo(label.c_str(),value.stringValue.c_str()))
                {
                    for(const auto& option:choices->choices)
                        if(ImGui::Selectable(option.label.c_str(),option.value.stringValue==value.stringValue))
                        { value=option.value; changed=true; }
                    ImGui::EndCombo();
                }
            }
            else if(path=="maps/profiles" || path=="queryFilters/excludedAreas" || path=="areas/default")
            {
                if(ImGui::BeginCombo(label.c_str(),value.stringValue.c_str()))
                {
                    for(const auto& option:DirectoryNames(root,path))
                        if(ImGui::Selectable(option.c_str(),option==value.stringValue))
                        { value.stringValue=option; changed=true; }
                    ImGui::EndCombo();
                }
            }
            else
            {
                std::array<char,256> buffer{}; std::snprintf(buffer.data(),buffer.size(),"%s",value.stringValue.c_str());
                if(ImGui::InputText(label.c_str(),buffer.data(),buffer.size()))
                { value.stringValue=buffer.data(); changed=true; }
            }
            break;
        }
        default: break;
        }
    }
    ImGui::PopID(); return changed;
}
}

void VansProjectSettingsWindow::DrawNavigationSettings(Vans::EditorAPI::INavigationEditorAPI& api)
{
    const auto current=api.GetNavigationSettings();
    if(!current.available) { ImGui::TextDisabled("No navigation project settings available"); return; }
    const bool dirty=m_NavigationInitialized && !Vans::SerializedValuesEqual(m_NavigationDraft,m_NavigationOriginal);
    bool externalChange=m_NavigationInitialized && !Vans::SerializedValuesEqual(current.document,m_NavigationOriginal);
    if(!m_NavigationInitialized || m_NavigationProjectRoot!=current.projectRoot || (externalChange && !dirty))
    {
        m_NavigationProjectRoot=current.projectRoot; m_NavigationOriginal=current.document;
        m_NavigationDraft=current.document; m_NavigationInitialized=true; m_NavigationApplyMessage.clear();
        m_NavigationPreviewMessage.clear();
        m_NavigationValidation=api.ValidateNavigationSettingsDocument(m_NavigationDraft);
        externalChange=false;
    }
    ImGui::TextWrapped("Edit profiles, maps, areas, filters and query budgets. Apply updates memory; Save Project Documents persists changes.");
    if(!current.runtimeDiagnostic.empty()) ImGui::TextWrapped("Navigation: %s",current.runtimeDiagnostic.c_str());
    if(externalChange && dirty) ImGui::TextWrapped("Navigation settings changed externally. Reload before applying this draft.");
    ImGui::BeginDisabled(!current.editable);
    if(DrawSettingsValue("Navigation Settings",m_NavigationDraft,"",current,m_NavigationDraft))
        m_NavigationValidation=api.ValidateNavigationSettingsDocument(m_NavigationDraft);
    if(!m_NavigationValidation.valid) ImGui::TextWrapped("%s",m_NavigationValidation.error.c_str());
    ImGui::BeginDisabled(!m_NavigationValidation.valid || externalChange || Vans::SerializedValuesEqual(m_NavigationDraft,m_NavigationOriginal));
    if(ImGui::Button("Apply Navigation Settings"))
    {
        const auto applied=api.ApplyNavigationSettings(m_NavigationDraft,m_NavigationOriginal);
        m_NavigationApplyMessage=applied.valid?"Applied in memory. Save project documents to persist.":applied.error;
        if(applied.valid) m_NavigationOriginal=m_NavigationDraft;
    }
    ImGui::EndDisabled(); ImGui::SameLine();
    if(ImGui::Button("Reload Navigation Settings"))
    {
        m_NavigationDraft=current.document; m_NavigationOriginal=current.document;
        m_NavigationApplyMessage.clear(); m_NavigationValidation=api.ValidateNavigationSettingsDocument(m_NavigationDraft);
    }
    ImGui::EndDisabled();
    if(!m_NavigationApplyMessage.empty()) ImGui::TextWrapped("%s",m_NavigationApplyMessage.c_str());

    ImGui::Separator();
    ImGui::TextUnformatted("Scene Navigation Build");
    const auto preview=api.GetNavigationBuildPreview();
    const auto* maps=Vans::FindObjectField(current.document,"maps");
    if(maps && !maps->arrayItems.empty())
    {
        const bool selected=std::any_of(maps->arrayItems.begin(),maps->arrayItems.end(),[&](const auto& map)
        { return Vans::ReadSerializedStringField(map,"id")==m_NavigationPreviewMap; });
        if(!selected) m_NavigationPreviewMap=Vans::ReadSerializedStringField(maps->arrayItems.front(),"id");
        ImGui::BeginDisabled(!preview.editable || preview.busy);
        if(ImGui::BeginCombo("Build Map",m_NavigationPreviewMap.c_str()))
        {
            for(const auto& map:maps->arrayItems)
            {
                const auto id=Vans::ReadSerializedStringField(map,"id");
                if(ImGui::Selectable(id.c_str(),id==m_NavigationPreviewMap)) m_NavigationPreviewMap=id;
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(!Vans::SerializedValuesEqual(m_NavigationDraft,m_NavigationOriginal));
        if(ImGui::Button("Build Preview"))
        {
            const auto built=api.BuildNavigationPreview(m_NavigationPreviewMap);
            m_NavigationPreviewMessage=built.valid?"Building an in-memory candidate from current applied settings.":built.error;
        }
        ImGui::EndDisabled(); ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!preview.requestId);
        if(ImGui::Button("Cancel / Clear Preview"))
        { api.CancelNavigationPreview(); m_NavigationPreviewMessage.clear(); }
        ImGui::EndDisabled();
    }
    if(!preview.available) ImGui::TextDisabled("Open an editable scene to build navigation.");
    if(preview.requestId)
    {
        ImGui::Text("%s / %s: %s (%.1f ms)",preview.mapId.c_str(),
            std::to_string(preview.requestId).c_str(),preview.state.c_str(),preview.elapsedMilliseconds);
        ImGui::Text("Captured sources: %zu, surfaces: %zu",preview.capturedSources,preview.capturedSurfaces);
        for(const auto& profile:preview.profiles) ImGui::Text("%s: %zu tiles",profile.id.c_str(),profile.tileCount);
        if(preview.ready) ImGui::Text("Payload: %.2f MiB, resident candidate: %.2f MiB",
            preview.payloadBytes/(1024.0*1024.0),preview.residentBytes/(1024.0*1024.0));
        if(!preview.diagnostic.empty()) ImGui::TextWrapped("%s",preview.diagnostic.c_str());
    }
    if(preview.ready)
    {
        if(m_NavigationBakePreviewId!=preview.requestId)
        {
            std::snprintf(m_NavigationBakeOutput.data(),m_NavigationBakeOutput.size(),"%s",preview.suggestedOutputPath.c_str());
            m_NavigationBakePreviewId=preview.requestId;
        }
        ImGui::BeginDisabled(!preview.editable);
        ImGui::InputText("Project-relative .vnavmesh output",m_NavigationBakeOutput.data(),m_NavigationBakeOutput.size());
        ImGui::BeginDisabled(!m_NavigationBakeOutput.front());
        if(ImGui::Button("Bake and Bind Map Surfaces"))
        {
            const auto baked=api.BakeNavigationPreview(m_NavigationBakeOutput.data());
            m_NavigationPreviewMessage=baked.valid?"Baked asset published. Save the Scene to persist any changed Surface bindings.":baked.error;
        }
        ImGui::EndDisabled(); ImGui::EndDisabled();
    }
    if(!m_NavigationPreviewMessage.empty()) ImGui::TextWrapped("%s",m_NavigationPreviewMessage.c_str());
}
}
