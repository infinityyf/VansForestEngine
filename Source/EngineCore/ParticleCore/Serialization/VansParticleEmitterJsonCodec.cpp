#include "VansParticleEmitterJsonCodec.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace VansGraphics
{
namespace
{
Vans::ParticleJson EncodeCurveKeys(const std::vector<CurveKey>& curve)
{
    auto keys = Vans::ParticleJson::array();
    for (const auto& key : curve)
        keys.push_back({ {"t", key.t}, {"value", key.value} });
    return keys;
}

void DecodeCurveKeys(const Vans::ParticleJson& root, std::vector<CurveKey>& curve)
{
    curve.clear();
    if (!root.is_array())
        return;

    for (const auto& key : root)
        curve.push_back({ key.value("t", 0.0f), key.value("value", 0.0f) });
}

void ValidateNormalizedCurve(const std::vector<CurveKey>& curve,
    const char* field, bool requireNonNegativeValues)
{
    if (curve.empty())
        throw std::invalid_argument(std::string(field) + " must contain at least one key");
    float previousTime = -1.0f;
    for (const CurveKey& key : curve)
    {
        const bool validValue = std::isfinite(key.value) &&
            (!requireNonNegativeValues || key.value >= 0.0f);
        if (!std::isfinite(key.t) || key.t < 0.0f || key.t > 1.0f ||
            key.t <= previousTime || !validValue)
        {
            throw std::invalid_argument(std::string(field) +
                " keys must use strictly increasing normalized t values and valid values");
        }
        previousTime = key.t;
    }
}

void ValidateColorGradient(const VansColorGradient& gradient)
{
    if (gradient.m_Stops.empty())
        throw std::invalid_argument(
            "UpdateColorOverLifetime.gradient must contain at least one stop");
    float previousTime = -1.0f;
    for (const ColorGradientStop& stop : gradient.m_Stops)
    {
        const bool finiteColor = std::isfinite(stop.color.r) &&
            std::isfinite(stop.color.g) && std::isfinite(stop.color.b) &&
            std::isfinite(stop.color.a);
        const bool validColor = glm::all(glm::greaterThanEqual(
            glm::vec3(stop.color), glm::vec3(0.0f))) &&
            stop.color.a >= 0.0f && stop.color.a <= 1.0f;
        if (!std::isfinite(stop.t) || stop.t < 0.0f || stop.t > 1.0f ||
            stop.t <= previousTime || !finiteColor || !validColor)
        {
            throw std::invalid_argument(
                "UpdateColorOverLifetime.gradient stops must be ordered in normalized time with non-negative RGB and alpha in [0,1]");
        }
        previousTime = stop.t;
    }
}

void ValidateNonNegativeFloatCurve(const VansFloatCurve& curve,
    const char* field, float minimumValue)
{
    const auto requireRange = [field, minimumValue](float minimum, float maximum)
    {
        if (!std::isfinite(minimum) || !std::isfinite(maximum) ||
            minimum < minimumValue || maximum < minimum)
        {
            throw std::invalid_argument(std::string(field) +
                " must contain a finite non-decreasing non-negative range");
        }
    };
    const auto requireKeys = [field, minimumValue](
        const std::vector<CurveKey>& keys)
    {
        ValidateNormalizedCurve(keys, field, true);
        for (const CurveKey& key : keys)
            if (key.value < minimumValue)
                throw std::invalid_argument(std::string(field) +
                    " contains a value below its supported minimum");
    };
    switch (curve.m_Mode)
    {
    case FloatCurveMode::Constant:
        requireRange(curve.m_Value, curve.m_Value);
        break;
    case FloatCurveMode::RandomBetween:
        requireRange(curve.m_Min, curve.m_Max);
        break;
    case FloatCurveMode::Curve:
        requireKeys(curve.m_Keys);
        break;
    case FloatCurveMode::RandomBetweenCurves:
        requireKeys(curve.m_MinKeys);
        requireKeys(curve.m_MaxKeys);
        break;
    }
}

Vans::ParticleJson EncodeFloatCurve(const VansFloatCurve& curve)
{
    Vans::ParticleJson root;
    switch (curve.m_Mode)
    {
    case FloatCurveMode::Constant:
        root["mode"] = "Constant";
        root["value"] = curve.m_Value;
        break;
    case FloatCurveMode::RandomBetween:
        root["mode"] = "RandomBetween";
        root["min"] = curve.m_Min;
        root["max"] = curve.m_Max;
        break;
    case FloatCurveMode::Curve:
        root["mode"] = "Curve";
        root["keys"] = EncodeCurveKeys(curve.m_Keys);
        break;
    case FloatCurveMode::RandomBetweenCurves:
        root["mode"] = "RandomBetweenCurves";
        root["minKeys"] = EncodeCurveKeys(curve.m_MinKeys);
        root["maxKeys"] = EncodeCurveKeys(curve.m_MaxKeys);
        break;
    }
    return root;
}

void DecodeFloatCurve(const Vans::ParticleJson& root, VansFloatCurve& curve)
{
    const std::string mode = root.value("mode", "Constant");
    if (mode == "Constant")
    {
        curve.m_Mode = FloatCurveMode::Constant;
        curve.m_Value = root.value("value", 1.0f);
    }
    else if (mode == "RandomBetween")
    {
        curve.m_Mode = FloatCurveMode::RandomBetween;
        curve.m_Min = root.value("min", 0.0f);
        curve.m_Max = root.value("max", 1.0f);
    }
    else if (mode == "Curve")
    {
        curve.m_Mode = FloatCurveMode::Curve;
        if (root.contains("keys"))
            DecodeCurveKeys(root["keys"], curve.m_Keys);
    }
    else if (mode == "RandomBetweenCurves")
    {
        curve.m_Mode = FloatCurveMode::RandomBetweenCurves;
        if (root.contains("minKeys"))
            DecodeCurveKeys(root["minKeys"], curve.m_MinKeys);
        if (root.contains("maxKeys"))
            DecodeCurveKeys(root["maxKeys"], curve.m_MaxKeys);
    }
}

Vans::ParticleJson EncodeColorGradient(const VansColorGradient& gradient)
{
    auto stops = Vans::ParticleJson::array();
    for (const auto& stop : gradient.m_Stops)
    {
        stops.push_back({
            {"t", stop.t},
            {"color", { stop.color.r, stop.color.g, stop.color.b, stop.color.a }}
        });
    }
    return Vans::ParticleJson{ {"stops", std::move(stops)} };
}

void DecodeColorGradient(const Vans::ParticleJson& root, VansColorGradient& gradient)
{
    gradient.m_Stops.clear();
    if (!root.contains("stops"))
        return;

    for (const auto& stopJson : root["stops"])
    {
        ColorGradientStop stop;
        stop.t = stopJson.value("t", 0.0f);
        if (stopJson.contains("color") && stopJson["color"].is_array() && stopJson["color"].size() >= 4)
        {
            stop.color = glm::vec4(
                stopJson["color"][0].get<float>(),
                stopJson["color"][1].get<float>(),
                stopJson["color"][2].get<float>(),
                stopJson["color"][3].get<float>());
        }
        gradient.m_Stops.push_back(stop);
    }
}

Vans::ParticleJson EncodeSpawnConfig(const VansParticleSpawnConfig& config)
{
    Vans::ParticleJson root;
    switch (config.m_Type)
    {
    case VansSpawnType::RateOverTime:
        root["type"] = "RateOverTime";
        root["rate"] = config.m_Rate;
        break;
    case VansSpawnType::Burst:
    {
        root["type"] = "Burst";
        auto bursts = Vans::ParticleJson::array();
        for (const auto& burst : config.m_Bursts)
        {
            bursts.push_back({
                {"time", burst.time},
                {"count", burst.count},
                {"cycles", burst.cycles},
                {"interval", burst.interval}
            });
        }
        root["bursts"] = std::move(bursts);
        break;
    }
    default:
        root["type"] = "RateOverTime";
        root["rate"] = config.m_Rate;
        break;
    }
    return root;
}

void DecodeSpawnConfig(const Vans::ParticleJson& root, VansParticleSpawnConfig& config)
{
    const std::string type = root.value("type", "RateOverTime");
    if (type == "Burst")
    {
        config.m_Type = VansSpawnType::Burst;
        config.m_Bursts.clear();
        if (root.contains("bursts"))
        {
            for (const auto& burstJson : root["bursts"])
            {
                BurstConfig burst;
                burst.time = burstJson.value("time", 0.0f);
                burst.count = burstJson.value("count", 10u);
                burst.cycles = burstJson.value("cycles", 1u);
                burst.interval = burstJson.value("interval", 0.1f);
                config.m_Bursts.push_back(burst);
            }
        }
    }
    else
    {
        config.m_Type = VansSpawnType::RateOverTime;
        config.m_Rate = root.value("rate", 30.0f);
    }
}

Vans::ParticleJson EncodeSixWayLighting(const VansParticleSixWayLightingConfig& config)
{
    Vans::ParticleJson root;
    root["enabled"] = config.m_Enabled;
    root["positiveAxesTexture"] = config.m_PositiveAxesTexture;
    root["negativeAxesTexture"] = config.m_NegativeAxesTexture;
    root["columns"] = config.m_Columns;
    root["rows"] = config.m_Rows;
    root["fps"] = config.m_FPS;
    root["alphaFromPositiveA"] = config.m_AlphaFromPositiveA;
    root["emissiveFromNegativeA"] = config.m_EmissiveFromNegativeA;
    root["lightIntensity"] = config.m_LightIntensity;
    root["ambientIntensity"] = config.m_AmbientIntensity;
    root["emissiveIntensity"] = config.m_EmissiveIntensity;
    root["absorptionStrength"] = config.m_AbsorptionStrength;
    root["lightmapRemapMin"] = config.m_LightmapRemapMin;
    root["lightmapRemapMax"] = config.m_LightmapRemapMax;
    return root;
}

void DecodeSixWayLighting(const Vans::ParticleJson& root, VansParticleSixWayLightingConfig& config)
{
    config.m_Enabled = root.value("enabled", false);
    config.m_PositiveAxesTexture = root.value("positiveAxesTexture", "");
    config.m_NegativeAxesTexture = root.value("negativeAxesTexture", "");
    config.m_Columns = root.value("columns", 1);
    config.m_Rows = root.value("rows", 1);
    config.m_FPS = root.value("fps", 0.0f);
    config.m_AlphaFromPositiveA = root.value("alphaFromPositiveA", true);
    config.m_EmissiveFromNegativeA = root.value("emissiveFromNegativeA", true);
    config.m_LightIntensity = root.value("lightIntensity", 1.0f);
    config.m_AmbientIntensity = root.value("ambientIntensity", 0.25f);
    config.m_EmissiveIntensity = root.value("emissiveIntensity", 1.0f);
    config.m_AbsorptionStrength = root.value("absorptionStrength", 0.0f);
    config.m_LightmapRemapMin = root.value("lightmapRemapMin", 0.0f);
    config.m_LightmapRemapMax = root.value("lightmapRemapMax", 1.0f);
}

Vans::ParticleJson EncodeVolumetricConfig(const VansParticleVolumetricConfig& config)
{
    return {
        { "enabled", config.m_Enabled },
        { "keepSurfaceRenderer", config.m_KeepSurfaceRenderer },
        { "radiusScale", config.m_RadiusScale },
        { "maxDistanceMeters", config.m_MaxDistanceMeters },
        { "densityMultiplier", config.m_DensityMultiplier },
        { "extinctionPerMeter", config.m_ExtinctionPerMeter },
        { "singleScatteringAlbedo", {
            config.m_SingleScatteringAlbedo.r,
            config.m_SingleScatteringAlbedo.g,
            config.m_SingleScatteringAlbedo.b } },
        { "anisotropy", config.m_Anisotropy },
        { "emissivePerMeter", {
            config.m_EmissivePerMeter.r,
            config.m_EmissivePerMeter.g,
            config.m_EmissivePerMeter.b } },
        { "edgeSoftness", config.m_EdgeSoftness },
        { "directLightingScale", config.m_DirectLightingScale },
        { "skyLightingScale", config.m_SkyLightingScale },
        { "receiveCloudShadows", config.m_ReceiveCloudShadows },
        { "injectionPriority", config.m_InjectionPriority }
    };
}

bool ShouldEncodeVolumetricConfig(const VansParticleVolumetricConfig& config)
{
    // Keep legacy assets unchanged, but retain authored tuning while the optional
    // renderer is disabled so artists can configure it before opting in.
    const VansParticleVolumetricConfig defaults;
    return config.m_Enabled != defaults.m_Enabled ||
        config.m_KeepSurfaceRenderer != defaults.m_KeepSurfaceRenderer ||
        config.m_RadiusScale != defaults.m_RadiusScale ||
        config.m_MaxDistanceMeters != defaults.m_MaxDistanceMeters ||
        config.m_DensityMultiplier != defaults.m_DensityMultiplier ||
        config.m_ExtinctionPerMeter != defaults.m_ExtinctionPerMeter ||
        glm::any(glm::notEqual(
            config.m_SingleScatteringAlbedo, defaults.m_SingleScatteringAlbedo)) ||
        config.m_Anisotropy != defaults.m_Anisotropy ||
        glm::any(glm::notEqual(config.m_EmissivePerMeter, defaults.m_EmissivePerMeter)) ||
        config.m_EdgeSoftness != defaults.m_EdgeSoftness ||
        config.m_DirectLightingScale != defaults.m_DirectLightingScale ||
        config.m_SkyLightingScale != defaults.m_SkyLightingScale ||
        config.m_ReceiveCloudShadows != defaults.m_ReceiveCloudShadows ||
        config.m_InjectionPriority != defaults.m_InjectionPriority;
}

void DecodeVolumetricConfig(
    const Vans::ParticleJson& root, VansParticleVolumetricConfig& config)
{
    config.m_Enabled = root.value("enabled", false);
    config.m_KeepSurfaceRenderer = root.value("keepSurfaceRenderer", false);
    config.m_RadiusScale = root.value("radiusScale", 1.0f);
    config.m_MaxDistanceMeters = root.value("maxDistanceMeters", 100.0f);
    config.m_DensityMultiplier = root.value("densityMultiplier", 1.0f);
    config.m_ExtinctionPerMeter = root.value("extinctionPerMeter", 0.1f);
    config.m_Anisotropy = root.value("anisotropy", 0.0f);
    config.m_EdgeSoftness = root.value("edgeSoftness", 0.35f);
    config.m_DirectLightingScale = root.value("directLightingScale", 1.0f);
    config.m_SkyLightingScale = root.value("skyLightingScale", 1.0f);
    config.m_ReceiveCloudShadows = root.value("receiveCloudShadows", true);
    config.m_InjectionPriority = root.value("injectionPriority", 128u);
    auto readVec3 = [](const Vans::ParticleJson& value, glm::vec3 fallback)
    {
        if (!value.is_array() || value.size() < 3)
            return fallback;
        return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
    };
    if (root.contains("singleScatteringAlbedo"))
        config.m_SingleScatteringAlbedo = readVec3(
            root["singleScatteringAlbedo"], glm::vec3(0.9f));
    if (root.contains("emissivePerMeter"))
        config.m_EmissivePerMeter = readVec3(
            root["emissivePerMeter"], glm::vec3(0.0f));

    const auto isFiniteVec3 = [](const glm::vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
            std::isfinite(value.z);
    };

    const bool finite = std::isfinite(config.m_RadiusScale) &&
        std::isfinite(config.m_MaxDistanceMeters) &&
        std::isfinite(config.m_DensityMultiplier) &&
        std::isfinite(config.m_ExtinctionPerMeter) &&
        std::isfinite(config.m_Anisotropy) &&
        std::isfinite(config.m_EdgeSoftness) &&
        std::isfinite(config.m_DirectLightingScale) &&
        std::isfinite(config.m_SkyLightingScale) &&
        isFiniteVec3(config.m_SingleScatteringAlbedo) &&
        isFiniteVec3(config.m_EmissivePerMeter);
    const bool ranges = config.m_RadiusScale > 0.0f &&
        config.m_MaxDistanceMeters > 0.0f &&
        config.m_DensityMultiplier >= 0.0f &&
        config.m_ExtinctionPerMeter >= 0.0f &&
        config.m_Anisotropy >= -0.9f && config.m_Anisotropy <= 0.9f &&
        config.m_EdgeSoftness > 0.0f && config.m_EdgeSoftness <= 1.0f &&
        config.m_DirectLightingScale >= 0.0f &&
        config.m_SkyLightingScale >= 0.0f &&
        glm::all(glm::greaterThanEqual(config.m_SingleScatteringAlbedo, glm::vec3(0.0f))) &&
        glm::all(glm::lessThanEqual(config.m_SingleScatteringAlbedo, glm::vec3(1.0f))) &&
        glm::all(glm::greaterThanEqual(config.m_EmissivePerMeter, glm::vec3(0.0f))) &&
        config.m_InjectionPriority <= 255u;
    if (!finite || !ranges)
        throw std::invalid_argument("renderer.volumetric contains invalid medium parameters");
}

Vans::ParticleJson EncodeRendererConfig(const VansParticleRendererConfig& config)
{
    auto typeToString = [](VansParticleRendererType type) -> std::string {
        switch (type)
        {
        case VansParticleRendererType::StretchedBillboard: return "StretchedBillboard";
        case VansParticleRendererType::Mesh: return "Mesh";
        default: return "Billboard";
        }
    };
    auto blendToString = [](VansParticleBlendMode blend) -> std::string {
        switch (blend)
        {
        case VansParticleBlendMode::Alpha: return "Alpha";
        case VansParticleBlendMode::Multiply: return "Multiply";
        default: return "Additive";
        }
    };
    auto sortToString = [](VansParticleSortMode sort) -> std::string {
        switch (sort)
        {
        case VansParticleSortMode::ByDistance: return "ByDistance";
        case VansParticleSortMode::OldestFirst: return "OldestFirst";
        case VansParticleSortMode::NewestFirst: return "NewestFirst";
        default: return "None";
        }
    };
    auto lightingToString = [](VansParticleLightingMode mode) -> std::string {
        switch (mode)
        {
        case VansParticleLightingMode::SixWayLit: return "SixWayLit";
        default: return "UnlitFlipbook";
        }
    };

    Vans::ParticleJson root;
    root["type"] = typeToString(config.m_Type);
    root["texture"] = config.m_Texture;
    root["blendMode"] = blendToString(config.m_BlendMode);
    root["spriteSheet"] = {
        {"enabled", config.m_SpriteSheetEnabled},
        {"columns", config.m_SpriteColumns},
        {"rows", config.m_SpriteRows}
    };
    root["sortMode"] = sortToString(config.m_SortMode);
    root["lightingMode"] = lightingToString(config.m_LightingMode);
    root["sixWayLighting"] = EncodeSixWayLighting(config.m_SixWayLighting);
    if (ShouldEncodeVolumetricConfig(config.m_Volumetric))
        root["volumetric"] = EncodeVolumetricConfig(config.m_Volumetric);
    root["castShadows"] = config.m_CastShadows;
    root["receiveShadows"] = config.m_ReceiveShadows;
    return root;
}

void DecodeRendererConfig(const Vans::ParticleJson& root, VansParticleRendererConfig& config)
{
    const std::string type = root.value("type", "Billboard");
    if (type == "StretchedBillboard") config.m_Type = VansParticleRendererType::StretchedBillboard;
    else if (type == "Mesh") config.m_Type = VansParticleRendererType::Mesh;
    else config.m_Type = VansParticleRendererType::Billboard;

    config.m_Texture = root.value("texture", "");

    const std::string blend = root.value("blendMode", "Additive");
    if (blend == "Alpha") config.m_BlendMode = VansParticleBlendMode::Alpha;
    else if (blend == "Multiply") config.m_BlendMode = VansParticleBlendMode::Multiply;
    else config.m_BlendMode = VansParticleBlendMode::Additive;

    if (root.contains("spriteSheet"))
    {
        const auto& spriteSheet = root["spriteSheet"];
        config.m_SpriteSheetEnabled = spriteSheet.value("enabled", false);
        config.m_SpriteColumns = spriteSheet.value("columns", 4);
        config.m_SpriteRows = spriteSheet.value("rows", 4);
    }

    const std::string sort = root.value("sortMode", "None");
    if (sort == "ByDistance") config.m_SortMode = VansParticleSortMode::ByDistance;
    else if (sort == "OldestFirst") config.m_SortMode = VansParticleSortMode::OldestFirst;
    else if (sort == "NewestFirst") config.m_SortMode = VansParticleSortMode::NewestFirst;
    else config.m_SortMode = VansParticleSortMode::None;

    const std::string lightingMode = root.value("lightingMode", "UnlitFlipbook");
    if (lightingMode == "SixWayLit") config.m_LightingMode = VansParticleLightingMode::SixWayLit;
    else config.m_LightingMode = VansParticleLightingMode::UnlitFlipbook;

    if (root.contains("sixWayLighting"))
    {
        DecodeSixWayLighting(root["sixWayLighting"], config.m_SixWayLighting);
        if (config.m_SixWayLighting.m_Enabled)
            config.m_LightingMode = VansParticleLightingMode::SixWayLit;
    }

    if (root.contains("volumetric"))
    {
        if (!root["volumetric"].is_object())
            throw std::invalid_argument("renderer.volumetric must be an object");
        DecodeVolumetricConfig(root["volumetric"], config.m_Volumetric);
    }

    if (config.m_SixWayLighting.m_Columns <= 1)
        config.m_SixWayLighting.m_Columns = config.m_SpriteSheetEnabled ? config.m_SpriteColumns : 1;
    if (config.m_SixWayLighting.m_Rows <= 1)
        config.m_SixWayLighting.m_Rows = config.m_SpriteSheetEnabled ? config.m_SpriteRows : 1;

    config.m_CastShadows = root.value("castShadows", false);
    config.m_ReceiveShadows = root.value("receiveShadows", false);
}

Vans::ParticleJson EncodeModule(const VansParticleModule& module)
{
    if (const auto* initLifetime = dynamic_cast<const VansInitLifetimeModule*>(&module))
    {
        return {
            {"module", "InitLifetime"},
            {"lifetime", EncodeFloatCurve(initLifetime->m_Lifetime)}
        };
    }
    if (const auto* initVelocity = dynamic_cast<const VansInitVelocityModule*>(&module))
    {
        return {
            {"module", "InitVelocity"},
            {"mode", initVelocity->m_VelocityMode == VansInitVelocityMode::Cone ? "Cone" : "Random"},
            {"angle", initVelocity->m_ConeAngle},
            {"speed", initVelocity->m_Speed}
        };
    }
    if (const auto* initSize = dynamic_cast<const VansInitSizeModule*>(&module))
    {
        return {
            {"module", "InitSize"},
            {"size", EncodeFloatCurve(initSize->m_Size)}
        };
    }
    if (const auto* initColor = dynamic_cast<const VansInitColorModule*>(&module))
    {
        return {
            {"module", "InitColor"},
            {"color", { initColor->m_Color.r, initColor->m_Color.g, initColor->m_Color.b, initColor->m_Color.a }}
        };
    }
    if (const auto* initRotation = dynamic_cast<const VansInitRotationModule*>(&module))
    {
        return {
            {"module", "InitRotation"},
            {"angle", EncodeFloatCurve(initRotation->m_Angle)}
        };
    }
    if (const auto* initPosition = dynamic_cast<const VansInitPositionModule*>(&module))
    {
        auto shapeToString = [](VansEmitterShape shape) -> std::string {
            switch (shape)
            {
            case VansEmitterShape::Sphere: return "Sphere";
            case VansEmitterShape::Box: return "Box";
            case VansEmitterShape::Cone: return "Cone";
            case VansEmitterShape::Disk: return "Disk";
            case VansEmitterShape::Edge: return "Edge";
            default: return "Cone";
            }
        };
        return {
            {"module", "InitPositionShape"},
            {"shape", shapeToString(initPosition->m_Shape)},
            {"radius", initPosition->m_Radius},
            {"arc", initPosition->m_Arc}
        };
    }
    if (const auto* updateGravity = dynamic_cast<const VansUpdateGravityModule*>(&module))
    {
        return {
            {"module", "UpdateGravity"},
            {"gravity", { updateGravity->m_Gravity.x, updateGravity->m_Gravity.y, updateGravity->m_Gravity.z }}
        };
    }
    if (const auto* updateColor = dynamic_cast<const VansUpdateColorOverLifetime*>(&module))
    {
        return {
            {"module", "UpdateColorOverLifetime"},
            {"gradient", EncodeColorGradient(updateColor->m_Gradient)}
        };
    }
    if (const auto* updateSize = dynamic_cast<const VansUpdateSizeOverLifetime*>(&module))
    {
        return {
            {"module", "UpdateSizeOverLifetime"},
            {"curve", EncodeCurveKeys(updateSize->m_Curve)}
        };
    }
    if (const auto* updateVelocity = dynamic_cast<const VansUpdateVelocityOverLifetime*>(&module))
    {
        return {
            {"module", "UpdateVelocityOverLifetime"},
            {"drag", updateVelocity->m_Drag},
            {"turbulence", {
                {"enabled", updateVelocity->m_TurbulenceEnabled},
                {"strength", updateVelocity->m_TurbulenceStrength},
                {"frequency", updateVelocity->m_TurbulenceFrequency},
                {"scrollSpeed", updateVelocity->m_TurbulenceScrollSpeed}
            }}
        };
    }
    if (const auto* updateRotation = dynamic_cast<const VansUpdateRotationOverLifetime*>(&module))
    {
        return {
            {"module", "UpdateRotationOverLifetime"},
            {"angularVelocity", EncodeFloatCurve(updateRotation->m_AngularVelocity)}
        };
    }
    if (const auto* updateSprite = dynamic_cast<const VansUpdateSpriteAnimModule*>(&module))
    {
        return {
            {"module", "UpdateSpriteAnim"},
            {"columns", updateSprite->m_Columns},
            {"rows", updateSprite->m_Rows},
            {"fps", updateSprite->m_FPS}
        };
    }
    return {};
}

std::unique_ptr<VansParticleModule> CreateModule(const Vans::ParticleJson& root)
{
    const std::string name = root.value("module", "");
    std::unique_ptr<VansParticleModule> module;

    if (name == "InitLifetime") module = std::make_unique<VansInitLifetimeModule>();
    else if (name == "InitVelocity") module = std::make_unique<VansInitVelocityModule>();
    else if (name == "InitSize") module = std::make_unique<VansInitSizeModule>();
    else if (name == "InitColor") module = std::make_unique<VansInitColorModule>();
    else if (name == "InitRotation") module = std::make_unique<VansInitRotationModule>();
    else if (name == "InitPositionShape") module = std::make_unique<VansInitPositionModule>();
    else if (name == "UpdateGravity") module = std::make_unique<VansUpdateGravityModule>();
    else if (name == "UpdateColorOverLifetime") module = std::make_unique<VansUpdateColorOverLifetime>();
    else if (name == "UpdateSizeOverLifetime") module = std::make_unique<VansUpdateSizeOverLifetime>();
    else if (name == "UpdateVelocityOverLifetime") module = std::make_unique<VansUpdateVelocityOverLifetime>();
    else if (name == "UpdateRotationOverLifetime") module = std::make_unique<VansUpdateRotationOverLifetime>();
    else if (name == "UpdateSpriteAnim") module = std::make_unique<VansUpdateSpriteAnimModule>();

    return module;
}

void DecodeModule(const Vans::ParticleJson& root, VansParticleModule& module)
{
    module.m_Enabled = root.value("enabled", true);
    if (auto* initLifetime = dynamic_cast<VansInitLifetimeModule*>(&module))
    {
        if (root.contains("lifetime"))
        {
            DecodeFloatCurve(root["lifetime"], initLifetime->m_Lifetime);
            ValidateNonNegativeFloatCurve(
                initLifetime->m_Lifetime, "InitLifetime.lifetime", 0.01f);
        }
    }
    else if (auto* initVelocity = dynamic_cast<VansInitVelocityModule*>(&module))
    {
        const std::string mode = root.value("mode", "Cone");
        initVelocity->m_VelocityMode = mode == "Random" ? VansInitVelocityMode::Random : VansInitVelocityMode::Cone;
        initVelocity->m_ConeAngle = root.value("angle", 25.0f);
        initVelocity->m_Speed = root.value("speed", 2.0f);
        if (!std::isfinite(initVelocity->m_ConeAngle) ||
            initVelocity->m_ConeAngle < 0.0f || initVelocity->m_ConeAngle > 180.0f ||
            !std::isfinite(initVelocity->m_Speed) || initVelocity->m_Speed < 0.0f)
        {
            throw std::invalid_argument(
                "InitVelocity angle must be in [0,180] and speed must be non-negative");
        }
    }
    else if (auto* initSize = dynamic_cast<VansInitSizeModule*>(&module))
    {
        if (root.contains("size"))
        {
            DecodeFloatCurve(root["size"], initSize->m_Size);
            ValidateNonNegativeFloatCurve(initSize->m_Size, "InitSize.size", 0.0f);
        }
    }
    else if (auto* initColor = dynamic_cast<VansInitColorModule*>(&module))
    {
        if (root.contains("color") && root["color"].is_array() && root["color"].size() >= 4)
        {
            initColor->m_Color = glm::vec4(
                root["color"][0].get<float>(),
                root["color"][1].get<float>(),
                root["color"][2].get<float>(),
                root["color"][3].get<float>());
        }
    }
    else if (auto* initRotation = dynamic_cast<VansInitRotationModule*>(&module))
    {
        if (root.contains("angle"))
            DecodeFloatCurve(root["angle"], initRotation->m_Angle);
    }
    else if (auto* initPosition = dynamic_cast<VansInitPositionModule*>(&module))
    {
        const std::string shape = root.value("shape", "Cone");
        if (shape == "Sphere") initPosition->m_Shape = VansEmitterShape::Sphere;
        else if (shape == "Box") initPosition->m_Shape = VansEmitterShape::Box;
        else if (shape == "Disk") initPosition->m_Shape = VansEmitterShape::Disk;
        else if (shape == "Edge") initPosition->m_Shape = VansEmitterShape::Edge;
        else initPosition->m_Shape = VansEmitterShape::Cone;

        initPosition->m_Radius = root.value("radius", 0.2f);
        initPosition->m_Arc = root.value("arc", 360.0f);
    }
    else if (auto* updateGravity = dynamic_cast<VansUpdateGravityModule*>(&module))
    {
        if (root.contains("gravity") && root["gravity"].is_array() && root["gravity"].size() >= 3)
        {
            updateGravity->m_Gravity = glm::vec3(
                root["gravity"][0].get<float>(),
                root["gravity"][1].get<float>(),
                root["gravity"][2].get<float>());
        }
    }
    else if (auto* updateColor = dynamic_cast<VansUpdateColorOverLifetime*>(&module))
    {
        if (root.contains("gradient"))
        {
            DecodeColorGradient(root["gradient"], updateColor->m_Gradient);
            ValidateColorGradient(updateColor->m_Gradient);
        }
    }
    else if (auto* updateSize = dynamic_cast<VansUpdateSizeOverLifetime*>(&module))
    {
        if (root.contains("curve"))
        {
            DecodeCurveKeys(root["curve"], updateSize->m_Curve);
            ValidateNormalizedCurve(updateSize->m_Curve,
                "UpdateSizeOverLifetime.curve", true);
        }
    }
    else if (auto* updateVelocity = dynamic_cast<VansUpdateVelocityOverLifetime*>(&module))
    {
        updateVelocity->m_Drag = root.value("drag", 0.1f);
        if (root.contains("turbulence"))
        {
            const auto& turbulence = root["turbulence"];
            updateVelocity->m_TurbulenceEnabled = turbulence.value("enabled", false);
            updateVelocity->m_TurbulenceStrength = turbulence.value("strength", 0.5f);
            updateVelocity->m_TurbulenceFrequency = turbulence.value("frequency", 1.0f);
            updateVelocity->m_TurbulenceScrollSpeed = turbulence.value("scrollSpeed", 0.2f);
        }
        if (!std::isfinite(updateVelocity->m_Drag) || updateVelocity->m_Drag < 0.0f ||
            !std::isfinite(updateVelocity->m_TurbulenceStrength) ||
            updateVelocity->m_TurbulenceStrength < 0.0f ||
            !std::isfinite(updateVelocity->m_TurbulenceFrequency) ||
            updateVelocity->m_TurbulenceFrequency < 0.0f ||
            !std::isfinite(updateVelocity->m_TurbulenceScrollSpeed) ||
            updateVelocity->m_TurbulenceScrollSpeed < 0.0f)
        {
            throw std::invalid_argument(
                "UpdateVelocityOverLifetime parameters must be finite and non-negative");
        }
    }
    else if (auto* updateRotation = dynamic_cast<VansUpdateRotationOverLifetime*>(&module))
    {
        if (root.contains("angularVelocity"))
            DecodeFloatCurve(root["angularVelocity"], updateRotation->m_AngularVelocity);
    }
    else if (auto* updateSprite = dynamic_cast<VansUpdateSpriteAnimModule*>(&module))
    {
        updateSprite->m_Columns = root.value("columns", 4);
        updateSprite->m_Rows = root.value("rows", 4);
        updateSprite->m_FPS = root.value("fps", 0.0f);
    }
}
}

Vans::ParticleJson VansParticleEmitterJsonCodec::EncodeEmitter(const VansParticleEmitter& emitter)
{
    Vans::ParticleJson root;
    root["name"] = emitter.m_Name;
    root["enabled"] = emitter.m_Enabled;
    root["maxParticles"] = emitter.m_MaxParticles;
    root["spawn"] = EncodeSpawnConfig(emitter.m_SpawnConfig);

    auto initialize = Vans::ParticleJson::array();
    for (const auto& module : emitter.m_InitModules)
    {
        if (module)
        {
            auto encoded = EncodeModule(*module);
            if (!encoded.empty())
            {
                encoded["enabled"] = module->m_Enabled;
                initialize.push_back(std::move(encoded));
            }
        }
    }
    root["initialize"] = std::move(initialize);

    auto update = Vans::ParticleJson::array();
    for (const auto& module : emitter.m_UpdateModules)
    {
        if (module)
        {
            auto encoded = EncodeModule(*module);
            if (!encoded.empty())
            {
                encoded["enabled"] = module->m_Enabled;
                update.push_back(std::move(encoded));
            }
        }
    }
    root["update"] = std::move(update);

    root["renderer"] = EncodeRendererConfig(emitter.m_RendererConfig);
    return root;
}

void VansParticleEmitterJsonCodec::DecodeEmitter(
    const Vans::ParticleJson& root,
    VansParticleEmitter& emitter)
{
    emitter.m_Name = root.value("name", "Emitter");
    emitter.m_Enabled = root.value("enabled", true);
    emitter.m_MaxParticles = root.value("maxParticles", 1000u);

    if (root.contains("spawn"))
        DecodeSpawnConfig(root["spawn"], emitter.m_SpawnConfig);

    emitter.m_InitModules.clear();
    if (root.contains("initialize") && root["initialize"].is_array())
    {
        for (const auto& moduleJson : root["initialize"])
        {
            auto module = CreateModule(moduleJson);
            if (module)
            {
                DecodeModule(moduleJson, *module);
                emitter.m_InitModules.push_back(std::move(module));
            }
        }
    }

    emitter.m_UpdateModules.clear();
    if (root.contains("update") && root["update"].is_array())
    {
        for (const auto& moduleJson : root["update"])
        {
            auto module = CreateModule(moduleJson);
            if (module)
            {
                DecodeModule(moduleJson, *module);
                emitter.m_UpdateModules.push_back(std::move(module));
            }
        }
    }

    if (root.contains("renderer"))
        DecodeRendererConfig(root["renderer"], emitter.m_RendererConfig);

    emitter.Initialize();
}
}
