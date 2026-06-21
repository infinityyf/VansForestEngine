#include "VansInspectorWindow.h"

#include "../VansEditorSelection.h"
#include "../VansEditorWindow.h"
#include "../VansSceneEditService.h"
#include "../../AssetCore/VansAssetDatabase.h"
#include "../../AssetCore/VansAssetGuid.h"
#include "../../AssetCore/VansAssetMeta.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../PhysicsCore/VansCollisionLayerManager.h"
#include "../../SceneCore/VansSceneDocument.h"
#include "../../Util/VansLog.h"

#include "imgui.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#include <../../GLM/gtx/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace VansGraphics
{
namespace
{
using Json = nlohmann::ordered_json;

std::string EscapePointerToken(const std::string& token)
{
    std::string result;
    for (const char c : token)
    {
        if (c == '~') result += "~0";
        else if (c == '/') result += "~1";
        else result += c;
    }
    return result;
}

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string FriendlyLabel(const std::string& key)
{
    if (key.empty()) return "Property";
    std::string label;
    label.reserve(key.size() + 8);
    for (std::size_t i = 0; i < key.size(); ++i)
    {
        const char c = key[i];
        if (c == '_' || c == '-') { label += ' '; continue; }
        if (i > 0 && std::isupper(static_cast<unsigned char>(c)) &&
            !std::isupper(static_cast<unsigned char>(key[i - 1]))) label += ' ';
        label += c;
    }
    label.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(label.front())));
    return label;
}

bool IsGuidText(const std::string& value)
{
    Vans::VansAssetGuid guid;
    return Vans::VansAssetGuid::TryParse(value, guid);
}

Vans::VansAssetType InferAssetType(const std::string& key,
    const std::string& parentKey, const std::string& componentType)
{
    const std::string field = Lower(key);
    const std::string parent = Lower(parentKey);
    const std::string component = Lower(componentType);
    if (field == "model" || field.find("mesh") != std::string::npos) return Vans::VansAssetType::Model;
    if (field.find("material") != std::string::npos || parent.find("materialoverride") != std::string::npos)
        return Vans::VansAssetType::Material;
    if (field.find("texture") != std::string::npos || parent == "textures" ||
        field == "basecolor" || field == "normal" || field == "metal" || field == "roughness" || field == "ao")
        return Vans::VansAssetType::Texture;
    if (field == "source" && component == "audio") return Vans::VansAssetType::Audio;
    if (field == "source" && component == "video") return Vans::VansAssetType::Video;
    return Vans::VansAssetType::Unknown;
}

const char* AssetTypeName(Vans::VansAssetType type)
{
    switch (type)
    {
    case Vans::VansAssetType::Model: return "Model";
    case Vans::VansAssetType::Texture: return "Texture";
    case Vans::VansAssetType::Material: return "Material";
    case Vans::VansAssetType::Audio: return "Audio";
    case Vans::VansAssetType::Video: return "Video";
    case Vans::VansAssetType::Scene: return "Scene";
    default: return "Asset";
    }
}

const std::vector<const char*>* EnumOptions(const std::string& key)
{
    static const std::vector<const char*> bodyType{ "static", "dynamic", "kinematic" };
    static const std::vector<const char*> colliderType{ "box", "sphere", "capsule", "mesh", "convex" };
    static const std::vector<const char*> renderType{ "opaque", "transparent", "decal" };
    static const std::vector<const char*> rayTracingMode{ "auto", "enabled", "disabled" };
    static const std::vector<const char*> materialType{
        "pbr", "coat", "transparent", "skin", "cloth", "hair", "subsurface", "grass", "emissive", "decal" };
    static const std::vector<const char*> colorSpace{ "sRGB", "linear" };
    static const std::vector<const char*> playMode{ "static", "streaming" };
    static const std::vector<const char*> normals{ "ifMissing", "always", "never" };
    static const std::vector<const char*> axis{ "auto", "x", "y", "z", "-x", "-y", "-z" };
    static const std::vector<const char*> collision{ "none", "mesh", "convex" };
    static const std::vector<const char*> climbing{ "easy", "constrained" };
    static const std::vector<const char*> driveMode{ "animation", "physics", "blend" };
    const std::string field = Lower(key);
    if (field == "bodytype") return &bodyType;
    if (field == "collidertype") return &colliderType;
    if (field == "rendertype") return &renderType;
    if (field == "raytracingmode") return &rayTracingMode;
    if (field == "materialtype") return &materialType;
    if (field == "colorspace") return &colorSpace;
    if (field == "playmode") return &playMode;
    if (field == "generatenormals") return &normals;
    if (field == "sourceupaxis") return &axis;
    if (field == "collision") return &collision;
    if (field == "climbingmode") return &climbing;
    if (field == "drive_mode") return &driveMode;
    return nullptr;
}

Json DefaultComponentData(const std::string& type)
{
    if (type == "ModelRenderer") return { { "model", { { "guid", "" } } }, { "castShadows", true },
        { "receiveShadows", true }, { "rayTracingMode", "auto" }, { "visibilityMask", 0xffffffffu },
        { "materialOverrides", Json::object() }, { "orphanOverrides", Json::object() }, { "renderType", "opaque" } };
    if (type == "Physics") return { { "name", "Physics" }, { "bodyType", "static" },
        { "colliderType", "box" }, { "boxExtents", { 0.5f, 0.5f, 0.5f } }, { "mass", 1.0f },
        { "layer", "Default" }, { "isTrigger", false },
        { "material", { { "staticFriction", 0.5f }, { "dynamicFriction", 0.5f }, { "restitution", 0.0f } } } };
    if (type == "Camera") return { { "fov", 60.0f }, { "nearClip", 0.1f }, { "farClip", 1000.0f } };
    if (type == "CharacterController") return { { "radius", 0.5f }, { "height", 1.8f },
        { "slopeLimit", 0.707f }, { "stepOffset", 0.3f }, { "contactOffset", 0.08f },
        { "climbingMode", "easy" }, { "layer", "Default" }, { "positionOffset", { 0.0f, 0.9f, 0.0f } } };
    if (type == "DirectionalLight") return { { "color", { 1.0f, 1.0f, 1.0f } }, { "intensity", 1.0f } };
    if (type == "PointLight") return { { "color", { 1.0f, 1.0f, 1.0f } }, { "intensity", 1.0f }, { "radius", 10.0f } };
    if (type == "SpotLight") return { { "color", { 1.0f, 1.0f, 1.0f } }, { "intensity", 1.0f },
        { "radius", 10.0f }, { "innercutoff", 15.0f }, { "outerCutoff", 30.0f } };
    if (type == "RectLight") return { { "color", { 1.0f, 1.0f, 1.0f } }, { "intensity", 1.0f },
        { "width", 1.0f }, { "height", 1.0f }, { "range", 10.0f }, { "two_sided", false }, { "shadow", false } };
    if (type == "Audio" || type == "Video") return { { "source", { { "guid", "" } } } };
    if (type == "Particle") return { { "asset", "" }, { "play_on_awake", true } };
    if (type == "Script") return { { "path", "Scripts/" }, { "class", "" } };
    if (type == "Animation") return { { "name", "Animation" }, { "root_motion", false }, { "animator", "" } };
    if (type == "Cloth") return { { "profilePath", "" }, { "physicsAttachOffsetY", 0.0f } };
    if (type == "Vehicle") return { { "bodyObject", "" }, { "tireObjects", Json::array() } };
    return Json::object();
}

bool IsColorField(const std::string& key)
{
    const std::string field = Lower(key);
    return field.find("color") != std::string::npos || field == "albedo" ||
        field.find("emissive") != std::string::npos || field.find("tint") != std::string::npos;
}

bool IsNormalizedField(const std::string& key)
{
    const std::string field = Lower(key);
    return field == "metallic" || field == "roughness" || field == "ao" ||
        field == "opacity" || field == "alpha" || field.find("blend") != std::string::npos;
}

void BeginProperty(const std::string& label)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(FriendlyLabel(label).c_str());
    ImGui::SameLine(150.0f);
    ImGui::SetNextItemWidth(-1.0f);
}
}

bool VansInspectorWindow::DrawAssetReference(const std::string& label, Json& reference,
    const std::string& pointer, int expectedAssetTypeValue)
{
    const auto expectedType = static_cast<Vans::VansAssetType>(expectedAssetTypeValue);
    Vans::VansAssetDatabase* database = Vans::VansProjectManager::Get().GetAssetDatabase();
    if (!database || !reference.is_object()) return false;

    std::string guidText = reference.value("guid", "");
    std::string preview = "None (" + std::string(AssetTypeName(expectedType)) + ")";
    Vans::VansAssetGuid guid;
    bool missing = false;
    if (Vans::VansAssetGuid::TryParse(guidText, guid))
    {
        if (const auto record = database->Find(guid)) preview = record->sourcePath.filename().string();
        else { preview = "Missing: " + guidText.substr(0, 8); missing = true; }
    }

    BeginProperty(label);
    bool changed = false;
    ImGui::PushID(pointer.c_str());
    if (missing) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.3f, 1.0f));
    if (ImGui::BeginCombo("##asset", preview.c_str()))
    {
        static char search[128]{};
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##search", "Search assets...", search, sizeof(search));
        const std::string filter = Lower(search);
        if (ImGui::Selectable("None", guidText.empty()))
        {
            reference["guid"] = "";
            changed = true;
        }
        for (const Vans::VansAssetRecord& record : database->All())
        {
            if (record.type != expectedType || record.state == Vans::VansAssetState::Missing) continue;
            if (!filter.empty() && Lower(record.sourcePath.filename().string()).find(filter) == std::string::npos) continue;
            const std::string candidateGuid = record.guid.ToString();
            const std::string itemLabel = record.sourcePath.filename().string() + "##" + candidateGuid;
            const bool selected = candidateGuid == guidText;
            if (ImGui::Selectable(itemLabel.c_str(), selected))
            {
                reference["guid"] = candidateGuid;
                changed = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", record.sourcePath.string().c_str());
        }
        ImGui::EndCombo();
    }
    if (missing) ImGui::PopStyleColor();
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VANS_ASSET_GUID"))
        {
            const std::string dropped(static_cast<const char*>(payload->Data));
            Vans::VansAssetGuid droppedGuid;
            if (Vans::VansAssetGuid::TryParse(dropped, droppedGuid))
                if (const auto record = database->Find(droppedGuid); record && record->type == expectedType)
                { reference["guid"] = dropped; changed = true; }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::PopID();
    return changed;
}

bool VansInspectorWindow::DrawJsonValue(const std::string& label, Json& value,
    const std::string& pointer, bool readOnly, const std::string& componentType, const std::string& parentKey)
{
    ImGui::PushID(pointer.c_str());
    bool changed = false;
    const Vans::VansAssetType assetType = InferAssetType(label, parentKey, componentType);
    if (value.is_string() && assetType != Vans::VansAssetType::Unknown &&
        (value.get_ref<const std::string&>().empty() || IsGuidText(value.get_ref<const std::string&>())))
    {
        Json reference = { { "guid", value.get<std::string>() } };
        ImGui::PopID();
        if (DrawAssetReference(label, reference, pointer, static_cast<int>(assetType)))
        { value = reference.value("guid", ""); return true; }
        return false;
    }
    if (value.is_object() && value.contains("guid") && value["guid"].is_string() &&
        assetType != Vans::VansAssetType::Unknown)
    {
        ImGui::PopID();
        return DrawAssetReference(label, value, pointer, static_cast<int>(assetType));
    }

    if (value.is_object())
    {
        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
            ImGuiTreeNodeFlags_SpanAvailWidth;
        if (ImGui::TreeNodeEx(FriendlyLabel(label).c_str(), flags))
        {
            for (auto iterator = value.begin(); iterator != value.end(); ++iterator)
            {
                const std::string childPointer = pointer + "/" + EscapePointerToken(iterator.key());
                const bool identity = iterator.key() == "id" || iterator.key() == "guid" ||
                    iterator.key() == "sceneGuid" || iterator.key() == "schemaVersion" ||
                    iterator.key() == "version" || iterator.key() == "importer";
                changed |= DrawJsonValue(iterator.key(), iterator.value(), childPointer,
                    readOnly || identity, componentType, label);
            }
            ImGui::TreePop();
        }
    }
    else if (value.is_array() && value.size() >= 2 && value.size() <= 4 &&
        std::all_of(value.begin(), value.end(), [](const Json& item) { return item.is_number(); }))
    {
        std::array<float, 4> values{};
        for (std::size_t i = 0; i < value.size(); ++i) values[i] = value[i].get<float>();
        BeginProperty(label);
        if (readOnly)
            ImGui::TextDisabled("%s", value.dump().c_str());
        else if (Lower(label) == "rotation" && value.size() == 4)
        {
            const glm::quat quaternion(values[3], values[0], values[1], values[2]);
            glm::vec3 euler = glm::degrees(glm::eulerAngles(quaternion));
            if (ImGui::DragFloat3("##value", &euler.x, 0.25f, -360.0f, 360.0f, "%.2f"))
            {
                const glm::quat edited = glm::quat(glm::radians(euler));
                value = Json::array({ edited.x, edited.y, edited.z, edited.w });
                changed = true;
            }
        }
        else if (IsColorField(label) && (value.size() == 3 || value.size() == 4))
        {
            const bool edited = value.size() == 3
                ? ImGui::ColorEdit3("##value", values.data()) : ImGui::ColorEdit4("##value", values.data());
            if (edited)
            {
                for (std::size_t i = 0; i < value.size(); ++i) value[i] = values[i];
                changed = true;
            }
        }
        else
        {
            bool edited = false;
            if (value.size() == 2) edited = ImGui::DragFloat2("##value", values.data(), 0.05f);
            if (value.size() == 3) edited = ImGui::DragFloat3("##value", values.data(), 0.05f);
            if (value.size() == 4) edited = ImGui::DragFloat4("##value", values.data(), 0.05f);
            if (edited)
            {
                for (std::size_t i = 0; i < value.size(); ++i) value[i] = values[i];
                changed = true;
            }
        }
    }
    else if (value.is_array())
    {
        if (ImGui::TreeNodeEx(FriendlyLabel(label).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (std::size_t index = 0; index < value.size(); ++index)
                changed |= DrawJsonValue("Element " + std::to_string(index), value[index],
                    pointer + "/" + std::to_string(index), readOnly, componentType, label);
            ImGui::TreePop();
        }
    }
    else if (value.is_boolean())
    {
        bool edited = value.get<bool>();
        BeginProperty(label);
        if (readOnly) ImGui::TextDisabled(edited ? "Enabled" : "Disabled");
        else if (ImGui::Checkbox("##value", &edited)) { value = edited; changed = true; }
    }
    else if (value.is_number_integer() || value.is_number_unsigned())
    {
        std::int64_t edited = value.get<std::int64_t>();
        BeginProperty(label);
        if (readOnly) ImGui::TextDisabled("%lld", static_cast<long long>(edited));
        else
        {
            const std::int64_t step = 1;
            if (ImGui::InputScalar("##value", ImGuiDataType_S64, &edited, &step)) { value = edited; changed = true; }
        }
    }
    else if (value.is_number_float())
    {
        float edited = value.get<float>();
        BeginProperty(label);
        if (readOnly) ImGui::TextDisabled("%.4f", edited);
        else if (IsNormalizedField(label))
        {
            if (ImGui::SliderFloat("##value", &edited, 0.0f, 1.0f, "%.3f")) { value = edited; changed = true; }
        }
        else if (ImGui::DragFloat("##value", &edited, 0.05f, 0.0f, 0.0f, "%.3f"))
        { value = edited; changed = true; }
    }
    else if (value.is_string())
    {
        const std::string current = value.get<std::string>();
        BeginProperty(label);
        if (readOnly) ImGui::TextDisabled("%s", current.c_str());
        else if (Lower(label) == "layer")
        {
            auto& layers = VansEngine::VansCollisionLayerManager::Get();
            if (ImGui::BeginCombo("##value", current.c_str()))
            {
                for (int index = 0; index < layers.GetLayerCount(); ++index)
                {
                    const std::string& option = layers.GetLayerName(index);
                    if (ImGui::Selectable(option.c_str(), current == option)) { value = option; changed = true; }
                }
                ImGui::EndCombo();
            }
        }
        else if (const auto* options = EnumOptions(label))
        {
            if (ImGui::BeginCombo("##value", current.c_str()))
            {
                for (const char* option : *options)
                    if (ImGui::Selectable(option, current == option)) { value = option; changed = true; }
                ImGui::EndCombo();
            }
        }
        else
        {
            char buffer[1024]{};
            std::strncpy(buffer, current.c_str(), sizeof(buffer) - 1);
            if (ImGui::InputText("##value", buffer, sizeof(buffer)))
            { value = std::string(buffer); changed = true; }
        }
    }
    else if (value.is_null())
    {
        BeginProperty(label);
        ImGui::TextDisabled("None");
    }
    ImGui::PopID();
    return changed;
}

bool VansInspectorWindow::DrawComponent(Json& component, const std::string& pointer, bool& removeRequested)
{
    const std::string type = component.value("type", "Component");
    ImGui::PushID(pointer.c_str());
    bool enabled = component.value("enabled", true);
    bool changed = false;
    ImGui::Checkbox("##enabled", &enabled);
    if (enabled != component.value("enabled", true)) { component["enabled"] = enabled; changed = true; }
    ImGui::SameLine();
    const bool open = ImGui::CollapsingHeader(type.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::BeginPopupContextItem("ComponentMenu"))
    {
        if (type != "Transform" && ImGui::MenuItem("Remove Component")) removeRequested = true;
        ImGui::TextDisabled("ID: %s", component.value("id", "").c_str());
        ImGui::EndPopup();
    }
    if (open)
    {
        ImGui::Indent(8.0f);
        if (!component.contains("data") || !component["data"].is_object()) component["data"] = Json::object();
        Json& data = component["data"];
        for (auto iterator = data.begin(); iterator != data.end(); ++iterator)
        {
            if (iterator.key() == "materialOverrides" && iterator.value().is_object())
            {
                Json& overrides = iterator.value();
                if (overrides.empty())
                {
                    Json reference = { { "guid", "" } };
                    if (DrawAssetReference("Material 0", reference, pointer + "/data/materialOverrides/default",
                        static_cast<int>(Vans::VansAssetType::Material)) && !reference.value("guid", "").empty())
                    { overrides["default"] = std::move(reference); changed = true; }
                }
                else
                {
                    std::size_t slotIndex = 0;
                    for (auto slot = overrides.begin(); slot != overrides.end(); ++slot, ++slotIndex)
                        changed |= DrawAssetReference("Material " + std::to_string(slotIndex), slot.value(),
                            pointer + "/data/materialOverrides/" + EscapePointerToken(slot.key()),
                            static_cast<int>(Vans::VansAssetType::Material));
                }
                continue;
            }
            changed |= DrawJsonValue(iterator.key(), iterator.value(), pointer + "/data/" + EscapePointerToken(iterator.key()),
                false, type, "data");
        }
        if (data.empty()) ImGui::TextDisabled("No properties");
        ImGui::Unindent(8.0f);
    }
    ImGui::PopID();
    return changed;
}

void VansInspectorWindow::DrawSceneEntity()
{
    Vans::VansSceneDocument* document = VansEditorWindow::GetSceneDocument();
    Vans::VansSceneEditService* editor = VansEditorWindow::GetSceneEditService();
    if (!document || !editor) return;
    const std::string& selected = Vans::VansEditorSelection::EntityGuid();
    const auto& entities = document->Root()["entities"];
    for (std::size_t index = 0; index < entities.size(); ++index)
    {
        if (entities[index].value("id", "") != selected) continue;
        Json edited = entities[index];
        const std::string pointer = "/entities/" + std::to_string(index);
        bool changed = false;

        char name[256]{};
        std::strncpy(name, edited.value("name", "Entity").c_str(), sizeof(name) - 1);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##EntityName", name, sizeof(name)))
        { edited["name"] = std::string(name); changed = true; }
        ImGui::TextDisabled("Entity %s", selected.substr(0, 8).c_str());
        ImGui::Separator();

        if (edited.contains("components") && edited["components"].is_array())
        {
            for (std::size_t componentIndex = 0; componentIndex < edited["components"].size();)
            {
                bool remove = false;
                changed |= DrawComponent(edited["components"][componentIndex],
                    pointer + "/components/" + std::to_string(componentIndex), remove);
                if (remove) { edited["components"].erase(edited["components"].begin() + componentIndex); changed = true; }
                else ++componentIndex;
            }
        }

        if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f))) ImGui::OpenPopup("AddComponent");
        if (ImGui::BeginPopup("AddComponent"))
        {
            static const char* types[] = { "ModelRenderer", "Physics", "Camera", "Animation",
                "CharacterController", "DirectionalLight", "PointLight", "SpotLight", "RectLight",
                "Audio", "Video", "Particle", "Cloth", "Vehicle", "Script" };
            for (const char* type : types)
            {
                const bool singleton = std::strcmp(type, "ModelRenderer") == 0 || std::strcmp(type, "Physics") == 0;
                bool alreadyPresent = false;
                if (singleton)
                    for (const Json& component : edited["components"])
                        if (component.value("type", "") == type) { alreadyPresent = true; break; }
                if (alreadyPresent) ImGui::BeginDisabled();
                const bool selectedType = ImGui::Selectable(type);
                if (alreadyPresent) ImGui::EndDisabled();
                if (!selectedType || alreadyPresent) continue;
                Json data = DefaultComponentData(type);
                edited["components"].push_back({ { "id", Vans::VansComponentGuid::New().ToString() },
                    { "type", type }, { "version", 1u }, { "enabled", true }, { "data", std::move(data) } });
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (changed)
        {
            const Vans::SceneEditResult result = editor->Set(pointer, std::move(edited));
            if (!result) VANS_LOG_ERROR("[Inspector] " << result.message);
        }
        return;
    }
    ImGui::TextDisabled("Selected entity no longer exists");
}

void VansInspectorWindow::DrawSceneSettings()
{
    Vans::VansSceneDocument* document = VansEditorWindow::GetSceneDocument();
    Vans::VansSceneEditService* editor = VansEditorWindow::GetSceneEditService();
    if (!document || !editor) return;
    Json settings = document->Root().value("settings", Json::object());
    if (DrawJsonValue("Scene Settings", settings, "/settings"))
    {
        const Vans::SceneEditResult result = editor->Set("/settings", std::move(settings));
        if (!result) VANS_LOG_ERROR("[Inspector] " << result.message);
    }
}

bool VansInspectorWindow::LoadAssetDocuments(const std::filesystem::path& sourcePath)
{
    m_AssetPath = sourcePath;
    m_MetaPath = Vans::VansAssetMeta::MetaPathFor(sourcePath);
    m_Error.clear();
    std::string sourceError;
    m_AssetDocument.Load(m_AssetPath, sourceError);
    m_MetaDocument.Load(m_MetaPath, m_Error);
    if (!m_MetaDocument.IsLoaded() && !sourceError.empty()) m_Error = std::move(sourceError);
    return m_AssetDocument.IsLoaded() || m_MetaDocument.IsLoaded();
}

bool VansInspectorWindow::SaveAssetDocuments()
{
    m_Error.clear();
    if (!m_AssetDocument.Save(m_Error)) return false;
    if (!m_MetaDocument.Save(m_Error)) return false;
    if (auto* database = Vans::VansProjectManager::Get().GetAssetDatabase())
    {
        std::string refreshError;
        if (!database->RegisterOrRefresh(m_AssetPath, false, refreshError))
            VANS_LOG_ERROR("[Inspector] Asset refresh failed: " << refreshError);
    }
    VansEditorWindow::ReloadCurrentSceneForEditing();
    return true;
}

void VansInspectorWindow::DrawAsset()
{
    const std::filesystem::path& selected = Vans::VansEditorSelection::AssetPath();
    if (selected != m_AssetPath) LoadAssetDocuments(selected);
    ImGui::TextUnformatted(selected.filename().string().c_str());
    ImGui::TextDisabled("%s", selected.parent_path().string().c_str());
    ImGui::Separator();

    if (m_AssetDocument.IsLoaded())
    {
        Json& root = m_AssetDocument.Root();
        for (auto iterator = root.begin(); iterator != root.end(); ++iterator)
        {
            const bool identity = iterator.key() == "schemaVersion" || iterator.key() == "guid";
            if (DrawJsonValue(iterator.key(), iterator.value(), "/asset/" + EscapePointerToken(iterator.key()), identity))
                m_AssetDocument.MarkDirty();
        }
    }
    else ImGui::TextDisabled("Binary asset");

    if (m_MetaDocument.IsLoaded())
    {
        Json& meta = m_MetaDocument.Root();
        if (meta.contains("settings") && DrawJsonValue("Import Settings", meta["settings"], "/meta/settings"))
            m_MetaDocument.MarkDirty();
        if (ImGui::TreeNode("Asset Identity"))
        {
            if (meta.contains("guid")) DrawJsonValue("GUID", meta["guid"], "/meta/guid", true);
            if (meta.contains("importer")) DrawJsonValue("Importer", meta["importer"], "/meta/importer", true);
            ImGui::TreePop();
        }
    }
    if (!m_Error.empty()) ImGui::TextColored(ImVec4(1, 0.35f, 0.3f, 1), "%s", m_Error.c_str());

    const bool dirty = m_AssetDocument.IsDirty() || m_MetaDocument.IsDirty();
    if (!dirty) ImGui::BeginDisabled();
    if (ImGui::Button("Apply", ImVec2(-1.0f, 0.0f)))
        if (!SaveAssetDocuments()) VANS_LOG_ERROR("[Inspector] " << m_Error);
    if (!dirty) ImGui::EndDisabled();
}

void VansInspectorWindow::ShowWindow(VansVKDevice& device)
{
    (void)device;
    ImGui::Begin("Inspector");
    if (Vans::VansEditorSelection::IsSceneSelected()) DrawSceneSettings();
    else if (!Vans::VansEditorSelection::EntityGuid().empty()) DrawSceneEntity();
    else if (!Vans::VansEditorSelection::AssetPath().empty()) DrawAsset();
    else ImGui::TextDisabled("Select an entity or project asset");
    ImGui::End();
}
}
