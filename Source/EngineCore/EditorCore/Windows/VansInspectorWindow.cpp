#include "VansInspectorWindow.h"

#include "../VansAssetDocumentEditService.h"
#include "../VansAssetDocumentRegistry.h"
#include "../VansEditorAssetSaveService.h"
#include "../VansEditorMaterialSchemaService.h"
#include "../VansEditorObjectReference.h"
#include "../VansEditorPropertyDescriptorRegistry.h"
#include "../VansEditorSelection.h"
#include "../VansEditorRuntimePreviewProjector.h"
#include "../VansEditorWindow.h"
#include "../VansSceneEditService.h"
#include "../VansSceneObjectReferenceResolver.h"
#include "../../AudioCore/VansAudioPreviewPlayer.h"
#include "../../AssetCore/VansAssetGuid.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../SceneCore/VansSceneDocument.h"
#include "../../ScriptCore/VansLuaScriptInspectorService.h"
#include "../../Util/VansLog.h"

#include "imgui.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#include <../../GLM/gtx/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace VansGraphics
{
namespace
{
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
    case Vans::EditorAPI::AssetType::AudioReverbPreset: return "Audio Reverb Preset";
    case Vans::EditorAPI::AssetType::AudioBusSnapshot: return "Audio Bus Snapshot";
    case Vans::EditorAPI::AssetType::AudioDuckingRules: return "Audio Ducking Rules";
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
        "pbr", "pbr_emissive", "coat", "transparent", "pbr_transmission", "skin", "cloth", "hair", "subsurface", "grass", "emissive", "decal" };
    static const std::vector<const char*> colorSpace{ "sRGB", "linear" };
    static const std::vector<const char*> playMode{ "static", "streaming" };
    static const std::vector<const char*> attenuationMode{ "linear", "inverse", "exponential" };
    static const std::vector<const char*> audioBus{ "SFX", "Music", "UI", "Ambient", "Voice", "Master" };
    static const std::vector<const char*> audioControlKind{
        "AudioReverbPreset", "AudioBusSnapshot", "AudioDuckingRules"
    };
    static const std::vector<const char*> reverbPreset{
        "generic", "room", "hall", "cave", "underwater"
    };
    static const std::vector<const char*> audioOcclusionMaterial{
        "custom", "thin", "wood", "stone", "metal", "glass", "fabric"
    };
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
    if (field == "attenuationmode") return &attenuationMode;
    if (field == "bus" || field == "trigger" || field == "target" ||
        field == "triggerbus" || field == "targetbus")
        return &audioBus;
    if (field == "assetkind") return &audioControlKind;
    if (field == "preset") return &reverbPreset;
    if (field == "occlusionmaterial") return &audioOcclusionMaterial;
    if (field == "generatenormals") return &normals;
    if (field == "sourceupaxis") return &axis;
    if (field == "collision") return &collision;
    if (field == "climbingmode") return &climbing;
    if (field == "drive_mode") return &driveMode;
    return nullptr;
}

const std::vector<const char*>* AudioReverbZoneShapeOptions(
    const std::string& label,
    const std::string& componentType)
{
    static const std::vector<const char*> reverbZoneShape{ "sphere", "box" };
    static const std::vector<const char*> reverbZonePreset{
        "generic", "room", "hall", "cave", "underwater"
    };
    const std::string loweredComponent = Lower(componentType);
    if ((loweredComponent == "audioreverbzone" || loweredComponent == "audiovolume") &&
        Lower(label) == "shape")
        return &reverbZoneShape;
    if ((loweredComponent == "audioreverbzone" || loweredComponent == "audiovolume") &&
        Lower(label) == "preset")
        return &reverbZonePreset;
    return nullptr;
}

Vans::VansSerializedValue DefaultSerializedComponentData(const std::string& type)
{
    using Value = Vans::VansSerializedValue;
    auto guidReference = []()
    {
        return Value::Object({ { "guid", Value::String("") } });
    };
    auto vec3 = [](double x, double y, double z)
    {
        return Value::Array({ Value::Float(x), Value::Float(y), Value::Float(z) });
    };
    auto shadowFields = []()
    {
        return std::vector<std::pair<std::string, Value>>{
            { "castShadows", Value::Bool(true) },
            { "shadowPolicy", Value::String("Auto") },
            { "shadowPriority", Value::Int(128) },
            { "shadowResolution", Value::String("Auto") },
            { "shadowUpdateMode", Value::String("OnChange") },
            { "shadowFallback", Value::String("ScreenSpace") },
            { "shadowMaxDistance", Value::Float(30.0) },
            { "shadowNearPlane", Value::Float(0.0) },
            { "shadowDepthBiasTexels", Value::Float(1.0) },
            { "shadowNormalBiasTexels", Value::Float(1.0) },
            { "shadowSourceRadius", Value::Float(0.02) },
            { "shadowAffectsFog", Value::Bool(true) },
            { "shadowAffectsGI", Value::Bool(true) },
            { "shadowCasterMask", Value::Int(0xffffffff) }
        };
    };

    if (type == "ModelRenderer")
        return Value::Object({
            { "model", guidReference() },
            { "castShadows", Value::Bool(true) },
            { "receiveShadows", Value::Bool(true) },
            { "rayTracingMode", Value::String("auto") },
            { "visibilityMask", Value::Int(0xffffffff) },
            { "shadowCasterMask", Value::Int(0xffffffff) },
            { "materialOverrides", Value::Object({}) },
            { "orphanOverrides", Value::Object({}) },
            { "renderType", Value::String("opaque") }
        });
    if (type == "Physics")
        return Value::Object({
            { "name", Value::String("Physics") },
            { "bodyType", Value::String("static") },
            { "colliderType", Value::String("box") },
            { "boxExtents", vec3(0.5, 0.5, 0.5) },
            { "mass", Value::Float(1.0) },
            { "layer", Value::String("Default") },
            { "isTrigger", Value::Bool(false) },
            { "material", Value::Object({
                { "staticFriction", Value::Float(0.5) },
                { "dynamicFriction", Value::Float(0.5) },
                { "restitution", Value::Float(0.0) }
            }) }
        });
    if (type == "Camera")
        return Value::Object({
            { "fov", Value::Float(60.0) },
            { "nearClip", Value::Float(0.1) },
            { "farClip", Value::Float(1000.0) }
        });
    if (type == "CharacterController")
        return Value::Object({
            { "radius", Value::Float(0.5) },
            { "height", Value::Float(1.8) },
            { "slopeLimit", Value::Float(0.707) },
            { "stepOffset", Value::Float(0.3) },
            { "contactOffset", Value::Float(0.08) },
            { "climbingMode", Value::String("easy") },
            { "layer", Value::String("Default") },
            { "positionOffset", vec3(0.0, 0.9, 0.0) }
        });
    if (type == "DirectionalLight")
        return Value::Object({
            { "color", vec3(1.0, 1.0, 1.0) },
            { "intensity", Value::Float(1.0) }
        });
    if (type == "PointLight" || type == "SpotLight")
    {
        std::vector<std::pair<std::string, Value>> fields{
            { "color", vec3(1.0, 1.0, 1.0) },
            { "intensity", Value::Float(1.0) },
            { "radius", Value::Float(10.0) }
        };
        if (type == "SpotLight")
        {
            fields.emplace_back("innercutoff", Value::Float(15.0));
            fields.emplace_back("outerCutoff", Value::Float(30.0));
        }
        std::vector<std::pair<std::string, Value>> shadows = shadowFields();
        fields.insert(fields.end(), shadows.begin(), shadows.end());
        return Value::Object(std::move(fields));
    }
    if (type == "RectLight")
    {
        std::vector<std::pair<std::string, Value>> fields{
            { "color", vec3(1.0, 1.0, 1.0) },
            { "intensity", Value::Float(1.0) },
            { "width", Value::Float(1.0) },
            { "height", Value::Float(1.0) },
            { "range", Value::Float(10.0) },
            { "two_sided", Value::Bool(false) }
        };
        std::vector<std::pair<std::string, Value>> shadows = shadowFields();
        for (auto& [fieldName, fieldValue] : shadows)
        {
            if (fieldName == "castShadows")
                fieldValue = Value::Bool(false);
        }
        fields.insert(fields.end(), shadows.begin(), shadows.end());
        return Value::Object(std::move(fields));
    }
    if (type == "Audio")
        return Value::Object({
            { "source", guidReference() },
            { "occlusionEnabled", Value::Bool(false) },
            { "occlusionGain", Value::Float(0.45) },
            { "occlusionHighFrequencyGain", Value::Float(0.35) },
            { "occlusionMaterial", Value::String("custom") },
            { "occlusionMaterialThickness", Value::Float(1.0) },
            { "occlusionAttack", Value::Float(0.08) },
            { "occlusionRelease", Value::Float(0.18) },
            { "occlusionQueryInterval", Value::Float(0.12) },
            { "occlusionMaxDistance", Value::Float(100.0) },
            { "occlusionMaxQueriesPerFrame", Value::Int(4) },
            { "coneEnabled", Value::Bool(false) },
            { "coneInnerAngle", Value::Float(360.0) },
            { "coneOuterAngle", Value::Float(360.0) },
            { "coneOuterGain", Value::Float(1.0) },
            { "dopplerEnabled", Value::Bool(false) }
        });
    if (type == "Video")
        return Value::Object({ { "source", guidReference() } });
    if (type == "AudioReverbZone" || type == "AudioVolume")
        return Value::Object({
            { "shape", Value::String("sphere") },
            { "preset", Value::String("generic") },
            { "presetAsset", guidReference() },
            { "radius", Value::Float(8.0) },
            { "halfExtents", vec3(4.0, 4.0, 4.0) },
            { "fadeDistance", Value::Float(2.0) },
            { "wetGain", Value::Float(0.6) },
            { "priority", Value::Int(0) },
            { "overridePresetParameters", Value::Bool(false) },
            { "density", Value::Float(1.0) },
            { "diffusion", Value::Float(1.0) },
            { "gain", Value::Float(0.32) },
            { "gainHF", Value::Float(0.89) },
            { "decayTime", Value::Float(1.49) }
        });
    if (type == "Particle")
        return Value::Object({
            { "asset", Value::String("") },
            { "play_on_awake", Value::Bool(true) }
        });
    if (type == "Script")
        return Value::Object({
            { "language", Value::String("lua") },
            { "path", Value::String("Scripts/") },
            { "entry", Value::String("") },
            { "fields", Value::Object({}) }
        });
    if (type == "Animation")
        return Value::Object({
            { "name", Value::String("Animation") },
            { "root_motion", Value::Bool(false) },
            { "animator", Value::String("") }
        });
    if (type == "Cloth")
        return Value::Object({
            { "profilePath", Value::String("") },
            { "physicsAttachOffsetY", Value::Float(0.0) }
        });
    if (type == "Vehicle")
        return Value::Object({
            { "bodyObject", Value::String("") },
            { "tireObjects", Value::Array({}) }
        });
    return Value::Object({});
}

Vans::VansSerializedValue MakeSerializedComponent(const std::string& type)
{
    return Vans::VansSerializedValue::Object({
        { "id", Vans::VansSerializedValue::String(Vans::VansComponentGuid::New().ToString()) },
        { "type", Vans::VansSerializedValue::String(type) },
        { "version", Vans::VansSerializedValue::Int(1) },
        { "enabled", Vans::VansSerializedValue::Bool(true) },
        { "data", DefaultSerializedComponentData(type) }
    });
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
        field == "transmission" || field == "subsurfaceamount" ||
        field.find("blend") != std::string::npos;
}

bool MaterialScalarLimits(const std::string& label, const std::string& parentKey,
    float& minValue, float& maxValue, float& speed)
{
    if (Lower(parentKey).find("parameters") == std::string::npos)
        return false;

    const std::string field = Lower(label);
    if (field == "ior")
    {
        minValue = 1.0f;
        maxValue = 2.5f;
        speed = 0.01f;
        return true;
    }
    if (field == "scatteringdistance" || field == "subsurfacepower")
    {
        minValue = 0.01f;
        maxValue = 100.0f;
        speed = 0.1f;
        return true;
    }
    if (field == "thickness")
    {
        minValue = 0.0f;
        maxValue = 100.0f;
        speed = 0.1f;
        return true;
    }
    return false;
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

float ReadSerializedFloatOr(const Vans::VansSerializedValue* value, float fallback)
{
    return value && (value->kind == Vans::VansSerializedValue::Kind::Int ||
        value->kind == Vans::VansSerializedValue::Kind::Float)
        ? static_cast<float>(Vans::ReadSerializedNumber(*value))
        : fallback;
}

bool AudioScalarLimits(const std::string& label, const std::string& parentKey,
    float& minValue, float& maxValue, float& speed)
{
    if (Lower(parentKey) != "import settings")
        return false;

    const std::string field = Lower(label);
    if (field == "volume")
    {
        minValue = 0.0f;
        maxValue = 1.0f;
        speed = 0.01f;
        return true;
    }
    if (field == "pitch")
    {
        minValue = 0.01f;
        maxValue = 4.0f;
        speed = 0.01f;
        return true;
    }
    if (field == "referencedistance")
    {
        minValue = 0.01f;
        maxValue = 10000.0f;
        speed = 0.05f;
        return true;
    }
    if (field == "maxdistance")
    {
        minValue = 0.02f;
        maxValue = 10000.0f;
        speed = 0.1f;
        return true;
    }
    if (field == "rolloff")
    {
        minValue = 0.0f;
        maxValue = 8.0f;
        speed = 0.01f;
        return true;
    }
    if (field == "reverbsend")
    {
        minValue = 0.0f;
        maxValue = 1.0f;
        speed = 0.01f;
        return true;
    }
    return false;
}

bool AudioReverbZoneScalarLimits(const std::string& label, const std::string& componentType,
    float& minValue, float& maxValue, float& speed)
{
    const std::string loweredComponent = Lower(componentType);
    if (loweredComponent != "audioreverbzone" && loweredComponent != "audiovolume")
        return false;

    const std::string field = Lower(label);
    if (field == "radius" || field == "fadedistance")
    {
        minValue = field == "radius" ? 0.01f : 0.0f;
        maxValue = 100000.0f;
        speed = 0.05f;
        return true;
    }
    if (field == "wetgain")
    {
        minValue = 0.0f;
        maxValue = 1.0f;
        speed = 0.01f;
        return true;
    }
    if (field == "density" || field == "diffusion" || field == "gain" || field == "gainhf")
    {
        minValue = 0.0f;
        maxValue = 1.0f;
        speed = 0.01f;
        return true;
    }
    if (field == "decaytime")
    {
        minValue = 0.1f;
        maxValue = 20.0f;
        speed = 0.05f;
        return true;
    }
    return false;
}

bool AudioComponentScalarLimits(const std::string& label, const std::string& componentType,
    float& minValue, float& maxValue, float& speed)
{
    if (Lower(componentType) != "audio")
        return false;

    const std::string field = Lower(label);
    if (field == "occlusiongain" || field == "occlusionhighfrequencygain")
    {
        minValue = 0.0f;
        maxValue = 1.0f;
        speed = 0.01f;
        return true;
    }
    if (field == "occlusionattack" || field == "occlusionrelease" ||
        field == "occlusionqueryinterval")
    {
        minValue = 0.001f;
        maxValue = 10.0f;
        speed = 0.005f;
        return true;
    }
    if (field == "occlusionmaxdistance")
    {
        minValue = 0.01f;
        maxValue = 100000.0f;
        speed = 0.1f;
        return true;
    }
    if (field == "occlusionmaterialthickness")
    {
        minValue = 0.0f;
        maxValue = 4.0f;
        speed = 0.01f;
        return true;
    }
    if (field == "coneinnerangle" || field == "coneouterangle")
    {
        minValue = 0.0f;
        maxValue = 360.0f;
        speed = 1.0f;
        return true;
    }
    if (field == "coneoutergain")
    {
        minValue = 0.0f;
        maxValue = 1.0f;
        speed = 0.01f;
        return true;
    }
    if (field == "occlusionmaxqueriesperframe")
    {
        minValue = 1.0f;
        maxValue = 64.0f;
        speed = 1.0f;
        return true;
    }
    return false;
}

bool AudioControlAssetScalarLimits(const std::string& label, const std::string& pointer,
    float& minValue, float& maxValue, float& speed)
{
    const std::string field = Lower(label);
    const std::string path = Lower(pointer);
    if (path.find("/rules/") != std::string::npos)
    {
        if (field == "gain" || field == "targetgain" || field == "target_gain")
        {
            minValue = 0.0f;
            maxValue = 1.0f;
            speed = 0.01f;
            return true;
        }
        if (field == "attack" || field == "attackseconds" || field == "attack_seconds" ||
            field == "release" || field == "releaseseconds" || field == "release_seconds")
        {
            minValue = 0.0f;
            maxValue = 10.0f;
            speed = 0.01f;
            return true;
        }
    }
    if (path.find("/buses/") != std::string::npos && field == "gain")
    {
        minValue = 0.0f;
        maxValue = 4.0f;
        speed = 0.01f;
        return true;
    }
    if (field == "fadeseconds" || field == "fade_seconds")
    {
        minValue = 0.0f;
        maxValue = 60.0f;
        speed = 0.01f;
        return true;
    }
    if (path.find("/parameters/") != std::string::npos)
    {
        if (field == "density" || field == "diffusion" || field == "gain" || field == "gainhf")
        {
            minValue = 0.0f;
            maxValue = 1.0f;
            speed = 0.01f;
            return true;
        }
        if (field == "decaytime")
        {
            minValue = 0.1f;
            maxValue = 20.0f;
            speed = 0.05f;
            return true;
        }
    }
    return false;
}

std::optional<Vans::VansSerializedValue> DefaultAudioControlArrayElement(
    const std::string& label,
    const std::string& pointer)
{
    using Value = Vans::VansSerializedValue;
    const std::string field = Lower(label);
    const std::string path = Lower(pointer);
    if (field == "rules" || path.find("/rules") != std::string::npos)
    {
        return Value::Object({
            { "trigger", Value::String("Voice") },
            { "target", Value::String("Music") },
            { "gain", Value::Float(0.35) },
            { "attack", Value::Float(0.08) },
            { "release", Value::Float(0.35) },
            { "enabled", Value::Bool(true) }
        });
    }
    if (field == "buses" || path.find("/buses") != std::string::npos)
    {
        return Value::Object({
            { "bus", Value::String("Music") },
            { "gain", Value::Float(1.0) }
        });
    }
    return std::nullopt;
}

bool IsAudioMetaRoot(const Vans::VansSerializedValue& metaRoot)
{
    return Vans::ReadSerializedStringField(metaRoot, "importer") == "AudioImporter";
}

bool EnsureSerializedField(
    Vans::VansSerializedValue& object,
    const std::string& name,
    Vans::VansSerializedValue defaultValue)
{
    if (object.kind != Vans::VansSerializedValue::Kind::Object)
        object = Vans::VansSerializedValue::Object({});
    if (Vans::FindObjectField(object, name))
        return false;
    Vans::SetSerializedObjectField(object, name, std::move(defaultValue));
    return true;
}

bool NormalizeAudioImportSettings(Vans::VansSerializedValue& settings)
{
    using Value = Vans::VansSerializedValue;
    bool changed = false;
    changed |= EnsureSerializedField(settings, "playMode", Value::String("static"));
    changed |= EnsureSerializedField(settings, "loop", Value::Bool(false));
    changed |= EnsureSerializedField(settings, "autoPlay", Value::Bool(false));
    changed |= EnsureSerializedField(settings, "volume", Value::Float(1.0));
    changed |= EnsureSerializedField(settings, "pitch", Value::Float(1.0));
    changed |= EnsureSerializedField(settings, "spatial", Value::Bool(false));
    changed |= EnsureSerializedField(settings, "referenceDistance", Value::Float(1.0));
    changed |= EnsureSerializedField(settings, "maxDistance", Value::Float(100.0));
    changed |= EnsureSerializedField(settings, "rolloff", Value::Float(1.0));
    changed |= EnsureSerializedField(settings, "attenuationMode", Value::String("linear"));
    changed |= EnsureSerializedField(settings, "reverbSend", Value::Float(0.0));
    changed |= EnsureSerializedField(settings, "bus", Value::String("SFX"));
    return changed;
}

VansEngine::VansAudioPreviewSettings BuildAudioPreviewSettings(const Vans::VansSerializedValue* settings)
{
    VansEngine::VansAudioPreviewSettings preview;
    if (!settings || settings->kind != Vans::VansSerializedValue::Kind::Object)
        return preview;

    const std::string playMode = Vans::ReadSerializedStringField(*settings, "playMode", "streaming");
    preview.streaming = Lower(playMode) == "streaming";
    preview.loop = Vans::ReadSerializedBoolField(*settings, "loop", false);
    preview.volume = std::clamp(
        ReadSerializedFloatOr(Vans::FindObjectField(*settings, "volume"), preview.volume),
        0.0f,
        4.0f);
    preview.pitch = std::max(
        ReadSerializedFloatOr(Vans::FindObjectField(*settings, "pitch"), preview.pitch),
        0.01f);
    preview.spatial = Vans::ReadSerializedBoolField(*settings, "spatial", false);
    preview.referenceDistance = std::max(
        ReadSerializedFloatOr(Vans::FindObjectField(*settings, "referenceDistance"), preview.referenceDistance),
        0.01f);
    preview.maxDistance = std::max(
        ReadSerializedFloatOr(Vans::FindObjectField(*settings, "maxDistance"), preview.maxDistance),
        preview.referenceDistance + 0.01f);
    preview.rolloff = std::max(
        ReadSerializedFloatOr(Vans::FindObjectField(*settings, "rolloff"), preview.rolloff),
        0.0f);
    preview.attenuationMode = Vans::ReadSerializedStringField(*settings, "attenuationMode", preview.attenuationMode);
    if (preview.attenuationMode.empty())
        preview.attenuationMode = "linear";
    preview.reverbSend = std::clamp(
        ReadSerializedFloatOr(Vans::FindObjectField(*settings, "reverbSend"), preview.reverbSend),
        0.0f,
        1.0f);
    preview.bus = Vans::ReadSerializedStringField(*settings, "bus", preview.bus);
    if (preview.bus.empty())
        preview.bus = "Preview";
    return preview;
}

std::filesystem::path NormalizePreviewPath(std::filesystem::path path)
{
    if (!path.is_absolute())
        path = std::filesystem::absolute(path);
    return path.lexically_normal();
}

bool IsSerializedNumericVector(const Vans::VansSerializedValue& value, std::size_t minimum, std::size_t maximum)
{
    return value.kind == Vans::VansSerializedValue::Kind::Array &&
        value.arrayItems.size() >= minimum &&
        value.arrayItems.size() <= maximum &&
        std::all_of(value.arrayItems.begin(), value.arrayItems.end(),
            [](const Vans::VansSerializedValue& item)
            {
                return item.kind == Vans::VansSerializedValue::Kind::Int ||
                    item.kind == Vans::VansSerializedValue::Kind::Float;
            });
}

std::string SerializedValueDisplayString(const Vans::VansSerializedValue& value)
{
    switch (value.kind)
    {
    case Vans::VansSerializedValue::Kind::Null:
        return "None";
    case Vans::VansSerializedValue::Kind::Bool:
        return value.boolValue ? "true" : "false";
    case Vans::VansSerializedValue::Kind::Int:
        return std::to_string(value.intValue);
    case Vans::VansSerializedValue::Kind::Float:
        return std::to_string(value.floatValue);
    case Vans::VansSerializedValue::Kind::String:
        return value.stringValue;
    case Vans::VansSerializedValue::Kind::Array:
    {
        std::string result = "[";
        for (std::size_t index = 0; index < value.arrayItems.size(); ++index)
        {
            if (index > 0)
                result += ", ";
            result += SerializedValueDisplayString(value.arrayItems[index]);
        }
        result += "]";
        return result;
    }
    case Vans::VansSerializedValue::Kind::Object:
        return "{...}";
    }
    return {};
}

bool DrawSerializedMaterialParameter(
    const std::string& label,
    Vans::VansSerializedValue& parameter,
    bool readOnly)
{
    if (parameter.kind != Vans::VansSerializedValue::Kind::Object)
        return false;

    Vans::VansSerializedValue* value = Vans::FindObjectField(parameter, "value");
    if (!value)
    {
        if (const Vans::VansSerializedValue* defaultValue = Vans::FindObjectField(parameter, "default"))
        {
            Vans::SetSerializedObjectField(parameter, "value", *defaultValue);
            value = Vans::FindObjectField(parameter, "value");
        }
        if (!value)
            return false;
    }

    const std::string type = Lower(Vans::ReadSerializedStringField(parameter, "type"));
    bool changed = false;
    BeginProperty(label);

    if (readOnly)
    {
        const std::string display = SerializedValueDisplayString(*value);
        ImGui::TextDisabled("%s", display.c_str());
        return false;
    }

    if ((type == "color" || (IsColorField(label) && (type == "vec3" || type == "vec4" || type.empty()))) &&
        IsSerializedNumericVector(*value, 3, 4))
    {
        std::array<float, 4> color{};
        for (std::size_t i = 0; i < value->arrayItems.size(); ++i)
            color[i] = static_cast<float>(Vans::ReadSerializedNumber(value->arrayItems[i]));
        const bool edited = value->arrayItems.size() == 3
            ? ImGui::ColorEdit3("##value", color.data())
            : ImGui::ColorEdit4("##value", color.data());
        if (edited)
        {
            for (std::size_t i = 0; i < value->arrayItems.size(); ++i)
                value->arrayItems[i] = Vans::VansSerializedValue::Float(color[i]);
            changed = true;
        }
    }
    else if (((type == "vec2" || type == "vec3" || type == "vec4") &&
        value->kind == Vans::VansSerializedValue::Kind::Array) ||
        (type.empty() && IsSerializedNumericVector(*value, 2, 4)))
    {
        std::array<float, 4> values{};
        const int count = type == "vec2" ? 2 :
            (type == "vec3" ? 3 :
                (type == "vec4" ? 4 : static_cast<int>(value->arrayItems.size())));
        while (value->arrayItems.size() < static_cast<std::size_t>(count))
            value->arrayItems.push_back(Vans::VansSerializedValue::Float(0.0));
        for (int i = 0; i < count; ++i)
            values[i] = static_cast<float>(Vans::ReadSerializedNumber(value->arrayItems[i]));
        bool edited = false;
        if (count == 2) edited = ImGui::DragFloat2("##value", values.data(), 0.01f);
        if (count == 3) edited = ImGui::DragFloat3("##value", values.data(), 0.01f);
        if (count == 4) edited = ImGui::DragFloat4("##value", values.data(), 0.01f);
        if (edited)
        {
            for (int i = 0; i < count; ++i)
                value->arrayItems[i] = Vans::VansSerializedValue::Float(values[i]);
            changed = true;
        }
    }
    else if (type == "bool" && value->kind == Vans::VansSerializedValue::Kind::Bool)
    {
        bool edited = value->boolValue;
        if (ImGui::Checkbox("##value", &edited))
        {
            *value = Vans::VansSerializedValue::Bool(edited);
            changed = true;
        }
    }
    else if ((type == "int" || type == "uint") && value->kind == Vans::VansSerializedValue::Kind::Int)
    {
        int edited = static_cast<int>(value->intValue);
        if (ImGui::InputInt("##value", &edited))
        {
            *value = Vans::VansSerializedValue::Int(edited);
            changed = true;
        }
    }
    else if (value->kind == Vans::VansSerializedValue::Kind::Int ||
        value->kind == Vans::VansSerializedValue::Kind::Float)
    {
        float edited = static_cast<float>(Vans::ReadSerializedNumber(*value));
        const float minValue = ReadSerializedFloatOr(Vans::FindObjectField(parameter, "min"), 0.0f);
        const float maxValue = ReadSerializedFloatOr(
            Vans::FindObjectField(parameter, "max"),
            IsNormalizedField(label) ? 1.0f : 0.0f);
        if (maxValue > minValue)
        {
            if (ImGui::SliderFloat("##value", &edited, minValue, maxValue, "%.3f"))
            {
                *value = Vans::VansSerializedValue::Float(edited);
                changed = true;
            }
        }
        else if (ImGui::DragFloat("##value", &edited, 0.01f, 0.0f, 0.0f, "%.3f"))
        {
            *value = Vans::VansSerializedValue::Float(edited);
            changed = true;
        }
    }
    else
    {
        const std::string display = SerializedValueDisplayString(*value);
        ImGui::TextDisabled("%s", display.c_str());
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

void CopyToImGuiBuffer(char* destination, std::size_t destinationSize, const std::string& source)
{
    if (!destination || destinationSize == 0)
        return;
    const std::size_t copied = std::min(destinationSize - 1, source.size());
    std::memcpy(destination, source.data(), copied);
    destination[copied] = '\0';
}

bool MergeLuaScriptFieldDefaults(
    Vans::VansSerializedValue& data,
    Vans::EditorAPI::IEngineEditorAPI& api,
    std::vector<Vans::LuaScriptFieldDescriptor>* descriptors)
{
    if (data.kind != Vans::VansSerializedValue::Kind::Object)
        data = Vans::VansSerializedValue::Object({});

    const std::string scriptPath = Vans::ReadSerializedStringField(data, "path");
    std::string entryName = Vans::ReadSerializedStringField(data, "entry");
    if (entryName.empty())
        entryName = Vans::ReadSerializedStringField(data, "class");
    if (scriptPath.empty())
        return false;

    Vans::VansSerializedValue* scriptFields = Vans::FindObjectField(data, "fields");
    if (!scriptFields || scriptFields->kind != Vans::VansSerializedValue::Kind::Object)
    {
        Vans::SetSerializedObjectField(data, "fields", Vans::VansSerializedValue::Object({}));
        scriptFields = Vans::FindObjectField(data, "fields");
    }

    struct LuaFieldSchemaCacheEntry
    {
        Vans::VansSerializedValue fields = Vans::VansSerializedValue::Object({});
        std::vector<Vans::LuaScriptFieldDescriptor> descriptors;
    };

    static std::unordered_map<std::string, LuaFieldSchemaCacheEntry> fieldDefaultsCache;
    const Vans::EditorAPI::ProjectBrowserRootSnapshot projectRoot = api.GetProjectBrowserRoot();
    std::filesystem::path absoluteScriptPath(scriptPath);
    if (!absoluteScriptPath.is_absolute())
        absoluteScriptPath = std::filesystem::path(projectRoot.rootPath) / scriptPath;
    absoluteScriptPath = absoluteScriptPath.lexically_normal();

    std::error_code timeError;
    const auto lastWrite = std::filesystem::last_write_time(absoluteScriptPath, timeError);
    std::error_code sizeError;
    const auto fileSize = std::filesystem::file_size(absoluteScriptPath, sizeError);
    const std::string key = projectRoot.rootPath + "|" + scriptPath + "|" + entryName + "|" +
        absoluteScriptPath.generic_string() + "|" +
        (timeError ? std::string("missing") : std::to_string(lastWrite.time_since_epoch().count())) + "|" +
        (sizeError ? std::string("nosize") : std::to_string(fileSize));
    auto found = fieldDefaultsCache.find(key);
    if (found == fieldDefaultsCache.end())
    {
        const Vans::LuaScriptFieldDefaultsResult result =
            Vans::VansLuaScriptInspectorService::BuildDefaultFieldData(
                projectRoot.rootPath, scriptPath, entryName);
        LuaFieldSchemaCacheEntry entry;
        if (result)
        {
            entry.fields = Vans::VansSerializedValue::Object({});
            for (const auto& [name, value] : result.fields)
                if (!name.empty() && !value.IsNull())
                    Vans::SetSerializedObjectField(entry.fields, name, value);
            entry.descriptors = result.descriptors;
        }
        found = fieldDefaultsCache.emplace(key, std::move(entry)).first;
        if (!result && !result.message.empty())
            VANS_LOG_WARN("[Inspector] Lua script field discovery failed: " << result.message);
    }

    if (descriptors)
        *descriptors = found->second.descriptors;

    bool changed = false;
    if (!scriptFields || scriptFields->kind != Vans::VansSerializedValue::Kind::Object)
        return changed;

    for (const auto& [fieldName, fieldValue] : found->second.fields.objectFields)
    {
        if (!Vans::FindObjectField(*scriptFields, fieldName))
        {
            Vans::SetSerializedObjectField(*scriptFields, fieldName, fieldValue);
            changed = true;
        }
    }
    return changed;
}

bool TryDrawSerializedLuaNumericField(
    const std::string& label,
    Vans::VansSerializedValue& value,
    const Vans::LuaScriptFieldDescriptor& descriptor,
    bool& changed)
{
    const bool hasNumericMetadata =
        descriptor.hasMinValue || descriptor.hasMaxValue || descriptor.hasSpeed;
    if (!hasNumericMetadata)
        return false;

    const float speed = static_cast<float>(descriptor.speed > 0.0 ? descriptor.speed : 0.05);
    ImGui::PushID(label.c_str());
    BeginProperty(label);

    if (descriptor.kind == Vans::LuaScriptInspectableFieldKind::Int)
    {
        std::int64_t edited = value.kind == Vans::VansSerializedValue::Kind::Int
            ? value.intValue
            : static_cast<std::int64_t>(Vans::ReadSerializedNumber(value));
        if (ImGui::DragScalar("##value", ImGuiDataType_S64, &edited, speed))
        {
            if (descriptor.hasMinValue)
                edited = (std::max)(edited, static_cast<std::int64_t>(descriptor.minValue));
            if (descriptor.hasMaxValue)
                edited = (std::min)(edited, static_cast<std::int64_t>(descriptor.maxValue));
            value = Vans::VansSerializedValue::Int(edited);
            changed = true;
        }
        ImGui::PopID();
        return true;
    }

    if (descriptor.kind == Vans::LuaScriptInspectableFieldKind::Float)
    {
        float edited = static_cast<float>(Vans::ReadSerializedNumber(value));
        const bool bounded = descriptor.hasMinValue && descriptor.hasMaxValue &&
            descriptor.maxValue > descriptor.minValue;
        const float minValue = bounded ? static_cast<float>(descriptor.minValue) : 0.0f;
        const float maxValue = bounded ? static_cast<float>(descriptor.maxValue) : 0.0f;
        if (ImGui::DragFloat("##value", &edited, speed, minValue, maxValue, "%.3f"))
        {
            if (descriptor.hasMinValue)
                edited = (std::max)(edited, static_cast<float>(descriptor.minValue));
            if (descriptor.hasMaxValue)
                edited = (std::min)(edited, static_cast<float>(descriptor.maxValue));
            value = Vans::VansSerializedValue::Float(edited);
            changed = true;
        }
        ImGui::PopID();
        return true;
    }

    ImGui::PopID();
    return false;
}

const Vans::VansSerializedValue* FindSerializedComponent(
    const Vans::VansSerializedValue& entity,
    const std::string& type)
{
    const Vans::VansSerializedValue* components = Vans::FindObjectField(entity, "components");
    if (!components || components->kind != Vans::VansSerializedValue::Kind::Array)
        return nullptr;
    for (const Vans::VansSerializedValue& component : components->arrayItems)
        if (Vans::ReadSerializedStringField(component, "type") == type)
            return &component;
    return nullptr;
}

bool ReadPreviewVec3(const Vans::VansSerializedValue& value, Vans::EditorAPI::Vec3& out)
{
    if (value.kind != Vans::VansSerializedValue::Kind::Array || value.arrayItems.size() < 3)
        return false;
    out = {
        static_cast<float>(Vans::ReadSerializedNumber(value.arrayItems[0])),
        static_cast<float>(Vans::ReadSerializedNumber(value.arrayItems[1])),
        static_cast<float>(Vans::ReadSerializedNumber(value.arrayItems[2]))
    };
    return true;
}

bool ReadPreviewRotationEuler(const Vans::VansSerializedValue& value, Vans::EditorAPI::Vec3& out)
{
    if (value.kind != Vans::VansSerializedValue::Kind::Array)
        return false;

    if (value.arrayItems.size() == 4)
    {
        const glm::quat quaternion(
            static_cast<float>(Vans::ReadSerializedNumber(value.arrayItems[3])),
            static_cast<float>(Vans::ReadSerializedNumber(value.arrayItems[0])),
            static_cast<float>(Vans::ReadSerializedNumber(value.arrayItems[1])),
            static_cast<float>(Vans::ReadSerializedNumber(value.arrayItems[2])));
        const glm::vec3 euler = glm::degrees(glm::eulerAngles(quaternion));
        out = { euler.x, euler.y, euler.z };
        return true;
    }

    return ReadPreviewVec3(value, out);
}

bool BuildRuntimeTransformPreview(
    const Vans::VansSerializedValue& entity,
    Vans::EditorAPI::RuntimeTransformEdit& edit)
{
    const std::string entityGuid = Vans::ReadSerializedStringField(entity, "id");
    if (entityGuid.empty())
        return false;

    const Vans::VansSerializedValue* transformComponent = FindSerializedComponent(entity, "Transform");
    if (!transformComponent || !Vans::ReadSerializedBoolField(*transformComponent, "enabled", true))
    {
        return false;
    }

    const Vans::VansSerializedValue* data = Vans::FindObjectField(*transformComponent, "data");
    if (!data)
        return false;

    edit = {};
    edit.entityGuid = entityGuid;
    edit.writePosition = false;
    edit.writeRotation = false;
    edit.writeScale = false;

    if (const Vans::VansSerializedValue* position = Vans::FindObjectField(*data, "position");
        position && ReadPreviewVec3(*position, edit.position))
    {
        edit.writePosition = true;
    }
    if (const Vans::VansSerializedValue* rotation = Vans::FindObjectField(*data, "rotation");
        rotation && ReadPreviewRotationEuler(*rotation, edit.rotationDegrees))
    {
        edit.writeRotation = true;
    }
    if (const Vans::VansSerializedValue* scale = Vans::FindObjectField(*data, "scale");
        scale && ReadPreviewVec3(*scale, edit.scale))
    {
        edit.writeScale = true;
    }

    return edit.writePosition || edit.writeRotation || edit.writeScale;
}

bool ReadPreviewColor(const Vans::VansSerializedValue& data, Vans::EditorAPI::Vec3& out)
{
    const Vans::VansSerializedValue* color = Vans::FindObjectField(data, "color");
    return color && ReadPreviewVec3(*color, out);
}

bool AppendRuntimeLightPreview(
    const Vans::VansSerializedValue& entity,
    const char* componentType,
    Vans::EditorAPI::RuntimePreviewLightType lightType,
    std::vector<Vans::EditorAPI::RuntimeLightEdit>& edits)
{
    const Vans::VansSerializedValue* component = FindSerializedComponent(entity, componentType);
    if (!component || !Vans::ReadSerializedBoolField(*component, "enabled", true))
        return false;

    const Vans::VansSerializedValue* data = Vans::FindObjectField(*component, "data");
    if (!data)
        return false;

    Vans::EditorAPI::RuntimeLightEdit edit;
    edit.type = lightType;
    edit.entityGuid = Vans::ReadSerializedStringField(entity, "id");
    if (edit.entityGuid.empty())
        return false;

    if (ReadPreviewColor(*data, edit.color))
        edit.writeColor = true;
    if (const Vans::VansSerializedValue* intensity = Vans::FindObjectField(*data, "intensity"))
    {
        edit.intensity = static_cast<float>(Vans::ReadSerializedNumber(*intensity));
        edit.writeIntensity = true;
    }

    if (lightType == Vans::EditorAPI::RuntimePreviewLightType::Point ||
        lightType == Vans::EditorAPI::RuntimePreviewLightType::Spot)
    {
        if (const Vans::VansSerializedValue* radius = Vans::FindObjectField(*data, "radius"))
        {
            edit.radius = static_cast<float>(Vans::ReadSerializedNumber(*radius));
            edit.writeRadius = true;
        }
    }

    if (lightType == Vans::EditorAPI::RuntimePreviewLightType::Spot)
    {
        if (const Vans::VansSerializedValue* innerCutoff = Vans::FindObjectField(*data, "innercutoff"))
        {
            edit.innerCutoffRadians = glm::radians(static_cast<float>(Vans::ReadSerializedNumber(*innerCutoff)));
            edit.writeInnerCutoff = true;
        }
        if (const Vans::VansSerializedValue* outerCutoff = Vans::FindObjectField(*data, "outerCutoff"))
        {
            edit.outerCutoffRadians = glm::radians(static_cast<float>(Vans::ReadSerializedNumber(*outerCutoff)));
            edit.writeOuterCutoff = true;
        }
    }

    if (lightType == Vans::EditorAPI::RuntimePreviewLightType::Rect)
    {
        if (const Vans::VansSerializedValue* width = Vans::FindObjectField(*data, "width"))
        {
            edit.rectWidth = static_cast<float>(Vans::ReadSerializedNumber(*width));
            edit.writeRectWidth = true;
        }
        if (const Vans::VansSerializedValue* height = Vans::FindObjectField(*data, "height"))
        {
            edit.rectHeight = static_cast<float>(Vans::ReadSerializedNumber(*height));
            edit.writeRectHeight = true;
        }
        if (const Vans::VansSerializedValue* range = Vans::FindObjectField(*data, "range"))
        {
            edit.rectRange = static_cast<float>(Vans::ReadSerializedNumber(*range));
            edit.writeRectRange = true;
        }
        if (const Vans::VansSerializedValue* twoSided = Vans::FindObjectField(*data, "two_sided"))
        {
            edit.rectTwoSided = Vans::ReadSerializedBool(*twoSided) ? 1.0f : 0.0f;
            edit.writeRectTwoSided = true;
        }
        if (const Vans::VansSerializedValue* shadow = Vans::FindObjectField(*data, "shadow"))
        {
            edit.rectShadowIndex = Vans::ReadSerializedBool(*shadow) ? 0.0f : -1.0f;
            edit.writeRectShadow = true;
        }
    }

    if (edit.writeColor || edit.writeIntensity || edit.writeRadius ||
        edit.writeInnerCutoff || edit.writeOuterCutoff ||
        edit.writeRectWidth || edit.writeRectHeight || edit.writeRectRange ||
        edit.writeRectTwoSided || edit.writeRectShadow)
    {
        edits.push_back(edit);
        return true;
    }
    return false;
}

std::string ReadPreviewAssetGuid(const Vans::VansSerializedValue& reference)
{
    if (reference.kind == Vans::VansSerializedValue::Kind::String)
        return reference.stringValue;
    if (reference.kind == Vans::VansSerializedValue::Kind::Object)
        return Vans::ReadSerializedStringField(reference, "guid");
    return {};
}

void AppendRuntimeMaterialOverridePreviews(
    const Vans::VansSerializedValue& entity,
    std::vector<Vans::EditorAPI::RuntimeRendererMaterialOverrideEdit>& edits)
{
    const std::string entityGuid = Vans::ReadSerializedStringField(entity, "id");
    if (entityGuid.empty())
        return;

    const Vans::VansSerializedValue* renderer = FindSerializedComponent(entity, "ModelRenderer");
    if (!renderer || !Vans::ReadSerializedBoolField(*renderer, "enabled", true))
        return;

    const Vans::VansSerializedValue* data = Vans::FindObjectField(*renderer, "data");
    if (!data || data->kind != Vans::VansSerializedValue::Kind::Object)
        return;

    const auto appendOverrides = [&](const Vans::VansSerializedValue* overrides)
    {
        if (!overrides || overrides->kind != Vans::VansSerializedValue::Kind::Object)
            return;
        for (const auto& [slot, reference] : overrides->objectFields)
        {
            const std::string materialGuid = ReadPreviewAssetGuid(reference);
            if (materialGuid.empty())
                continue;
            edits.push_back({
                entityGuid,
                slot,
                materialGuid
            });
        }
    };

    appendOverrides(Vans::FindObjectField(*data, "materialOverrides"));
    appendOverrides(Vans::FindObjectField(*data, "submeshMaterialOverrides"));
}

Vans::EditorAPI::RuntimeEntityPreviewChange BuildRuntimeEntityPreviewChange(
    const Vans::VansSerializedValue& entity)
{
    Vans::EditorAPI::RuntimeEntityPreviewChange change;
    const std::string entityGuid = Vans::ReadSerializedStringField(entity, "id");
    const Vans::VansSerializedValue* components = Vans::FindObjectField(entity, "components");
    if (components && components->kind == Vans::VansSerializedValue::Kind::Array)
    {
        for (const Vans::VansSerializedValue& component : components->arrayItems)
        {
            const std::string componentType = Vans::ReadSerializedStringField(component, "type");
            if (entityGuid.empty() || componentType.empty())
                continue;
            change.componentEnabled.push_back({
                entityGuid,
                componentType,
                Vans::ReadSerializedBoolField(component, "enabled", true)
            });
        }
    }
    change.hasTransform = BuildRuntimeTransformPreview(entity, change.transform);
    AppendRuntimeLightPreview(entity, "DirectionalLight",
        Vans::EditorAPI::RuntimePreviewLightType::Directional, change.lights);
    AppendRuntimeLightPreview(entity, "PointLight",
        Vans::EditorAPI::RuntimePreviewLightType::Point, change.lights);
    AppendRuntimeLightPreview(entity, "SpotLight",
        Vans::EditorAPI::RuntimePreviewLightType::Spot, change.lights);
    AppendRuntimeLightPreview(entity, "RectLight",
        Vans::EditorAPI::RuntimePreviewLightType::Rect, change.lights);
    AppendRuntimeMaterialOverridePreviews(entity, change.materialOverrides);
    return change;
}

}

struct VansInspectorWindow::Impl
{
    void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& api);
    void DrawSceneEntity(Vans::EditorAPI::IEngineEditorAPI& api);
    void DrawSceneSettings();
    void DrawAsset(Vans::EditorAPI::IEngineEditorAPI& api);
    void DrawAudioAssetPreview(const std::filesystem::path& sourcePath,
        const Vans::VansSerializedValue& metaRoot);
    bool DrawSerializedValue(const std::string& label, Vans::VansSerializedValue& value,
        const std::string& pointer, bool readOnly = false,
        const std::string& componentType = {}, const std::string& parentKey = {});
    bool DrawSerializedAssetReference(const std::string& label, Vans::VansSerializedValue& reference,
        const std::string& pointer, const Vans::ObjectReferenceSlotDescriptor& slot);
    bool DrawSerializedEditorObjectReference(const std::string& label, Vans::VansSerializedValue& reference,
        const std::string& pointer,
        const Vans::ObjectReferenceSlotDescriptor* declaredSlot = nullptr);
    bool DrawSerializedLuaScriptFields(Vans::VansSerializedValue& fields,
        const std::vector<Vans::LuaScriptFieldDescriptor>& descriptors,
        const std::string& pointer);
    bool DrawComponent(Vans::EditorAPI::IEngineEditorAPI& api, Vans::VansSerializedValue& component,
        const std::string& pointer, bool& removeRequested);
    bool LoadAssetDocuments(const std::filesystem::path& sourcePath);
    bool SaveAssetDocuments(bool reloadSceneOnSuccess = true);

    std::filesystem::path m_AssetPath;
    std::shared_ptr<Vans::VansOpenAssetDocument> m_AssetDocuments;
    std::string m_Error;
    std::vector<std::string> m_CollisionLayerNames;
    VansEngine::VansAudioPreviewPlayer m_AudioPreview;
    Vans::EditorAPI::IEngineEditorAPI* m_ActiveAPI = nullptr;
    bool m_PendingVehicleRebuild = false;
    std::optional<Vans::ObjectReferenceAssignment> m_PendingObjectReferenceEdit;
};

VansInspectorWindow::VansInspectorWindow()
    : m_Impl(std::make_unique<Impl>())
{
}

VansInspectorWindow::~VansInspectorWindow() = default;

void VansInspectorWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& api)
{
    m_Impl->ShowWindow(api);
}

bool VansInspectorWindow::Impl::DrawSerializedLuaScriptFields(
    Vans::VansSerializedValue& fields,
    const std::vector<Vans::LuaScriptFieldDescriptor>& descriptors,
    const std::string& pointer)
{
    if (fields.kind != Vans::VansSerializedValue::Kind::Object)
        fields = Vans::VansSerializedValue::Object({});

    bool changed = false;
    std::vector<std::string> rendered;
    rendered.reserve(descriptors.size());
    for (const Vans::LuaScriptFieldDescriptor& descriptor : descriptors)
    {
        if (descriptor.name.empty())
            continue;

        Vans::VansSerializedValue* field = Vans::FindObjectField(fields, descriptor.name);
        if (!field && Vans::HasLuaScriptFieldDefault(descriptor))
        {
            Vans::SetSerializedObjectField(fields, descriptor.name, descriptor.defaultValue);
            field = Vans::FindObjectField(fields, descriptor.name);
            changed = true;
        }
        if (!field)
            continue;

        changed |= Vans::NormalizeLuaScriptFieldValue(*field, descriptor);
        rendered.push_back(descriptor.name);
        const std::string fieldPointer = pointer + "/" + EscapePointerToken(descriptor.name);
        Vans::ObjectReferenceSlotDescriptor objectReferenceSlot;
        if (Vans::VansEditorPropertyDescriptorRegistry::TryResolveLuaScriptFieldObjectReferenceSlot(
            descriptor,
            objectReferenceSlot))
        {
            if (objectReferenceSlot.expectedDomain == Vans::EditorObjectDomain::ProjectAsset)
            {
                changed |= DrawSerializedAssetReference(
                    descriptor.name,
                    *field,
                    fieldPointer,
                    objectReferenceSlot);
            }
            else
            {
                changed |= DrawSerializedEditorObjectReference(
                    descriptor.name,
                    *field,
                    fieldPointer,
                    &objectReferenceSlot);
            }
            continue;
        }
        if (!TryDrawSerializedLuaNumericField(descriptor.name, *field, descriptor, changed))
        {
            changed |= DrawSerializedValue(
                descriptor.name,
                *field,
                fieldPointer,
                false,
                "Script",
                "fields");
        }
    }

    for (auto& [fieldName, fieldValue] : fields.objectFields)
    {
        if (std::find(rendered.begin(), rendered.end(), fieldName) != rendered.end())
            continue;
        changed |= DrawSerializedValue(
            fieldName,
            fieldValue,
            pointer + "/" + EscapePointerToken(fieldName),
            false,
            "Script",
            "fields");
    }
    return changed;
}

bool VansInspectorWindow::Impl::DrawSerializedAssetReference(
    const std::string& label,
    Vans::VansSerializedValue& reference,
    const std::string& pointer,
    const Vans::ObjectReferenceSlotDescriptor& objectSlot)
{
    if (!m_ActiveAPI)
        return false;

    const Vans::EditorAPI::AssetType expectedType = objectSlot.expectedAssetType;
    const auto assignReferenceHandle = [&](Vans::EditorObjectHandle handle)
    {
        if (handle.assetType == Vans::EditorAPI::AssetType::Unknown)
            handle.assetType = expectedType;

        Vans::ObjectReferenceAssignment assignment = Vans::MakeObjectReferenceAssignment(
            Vans::MakeInspectorDocumentPropertyPath(pointer),
            objectSlot,
            std::move(handle));

        Vans::VansSerializedValue encodedReference;
        if (!Vans::TryEncodeProjectAssetReferenceAssignment(assignment, encodedReference))
            return false;
        reference = std::move(encodedReference);
        return true;
    };
    const auto assignReference = [&](std::string guid, Vans::EditorAPI::AssetType assetType)
    {
        Vans::EditorObjectHandle handle;
        handle.domain = Vans::EditorObjectDomain::ProjectAsset;
        handle.guid = std::move(guid);
        handle.assetType = assetType;
        return assignReferenceHandle(std::move(handle));
    };

    const Vans::EditorObjectHandle currentHandle =
        Vans::ReadObjectReferenceSlotHandle(reference, objectSlot);
    const std::string guidText = currentHandle.guid;

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
    if (missing)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.3f, 1.0f));
    if (ImGui::BeginCombo("##asset", preview.c_str()))
    {
        static char search[128]{};
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##search", "Search assets...", search, sizeof(search));
        const std::string filter = Lower(search);
        if (ImGui::Selectable("None", guidText.empty()))
        {
            changed |= assignReference("", expectedType);
        }
        Vans::EditorAPI::AssetTypeFilter assetFilter;
        assetFilter.type = expectedType;
        assetFilter.includeUnknown = expectedType == Vans::EditorAPI::AssetType::Unknown;
        for (const Vans::EditorAPI::AssetEntry& asset : m_ActiveAPI->QueryAssets(assetFilter))
        {
            if (!filter.empty() && Lower(asset.name).find(filter) == std::string::npos)
                continue;
            const std::string candidateGuid = asset.guid;
            if (candidateGuid.empty())
                continue;
            const std::string itemLabel = asset.name + "##" + candidateGuid;
            const bool selected = candidateGuid == guidText;
            if (ImGui::Selectable(itemLabel.c_str(), selected))
            {
                changed |= assignReference(candidateGuid, asset.type);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", asset.relativePath.c_str());
        }
        ImGui::EndCombo();
    }
    if (missing)
        ImGui::PopStyleColor();
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(Vans::VansObjectReferenceDragPayloadType))
        {
            Vans::EditorObjectHandle droppedHandle;
            if (Vans::TryDeserializeEditorObjectHandle(payload->Data,
                static_cast<std::size_t>(payload->DataSize), droppedHandle))
            {
                changed |= assignReferenceHandle(std::move(droppedHandle));
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::PopID();
    return changed;
}

bool VansInspectorWindow::Impl::DrawSerializedEditorObjectReference(
    const std::string& label,
    Vans::VansSerializedValue& reference,
    const std::string& pointer,
    const Vans::ObjectReferenceSlotDescriptor* declaredSlot)
{
    bool changed = false;
    const Vans::VansSceneDocument* document = VansEditorWindow::GetSceneDocument();
    if (declaredSlot &&
        (declaredSlot->expectedDomain == Vans::EditorObjectDomain::SceneEntity ||
            declaredSlot->expectedDomain == Vans::EditorObjectDomain::SceneComponent))
    {
        changed |= Vans::NormalizeObjectReferenceSlotValue(reference, *declaredSlot);
    }
    else if (reference.kind != Vans::VansSerializedValue::Kind::Object)
    {
        return false;
    }

    Vans::ObjectReferenceSlotDescriptor slot;
    if (declaredSlot)
        slot = *declaredSlot;
    else
        slot.storagePolicy = Vans::ObjectReferenceStoragePolicy::EditorObjectReference;

    const Vans::EditorObjectHandle storedHandle =
        Vans::ReadObjectReferenceSlotHandle(reference, slot);
    const Vans::EditorObjectDomain expectedDomain =
        declaredSlot && declaredSlot->expectedDomain != Vans::EditorObjectDomain::Unknown
            ? declaredSlot->expectedDomain
            : storedHandle.domain;
    if (expectedDomain != Vans::EditorObjectDomain::SceneEntity &&
        expectedDomain != Vans::EditorObjectDomain::SceneComponent)
    {
        return false;
    }

    const std::string entityGuid =
        storedHandle.entityGuid.empty() ? storedHandle.guid : storedHandle.entityGuid;
    const std::string componentType = declaredSlot && !declaredSlot->expectedComponentType.empty()
        ? declaredSlot->expectedComponentType
        : storedHandle.componentType;

    slot.expectedDomain = expectedDomain;
    if (!componentType.empty())
        slot.expectedComponentType = componentType;

    auto tryAssignSceneReference = [&](const Vans::EditorObjectHandle& handle)
    {
        const Vans::DocumentPropertyPath targetPath = Vans::MakeInspectorDocumentPropertyPath(pointer);
        Vans::ObjectReferenceAssignment assignment =
            Vans::MakeObjectReferenceAssignment(targetPath, slot, handle);

        Vans::VansSerializedValue encodedReference;
        if (!document ||
            !Vans::TryEncodeSceneDocumentObjectReferenceAssignment(
                *document,
                assignment,
                encodedReference))
        {
            return false;
        }

        reference = std::move(encodedReference);
        if (targetPath.space == Vans::DocumentPropertySpace::Scene)
        {
            m_PendingObjectReferenceEdit = std::move(assignment);
        }

        return true;
    };

    std::string preview = "None";
    if (document && !entityGuid.empty())
    {
        const Vans::SceneObjectReferenceResolution resolved =
            Vans::ResolveSceneObjectReference(*document, slot, storedHandle);
        if (resolved)
        {
            preview = resolved.entityDisplayName.empty()
                ? entityGuid.substr(0, 8)
                : resolved.entityDisplayName;
            if (expectedDomain == Vans::EditorObjectDomain::SceneComponent)
                preview += " / " + (!resolved.componentDisplayType.empty()
                    ? resolved.componentDisplayType
                    : componentType);
        }
        else if (expectedDomain == Vans::EditorObjectDomain::SceneComponent &&
            !resolved.entityDisplayName.empty())
        {
            preview = resolved.entityDisplayName;
            if (!componentType.empty())
                preview += " / Missing " + componentType;
        }
        else
        {
            preview = "Missing: " + entityGuid.substr(0, 8);
        }
    }

    BeginProperty(label);
    ImGui::PushID(pointer.c_str());
    const ImGuiStyle& style = ImGui::GetStyle();
    const float clearWidth = ImGui::CalcTextSize("Clear").x + style.FramePadding.x * 2.0f;
    const float previewWidth = std::max(1.0f,
        ImGui::GetContentRegionAvail().x - clearWidth - style.ItemInnerSpacing.x);
    ImGui::Button(preview.c_str(), ImVec2(previewWidth, 0.0f));
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(Vans::VansObjectReferenceDragPayloadType))
        {
            Vans::EditorObjectHandle handle;
            if (Vans::TryDeserializeEditorObjectHandle(payload->Data,
                static_cast<std::size_t>(payload->DataSize), handle) &&
                tryAssignSceneReference(handle))
            {
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear") && document)
    {
        Vans::EditorObjectHandle clearedHandle;
        clearedHandle.domain = expectedDomain;
        clearedHandle.componentType = slot.expectedComponentType;
        changed |= tryAssignSceneReference(clearedHandle);
    }
    ImGui::PopID();
    return changed;
}

bool VansInspectorWindow::Impl::DrawSerializedValue(
    const std::string& label,
    Vans::VansSerializedValue& value,
    const std::string& pointer,
    bool readOnly,
    const std::string& componentType,
    const std::string& parentKey)
{
    ImGui::PushID(pointer.c_str());
    bool changed = false;
    const Vans::EditorPropertyDescriptor propertyDescriptor =
        Vans::VansEditorPropertyDescriptorRegistry::Resolve(componentType, parentKey, label);
    const Vans::ObjectReferenceSlotDescriptor* objectReferenceSlot =
        propertyDescriptor.IsObjectReference() ? &propertyDescriptor.objectReferenceSlot : nullptr;
    const Vans::EditorAPI::AssetType assetType = objectReferenceSlot
        ? objectReferenceSlot->expectedAssetType
        : Vans::EditorAPI::AssetType::Unknown;
    const bool allowProjectAssetSlot =
        objectReferenceSlot &&
        objectReferenceSlot->expectedDomain == Vans::EditorObjectDomain::ProjectAsset &&
        assetType != Vans::EditorAPI::AssetType::Unknown &&
        propertyDescriptor.IsDeclared();
    if (objectReferenceSlot && objectReferenceSlot->expectedDomain == Vans::EditorObjectDomain::ProjectAsset &&
        assetType != Vans::EditorAPI::AssetType::Unknown)
    {
        const bool normalizedProjectAssetReference =
            Vans::NormalizeObjectReferenceSlotValue(value, *objectReferenceSlot);
        if (value.kind == Vans::VansSerializedValue::Kind::String)
        {
            const bool isEmptyOrKnownGuid = value.stringValue.empty() ||
                (m_ActiveAPI && m_ActiveAPI->ResolveAssetGuid(value.stringValue).found);
            if (allowProjectAssetSlot ||
                (!value.stringValue.empty() && isEmptyOrKnownGuid))
            {
                ImGui::PopID();
                Vans::ObjectReferenceSlotDescriptor stringReferenceSlot = *objectReferenceSlot;
                stringReferenceSlot.storagePolicy = Vans::ObjectReferenceStoragePolicy::GuidString;
                const bool slotEdited = DrawSerializedAssetReference(label, value, pointer, stringReferenceSlot);
                return normalizedProjectAssetReference || slotEdited;
            }
        }
        else if (value.kind == Vans::VansSerializedValue::Kind::Object &&
            Vans::FindObjectField(value, "guid") != nullptr)
        {
            ImGui::PopID();
            const bool slotEdited = DrawSerializedAssetReference(label, value, pointer, *objectReferenceSlot);
            return normalizedProjectAssetReference || slotEdited;
        }
    }
    if (value.kind == Vans::VansSerializedValue::Kind::Object &&
        Vans::FindObjectField(value, "domain") != nullptr)
    {
        ImGui::PopID();
        return DrawSerializedEditorObjectReference(label, value, pointer, objectReferenceSlot);
    }
    const std::string loweredParentKey = Lower(parentKey);
    if (value.kind == Vans::VansSerializedValue::Kind::Object &&
        loweredParentKey.find("parameters") != std::string::npos &&
        (Vans::FindObjectField(value, "value") || Vans::FindObjectField(value, "default")))
    {
        ImGui::PopID();
        return DrawSerializedMaterialParameter(label, value, readOnly);
    }

    switch (value.kind)
    {
    case Vans::VansSerializedValue::Kind::Object:
    {
        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
            ImGuiTreeNodeFlags_SpanAvailWidth;
        if (ImGui::TreeNodeEx(FriendlyLabel(label).c_str(), flags))
        {
            for (auto& [fieldName, fieldValue] : value.objectFields)
            {
                const std::string childPointer = pointer + "/" + EscapePointerToken(fieldName);
                const bool identity = fieldName == "id" || fieldName == "guid" ||
                    fieldName == "sceneGuid" || fieldName == "schemaVersion" ||
                    fieldName == "version" || fieldName == "importer" ||
                    fieldName == "assetKind";
                changed |= DrawSerializedValue(
                    fieldName,
                    fieldValue,
                    childPointer,
                    readOnly || identity,
                    componentType,
                    label);
            }
            ImGui::TreePop();
        }
        break;
    }
    case Vans::VansSerializedValue::Kind::Array:
    {
        const bool numericVector = value.arrayItems.size() >= 2 && value.arrayItems.size() <= 4 &&
            std::all_of(value.arrayItems.begin(), value.arrayItems.end(),
                [](const Vans::VansSerializedValue& item)
                {
                    return item.kind == Vans::VansSerializedValue::Kind::Int ||
                        item.kind == Vans::VansSerializedValue::Kind::Float;
                });
        if (numericVector)
        {
            std::array<float, 4> values{};
            for (std::size_t i = 0; i < value.arrayItems.size(); ++i)
                values[i] = static_cast<float>(Vans::ReadSerializedNumber(value.arrayItems[i]));

            BeginProperty(label);
            if (readOnly)
            {
                std::string display = "[";
                for (std::size_t i = 0; i < value.arrayItems.size(); ++i)
                {
                    if (i > 0)
                        display += ", ";
                    display += std::to_string(values[i]);
                }
                display += "]";
                ImGui::TextDisabled("%s", display.c_str());
            }
            else if (Lower(label) == "rotation" && value.arrayItems.size() == 4)
            {
                const glm::quat quaternion(values[3], values[0], values[1], values[2]);
                glm::vec3 euler = glm::degrees(glm::eulerAngles(quaternion));
                if (ImGui::DragFloat3("##value", &euler.x, 0.25f, -360.0f, 360.0f, "%.2f"))
                {
                    const glm::quat edited = glm::quat(glm::radians(euler));
                    value.arrayItems[0] = Vans::VansSerializedValue::Float(edited.x);
                    value.arrayItems[1] = Vans::VansSerializedValue::Float(edited.y);
                    value.arrayItems[2] = Vans::VansSerializedValue::Float(edited.z);
                    value.arrayItems[3] = Vans::VansSerializedValue::Float(edited.w);
                    changed = true;
                }
            }
            else if (IsColorField(label) && (value.arrayItems.size() == 3 || value.arrayItems.size() == 4))
            {
                const bool edited = value.arrayItems.size() == 3
                    ? ImGui::ColorEdit3("##value", values.data())
                    : ImGui::ColorEdit4("##value", values.data());
                if (edited)
                {
                    for (std::size_t i = 0; i < value.arrayItems.size(); ++i)
                        value.arrayItems[i] = Vans::VansSerializedValue::Float(values[i]);
                    changed = true;
                }
            }
            else
            {
                bool edited = false;
                if (value.arrayItems.size() == 2) edited = ImGui::DragFloat2("##value", values.data(), 0.05f);
                if (value.arrayItems.size() == 3) edited = ImGui::DragFloat3("##value", values.data(), 0.05f);
                if (value.arrayItems.size() == 4) edited = ImGui::DragFloat4("##value", values.data(), 0.05f);
                if (edited)
                {
                    for (std::size_t i = 0; i < value.arrayItems.size(); ++i)
                        value.arrayItems[i] = Vans::VansSerializedValue::Float(values[i]);
                    changed = true;
                }
            }
        }
        else if (ImGui::TreeNodeEx(FriendlyLabel(label).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (std::size_t index = 0; index < value.arrayItems.size();)
            {
                const std::string elementPointer = pointer + "/" + std::to_string(index);
                if (!readOnly)
                {
                    ImGui::PushID(elementPointer.c_str());
                    if (ImGui::SmallButton("Remove"))
                    {
                        value.arrayItems.erase(value.arrayItems.begin() + static_cast<std::ptrdiff_t>(index));
                        changed = true;
                        ImGui::PopID();
                        continue;
                    }
                    ImGui::SameLine();
                    ImGui::PopID();
                }
                changed |= DrawSerializedValue(
                    "Element " + std::to_string(index),
                    value.arrayItems[index],
                    elementPointer,
                    readOnly,
                    componentType,
                    label);
                ++index;
            }
            if (!readOnly)
            {
                if (std::optional<Vans::VansSerializedValue> defaultElement =
                    DefaultAudioControlArrayElement(label, pointer))
                {
                    const std::string buttonLabel =
                        Lower(label) == "rules" ? "Add Rule" : "Add Bus";
                    if (ImGui::SmallButton(buttonLabel.c_str()))
                    {
                        value.arrayItems.push_back(std::move(*defaultElement));
                        changed = true;
                    }
                }
            }
            ImGui::TreePop();
        }
        break;
    }
    case Vans::VansSerializedValue::Kind::Bool:
    {
        bool edited = value.boolValue;
        BeginProperty(label);
        if (readOnly)
            ImGui::TextDisabled(edited ? "Enabled" : "Disabled");
        else if (ImGui::Checkbox("##value", &edited))
        {
            value = Vans::VansSerializedValue::Bool(edited);
            changed = true;
        }
        break;
    }
    case Vans::VansSerializedValue::Kind::Int:
    {
        std::int64_t edited = value.intValue;
        float minValue = 0.0f;
        float maxValue = 0.0f;
        float speed = 0.05f;
        BeginProperty(label);
        if (readOnly)
        {
            ImGui::TextDisabled("%lld", static_cast<long long>(edited));
        }
        else if (IsNormalizedField(label))
        {
            float normalized = static_cast<float>(edited);
            if (ImGui::SliderFloat("##value", &normalized, 0.0f, 1.0f, "%.3f"))
            {
                value = Vans::VansSerializedValue::Float(normalized);
                changed = true;
            }
        }
        else if (AudioControlAssetScalarLimits(label, pointer, minValue, maxValue, speed))
        {
            float numeric = static_cast<float>(edited);
            if (ImGui::DragFloat("##value", &numeric, speed, minValue, maxValue, "%.3f"))
            {
                value = Vans::VansSerializedValue::Float(std::clamp(numeric, minValue, maxValue));
                changed = true;
            }
        }
        else if (AudioScalarLimits(label, parentKey, minValue, maxValue, speed))
        {
            float numeric = static_cast<float>(edited);
            if (ImGui::DragFloat("##value", &numeric, speed, minValue, maxValue, "%.3f"))
            {
                value = Vans::VansSerializedValue::Float(std::clamp(numeric, minValue, maxValue));
                changed = true;
            }
        }
        else if (AudioReverbZoneScalarLimits(label, componentType, minValue, maxValue, speed))
        {
            float numeric = static_cast<float>(edited);
            if (ImGui::DragFloat("##value", &numeric, speed, minValue, maxValue, "%.3f"))
            {
                value = Vans::VansSerializedValue::Float(std::clamp(numeric, minValue, maxValue));
                changed = true;
            }
        }
        else if (AudioComponentScalarLimits(label, componentType, minValue, maxValue, speed))
        {
            float numeric = static_cast<float>(edited);
            if (ImGui::DragFloat("##value", &numeric, speed, minValue, maxValue, "%.3f"))
            {
                value = Vans::VansSerializedValue::Int(
                    static_cast<std::int64_t>(std::clamp(numeric, minValue, maxValue)));
                changed = true;
            }
        }
        else if (MaterialScalarLimits(label, parentKey, minValue, maxValue, speed))
        {
            float numeric = static_cast<float>(edited);
            if (ImGui::DragFloat("##value", &numeric, speed, minValue, maxValue, "%.3f"))
            {
                value = Vans::VansSerializedValue::Float(std::clamp(numeric, minValue, maxValue));
                changed = true;
            }
        }
        else if (ShouldUseFloatControl(label, parentKey))
        {
            float numeric = static_cast<float>(edited);
            if (ImGui::DragFloat("##value", &numeric, 0.05f, 0.0f, 0.0f, "%.3f"))
            {
                value = Vans::VansSerializedValue::Float(numeric);
                changed = true;
            }
        }
        else
        {
            const std::int64_t step = 1;
            if (ImGui::InputScalar("##value", ImGuiDataType_S64, &edited, &step))
            {
                value = Vans::VansSerializedValue::Int(edited);
                changed = true;
            }
        }
        break;
    }
    case Vans::VansSerializedValue::Kind::Float:
    {
        float edited = static_cast<float>(value.floatValue);
        float minValue = 0.0f;
        float maxValue = 0.0f;
        float speed = 0.05f;
        BeginProperty(label);
        if (readOnly)
            ImGui::TextDisabled("%.4f", edited);
        else if (VehicleScalarLimits(label, componentType, minValue, maxValue, speed))
        {
            if (ImGui::DragFloat("##value", &edited, speed, minValue, maxValue, "%.3f"))
            {
                value = Vans::VansSerializedValue::Float(std::clamp(edited, minValue, maxValue));
                changed = true;
            }
        }
        else if (IsNormalizedField(label))
        {
            if (ImGui::SliderFloat("##value", &edited, 0.0f, 1.0f, "%.3f"))
            {
                value = Vans::VansSerializedValue::Float(edited);
                changed = true;
            }
        }
        else if (AudioControlAssetScalarLimits(label, pointer, minValue, maxValue, speed))
        {
            if (ImGui::DragFloat("##value", &edited, speed, minValue, maxValue, "%.3f"))
            {
                value = Vans::VansSerializedValue::Float(std::clamp(edited, minValue, maxValue));
                changed = true;
            }
        }
        else if (AudioScalarLimits(label, parentKey, minValue, maxValue, speed))
        {
            if (ImGui::DragFloat("##value", &edited, speed, minValue, maxValue, "%.3f"))
            {
                value = Vans::VansSerializedValue::Float(std::clamp(edited, minValue, maxValue));
                changed = true;
            }
        }
        else if (AudioReverbZoneScalarLimits(label, componentType, minValue, maxValue, speed))
        {
            if (ImGui::DragFloat("##value", &edited, speed, minValue, maxValue, "%.3f"))
            {
                value = Vans::VansSerializedValue::Float(std::clamp(edited, minValue, maxValue));
                changed = true;
            }
        }
        else if (MaterialScalarLimits(label, parentKey, minValue, maxValue, speed))
        {
            if (ImGui::DragFloat("##value", &edited, speed, minValue, maxValue, "%.3f"))
            {
                value = Vans::VansSerializedValue::Float(std::clamp(edited, minValue, maxValue));
                changed = true;
            }
        }
        else if (ImGui::DragFloat("##value", &edited, 0.05f, 0.0f, 0.0f, "%.3f"))
        {
            value = Vans::VansSerializedValue::Float(edited);
            changed = true;
        }
        break;
    }
    case Vans::VansSerializedValue::Kind::String:
    {
        const std::string current = value.stringValue;
        BeginProperty(label);
        if (readOnly)
        {
            ImGui::TextDisabled("%s", current.c_str());
        }
        else if (Lower(label) == "layer")
        {
            if (ImGui::BeginCombo("##value", current.c_str()))
            {
                for (const std::string& option : m_CollisionLayerNames)
                {
                    if (ImGui::Selectable(option.c_str(), current == option))
                    {
                        value = Vans::VansSerializedValue::String(option);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        else if (const auto* options = AudioReverbZoneShapeOptions(label, componentType))
        {
            if (ImGui::BeginCombo("##value", current.c_str()))
            {
                for (const char* option : *options)
                {
                    if (ImGui::Selectable(option, current == option))
                    {
                        value = Vans::VansSerializedValue::String(option);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        else if (const auto* options = EnumOptions(label))
        {
            if (ImGui::BeginCombo("##value", current.c_str()))
            {
                for (const char* option : *options)
                {
                    if (ImGui::Selectable(option, current == option))
                    {
                        value = Vans::VansSerializedValue::String(option);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        else
        {
            char buffer[1024]{};
            CopyToImGuiBuffer(buffer, sizeof(buffer), current);
            if (ImGui::InputText("##value", buffer, sizeof(buffer)))
            {
                value = Vans::VansSerializedValue::String(buffer);
                changed = true;
            }
        }
        break;
    }
    case Vans::VansSerializedValue::Kind::Null:
    default:
        BeginProperty(label);
        ImGui::TextDisabled("None");
        break;
    }
    ImGui::PopID();
    return changed;
}

bool VansInspectorWindow::Impl::DrawComponent(Vans::EditorAPI::IEngineEditorAPI& api,
    Vans::VansSerializedValue& component,
    const std::string& pointer, bool& removeRequested)
{
    if (component.kind != Vans::VansSerializedValue::Kind::Object)
        component = Vans::VansSerializedValue::Object({});

    const std::string type = Vans::ReadSerializedStringField(component, "type", "Component");
    ImGui::PushID(pointer.c_str());
    bool enabled = Vans::ReadSerializedBoolField(component, "enabled", true);
    bool changed = false;
    if (ImGui::Checkbox("##enabled", &enabled))
    {
        if (enabled != Vans::ReadSerializedBoolField(component, "enabled", true))
        {
            Vans::SetSerializedObjectField(component, "enabled", Vans::VansSerializedValue::Bool(enabled));
            changed = true;
        }
    }
    ImGui::SameLine();
    const bool open = ImGui::CollapsingHeader(type.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::BeginDragDropSource())
    {
        Vans::EditorObjectHandle handle;
        handle.domain = Vans::EditorObjectDomain::SceneComponent;
        handle.guid = Vans::ReadSerializedStringField(component, "id");
        handle.entityGuid = Vans::VansEditorSelection::EntityGuid();
        handle.componentGuid = handle.guid;
        handle.componentType = type;
        handle.displayName = type;
        const std::string payload = Vans::SerializeEditorObjectHandle(handle);
        ImGui::SetDragDropPayload(Vans::VansObjectReferenceDragPayloadType,
            payload.c_str(),
            payload.size() + 1);
        ImGui::TextUnformatted(type.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginPopupContextItem("ComponentMenu"))
    {
        if (type != "Transform" && ImGui::MenuItem("Remove Component")) removeRequested = true;
        ImGui::TextDisabled("ID: %s", Vans::ReadSerializedStringField(component, "id").c_str());
        ImGui::EndPopup();
    }
    if (open)
    {
        ImGui::Indent(8.0f);
        Vans::VansSerializedValue* data = Vans::FindObjectField(component, "data");
        if (!data || data->kind != Vans::VansSerializedValue::Kind::Object)
        {
            Vans::SetSerializedObjectField(component, "data", Vans::VansSerializedValue::Object({}));
            data = Vans::FindObjectField(component, "data");
        }

        std::vector<Vans::LuaScriptFieldDescriptor> scriptFieldDescriptors;
        if (type == "Script")
        {
            changed |= MergeLuaScriptFieldDefaults(
                *data,
                api,
                &scriptFieldDescriptors);
        }

        if (data && data->kind == Vans::VansSerializedValue::Kind::Object)
        {
            for (auto& [fieldName, fieldValue] : data->objectFields)
            {
                if (type == "Script" &&
                    fieldName == "fields" &&
                    fieldValue.kind == Vans::VansSerializedValue::Kind::Object)
                {
                    if (ImGui::TreeNodeEx("Fields", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        changed |= DrawSerializedLuaScriptFields(
                            fieldValue,
                            scriptFieldDescriptors,
                            pointer + "/data/fields");
                        ImGui::TreePop();
                    }
                    continue;
                }

                if (fieldName == "materialOverrides" &&
                    fieldValue.kind == Vans::VansSerializedValue::Kind::Object)
                {
                    const Vans::ObjectReferenceSlotDescriptor materialSlot =
                        Vans::VansEditorPropertyDescriptorRegistry::ProjectAssetReferenceSlot(
                            Vans::EditorAPI::AssetType::Material);
                    bool overridesChanged = false;
                    if (fieldValue.objectFields.empty())
                    {
                        Vans::VansSerializedValue reference;
                        Vans::NormalizeObjectReferenceSlotValue(
                            reference,
                            materialSlot);
                        if (DrawSerializedAssetReference(
                            "Material 0",
                            reference,
                            pointer + "/data/materialOverrides/default",
                            materialSlot) &&
                            !Vans::ReadObjectReferenceSlotHandle(
                                reference,
                                materialSlot).guid.empty())
                        {
                            Vans::SetSerializedObjectField(fieldValue, "default", std::move(reference));
                            overridesChanged = true;
                        }
                    }
                    else
                    {
                        std::size_t slotIndex = 0;
                        for (auto& [slotName, slotValue] : fieldValue.objectFields)
                        {
                            overridesChanged |= DrawSerializedAssetReference(
                                "Material " + std::to_string(slotIndex),
                                slotValue,
                                pointer + "/data/materialOverrides/" + EscapePointerToken(slotName),
                                materialSlot);
                            ++slotIndex;
                        }
                    }
                    changed |= overridesChanged;
                    continue;
                }

                changed |= DrawSerializedValue(
                    fieldName,
                    fieldValue,
                    pointer + "/data/" + EscapePointerToken(fieldName),
                    false,
                    type,
                    "data");
            }
            if (data->objectFields.empty()) ImGui::TextDisabled("No properties");
        }
        ImGui::Unindent(8.0f);
    }
    if (type == "Vehicle" && changed)
        m_PendingVehicleRebuild = true;
    ImGui::PopID();
    return changed;
}

void VansInspectorWindow::Impl::DrawSceneEntity(Vans::EditorAPI::IEngineEditorAPI& api)
{
    Vans::VansSceneDocument* document = VansEditorWindow::GetSceneDocument();
    Vans::VansSceneEditService* editor = VansEditorWindow::GetSceneEditService();
    if (!document || !editor) return;
    const std::string& selected = Vans::VansEditorSelection::EntityGuid();
    const Vans::VansSerializedValue sceneRoot = document->SerializedRootSnapshot();
    const Vans::VansSerializedValue* entities = Vans::FindObjectField(sceneRoot, "entities");
    if (!entities || entities->kind != Vans::VansSerializedValue::Kind::Array)
    {
        ImGui::TextDisabled("Scene document has no entities");
        return;
    }

    for (std::size_t index = 0; index < entities->arrayItems.size(); ++index)
    {
        const Vans::VansSerializedValue& selectedEntity = entities->arrayItems[index];
        if (Vans::ReadSerializedStringField(selectedEntity, "id") != selected) continue;
        m_PendingObjectReferenceEdit.reset();
        Vans::VansSerializedValue editedEntity = selectedEntity;
        const std::string pointer = "/entities/" + std::to_string(index);
        bool changed = false;

        char name[256]{};
        CopyToImGuiBuffer(name, sizeof(name),
            Vans::ReadSerializedStringField(editedEntity, "name", "Entity"));
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##EntityName", name, sizeof(name)))
        {
            Vans::SetSerializedObjectField(
                editedEntity,
                "name",
                Vans::VansSerializedValue::String(name));
            changed = true;
        }
        ImGui::TextDisabled("Entity %s", selected.substr(0, 8).c_str());
        ImGui::Separator();

        Vans::VansSerializedValue* editedComponents =
            Vans::FindObjectField(editedEntity, "components");
        if (editedComponents && editedComponents->kind == Vans::VansSerializedValue::Kind::Array)
        {
            for (std::size_t componentIndex = 0; componentIndex < editedComponents->arrayItems.size();)
            {
                bool remove = false;
                changed |= DrawComponent(api, editedComponents->arrayItems[componentIndex],
                    pointer + "/components/" + std::to_string(componentIndex), remove);
                if (remove)
                {
                    editedComponents->arrayItems.erase(
                        editedComponents->arrayItems.begin() + componentIndex);
                    changed = true;
                }
                else ++componentIndex;
            }
        }

        if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f))) ImGui::OpenPopup("AddComponent");
        if (ImGui::BeginPopup("AddComponent"))
        {
            static const char* types[] = { "ModelRenderer", "Physics", "Camera", "Animation",
                "CharacterController", "DirectionalLight", "PointLight", "SpotLight", "RectLight",
                "Audio", "AudioVolume", "AudioReverbZone", "Video", "Particle", "Cloth", "Vehicle", "Script" };
            for (const char* type : types)
            {
                const bool singleton = std::strcmp(type, "ModelRenderer") == 0 || std::strcmp(type, "Physics") == 0;
                bool alreadyPresent = false;
                if (singleton)
                {
                    if (editedComponents && editedComponents->kind == Vans::VansSerializedValue::Kind::Array)
                    {
                        for (const Vans::VansSerializedValue& component : editedComponents->arrayItems)
                        {
                            if (Vans::ReadSerializedStringField(component, "type") == type)
                            {
                                alreadyPresent = true;
                                break;
                            }
                        }
                    }
                }
                if (alreadyPresent) ImGui::BeginDisabled();
                const bool selectedType = ImGui::Selectable(type);
                if (alreadyPresent) ImGui::EndDisabled();
                if (!selectedType || alreadyPresent) continue;
                if (!editedComponents || editedComponents->kind != Vans::VansSerializedValue::Kind::Array)
                {
                    Vans::SetSerializedObjectField(
                        editedEntity,
                        "components",
                        Vans::VansSerializedValue::Array({}));
                    editedComponents = Vans::FindObjectField(editedEntity, "components");
                }
                if (editedComponents)
                    editedComponents->arrayItems.push_back(MakeSerializedComponent(type));
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (changed)
        {
            Vans::SceneEditResult result;
            if (m_PendingObjectReferenceEdit)
            {
                const Vans::ObjectReferenceAssignment edit = *m_PendingObjectReferenceEdit;
                result = editor->SetAndAssignObjectReference(
                    Vans::MakeDocumentPropertyPath(Vans::DocumentPropertySpace::Scene, pointer),
                    editedEntity,
                    edit);
                m_PendingObjectReferenceEdit.reset();
            }
            else
            {
                result = editor->Set(
                    Vans::MakeDocumentPropertyPath(Vans::DocumentPropertySpace::Scene, pointer),
                    editedEntity);
            }
            if (!result) VANS_LOG_ERROR("[Inspector] " << result.message);
            else
            {
                const Vans::EditorAPI::RuntimeEntityPreviewChange previewChange =
                    BuildRuntimeEntityPreviewChange(editedEntity);
                api.ApplyRuntimeEntityPreviewChange(previewChange);
            }
        }
        return;
    }
    ImGui::TextDisabled("Selected entity no longer exists");
}

void VansInspectorWindow::Impl::DrawSceneSettings()
{
    Vans::VansSceneDocument* document = VansEditorWindow::GetSceneDocument();
    Vans::VansSceneEditService* editor = VansEditorWindow::GetSceneEditService();
    if (!document || !editor) return;
    const Vans::VansSerializedValue sceneRoot = document->SerializedRootSnapshot();
    const Vans::VansSerializedValue* settingsValue = Vans::FindObjectField(sceneRoot, "settings");
    Vans::VansSerializedValue settings = settingsValue
        ? *settingsValue
        : Vans::VansSerializedValue::Object({});
    if (DrawSerializedValue("Scene Settings", settings, "/settings"))
    {
        const Vans::SceneEditResult result = editor->Set(
            Vans::MakeDocumentPropertyPath(Vans::DocumentPropertySpace::Scene, "/settings"),
            std::move(settings));
        if (!result) VANS_LOG_ERROR("[Inspector] " << result.message);
    }
}

bool VansInspectorWindow::Impl::LoadAssetDocuments(const std::filesystem::path& sourcePath)
{
    if (sourcePath != m_AssetPath)
        m_AudioPreview.Stop();
    m_AssetPath = sourcePath;
    m_AssetDocuments = Vans::VansAssetDocumentRegistry::Get().GetOrOpen(sourcePath);
    m_Error = m_AssetDocuments ? m_AssetDocuments->lastError : "Cannot open asset document";
    return m_AssetDocuments &&
        (m_AssetDocuments->sourceDocument.IsLoaded() || m_AssetDocuments->metaDocument.IsLoaded());
}

bool VansInspectorWindow::Impl::SaveAssetDocuments(bool reloadSceneOnSuccess)
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

void VansInspectorWindow::Impl::DrawAudioAssetPreview(
    const std::filesystem::path& sourcePath,
    const Vans::VansSerializedValue& metaRoot)
{
    if (!IsAudioMetaRoot(metaRoot))
        return;

    const std::filesystem::path previewPath = NormalizePreviewPath(sourcePath);
    const Vans::VansSerializedValue* settings = Vans::FindObjectField(metaRoot, "settings");
    const VansEngine::VansAudioPreviewSettings previewSettings = BuildAudioPreviewSettings(settings);

    const bool sameAsset = !m_AudioPreview.CurrentPath().empty() &&
        NormalizePreviewPath(m_AudioPreview.CurrentPath()).wstring() == previewPath.wstring();
    const bool playing = sameAsset && m_AudioPreview.IsPlaying();

    ImGui::Separator();
    ImGui::TextUnformatted("Preview");
    ImGui::PushID("AudioAssetPreview");
    if (playing)
        ImGui::BeginDisabled();
    if (ImGui::Button("Play"))
    {
        std::string previewError;
        if (!m_AudioPreview.Play(previewPath, previewSettings, previewError))
            m_Error = previewError;
        else
            m_Error.clear();
    }
    if (playing)
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (!playing)
        ImGui::BeginDisabled();
    if (ImGui::Button("Stop"))
        m_AudioPreview.Stop();
    if (!playing)
        ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", playing ? "Playing" : "Stopped");
    ImGui::TextDisabled(
        "%s  volume %.2f  pitch %.2f  bus %s%s",
        previewSettings.streaming ? "Streaming" : "Static",
        previewSettings.volume,
        previewSettings.pitch,
        previewSettings.bus.c_str(),
        previewSettings.spatial ? "  spatial" : "");
    ImGui::PopID();
}

void VansInspectorWindow::Impl::DrawAsset(Vans::EditorAPI::IEngineEditorAPI& api)
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
        Vans::VansSerializedValue displayRootValue =
            m_AssetDocuments->sourceDocument.SerializedRootSnapshot();
        if (displayRootValue.kind != Vans::VansSerializedValue::Kind::Object)
            displayRootValue = Vans::VansSerializedValue::Object({});
        Vans::MergeMaterialAuthoringSchema(api, displayRootValue);
        for (auto& [propertyKey, propertyValue] : displayRootValue.objectFields)
        {
            const std::string propertyPointer = "/asset/" + EscapePointerToken(propertyKey);
            const bool identity = propertyKey == "schemaVersion" || propertyKey == "guid";
            if (DrawSerializedValue(propertyKey, propertyValue, propertyPointer, identity))
            {
                bool editApplied = false;
                if (m_PendingObjectReferenceEdit)
                {
                    const Vans::ObjectReferenceAssignment edit = *m_PendingObjectReferenceEdit;
                    const Vans::AssetDocumentEditResult result =
                        Vans::VansAssetDocumentEditService::SetAndAssignObjectReference(
                            m_AssetDocuments->sourceDocument,
                            Vans::MakeInspectorDocumentPropertyPath(propertyPointer),
                            propertyValue,
                            edit);
                    if (!result)
                        VANS_LOG_ERROR("[Inspector] " << result.message);
                    editApplied = static_cast<bool>(result);
                    m_PendingObjectReferenceEdit.reset();
                }
                else
                {
                    const Vans::AssetDocumentEditResult result =
                        Vans::VansAssetDocumentEditService::Set(
                            m_AssetDocuments->sourceDocument,
                            Vans::MakeInspectorDocumentPropertyPath(propertyPointer),
                            propertyValue);
                    if (!result)
                        VANS_LOG_ERROR("[Inspector] " << result.message);
                    editApplied = static_cast<bool>(result);
                }

                if (editApplied)
                    api.ApplyRuntimeMaterialPreviewChange(
                        Vans::BuildRuntimeMaterialPreviewChange(
                            selected,
                            m_AssetDocuments->sourceDocument.SerializedRootSnapshot(),
                            propertyPointer));
                break;
            }
        }
    }
    else ImGui::TextDisabled("Binary asset");

    if (m_AssetDocuments && m_AssetDocuments->metaDocument.IsLoaded())
    {
        const Vans::VansSerializedValue metaRoot = m_AssetDocuments->metaDocument.SerializedRootSnapshot();
        const bool audioMeta = IsAudioMetaRoot(metaRoot);
        DrawAudioAssetPreview(selected, metaRoot);
        if (const Vans::VansSerializedValue* settingsValue = Vans::FindObjectField(metaRoot, "settings"))
        {
            Vans::VansSerializedValue editedSettings = *settingsValue;
            const bool normalizedSettings =
                audioMeta ? NormalizeAudioImportSettings(editedSettings) : false;
            if (DrawSerializedValue("Import Settings", editedSettings, "/meta/settings") ||
                normalizedSettings)
            {
                const Vans::AssetDocumentEditResult result =
                    Vans::VansAssetDocumentEditService::Set(
                        m_AssetDocuments->metaDocument,
                        Vans::MakeDocumentPropertyPath(Vans::DocumentPropertySpace::AssetMeta, "/settings"),
                        std::move(editedSettings));
                if (!result)
                    VANS_LOG_ERROR("[Inspector] " << result.message);
            }
        }
        else if (audioMeta)
        {
            Vans::VansSerializedValue editedSettings = Vans::VansSerializedValue::Object({});
            NormalizeAudioImportSettings(editedSettings);
            const Vans::AssetDocumentEditResult result =
                Vans::VansAssetDocumentEditService::Set(
                    m_AssetDocuments->metaDocument,
                    Vans::MakeDocumentPropertyPath(Vans::DocumentPropertySpace::AssetMeta, "/settings"),
                    std::move(editedSettings));
            if (!result)
                VANS_LOG_ERROR("[Inspector] " << result.message);
        }
        if (ImGui::TreeNode("Asset Identity"))
        {
            if (const Vans::VansSerializedValue* guid = Vans::FindObjectField(metaRoot, "guid"))
            {
                Vans::VansSerializedValue displayGuid = *guid;
                DrawSerializedValue("GUID", displayGuid, "/meta/guid", true);
            }
            if (const Vans::VansSerializedValue* importer = Vans::FindObjectField(metaRoot, "importer"))
            {
                Vans::VansSerializedValue displayImporter = *importer;
                DrawSerializedValue("Importer", displayImporter, "/meta/importer", true);
            }
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

void VansInspectorWindow::Impl::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& api)
{
    m_AudioPreview.Tick();
    ImGui::Begin("Inspector");
    m_ActiveAPI = &api;
    m_CollisionLayerNames = api.GetRuntimeCollisionLayerNames();
    m_PendingObjectReferenceEdit.reset();
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
