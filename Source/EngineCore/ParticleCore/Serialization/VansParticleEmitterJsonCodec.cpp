#include "VansParticleEmitterJsonCodec.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <array>
#include <unordered_set>

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
    const char* mode = "Constant";
    switch (curve.m_Mode)
    {
    case FloatCurveMode::RandomBetween: mode = "RandomBetween"; break;
    case FloatCurveMode::Curve: mode = "Curve"; break;
    case FloatCurveMode::RandomBetweenCurves: mode = "RandomBetweenCurves"; break;
    default: break;
    }
    return {{"mode", mode}, {"value", curve.m_Value}, {"min", curve.m_Min}, {"max", curve.m_Max},
        {"keys", EncodeCurveKeys(curve.m_Keys)}, {"minKeys", EncodeCurveKeys(curve.m_MinKeys)},
        {"maxKeys", EncodeCurveKeys(curve.m_MaxKeys)}};
}

void DecodeFloatCurve(const Vans::ParticleJson& root, VansFloatCurve& curve)
{
    const std::string mode = root.value("mode", "Constant");
    if (mode == "Constant") curve.m_Mode = FloatCurveMode::Constant;
    else if (mode == "RandomBetween") curve.m_Mode = FloatCurveMode::RandomBetween;
    else if (mode == "Curve") curve.m_Mode = FloatCurveMode::Curve;
    else if (mode == "RandomBetweenCurves") curve.m_Mode = FloatCurveMode::RandomBetweenCurves;
    else throw std::invalid_argument("Unknown float curve mode: " + mode);
    curve.m_Value = root.value("value", curve.m_Value);
    curve.m_Min = root.value("min", curve.m_Min);
    curve.m_Max = root.value("max", curve.m_Max);
    if (root.contains("keys")) DecodeCurveKeys(root["keys"], curve.m_Keys);
    if (root.contains("minKeys")) DecodeCurveKeys(root["minKeys"], curve.m_MinKeys);
    if (root.contains("maxKeys")) DecodeCurveKeys(root["maxKeys"], curve.m_MaxKeys);
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
    auto bursts = Vans::ParticleJson::array();
    for (const auto& burst : config.m_Bursts)
        bursts.push_back({{"time", burst.time}, {"count", burst.count}, {"cycles", burst.cycles}, {"interval", burst.interval}});
    return {{"type", config.m_Type == VansSpawnType::Burst ? "Burst" : "RateOverTime"},
        {"rate", config.m_Rate}, {"bursts", bursts}};
}

void DecodeSpawnConfig(const Vans::ParticleJson& root, VansParticleSpawnConfig& config)
{
    const std::string type = root.value("type", "RateOverTime");
    if (type != "Burst" && type != "RateOverTime") throw std::invalid_argument("Unsupported spawn.type: " + type);
    config.m_Type = type == "Burst" ? VansSpawnType::Burst : VansSpawnType::RateOverTime;
    {
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
                if (!std::isfinite(burst.time) || burst.time < 0 || burst.count == 0 || burst.count > 65536
                    || burst.cycles > 100000 || !std::isfinite(burst.interval) || burst.interval < 0
                    || (burst.cycles != 1 && burst.interval <= 0))
                    throw std::invalid_argument("spawn.bursts contains an invalid time/count/cycles/interval");
                config.m_Bursts.push_back(burst);
            }
        }
    }
    {
        config.m_Rate = root.value("rate", 30.0f);
        if (!std::isfinite(config.m_Rate) || config.m_Rate < 0 || config.m_Rate > 65536)
            throw std::invalid_argument("spawn.rate must be finite and in [0,65536]");
    }
}

Vans::ParticleJson EncodeSixWayLighting(const VansParticleSixWayLightingConfig& config)
{
    Vans::ParticleJson root;
    root["positiveAxesTextureGuid"] = config.m_PositiveAxesTextureGuid;
    root["negativeAxesTextureGuid"] = config.m_NegativeAxesTextureGuid;
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
    for (const char* field : {"enabled", "positiveAxesTexture", "negativeAxesTexture", "columns", "rows", "fps", "alphaFromPositiveA", "emissiveFromNegativeA"})
        if (root.contains(field)) throw std::invalid_argument(std::string("Removed sixWayLighting field: ") + field);
    config.m_PositiveAxesTextureGuid = root.value("positiveAxesTextureGuid", "");
    config.m_NegativeAxesTextureGuid = root.value("negativeAxesTextureGuid", "");
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

void DecodeVolumetricConfig(
    const Vans::ParticleJson& root, VansParticleVolumetricConfig& config)
{
    if (root.contains("keepSurfaceRenderer")) throw std::invalid_argument("Use renderer.type=None for volume-only particles");
    config.m_Enabled = root.value("enabled", false);
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
        case VansParticleRendererType::None: return "None";
        case VansParticleRendererType::Ribbon: return "Ribbon";
        default: return "Billboard";
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
    root["textureGuid"] = config.m_TextureGuid;
    root["spriteSheet"] = {
        {"enabled", config.m_SpriteSheetEnabled},
        {"columns", config.m_SpriteColumns},
        {"rows", config.m_SpriteRows}
    };
    root["sortMode"] = sortToString(config.m_SortMode);
    root["lightingMode"] = lightingToString(config.m_LightingMode);
    root["sixWayLighting"] = EncodeSixWayLighting(config.m_SixWayLighting);
    root["volumetric"] = EncodeVolumetricConfig(config.m_Volumetric);
    {
        const auto& ribbon = config.m_Ribbon;
        root["ribbon"] = {
            {"rootMode", ribbon.rootMode == VansRibbonRootMode::FollowSource ? "FollowSource" : "None"},
            {"stopAttachment", ribbon.stopAttachment == VansRibbonStopAttachment::KeepUntilInvisible ? "KeepUntilInvisible" : "DetachOnStop"},
            {"rootWidth", ribbon.rootWidth},
            {"rootColor", {ribbon.rootColor.r, ribbon.rootColor.g, ribbon.rootColor.b, ribbon.rootColor.a}},
            {"maxSegmentLength", ribbon.maxSegmentLength}, {"uvFlowSpeed", ribbon.uvFlowSpeed},
            {"softIntersection", ribbon.softIntersection}, {"tipFadeDistance", ribbon.tipFadeDistance}
        };
    }
    return root;
}

void DecodeRendererConfig(const Vans::ParticleJson& root, VansParticleRendererConfig& config)
{
    const std::string type = root.value("type", "Billboard");
    if (type == "None") config.m_Type = VansParticleRendererType::None;
    else if (type == "Ribbon") config.m_Type = VansParticleRendererType::Ribbon;
    else if (type == "Billboard") config.m_Type = VansParticleRendererType::Billboard;
    else throw std::invalid_argument("Unsupported renderer.type: " + type);

    if (root.contains("texture") || root.contains("blendMode") || root.contains("castShadows") || root.contains("receiveShadows"))
        throw std::invalid_argument("Particle renderer contains removed properties; use explicit textureGuid and the current alpha surface schema");
    config.m_TextureGuid = root.value("textureGuid", config.m_TextureGuid);
    Vans::VansAssetGuid texture;
    if (!Vans::VansAssetGuid::TryParse(config.m_TextureGuid, texture) || !texture.IsValid())
        throw std::invalid_argument("renderer.textureGuid requires a valid asset GUID");

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
    else if (sort == "None") config.m_SortMode = VansParticleSortMode::None;
    else throw std::invalid_argument("Invalid sortMode");

    const std::string lightingMode = root.value("lightingMode", "UnlitFlipbook");
    if (lightingMode == "SixWayLit") config.m_LightingMode = VansParticleLightingMode::SixWayLit;
    else if (lightingMode == "UnlitFlipbook") config.m_LightingMode = VansParticleLightingMode::UnlitFlipbook;
    else throw std::invalid_argument("Invalid lightingMode");

    if (root.contains("sixWayLighting"))
    {
        DecodeSixWayLighting(root["sixWayLighting"], config.m_SixWayLighting);
    }
    if (config.m_Type != VansParticleRendererType::None && config.m_LightingMode == VansParticleLightingMode::SixWayLit)
        for (const auto* guid : { &config.m_SixWayLighting.m_PositiveAxesTextureGuid, &config.m_SixWayLighting.m_NegativeAxesTextureGuid })
            if (!Vans::VansAssetGuid::TryParse(*guid, texture) || !texture.IsValid())
                throw std::invalid_argument("SixWayLit requires two texture asset GUIDs");

    if (root.contains("volumetric"))
    {
        if (!root["volumetric"].is_object())
            throw std::invalid_argument("renderer.volumetric must be an object");
        DecodeVolumetricConfig(root["volumetric"], config.m_Volumetric);
    }

    if (config.m_Type == VansParticleRendererType::Ribbon &&
        (config.m_LightingMode != VansParticleLightingMode::UnlitFlipbook || config.m_SpriteSheetEnabled))
        throw std::invalid_argument("Ribbon requires UnlitFlipbook lighting and disabled spriteSheet");
    // 未启用的配置也要往返保留，切换渲染类型不会丢失调好的参数。
    {
        const auto ribbon = root.value("ribbon", Vans::ParticleJson::object());
        auto& target = config.m_Ribbon;
        const std::string rootMode = ribbon.value("rootMode", "None");
        if (rootMode != "None" && rootMode != "FollowSource") throw std::invalid_argument("Invalid ribbon.rootMode");
        target.rootMode = rootMode == "FollowSource" ? VansRibbonRootMode::FollowSource : VansRibbonRootMode::None;
        const std::string stop = ribbon.value("stopAttachment", "KeepUntilInvisible");
        if (stop != "KeepUntilInvisible" && stop != "DetachOnStop") throw std::invalid_argument("Invalid ribbon.stopAttachment");
        target.stopAttachment = stop == "DetachOnStop" ? VansRibbonStopAttachment::DetachOnStop : VansRibbonStopAttachment::KeepUntilInvisible;
        target.rootWidth = ribbon.value("rootWidth", target.rootWidth);
        target.maxSegmentLength = ribbon.value("maxSegmentLength", target.maxSegmentLength);
        target.uvFlowSpeed = ribbon.value("uvFlowSpeed", target.uvFlowSpeed);
        target.softIntersection = ribbon.value("softIntersection", target.softIntersection);
        target.tipFadeDistance = ribbon.value("tipFadeDistance", target.tipFadeDistance);
        if (ribbon.contains("rootColor"))
        {
            const auto& color = ribbon.at("rootColor");
            if (!color.is_array() || color.size() != 4) throw std::invalid_argument("ribbon.rootColor requires RGBA");
            for (int i=0; i<4; ++i) target.rootColor[i] = color[i].get<float>();
        }
        bool valid = std::isfinite(target.rootWidth) && target.rootWidth >= 0 && target.rootWidth <= 100
            && std::isfinite(target.maxSegmentLength) && target.maxSegmentLength >= 0.001f && target.maxSegmentLength <= 100
            && std::isfinite(target.uvFlowSpeed) && std::abs(target.uvFlowSpeed) <= 100
            && std::isfinite(target.softIntersection) && target.softIntersection >= 0 && target.softIntersection <= 10
            && std::isfinite(target.tipFadeDistance) && target.tipFadeDistance >= 0 && target.tipFadeDistance <= 10;
        for (int i=0; i<4; ++i) valid &= std::isfinite(target.rootColor[i]) && target.rootColor[i] >= 0;
        if (!valid || target.rootColor.a > 1) throw std::invalid_argument("Invalid finite Ribbon dimensions or color");
    }
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

struct ModuleEntry
{
    const char* name;
    bool initialize;
    std::unique_ptr<VansParticleModule> (*create)();
};
template<class T> std::unique_ptr<VansParticleModule> MakeModule() { return std::make_unique<T>(); }
const std::array<ModuleEntry, 12> ModuleCatalog{{
    {"InitLifetime", true, MakeModule<VansInitLifetimeModule>},
    {"InitVelocity", true, MakeModule<VansInitVelocityModule>},
    {"InitSize", true, MakeModule<VansInitSizeModule>},
    {"InitColor", true, MakeModule<VansInitColorModule>},
    {"InitRotation", true, MakeModule<VansInitRotationModule>},
    {"InitPositionShape", true, MakeModule<VansInitPositionModule>},
    {"UpdateGravity", false, MakeModule<VansUpdateGravityModule>},
    {"UpdateColorOverLifetime", false, MakeModule<VansUpdateColorOverLifetime>},
    {"UpdateSizeOverLifetime", false, MakeModule<VansUpdateSizeOverLifetime>},
    {"UpdateVelocityOverLifetime", false, MakeModule<VansUpdateVelocityOverLifetime>},
    {"UpdateRotationOverLifetime", false, MakeModule<VansUpdateRotationOverLifetime>},
    {"UpdateSpriteAnim", false, MakeModule<VansUpdateSpriteAnimModule>}
}};

std::unique_ptr<VansParticleModule> CreateModule(const Vans::ParticleJson& root, bool initialize)
{
    const std::string name = root.value("module", "");
    for (const auto& entry : ModuleCatalog)
        if (name == entry.name && entry.initialize == initialize) return entry.create();
    throw std::invalid_argument("Unknown particle module or incorrect phase: " + name);
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

Vans::ParticleJson VansParticleEmitterJsonCodec::ModuleDefaults()
{
    auto result = Vans::ParticleJson::array();
    for (const auto& entry : ModuleCatalog)
    {
        auto module = entry.create();
        auto definition = EncodeModule(*module);
        definition["enabled"] = module->m_Enabled;
        result.push_back({{"phase", entry.initialize ? "initialize" : "update"}, {"definition", definition}});
    }
    return result;
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
    if (emitter.m_MaxParticles == 0 || emitter.m_MaxParticles > 65536)
        throw std::invalid_argument("maxParticles must be in [1,65536]");

    if (root.contains("spawn"))
        DecodeSpawnConfig(root["spawn"], emitter.m_SpawnConfig);

    emitter.m_InitModules.clear();
    if (root.contains("initialize") && root["initialize"].is_array())
    {
        std::unordered_set<std::string> names;
        for (const auto& moduleJson : root["initialize"])
        {
            if (!names.insert(moduleJson.value("module", "")).second)
                throw std::invalid_argument("Duplicate particle module in initialize");
            auto module = CreateModule(moduleJson, true);
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
        std::unordered_set<std::string> names;
        for (const auto& moduleJson : root["update"])
        {
            if (!names.insert(moduleJson.value("module", "")).second)
                throw std::invalid_argument("Duplicate particle module in update");
            auto module = CreateModule(moduleJson, false);
            if (module)
            {
                DecodeModule(moduleJson, *module);
                emitter.m_UpdateModules.push_back(std::move(module));
            }
        }
    }

    if (root.contains("renderer"))
        DecodeRendererConfig(root["renderer"], emitter.m_RendererConfig);
    if (emitter.m_RendererConfig.m_Type == VansParticleRendererType::Ribbon
        && (emitter.m_MaxParticles < 2 || emitter.m_SpawnConfig.m_Type != VansSpawnType::RateOverTime))
        throw std::invalid_argument("Ribbon requires at least two points and RateOverTime emission");

    // 发布的定义不包含活粒子；创建 Runtime 时才分配模拟池。
}
}
