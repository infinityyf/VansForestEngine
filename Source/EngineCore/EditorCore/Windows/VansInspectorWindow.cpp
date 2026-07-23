#include "VansInspectorWindow.h"

#include "../VansAssetDocumentEditService.h"
#include "../VansEditorAssetSaveService.h"
#include "../VansAssetReferenceSlotRegistry.h"
#include "../VansEditorSelection.h"
#include "../VansEditorWindow.h"
#include "../VansSceneEditService.h"
#include "../../AssetCore/VansAssetGuid.h"
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
#include <fstream>
#include <string>
#include <unordered_map>
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

const char* AssetTypeName(Vans::EditorAPI::AssetType type)
{
    switch (type)
    {
    case Vans::EditorAPI::AssetType::Model: return "Model";
    case Vans::EditorAPI::AssetType::Texture: return "Texture";
    case Vans::EditorAPI::AssetType::Material: return "Material";
    case Vans::EditorAPI::AssetType::Shader: return "Shader";
    case Vans::EditorAPI::AssetType::Audio: return "Audio";
    case Vans::EditorAPI::AssetType::Video: return "Video";
    case Vans::EditorAPI::AssetType::Scene: return "Scene";
    case Vans::EditorAPI::AssetType::Particle: return "Particle";
    case Vans::EditorAPI::AssetType::AnimationClip: return "Animation Clip";
    case Vans::EditorAPI::AssetType::AnimatorController: return "Animator Controller";
    case Vans::EditorAPI::AssetType::ClothProfile: return "Cloth Profile";
    case Vans::EditorAPI::AssetType::PostProcessProfile: return "Post Process Profile";
    case Vans::EditorAPI::AssetType::RagdollProfile: return "Ragdoll Profile";
    default: return "Asset";
    }
}

Vans::EditorAPI::AssetType ToEditorAssetType(int value)
{
    return static_cast<Vans::EditorAPI::AssetType>(value);
}

const std::vector<const char*>* EnumOptions(const std::string& key)
{
    static const std::vector<const char*> bodyType{ "static", "dynamic", "kinematic" };
    static const std::vector<const char*> colliderType{ "box", "sphere", "capsule", "mesh", "convex" };
    static const std::vector<const char*> renderType{ "opaque", "transparent", "decal" };
    static const std::vector<const char*> rayTracingMode{ "auto", "enabled", "disabled" };
    static const std::vector<const char*> materialType{
        "pbr", "coat", "transparent", "pbr_transmission", "skin", "cloth", "hair", "subsurface", "grass", "emissive", "decal" };
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
    if (type == "PointLight") return { { "color", { 1.0f, 1.0f, 1.0f } }, { "intensity", 1.0f },
        { "radius", 10.0f }, { "castShadows", true }, { "shadowPolicy", "Auto" },
        { "shadowPriority", 128 }, { "shadowResolution", "Auto" }, { "shadowUpdateMode", "OnChange" },
        { "shadowFallback", "ScreenSpace" }, { "shadowMaxDistance", 30.0f }, { "shadowNearPlane", 0.0f },
        { "shadowDepthBiasTexels", 1.0f }, { "shadowNormalBiasTexels", 1.0f },
        { "shadowSourceRadius", 0.02f }, { "shadowAffectsFog", true }, { "shadowAffectsGI", true } };
    if (type == "SpotLight") return { { "color", { 1.0f, 1.0f, 1.0f } }, { "intensity", 1.0f },
        { "radius", 10.0f }, { "innercutoff", 15.0f }, { "outerCutoff", 30.0f },
        { "castShadows", true }, { "shadowPolicy", "Auto" }, { "shadowPriority", 128 },
        { "shadowResolution", "Auto" }, { "shadowUpdateMode", "OnChange" }, { "shadowFallback", "ScreenSpace" },
        { "shadowMaxDistance", 30.0f }, { "shadowNearPlane", 0.0f }, { "shadowDepthBiasTexels", 1.0f },
        { "shadowNormalBiasTexels", 1.0f }, { "shadowSourceRadius", 0.02f },
        { "shadowAffectsFog", true }, { "shadowAffectsGI", true } };
    if (type == "RectLight") return { { "color", { 1.0f, 1.0f, 1.0f } }, { "intensity", 1.0f },
        { "width", 1.0f }, { "height", 1.0f }, { "range", 10.0f }, { "two_sided", false },
        { "castShadows", false }, { "shadowPolicy", "Auto" }, { "shadowPriority", 128 },
        { "shadowResolution", "Auto" }, { "shadowUpdateMode", "OnChange" }, { "shadowFallback", "ScreenSpace" },
        { "shadowMaxDistance", 30.0f }, { "shadowNearPlane", 0.0f }, { "shadowDepthBiasTexels", 1.0f },
        { "shadowNormalBiasTexels", 1.0f }, { "shadowSourceRadius", 0.02f },
        { "shadowAffectsFog", true }, { "shadowAffectsGI", true } };
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
        field == "opacity" || field == "alpha" || field == "alphacoverage" ||
        field == "transmission" || field.find("blend") != std::string::npos;
}

bool ShouldUseFloatControl(const std::string& label, const std::string& parentKey)
{
    const std::string parent = Lower(parentKey);
    const std::string field = Lower(label);
    if (parent.find("parameters") != std::string::npos)
        return field != "refractionmode";
    return false;
}

bool VehicleScalarLimits(const std::string& label, const std::string& componentType,
    float& minValue, float& maxValue, float& speed)
{
    if (Lower(componentType) != "vehicle")
        return false;

    const std::string field = Lower(label);
    if (field == "wheelradius" || field == "wheelhalfwidth")
    {
        minValue = 0.01f;
        maxValue = 2.0f;
        speed = 0.005f;
        return true;
    }
    if (field == "groundclearance" || field == "wheelvisualgroundclearance")
    {
        minValue = -0.5f;
        maxValue = 1.0f;
        speed = 0.005f;
        return true;
    }
    if (field == "suspensiontraveldist")
    {
        minValue = 0.01f;
        maxValue = 2.0f;
        speed = 0.005f;
        return true;
    }
    return false;
}

void BeginProperty(const std::string& label);

Json LoadShaderAssetFromMaterial(Vans::EditorAPI::IEngineEditorAPI& api, const Json& materialRoot)
{
    if (!materialRoot.is_object() || Lower(materialRoot.value("materialType", "")) != "customshader")
        return Json::object();
    if (!materialRoot.contains("shader") || !materialRoot["shader"].is_object())
        return Json::object();

    const std::string shaderGuidText = materialRoot["shader"].value("guid", "");
    const Vans::EditorAPI::AssetGuidResolution shaderAsset = api.ResolveAssetGuid(shaderGuidText);
    if (!shaderAsset.found || shaderAsset.asset.type != Vans::EditorAPI::AssetType::Shader)
        return Json::object();

    std::ifstream shaderInput(shaderAsset.sourcePath);
    Json shader = shaderInput ? Json::parse(shaderInput, nullptr, false) : Json();
    return shader.is_object() ? shader : Json::object();
}

void MergeCustomShaderParameterSchema(Vans::EditorAPI::IEngineEditorAPI& api, Json& materialRoot)
{
    Json shader = LoadShaderAssetFromMaterial(api, materialRoot);
    if (!shader.contains("parameters") || !shader["parameters"].is_object())
        return;

    if (!materialRoot.contains("parameters") || !materialRoot["parameters"].is_object())
        materialRoot["parameters"] = Json::object();

    for (const auto& [name, schema] : shader["parameters"].items())
    {
        if (!schema.is_object())
            continue;

        Json& materialParameter = materialRoot["parameters"][name];
        if (!materialParameter.is_object())
        {
            Json value = materialParameter;
            materialParameter = Json::object({ { "value", value } });
        }

        for (const auto& [field, schemaValue] : schema.items())
        {
            if (!materialParameter.contains(field))
                materialParameter[field] = schemaValue;
        }
        if (!materialParameter.contains("value") && schema.contains("default"))
            materialParameter["value"] = schema["default"];
    }
}

float ReadFloatOr(const Json& value, float fallback)
{
    return value.is_number() ? value.get<float>() : fallback;
}

bool DrawTypedMaterialParameter(const std::string& label, Json& parameter, bool readOnly)
{
    if (!parameter.is_object())
        return false;

    if (!parameter.contains("value"))
    {
        if (parameter.contains("default"))
            parameter["value"] = parameter["default"];
        else
            return false;
    }

    Json& value = parameter["value"];
    const std::string type = Lower(parameter.value("type", ""));
    bool changed = false;
    BeginProperty(label);

    if (readOnly)
    {
        ImGui::TextDisabled("%s", value.dump().c_str());
        return false;
    }

    if ((type == "color" || (IsColorField(label) && (type == "vec3" || type == "vec4" || type.empty()))) &&
        value.is_array() && (value.size() == 3 || value.size() == 4))
    {
        std::array<float, 4> color{};
        for (std::size_t i = 0; i < value.size(); ++i) color[i] = value[i].get<float>();
        const bool edited = value.size() == 3
            ? ImGui::ColorEdit3("##value", color.data()) : ImGui::ColorEdit4("##value", color.data());
        if (edited)
        {
            for (std::size_t i = 0; i < value.size(); ++i) value[i] = color[i];
            changed = true;
        }
    }
    else if (((type == "vec2" || type == "vec3" || type == "vec4") && value.is_array()) ||
        (type.empty() && value.is_array() && value.size() >= 2 && value.size() <= 4 &&
            std::all_of(value.begin(), value.end(), [](const Json& item) { return item.is_number(); })))
    {
        std::array<float, 4> values{};
        const int count = type == "vec2" ? 2 : (type == "vec3" ? 3 : (type == "vec4" ? 4 : static_cast<int>(value.size())));
        while (value.size() < static_cast<std::size_t>(count)) value.push_back(0.0f);
        for (int i = 0; i < count; ++i) values[i] = value[i].get<float>();
        bool edited = false;
        if (count == 2) edited = ImGui::DragFloat2("##value", values.data(), 0.01f);
        if (count == 3) edited = ImGui::DragFloat3("##value", values.data(), 0.01f);
        if (count == 4) edited = ImGui::DragFloat4("##value", values.data(), 0.01f);
        if (edited)
        {
            for (int i = 0; i < count; ++i) value[i] = values[i];
            changed = true;
        }
    }
    else if (type == "bool" && value.is_boolean())
    {
        bool edited = value.get<bool>();
        if (ImGui::Checkbox("##value", &edited)) { value = edited; changed = true; }
    }
    else if ((type == "int" || type == "uint") && value.is_number_integer())
    {
        int edited = value.get<int>();
        if (ImGui::InputInt("##value", &edited)) { value = edited; changed = true; }
    }
    else if (value.is_number())
    {
        float edited = value.get<float>();
        const float minValue = ReadFloatOr(parameter.contains("min") ? parameter["min"] : Json(), 0.0f);
        const float maxValue = ReadFloatOr(parameter.contains("max") ? parameter["max"] : Json(), IsNormalizedField(label) ? 1.0f : 0.0f);
        if (maxValue > minValue)
        {
            if (ImGui::SliderFloat("##value", &edited, minValue, maxValue, "%.3f"))
            { value = edited; changed = true; }
        }
        else if (ImGui::DragFloat("##value", &edited, 0.01f, 0.0f, 0.0f, "%.3f"))
        { value = edited; changed = true; }
    }
    else
    {
        ImGui::TextDisabled("%s", value.dump().c_str());
    }
    return changed;
}

void BeginProperty(const std::string& label)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(FriendlyLabel(label).c_str());
    ImGui::SameLine(150.0f);
    ImGui::SetNextItemWidth(-1.0f);
}

Json* FindComponent(Json& entity, const std::string& type)
{
    if (!entity.contains("components") || !entity["components"].is_array()) return nullptr;
    for (Json& component : entity["components"])
        if (component.value("type", "") == type) return &component;
    return nullptr;
}

const Json* FindComponent(const Json& entity, const std::string& type)
{
    if (!entity.contains("components") || !entity["components"].is_array()) return nullptr;
    for (const Json& component : entity["components"])
        if (component.value("type", "") == type) return &component;
    return nullptr;
}

}

bool VansInspectorWindow::DrawAssetReference(const std::string& label, Json& reference,
    const std::string& pointer, int expectedAssetTypeValue, bool writeObjectReference)
{
    const auto expectedType = ToEditorAssetType(expectedAssetTypeValue);
    if (!m_ActiveAPI || !reference.is_object()) return false;

    auto assignGuid = [&](std::string guid)
    {
        reference["guid"] = guid;
        m_PendingAssetReferenceEdit = PendingAssetReferenceEdit{
            pointer,
            std::move(guid),
            expectedAssetTypeValue,
            writeObjectReference
        };
    };

    std::string guidText = reference.value("guid", "");
    std::string preview = "None (" + std::string(AssetTypeName(expectedType)) + ")";
    bool missing = false;
    if (!guidText.empty())
    {
        const Vans::EditorAPI::AssetGuidResolution resolved = m_ActiveAPI->ResolveAssetGuid(guidText);
        if (resolved.found)
            preview = resolved.asset.name;
        else
        {
            preview = "Missing: " + guidText.substr(0, 8);
            missing = true;
        }
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
            assignGuid("");
            changed = true;
        }
        Vans::EditorAPI::AssetTypeFilter assetFilter;
        assetFilter.type = expectedType;
        for (const Vans::EditorAPI::AssetEntry& asset : m_ActiveAPI->QueryAssets(assetFilter))
        {
            if (!filter.empty() && Lower(asset.name).find(filter) == std::string::npos) continue;
            const std::string candidateGuid = asset.guid;
            if (candidateGuid.empty()) continue;
            const std::string itemLabel = asset.name + "##" + candidateGuid;
            const bool selected = candidateGuid == guidText;
            if (ImGui::Selectable(itemLabel.c_str(), selected))
            {
                assignGuid(candidateGuid);
                changed = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", asset.relativePath.c_str());
        }
        ImGui::EndCombo();
    }
    if (missing) ImGui::PopStyleColor();
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VANS_ASSET_GUID"))
        {
            const std::string dropped(static_cast<const char*>(payload->Data));
            const Vans::EditorAPI::AssetGuidResolution resolved = m_ActiveAPI->ResolveAssetGuid(dropped);
            if (resolved.found && resolved.asset.type == expectedType)
            {
                assignGuid(dropped);
                changed = true;
            }
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
    const Vans::AssetReferenceSlotDescriptor slotDescriptor =
        Vans::VansAssetReferenceSlotRegistry::Resolve(componentType, parentKey, label);
    const Vans::EditorAPI::AssetType assetType = slotDescriptor.expectedType;
    const std::string* stringValue = value.is_string() ? &value.get_ref<const std::string&>() : nullptr;
    const bool isEmptyOrKnownGuid = stringValue &&
        (stringValue->empty() || (m_ActiveAPI && m_ActiveAPI->ResolveAssetGuid(*stringValue).found));
    if (value.is_string() && assetType != Vans::EditorAPI::AssetType::Unknown &&
        isEmptyOrKnownGuid)
    {
        Json reference = { { "guid", value.get<std::string>() } };
        ImGui::PopID();
        if (DrawAssetReference(label, reference, pointer, static_cast<int>(assetType), false))
        { value = reference.value("guid", ""); return true; }
        return false;
    }
    if (value.is_object() && value.contains("guid") && value["guid"].is_string() &&
        assetType != Vans::EditorAPI::AssetType::Unknown)
    {
        ImGui::PopID();
        return DrawAssetReference(label, value, pointer, static_cast<int>(assetType));
    }

    const std::string loweredParentKey = Lower(parentKey);
    if (value.is_object() && loweredParentKey.find("parameters") != std::string::npos &&
        (value.contains("value") || value.contains("default")))
    {
        changed = DrawTypedMaterialParameter(label, value, readOnly);
        ImGui::PopID();
        return changed;
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
        else if (IsNormalizedField(label))
        {
            float normalized = static_cast<float>(edited);
            if (ImGui::SliderFloat("##value", &normalized, 0.0f, 1.0f, "%.3f"))
            {
                value = normalized;
                changed = true;
            }
        }
        else if (ShouldUseFloatControl(label, parentKey))
        {
            float numeric = static_cast<float>(edited);
            if (ImGui::DragFloat("##value", &numeric, 0.05f, 0.0f, 0.0f, "%.3f"))
            {
                value = numeric;
                changed = true;
            }
        }
        else
        {
            const std::int64_t step = 1;
            if (ImGui::InputScalar("##value", ImGuiDataType_S64, &edited, &step)) { value = edited; changed = true; }
        }
    }
    else if (value.is_number_float())
    {
        float edited = value.get<float>();
        float minValue = 0.0f;
        float maxValue = 0.0f;
        float speed = 0.05f;
        BeginProperty(label);
        if (readOnly) ImGui::TextDisabled("%.4f", edited);
        else if (VehicleScalarLimits(label, componentType, minValue, maxValue, speed))
        {
            if (ImGui::DragFloat("##value", &edited, speed, minValue, maxValue, "%.3f"))
            {
                edited = std::clamp(edited, minValue, maxValue);
                value = edited;
                changed = true;
            }
        }
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
            if (ImGui::BeginCombo("##value", current.c_str()))
            {
                for (const std::string& option : m_CollisionLayerNames)
                {
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

bool VansInspectorWindow::DrawComponent(Vans::EditorAPI::IEngineEditorAPI& api, Json& component,
    const std::string& pointer, bool& removeRequested)
{
    const std::string type = component.value("type", "Component");
    ImGui::PushID(pointer.c_str());
    bool enabled = component.value("enabled", true);
    bool changed = false;
    if (ImGui::Checkbox("##enabled", &enabled))
    {
        if (enabled != component.value("enabled", true))
        {
            component["enabled"] = enabled;
            changed = true;

            // 连线到运行时：将 enabled 状态同步到 VansScriptComponent → VansNode
            ApplyComponentEnabled(api, type, enabled);
        }
    }
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
                        static_cast<int>(Vans::EditorAPI::AssetType::Material)) && !reference.value("guid", "").empty())
                    { overrides["default"] = std::move(reference); changed = true; }
                }
                else
                {
                    std::size_t slotIndex = 0;
                    for (auto slot = overrides.begin(); slot != overrides.end(); ++slot, ++slotIndex)
                        changed |= DrawAssetReference("Material " + std::to_string(slotIndex), slot.value(),
                            pointer + "/data/materialOverrides/" + EscapePointerToken(slot.key()),
                            static_cast<int>(Vans::EditorAPI::AssetType::Material));
                }
                continue;
            }
            changed |= DrawJsonValue(iterator.key(), iterator.value(), pointer + "/data/" + EscapePointerToken(iterator.key()),
                false, type, "data");
        }
        if (data.empty()) ImGui::TextDisabled("No properties");
        ImGui::Unindent(8.0f);
    }
    if (type == "Vehicle" && changed)
        m_PendingVehicleRebuild = true;
    ImGui::PopID();
    return changed;
}

void VansInspectorWindow::DrawSceneEntity(Vans::EditorAPI::IEngineEditorAPI& api)
{
    Vans::VansSceneDocument* document = VansEditorWindow::GetSceneDocument();
    Vans::VansSceneEditService* editor = VansEditorWindow::GetSceneEditService();
    if (!document || !editor) return;
    const std::string& selected = Vans::VansEditorSelection::EntityGuid();
    const auto& entities = document->Root()["entities"];
    for (std::size_t index = 0; index < entities.size(); ++index)
    {
        if (entities[index].value("id", "") != selected) continue;
        m_PendingAssetReferenceEdit.reset();
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
                changed |= DrawComponent(api, edited["components"][componentIndex],
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
            Json runtimeEdited = edited;
            Vans::SceneEditResult result;
            if (m_PendingAssetReferenceEdit)
            {
                const PendingAssetReferenceEdit edit = *m_PendingAssetReferenceEdit;
                result = editor->AssignAssetReference(edit.pointer,
                    edit.guid,
                    static_cast<Vans::EditorAPI::AssetType>(edit.expectedAssetType),
                    edit.writeObjectReference);
                m_PendingAssetReferenceEdit.reset();
            }
            else
            {
                result = editor->Set(pointer, std::move(edited));
            }
            if (!result) VANS_LOG_ERROR("[Inspector] " << result.message);
            else api.ApplyRuntimeEntityPatchJson(runtimeEdited.dump());
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
    m_AssetDocuments = Vans::VansAssetDocumentRegistry::Get().GetOrOpen(sourcePath);
    m_Error = m_AssetDocuments ? m_AssetDocuments->lastError : "Cannot open asset document";
    return m_AssetDocuments &&
        (m_AssetDocuments->sourceDocument.IsLoaded() || m_AssetDocuments->metaDocument.IsLoaded());
}

bool VansInspectorWindow::SaveAssetDocuments(bool reloadSceneOnSuccess)
{
    m_Error.clear();
    if (!m_ActiveAPI)
    {
        m_Error = "Engine editor API is not available";
        return false;
    }

    const Vans::VansAssetSaveResult result =
        Vans::VansEditorAssetSaveService::Get().SaveAsset(*m_ActiveAPI, m_AssetDocuments);
    if (!result)
    {
        m_Error = result.message;
        for (const std::string& error : result.errors)
            VANS_LOG_ERROR("[AssetSave] " << error);
        return false;
    }
    if (reloadSceneOnSuccess && result.wroteFile)
        VansEditorWindow::ReloadCurrentSceneForEditing();
    return true;
}

void VansInspectorWindow::DrawAsset(Vans::EditorAPI::IEngineEditorAPI& api)
{
    const std::filesystem::path& selected = Vans::VansEditorSelection::AssetPath();
    if (selected != m_AssetPath) LoadAssetDocuments(selected);
    ImGui::TextUnformatted(selected.filename().string().c_str());
    ImGui::TextDisabled("%s", selected.parent_path().string().c_str());
    if (m_AssetDocuments)
    {
        ImGui::TextDisabled("Source: %s  Meta: %s",
            m_AssetDocuments->sourceDocument.IsDirty() ? "Dirty" : "Clean",
            m_AssetDocuments->metaDocument.IsDirty() ? "Dirty" : "Clean");
    }
    ImGui::Separator();

    if (m_AssetDocuments && m_AssetDocuments->sourceDocument.IsLoaded())
    {
        Json& root = m_AssetDocuments->sourceDocument.Root();
        MergeCustomShaderParameterSchema(api, root);
        for (auto iterator = root.begin(); iterator != root.end(); ++iterator)
        {
            const std::string propertyKey = iterator.key();
            const std::string propertyPointer = "/asset/" + EscapePointerToken(propertyKey);
            const bool identity = propertyKey == "schemaVersion" || propertyKey == "guid";
            const Json beforeEditRoot = root;
            if (DrawJsonValue(propertyKey, iterator.value(), propertyPointer, identity))
            {
                bool editApplied = false;
                bool rootRestored = false;
                if (m_PendingAssetReferenceEdit)
                {
                    const PendingAssetReferenceEdit edit = *m_PendingAssetReferenceEdit;
                    root = beforeEditRoot;
                    rootRestored = true;
                    const Vans::AssetDocumentEditResult result =
                        Vans::VansAssetDocumentEditService::SetAssetReference(
                            m_AssetDocuments->sourceDocument,
                            edit.pointer,
                            edit.guid,
                            edit.writeObjectReference);
                    if (!result)
                        VANS_LOG_ERROR("[Inspector] " << result.message);
                    editApplied = static_cast<bool>(result);
                    m_PendingAssetReferenceEdit.reset();
                }
                else
                {
                    m_AssetDocuments->sourceDocument.MarkDirty();
                    editApplied = true;
                }

                if (editApplied)
                    api.ApplyRuntimeMaterialAssetPatch(
                        selected.string(),
                        root.dump(),
                        propertyPointer);
                if (rootRestored)
                    break;
            }
        }
    }
    else ImGui::TextDisabled("Binary asset");

    if (m_AssetDocuments && m_AssetDocuments->metaDocument.IsLoaded())
    {
        Json& meta = m_AssetDocuments->metaDocument.Root();
        if (meta.contains("settings") && DrawJsonValue("Import Settings", meta["settings"], "/meta/settings"))
            m_AssetDocuments->metaDocument.MarkDirty();
        if (ImGui::TreeNode("Asset Identity"))
        {
            if (meta.contains("guid")) DrawJsonValue("GUID", meta["guid"], "/meta/guid", true);
            if (meta.contains("importer")) DrawJsonValue("Importer", meta["importer"], "/meta/importer", true);
            ImGui::TreePop();
        }
    }
    if (!m_Error.empty()) ImGui::TextColored(ImVec4(1, 0.35f, 0.3f, 1), "%s", m_Error.c_str());

    const bool dirty = m_AssetDocuments && m_AssetDocuments->IsDirty();
    if (!dirty) ImGui::BeginDisabled();
    if (ImGui::Button("Apply", ImVec2(-1.0f, 0.0f)))
        if (!SaveAssetDocuments()) VANS_LOG_ERROR("[Inspector] " << m_Error);
    if (!dirty) ImGui::EndDisabled();
}

void VansInspectorWindow::ApplyComponentEnabled(
    Vans::EditorAPI::IEngineEditorAPI& api, const std::string& componentType, bool enabled)
{
    const std::string& selectedGuid = Vans::VansEditorSelection::EntityGuid();
    api.SetRuntimeComponentEnabled(selectedGuid, componentType, enabled);
}

void VansInspectorWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& api)
{
    ImGui::Begin("Inspector");
    m_ActiveAPI = &api;
    m_CollisionLayerNames = api.GetRuntimeCollisionLayerNames();
    m_PendingAssetReferenceEdit.reset();
    if (Vans::VansEditorSelection::IsSceneSelected()) DrawSceneSettings();
    else if (!Vans::VansEditorSelection::EntityGuid().empty()) DrawSceneEntity(api);
    else if (!Vans::VansEditorSelection::AssetPath().empty()) DrawAsset(api);
    else ImGui::TextDisabled("Select an entity or project asset");
    ImGui::End();
    m_ActiveAPI = nullptr;

    if (m_PendingVehicleRebuild && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        m_PendingVehicleRebuild = false;
        VansEditorWindow::ReloadCurrentSceneForEditing();
    }
}
}
