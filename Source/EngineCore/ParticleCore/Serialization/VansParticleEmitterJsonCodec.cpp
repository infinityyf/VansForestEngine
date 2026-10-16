#include "VansParticleEmitterJsonCodec.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../AssetCore/VansAssetDocumentJson.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <memory>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <array>
#include <limits>
#include <unordered_set>

namespace
{
using ParticleJsonValue = Vans::AssetDocumentJson;
}

namespace VansGraphics
{
namespace
{
ParticleJsonValue EncodeCurveKeys(const std::vector<CurveKey>& curve)
{
    auto keys = ParticleJsonValue::array();
    for (const auto& key : curve)
        keys.push_back({ {"t", key.t}, {"value", key.value} });
    return keys;
}

void DecodeCurveKeys(const ParticleJsonValue& root, std::vector<CurveKey>& curve)
{
    curve.clear();
    if (!root.is_array())
        return;

    for (const auto& key : root)
        curve.push_back({ key.value("t", 0.0f), key.value("value", 0.0f) });
}

void ValidateOrderedCurve(const std::vector<CurveKey>& curve,
    const char* field, bool requireNonNegativeValues)
{
    if (curve.empty())
        throw std::invalid_argument(std::string(field) + " must contain at least one key");
    float previousTime = -1.0f;
    for (const CurveKey& key : curve)
    {
        const bool validValue = std::isfinite(key.value) &&
            (!requireNonNegativeValues || key.value >= 0.0f);
        if (key.t <= previousTime || !validValue)
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
        if (stop.t <= previousTime || !finiteColor || !validColor)
        {
            throw std::invalid_argument(
                "UpdateColorOverLifetime.gradient stops must be ordered in normalized time with non-negative RGB and alpha in [0,1]");
        }
        previousTime = stop.t;
    }
}

void ValidateNonNegativeFloatCurve(const VansFloatCurve& curve,
    const char* field, float minimumValue, float maximumValue = std::numeric_limits<float>::max(),
    bool allowReversedRandomRange = false)
{
    const auto requireRange = [field, minimumValue, maximumValue, allowReversedRandomRange](float minimum, float maximum)
    {
        if (allowReversedRandomRange && minimum > maximum) std::swap(minimum, maximum);
        if (!std::isfinite(minimum) || !std::isfinite(maximum) ||
            minimum < minimumValue || maximum < minimum || maximum > maximumValue)
        {
            throw std::invalid_argument(std::string(field) +
                " must contain a finite non-decreasing non-negative range");
        }
    };
    const auto requireKeys = [field, minimumValue, maximumValue](
        const std::vector<CurveKey>& keys)
    {
        ValidateOrderedCurve(keys, field, true);
        for (const CurveKey& key : keys)
            if (key.value < minimumValue || key.value > maximumValue)
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

ParticleJsonValue EncodeFloatCurve(const VansFloatCurve& curve)
{
    const char* mode = "Constant";
    switch (curve.m_Mode)
    {
    case FloatCurveMode::RandomBetween: mode = "RandomBetween"; break;
    case FloatCurveMode::Curve: mode = "Curve"; break;
    case FloatCurveMode::RandomBetweenCurves: mode = "RandomBetweenCurves"; break;
    default: break;
    }
    const char* distribution = curve.m_RandomDistribution == VansParticleRandomDistribution::StandardBias
        ? "StandardBias" : curve.m_RandomDistribution == VansParticleRandomDistribution::ExponentialBias
        ? "ExponentialBias" : "Uniform";
    return {{"mode", mode}, {"randomDistribution", distribution}, {"randomBias", curve.m_RandomBias},
        {"value", curve.m_Value}, {"min", curve.m_Min}, {"max", curve.m_Max},
        {"keys", EncodeCurveKeys(curve.m_Keys)}, {"minKeys", EncodeCurveKeys(curve.m_MinKeys)},
        {"maxKeys", EncodeCurveKeys(curve.m_MaxKeys)}};
}

void DecodeFloatCurve(const ParticleJsonValue& root, VansFloatCurve& curve)
{
    const std::string mode = root.value("mode", "Constant");
    if (mode == "Constant") curve.m_Mode = FloatCurveMode::Constant;
    else if (mode == "RandomBetween") curve.m_Mode = FloatCurveMode::RandomBetween;
    else if (mode == "Curve") curve.m_Mode = FloatCurveMode::Curve;
    else if (mode == "RandomBetweenCurves") curve.m_Mode = FloatCurveMode::RandomBetweenCurves;
    else throw std::invalid_argument("Unknown float curve mode: " + mode);
    const auto distribution = root.value("randomDistribution", "Uniform");
    if (distribution == "Uniform") curve.m_RandomDistribution = VansParticleRandomDistribution::Uniform;
    else if (distribution == "StandardBias") curve.m_RandomDistribution = VansParticleRandomDistribution::StandardBias;
    else if (distribution == "ExponentialBias") curve.m_RandomDistribution = VansParticleRandomDistribution::ExponentialBias;
    else throw std::invalid_argument("Unknown random distribution: " + distribution);
    curve.m_RandomBias = root.value("randomBias", 0.f);
    if (!std::isfinite(curve.m_RandomBias) || curve.m_RandomBias < -1.f || curve.m_RandomBias > 1.f)
        throw std::invalid_argument("randomBias must be finite and in [-1,1]");
    curve.m_Value = root.value("value", curve.m_Value);
    curve.m_Min = root.value("min", curve.m_Min);
    curve.m_Max = root.value("max", curve.m_Max);
    if (root.contains("keys")) DecodeCurveKeys(root["keys"], curve.m_Keys);
    if (root.contains("minKeys")) DecodeCurveKeys(root["minKeys"], curve.m_MinKeys);
    if (root.contains("maxKeys")) DecodeCurveKeys(root["maxKeys"], curve.m_MaxKeys);
}

ParticleJsonValue EncodeColorGradient(const VansColorGradient& gradient)
{
    auto stops = ParticleJsonValue::array();
    for (const auto& stop : gradient.m_Stops)
    {
        stops.push_back({
            {"t", stop.t},
            {"color", { stop.color.r, stop.color.g, stop.color.b, stop.color.a }}
        });
    }
    return ParticleJsonValue{ {"stops", std::move(stops)} };
}

void DecodeColorGradient(const ParticleJsonValue& root, VansColorGradient& gradient)
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

ParticleJsonValue EncodeSpawnConfig(const VansParticleSpawnConfig& config)
{
    auto bursts = ParticleJsonValue::array();
    for (const auto& burst : config.m_Bursts)
        bursts.push_back({{"time", burst.time}, {"count", burst.count}, {"cycles", burst.cycles}, {"interval", burst.interval}});
    return {{"type", config.m_Type == VansSpawnType::Burst ? "Burst" : "RateOverTime"},
        {"rate", config.m_Rate}, {"bursts", bursts}};
}

void DecodeSpawnConfig(const ParticleJsonValue& root, VansParticleSpawnConfig& config)
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

ParticleJsonValue EncodeSixWayLighting(const VansParticleSixWayLightingConfig& config)
{
    ParticleJsonValue root;
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

void DecodeSixWayLighting(const ParticleJsonValue& root, VansParticleSixWayLightingConfig& config)
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

ParticleJsonValue EncodeVolumetricConfig(const VansParticleVolumetricConfig& config)
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
    const ParticleJsonValue& root, VansParticleVolumetricConfig& config)
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
    auto readVec3 = [](const ParticleJsonValue& value, glm::vec3 fallback)
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

ParticleJsonValue EncodeRendererConfig(const VansParticleRendererConfig& config)
{
    auto typeToString = [](VansParticleRendererType type) -> std::string {
        switch (type)
        {
        case VansParticleRendererType::None: return "None";
        case VansParticleRendererType::Ribbon: return "Ribbon";
        default: return "Billboard";
        }
    };
    auto simulationOrderToString = [](VansParticleSimulationOrder order) -> std::string {
        switch (order)
        {
        case VansParticleSimulationOrder::OldestFirst: return "OldestFirst";
        case VansParticleSimulationOrder::NewestFirst: return "NewestFirst";
        default: return "Stable";
        }
    };
    auto lightingToString = [](VansParticleLightingMode mode) -> std::string {
        switch (mode)
        {
        case VansParticleLightingMode::SixWayLit: return "SixWayLit";
        case VansParticleLightingMode::SpriteCard: return "SpriteCard";
        default: return "UnlitFlipbook";
        }
    };

    ParticleJsonValue root;
    root["type"] = typeToString(config.m_Type);
    root["textureGuid"] = config.m_TextureGuid;
    root["spriteSheet"] = {
        {"enabled", config.m_SpriteSheetEnabled},
        {"columns", config.m_SpriteColumns},
        {"rows", config.m_SpriteRows}
    };
    if (config.m_SimulationOrder != VansParticleSimulationOrder::Stable &&
        config.m_RenderSortMode != VansParticleRenderSortMode::None)
        throw std::invalid_argument("Particle renderer cannot combine simulation ordering and render depth sorting");
    root["simulationOrder"] = simulationOrderToString(config.m_SimulationOrder);
    root["renderSortMode"] = config.m_RenderSortMode == VansParticleRenderSortMode::ByDistance
        ? "ByDistance" : "None";
    root["lightingMode"] = lightingToString(config.m_LightingMode);
    root["sixWayLighting"] = EncodeSixWayLighting(config.m_SixWayLighting);
    root["volumetric"] = EncodeVolumetricConfig(config.m_Volumetric);
    {
        const auto& card = config.m_Card;
        auto sequences = ParticleJsonValue::array();
        for (const auto& sequence : card.sequences)
            sequences.push_back({{"firstFrame",sequence.firstFrame},{"frameCount",sequence.frameCount}});
        root["spriteCard"] = {{"orientation",card.orientation == VansParticleCardOrientation::Axis ? "Axis"
            : card.orientation == VansParticleCardOrientation::Surface ? "Surface" : "Camera"},
            {"output",card.output == VansParticleCardOutput::Additive ? "Additive"
            : card.output == VansParticleCardOutput::HalfBlendAdd ? "HalfBlendAdd"
            : card.output == VansParticleCardOutput::BlendAdd ? "BlendAdd" : "Alpha"},
            {"aspectRatio",card.aspectRatio},{"pivotY",card.pivotY},{"softIntersection",card.softIntersection},
            {"desaturation",card.desaturation},{"overbright",card.overbright},{"lookupBlend",card.lookupBlend},
            {"lookupTextureGuid",card.lookupTextureGuid},{"sequences",sequences},
            {"lookupMultiply",card.lookupMultiply},{"tintBeforeDesaturation",card.tintBeforeDesaturation},
            {"channelMix",card.channels == VansParticleCardChannels::RGBAlpha ? "RGBAlpha" : card.channels == VansParticleCardChannels::RedAlpha ? "RedAlpha" : "RGBA"},
            {"sizeOverEffect",EncodeFloatCurve(card.sizeOverEffect)},
            {"breakup",card.breakup == VansParticleCardBreakup::Zoom ? "Zoom" : card.breakup == VansParticleCardBreakup::Standard ? "Standard" : "None"},
            {"breakupTextureGuid",card.breakupTextureGuid},{"breakupPreserveAlpha",card.breakupPreserveAlpha},
            {"breakupZoomScale",card.breakupZoomScale},{"breakupWeightOverEffect",EncodeFloatCurve(card.breakupWeightOverEffect)},
            {"alphaOverEffect",EncodeFloatCurve(card.alphaOverEffect)},
            {"alphaClock",card.alphaClock == VansParticleCardAlphaClock::EffectSeconds ? "EffectSeconds"
                : card.alphaClock == VansParticleCardAlphaClock::EmissionPointSeconds ? "EmissionPointSeconds" : "NormalizedEffect"}};
    }
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

void DecodeRendererConfig(const ParticleJsonValue& root, VansParticleRendererConfig& config)
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

    if (root.contains("sortMode"))
        throw std::invalid_argument("Removed renderer.sortMode; use simulationOrder and renderSortMode");
    const std::string simulationOrder = root.value("simulationOrder", "Stable");
    if (simulationOrder == "Stable") config.m_SimulationOrder = VansParticleSimulationOrder::Stable;
    else if (simulationOrder == "OldestFirst") config.m_SimulationOrder = VansParticleSimulationOrder::OldestFirst;
    else if (simulationOrder == "NewestFirst") config.m_SimulationOrder = VansParticleSimulationOrder::NewestFirst;
    else throw std::invalid_argument("Invalid simulationOrder");
    const std::string renderSortMode = root.value("renderSortMode", "None");
    if (renderSortMode == "None") config.m_RenderSortMode = VansParticleRenderSortMode::None;
    else if (renderSortMode == "ByDistance") config.m_RenderSortMode = VansParticleRenderSortMode::ByDistance;
    else throw std::invalid_argument("Invalid renderSortMode");
    if (config.m_SimulationOrder != VansParticleSimulationOrder::Stable &&
        config.m_RenderSortMode != VansParticleRenderSortMode::None)
        throw std::invalid_argument("Particle renderer cannot combine simulation ordering and render depth sorting");

    const std::string lightingMode = root.value("lightingMode", "UnlitFlipbook");
    if (lightingMode == "SixWayLit") config.m_LightingMode = VansParticleLightingMode::SixWayLit;
    else if (lightingMode == "UnlitFlipbook") config.m_LightingMode = VansParticleLightingMode::UnlitFlipbook;
    else if (lightingMode == "SpriteCard") config.m_LightingMode = VansParticleLightingMode::SpriteCard;
    else throw std::invalid_argument("Invalid lightingMode");

    if (root.contains("spriteCard"))
    {
        const auto& source = root.at("spriteCard");
        if (!source.is_object()) throw std::invalid_argument("spriteCard must be an object");
        if (source.contains("breakupUseAccumulatedAlpha"))
            throw std::invalid_argument("spriteCard.breakupUseAccumulatedAlpha is retired; use breakupPreserveAlpha");
        auto& card = config.m_Card;
        const auto orientation = source.value("orientation","Camera");
        if (orientation == "Camera") card.orientation = VansParticleCardOrientation::Camera;
        else if (orientation == "Axis") card.orientation = VansParticleCardOrientation::Axis;
        else if (orientation == "Surface") card.orientation = VansParticleCardOrientation::Surface;
        else throw std::invalid_argument("Invalid spriteCard.orientation");
        const auto output = source.value("output","Alpha");
        if (output == "Alpha") card.output = VansParticleCardOutput::Alpha;
        else if (output == "Additive") card.output = VansParticleCardOutput::Additive;
        else if (output == "HalfBlendAdd") card.output = VansParticleCardOutput::HalfBlendAdd;
        else if (output == "BlendAdd") card.output = VansParticleCardOutput::BlendAdd;
        else throw std::invalid_argument("Invalid spriteCard.output");
        const auto channels = source.value("channelMix","RGBA");
        if (channels=="RGBA") card.channels=VansParticleCardChannels::RGBA;
        else if (channels=="RGBAlpha") card.channels=VansParticleCardChannels::RGBAlpha;
        else if (channels=="RedAlpha") card.channels=VansParticleCardChannels::RedAlpha;
        else throw std::invalid_argument("Invalid spriteCard.channelMix");
        card.aspectRatio = source.value("aspectRatio",card.aspectRatio);
        card.pivotY = source.value("pivotY",card.pivotY);
        card.softIntersection = source.value("softIntersection",card.softIntersection);
        card.desaturation = source.value("desaturation",card.desaturation);
        card.overbright = source.value("overbright",card.overbright);
        card.lookupBlend = source.value("lookupBlend",card.lookupBlend);
        card.lookupMultiply = source.value("lookupMultiply",false);
        card.tintBeforeDesaturation = source.value("tintBeforeDesaturation",false);
        if (!std::isfinite(card.aspectRatio) || card.aspectRatio<=0 || card.aspectRatio>100
            || !std::isfinite(card.pivotY) || std::abs(card.pivotY)>1
            || !std::isfinite(card.softIntersection) || card.softIntersection<0 || card.softIntersection>10
            || !std::isfinite(card.desaturation) || card.desaturation<0 || card.desaturation>1
            || !std::isfinite(card.overbright) || card.overbright<0 || card.overbright>100
            || !std::isfinite(card.lookupBlend) || card.lookupBlend<0 || card.lookupBlend>1)
            throw std::invalid_argument("SpriteCard material parameters are invalid");
        card.lookupTextureGuid = source.value("lookupTextureGuid",std::string{});
        if (!card.lookupTextureGuid.empty() && (!Vans::VansAssetGuid::TryParse(card.lookupTextureGuid,texture) || !texture.IsValid()))
            throw std::invalid_argument("spriteCard.lookupTextureGuid requires an asset GUID");
        if (card.lookupBlend > 0 && card.lookupTextureGuid.empty())
            throw std::invalid_argument("spriteCard lookup needs a texture dependency");
        const auto breakup = source.value("breakup","None");
        if (breakup=="None") card.breakup=VansParticleCardBreakup::None;
        else if (breakup=="Standard") card.breakup=VansParticleCardBreakup::Standard;
        else if (breakup=="Zoom") card.breakup=VansParticleCardBreakup::Zoom;
        else throw std::invalid_argument("Invalid spriteCard.breakup");
        card.breakupTextureGuid=source.value("breakupTextureGuid",std::string{});
        card.breakupPreserveAlpha=source.value("breakupPreserveAlpha",false);
        card.breakupZoomScale=source.value("breakupZoomScale",card.breakupZoomScale);
        if (!std::isfinite(card.breakupZoomScale) || card.breakupZoomScale<0 || card.breakupZoomScale>100)
            throw std::invalid_argument("Invalid spriteCard.breakupZoomScale");
        if ((!card.breakupTextureGuid.empty() || card.breakup!=VansParticleCardBreakup::None)
            && (!Vans::VansAssetGuid::TryParse(card.breakupTextureGuid,texture) || !texture.IsValid()))
            throw std::invalid_argument("spriteCard breakup requires a texture asset GUID");
        if (source.contains("breakupWeightOverEffect")) DecodeFloatCurve(source.at("breakupWeightOverEffect"),card.breakupWeightOverEffect);
        ValidateNonNegativeFloatCurve(card.breakupWeightOverEffect,"spriteCard.breakupWeightOverEffect",0,1);
        if (source.contains("sizeOverEffect")) DecodeFloatCurve(source.at("sizeOverEffect"),card.sizeOverEffect);
        if (source.contains("alphaOverEffect")) DecodeFloatCurve(source.at("alphaOverEffect"),card.alphaOverEffect);
        const auto alphaClock = source.value("alphaClock", "NormalizedEffect");
        if (alphaClock == "NormalizedEffect") card.alphaClock = VansParticleCardAlphaClock::NormalizedEffect;
        else if (alphaClock == "EffectSeconds") card.alphaClock = VansParticleCardAlphaClock::EffectSeconds;
        else if (alphaClock == "EmissionPointSeconds") card.alphaClock = VansParticleCardAlphaClock::EmissionPointSeconds;
        else throw std::invalid_argument("Invalid spriteCard.alphaClock");
        const float maxAlphaTime = card.alphaClock == VansParticleCardAlphaClock::NormalizedEffect ? 1.f : 3600.f;
        for (const auto* keys : {&card.alphaOverEffect.m_Keys, &card.alphaOverEffect.m_MinKeys, &card.alphaOverEffect.m_MaxKeys})
            for (const auto& key : *keys)
                if (!std::isfinite(key.t) || key.t < 0 || key.t > maxAlphaTime)
                    throw std::invalid_argument("spriteCard.alphaOverEffect time is outside its selected clock domain");
        ValidateNonNegativeFloatCurve(card.sizeOverEffect,"spriteCard.sizeOverEffect",0);
        ValidateNonNegativeFloatCurve(card.alphaOverEffect,"spriteCard.alphaOverEffect",0);
        card.sequences.clear();
        if (source.contains("sequences"))
        {
            if (!source.at("sequences").is_array() || source.at("sequences").size() > 64)
                throw std::invalid_argument("spriteCard supports at most 64 sequences");
            for (const auto& sequence : source.at("sequences"))
            {
                const auto first = sequence.at("firstFrame").get<uint32_t>();
                const auto count = sequence.at("frameCount").get<uint32_t>();
                if (count == 0 || uint64_t(first)+count > uint64_t(config.m_SpriteColumns)*config.m_SpriteRows)
                    throw std::invalid_argument("SpriteCard sequence exceeds its atlas");
                card.sequences.push_back({first,count});
            }
        }
    }
    if (config.m_LightingMode == VansParticleLightingMode::SpriteCard && config.m_Type != VansParticleRendererType::Billboard)
        throw std::invalid_argument("SpriteCard requires Billboard geometry");
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
        const auto ribbon = root.value("ribbon", ParticleJsonValue::object());
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

VansParticleFieldRule ChoiceRule(std::string path, std::vector<std::string> choices)
{
    return {std::move(path), std::move(choices)};
}

VansParticleFieldRule RangeRule(
    std::string path, double minimum, double maximum, double step = 0.01)
{
    minimum = std::min(minimum, double(float(minimum)));
    maximum = std::max(maximum, double(float(maximum)));
    return {std::move(path), {}, true, minimum, maximum, step};
}

ParticleJsonValue EncodeInitLifetime(const VansInitLifetimeModule& module)
{
    const char* operation = nullptr;
    switch (module.m_CollectionAgeOperation)
    {
    case VansParticleLifetimeAgeOperation::None: operation = "None"; break;
    case VansParticleLifetimeAgeOperation::Multiply: operation = "Multiply"; break;
    case VansParticleLifetimeAgeOperation::Replace: operation = "Replace"; break;
    default: throw std::invalid_argument("Unknown InitLifetime collection-age operation");
    }
    return {{"module", "InitLifetime"}, {"lifetime", EncodeFloatCurve(module.m_Lifetime)},
        {"collectionAge", {{"operation", operation}, {"loopPeriod", module.m_CollectionAgeLoopPeriod},
            {"curve", EncodeFloatCurve(module.m_CollectionAgeCurve)}}}};
}

void DecodeInitLifetime(const ParticleJsonValue& root, VansInitLifetimeModule& module)
{
    if (root.contains("lifetime")) DecodeFloatCurve(root["lifetime"], module.m_Lifetime);
    ValidateNonNegativeFloatCurve(module.m_Lifetime, "InitLifetime.lifetime", 0.01f);
    if (root.contains("collectionAge"))
    {
        const auto& age = root.at("collectionAge");
        const auto operation = age.value("operation", "None");
        if (operation == "None") module.m_CollectionAgeOperation = VansParticleLifetimeAgeOperation::None;
        else if (operation == "Multiply") module.m_CollectionAgeOperation = VansParticleLifetimeAgeOperation::Multiply;
        else if (operation == "Replace") module.m_CollectionAgeOperation = VansParticleLifetimeAgeOperation::Replace;
        else throw std::invalid_argument("Unknown InitLifetime collection-age operation: " + operation);
        module.m_CollectionAgeLoopPeriod = age.value("loopPeriod", 0.f);
        if (age.contains("curve")) DecodeFloatCurve(age.at("curve"), module.m_CollectionAgeCurve);
        ValidateNonNegativeFloatCurve(module.m_CollectionAgeCurve, "InitLifetime.collectionAge.curve",
            module.m_CollectionAgeOperation == VansParticleLifetimeAgeOperation::Replace ? .01f : 0.f);
    }
}

std::vector<VansParticleFieldRule> InitLifetimeFields()
{
    auto fields = VansParticleEmitterJsonCodec::FloatCurveFields("/lifetime");
    fields.push_back(ChoiceRule("/collectionAge/operation", {"None", "Multiply", "Replace"}));
    fields.push_back(RangeRule("/collectionAge/loopPeriod", 0, 3600));
    const auto ageFields = VansParticleEmitterJsonCodec::FloatCurveFields("/collectionAge/curve", 3600);
    fields.insert(fields.end(), ageFields.begin(), ageFields.end());
    return fields;
}

ParticleJsonValue EncodeInitVelocity(const VansInitVelocityModule& module)
{
    return {{"module", "InitVelocity"},
        {"mode", module.m_VelocityMode == VansInitVelocityMode::Cone ? "Cone" : module.m_VelocityMode == VansInitVelocityMode::Box ? "Box" : "Random"},
        {"angle", module.m_ConeAngle}, {"speed", module.m_Speed},
        {"minVelocity",{module.m_MinVelocity.x,module.m_MinVelocity.y,module.m_MinVelocity.z}},
        {"maxVelocity",{module.m_MaxVelocity.x,module.m_MaxVelocity.y,module.m_MaxVelocity.z}}};
}

void DecodeInitVelocity(const ParticleJsonValue& root, VansInitVelocityModule& module)
{
    const auto mode=root.value("mode","Cone");
    module.m_VelocityMode=mode=="Box" ? VansInitVelocityMode::Box : mode=="Random" ? VansInitVelocityMode::Random : VansInitVelocityMode::Cone;
    const auto vector=[&](const char* name,glm::vec3& target) {
        if (!root.contains(name)) return;
        const auto& value=root.at(name);
        if (!value.is_array() || value.size()!=3) throw std::invalid_argument("Velocity range requires three components");
        target={value[0].get<float>(),value[1].get<float>(),value[2].get<float>()};
    };
    vector("minVelocity",module.m_MinVelocity); vector("maxVelocity",module.m_MaxVelocity);
    if (!std::isfinite(glm::length(module.m_MinVelocity)) || !std::isfinite(glm::length(module.m_MaxVelocity))
        || glm::any(glm::greaterThan(module.m_MinVelocity,module.m_MaxVelocity)))
        throw std::invalid_argument("Velocity box must use finite ordered components");
    module.m_ConeAngle = root.value("angle", 25.0f);
    module.m_Speed = root.value("speed", 2.0f);
}

ParticleJsonValue EncodeInitSize(const VansInitSizeModule& module)
{
    return {{"module", "InitSize"}, {"size", EncodeFloatCurve(module.m_Size)},
        {"collectionAgeScale", EncodeFloatCurve(module.m_CollectionAgeScale)}};
}

void DecodeInitSize(const ParticleJsonValue& root, VansInitSizeModule& module)
{
    if (root.contains("size")) DecodeFloatCurve(root["size"], module.m_Size);
    ValidateNonNegativeFloatCurve(module.m_Size, "InitSize.size", 0.0f);
    if (root.contains("collectionAgeScale")) DecodeFloatCurve(root["collectionAgeScale"], module.m_CollectionAgeScale);
    ValidateNonNegativeFloatCurve(module.m_CollectionAgeScale, "InitSize.collectionAgeScale", 0.0f);
}

std::vector<VansParticleFieldRule> InitSizeFields()
{
    auto fields = VansParticleEmitterJsonCodec::FloatCurveFields("/size");
    const auto ageFields = VansParticleEmitterJsonCodec::FloatCurveFields("/collectionAgeScale", 3600);
    fields.insert(fields.end(), ageFields.begin(), ageFields.end());
    return fields;
}

ParticleJsonValue EncodeInitColor(const VansInitColorModule& module)
{
    auto scales = ParticleJsonValue::array();
    for (const auto& scale : module.m_AlphaScales) scales.push_back(EncodeFloatCurve(scale));
    return {{"module", "InitColor"},
        {"color", {module.m_Color.r, module.m_Color.g, module.m_Color.b, module.m_Color.a}},
        {"alphaScales", std::move(scales)}};
}

void DecodeInitColor(const ParticleJsonValue& root, VansInitColorModule& module)
{
    if (root.contains("color") && root["color"].is_array() && root["color"].size() >= 4)
        module.m_Color = glm::vec4(root["color"][0].get<float>(), root["color"][1].get<float>(),
            root["color"][2].get<float>(), root["color"][3].get<float>());
    if (root.contains("alphaScales"))
    {
        const auto& scales = root.at("alphaScales");
        if (!scales.is_array() || scales.size() > 8)
            throw std::invalid_argument("InitColor.alphaScales must be an array of at most 8 curves");
        for (const auto& value : scales)
        {
            VansFloatCurve scale;
            DecodeFloatCurve(value, scale);
            ValidateNonNegativeFloatCurve(scale, "InitColor.alphaScales", 0, std::numeric_limits<float>::max(), true);
            module.m_AlphaScales.push_back(std::move(scale));
        }
    }
}

ParticleJsonValue EncodeInitRotation(const VansInitRotationModule& module)
{
    return {{"module", "InitRotation"}, {"angle", EncodeFloatCurve(module.m_Angle)}};
}

void DecodeInitRotation(const ParticleJsonValue& root, VansInitRotationModule& module)
{
    if (root.contains("angle")) DecodeFloatCurve(root["angle"], module.m_Angle);
}

ParticleJsonValue EncodeInitPosition(const VansInitPositionModule& module)
{
    const char* shape = "Cone";
    switch (module.m_Shape)
    {
    case VansEmitterShape::Sphere: shape = "Sphere"; break;
    case VansEmitterShape::Box: shape = "Box"; break;
    case VansEmitterShape::Disk: shape = "Disk"; break;
    case VansEmitterShape::Edge: shape = "Edge"; break;
    default: break;
    }
    return {{"module", "InitPositionShape"}, {"shape", shape},
        {"radius", module.m_Radius}, {"arc", module.m_Arc}};
}

void DecodeInitPosition(const ParticleJsonValue& root, VansInitPositionModule& module)
{
    const std::string shape = root.value("shape", "Cone");
    if (shape == "Sphere") module.m_Shape = VansEmitterShape::Sphere;
    else if (shape == "Box") module.m_Shape = VansEmitterShape::Box;
    else if (shape == "Disk") module.m_Shape = VansEmitterShape::Disk;
    else if (shape == "Edge") module.m_Shape = VansEmitterShape::Edge;
    else module.m_Shape = VansEmitterShape::Cone;
    module.m_Radius = root.value("radius", 0.2f);
    module.m_Arc = root.value("arc", 360.0f);
}

ParticleJsonValue EncodeInitPositionOffset(const VansInitPositionOffsetModule& module)
{
    return {{"module","InitPositionOffset"},
        {"minOffset",{module.m_MinOffset.x,module.m_MinOffset.y,module.m_MinOffset.z}},
        {"maxOffset",{module.m_MaxOffset.x,module.m_MaxOffset.y,module.m_MaxOffset.z}},
        {"localCoordinates",module.m_LocalCoordinates}};
}
void DecodeInitPositionOffset(const ParticleJsonValue& root,VansInitPositionOffsetModule& module)
{
    const auto vector=[&](const char* name,glm::vec3& target) {
        if (!root.contains(name)) return;
        const auto& value=root.at(name);
        if (!value.is_array() || value.size()!=3) throw std::invalid_argument("Position offset requires three components");
        target={value[0].get<float>(),value[1].get<float>(),value[2].get<float>()};
    };
    vector("minOffset",module.m_MinOffset); vector("maxOffset",module.m_MaxOffset);
    if (!std::isfinite(glm::length(module.m_MinOffset)) || !std::isfinite(glm::length(module.m_MaxOffset))
        || glm::any(glm::greaterThan(module.m_MinOffset,module.m_MaxOffset)))
        throw std::invalid_argument("Position offset must use finite ordered components");
    module.m_LocalCoordinates=root.value("localCoordinates",true);
}

ParticleJsonValue EncodeUpdateGravity(const VansUpdateGravityModule& module)
{
    return {{"module", "UpdateGravity"},
        {"gravity", {module.m_Gravity.x, module.m_Gravity.y, module.m_Gravity.z}}};
}

void DecodeUpdateGravity(const ParticleJsonValue& root, VansUpdateGravityModule& module)
{
    if (!root.contains("gravity") || !root["gravity"].is_array() || root["gravity"].size() < 3) return;
    module.m_Gravity = glm::vec3(root["gravity"][0].get<float>(),
        root["gravity"][1].get<float>(), root["gravity"][2].get<float>());
}

ParticleJsonValue EncodeUpdateColor(const VansUpdateColorOverLifetime& module)
{
    return {{"module", "UpdateColorOverLifetime"},
        {"scaleInitialAlpha", module.m_ScaleInitialAlpha},
        {"gradient", EncodeColorGradient(module.m_Gradient)}};
}

void DecodeUpdateColor(const ParticleJsonValue& root, VansUpdateColorOverLifetime& module)
{
    module.m_ScaleInitialAlpha = root.value("scaleInitialAlpha", false);
    if (!root.contains("gradient")) return;
    DecodeColorGradient(root["gradient"], module.m_Gradient);
    ValidateColorGradient(module.m_Gradient);
}

ParticleJsonValue EncodeAlphaFade(const VansParticleAlphaFade& fade)
{
    return {{"enabled", fade.enabled}, {"durationMin", fade.durationMin},
        {"durationMax", fade.durationMax}, {"randomExponent", fade.randomExponent},
        {"timeDomain", fade.timeDomain == VansParticleFadeTimeDomain::Seconds ? "Seconds" : "Lifetime"},
        {"easing", fade.easing == VansParticleFadeEasing::Linear ? "Linear" : "SmoothStep"}};
}

void DecodeAlphaFade(const ParticleJsonValue& root, VansParticleAlphaFade& fade)
{
    if (!root.is_object()) throw std::invalid_argument("Alpha fade must be an object");
    const auto defaults = EncodeAlphaFade(fade);
    for (const auto& field : root.items())
        if (!defaults.contains(field.key()))
            throw std::invalid_argument("Unknown Alpha fade field: " + field.key());
    fade.enabled = root.value("enabled", fade.enabled);
    fade.durationMin = root.value("durationMin", fade.durationMin);
    fade.durationMax = root.value("durationMax", fade.durationMax);
    fade.randomExponent = root.value("randomExponent", fade.randomExponent);
    // 端点允许反向插值；标量范围与枚举由同一模块目录校验。
    fade.timeDomain = root.value("timeDomain", "Lifetime") == "Seconds"
        ? VansParticleFadeTimeDomain::Seconds : VansParticleFadeTimeDomain::Lifetime;
    fade.easing = root.value("easing", "SmoothStep") == "Linear"
        ? VansParticleFadeEasing::Linear : VansParticleFadeEasing::SmoothStep;
}

ParticleJsonValue EncodeUpdateAlphaFade(const VansUpdateAlphaFadeModule& module)
{
    return {{"module", "UpdateAlphaFade"}, {"fadeInDelay", module.m_FadeInDelay},
        {"fadeInFirst", module.m_FadeInFirst}, {"fadeIn", EncodeAlphaFade(module.m_FadeIn)},
        {"fadeOut", EncodeAlphaFade(module.m_FadeOut)}};
}

void DecodeUpdateAlphaFade(const ParticleJsonValue& root, VansUpdateAlphaFadeModule& module)
{
    for (const auto& field : root.items())
        if (field.key() != "module" && field.key() != "enabled" &&
            field.key() != "fadeIn" && field.key() != "fadeOut" && field.key() != "fadeInFirst" && field.key() != "fadeInDelay")
            throw std::invalid_argument("Unknown UpdateAlphaFade field: " + field.key());
    module.m_FadeInFirst = root.value("fadeInFirst", true);
    module.m_FadeInDelay = root.value("fadeInDelay", 0.f);
    if (root.contains("fadeIn")) DecodeAlphaFade(root["fadeIn"], module.m_FadeIn);
    if (root.contains("fadeOut")) DecodeAlphaFade(root["fadeOut"], module.m_FadeOut);
}

ParticleJsonValue EncodeUpdateSize(const VansUpdateSizeOverLifetime& module)
{
    return {{"module", "UpdateSizeOverLifetime"}, {"curve", EncodeCurveKeys(module.m_Curve)}};
}

void DecodeUpdateSize(const ParticleJsonValue& root, VansUpdateSizeOverLifetime& module)
{
    if (!root.contains("curve")) return;
    DecodeCurveKeys(root["curve"], module.m_Curve);
    ValidateOrderedCurve(module.m_Curve, "UpdateSizeOverLifetime.curve", true);
}

ParticleJsonValue EncodeUpdateVelocity(const VansUpdateVelocityOverLifetime& module)
{
    return {{"module", "UpdateVelocityOverLifetime"}, {"drag", module.m_Drag},
        {"turbulence", {{"enabled", module.m_TurbulenceEnabled},
            {"strength", module.m_TurbulenceStrength},
            {"frequency", module.m_TurbulenceFrequency},
            {"scrollSpeed", module.m_TurbulenceScrollSpeed}}}};
}

void DecodeUpdateVelocity(const ParticleJsonValue& root, VansUpdateVelocityOverLifetime& module)
{
    module.m_Drag = root.value("drag", 0.1f);
    if (!root.contains("turbulence")) return;
    const auto& turbulence = root["turbulence"];
    module.m_TurbulenceEnabled = turbulence.value("enabled", false);
    module.m_TurbulenceStrength = turbulence.value("strength", 0.5f);
    module.m_TurbulenceFrequency = turbulence.value("frequency", 1.0f);
    module.m_TurbulenceScrollSpeed = turbulence.value("scrollSpeed", 0.2f);
}

ParticleJsonValue EncodeUpdateRotation(const VansUpdateRotationOverLifetime& module)
{
    return {{"module", "UpdateRotationOverLifetime"},
        {"angularVelocity", EncodeFloatCurve(module.m_AngularVelocity)}};
}

void DecodeUpdateRotation(const ParticleJsonValue& root, VansUpdateRotationOverLifetime& module)
{
    if (root.contains("angularVelocity"))
        DecodeFloatCurve(root["angularVelocity"], module.m_AngularVelocity);
}

ParticleJsonValue EncodeUpdateSprite(const VansUpdateSpriteAnimModule& module)
{
    return {{"module", "UpdateSpriteAnim"}, {"columns", module.m_Columns},
        {"rows", module.m_Rows}, {"fps", module.m_FPS}};
}

void DecodeUpdateSprite(const ParticleJsonValue& root, VansUpdateSpriteAnimModule& module)
{
    module.m_Columns = root.value("columns", 4);
    module.m_Rows = root.value("rows", 4);
    module.m_FPS = root.value("fps", 0.0f);
}

struct ModuleEntry
{
    VansParticleModuleSchema schema;
    std::unique_ptr<VansParticleModule> (*create)();
    bool (*matches)(const VansParticleModule&);
    ParticleJsonValue (*encode)(const VansParticleModule&);
    void (*decode)(const ParticleJsonValue&, VansParticleModule&);
};

template<class T>
std::unique_ptr<VansParticleModule> MakeModule()
{
    return std::make_unique<T>();
}

template<class T>
bool MatchesModule(const VansParticleModule& module)
{
    return dynamic_cast<const T*>(&module) != nullptr;
}

template<class T, ParticleJsonValue (*Encode)(const T&)>
ParticleJsonValue EncodeTypedModule(const VansParticleModule& module)
{
    return Encode(static_cast<const T&>(module));
}

template<class T, void (*Decode)(const ParticleJsonValue&, T&)>
void DecodeTypedModule(const ParticleJsonValue& root, VansParticleModule& module)
{
    Decode(root, static_cast<T&>(module));
}

template<class T, ParticleJsonValue (*Encode)(const T&),
    void (*Decode)(const ParticleJsonValue&, T&)>
ModuleEntry MakeModuleEntry(const char* name, VansParticleModulePhase phase,
    std::vector<VansParticleFieldRule> fields = {})
{
    return {{name, phase, std::move(fields)}, MakeModule<T>, MatchesModule<T>,
        EncodeTypedModule<T, Encode>, DecodeTypedModule<T, Decode>};
}

const auto& ModuleCatalog()
{
    static const std::array entries{
        MakeModuleEntry<VansInitLifetimeModule, EncodeInitLifetime, DecodeInitLifetime>(
            "InitLifetime", VansParticleModulePhase::Initialize, InitLifetimeFields()),
        MakeModuleEntry<VansInitVelocityModule, EncodeInitVelocity, DecodeInitVelocity>(
            "InitVelocity", VansParticleModulePhase::Initialize,
            {ChoiceRule("/mode", {"Cone", "Random", "Box"}), RangeRule("/angle", 0.0, 180.0, 0.25),
                RangeRule("/speed", 0.0, 10000.0), RangeRule("/minVelocity/*",-10000,10000),RangeRule("/maxVelocity/*",-10000,10000)}),
        MakeModuleEntry<VansInitSizeModule, EncodeInitSize, DecodeInitSize>(
            "InitSize", VansParticleModulePhase::Initialize, InitSizeFields()),
        MakeModuleEntry<VansInitColorModule, EncodeInitColor, DecodeInitColor>(
            "InitColor", VansParticleModulePhase::Initialize, VansParticleEmitterJsonCodec::FloatCurveFields("/alphaScales/*")),
        MakeModuleEntry<VansInitRotationModule, EncodeInitRotation, DecodeInitRotation>(
            "InitRotation", VansParticleModulePhase::Initialize, VansParticleEmitterJsonCodec::FloatCurveFields("/angle")),
        MakeModuleEntry<VansInitPositionModule, EncodeInitPosition, DecodeInitPosition>(
            "InitPositionShape", VansParticleModulePhase::Initialize,
            {ChoiceRule("/shape", {"Sphere", "Box", "Cone", "Disk", "Edge"}),
                RangeRule("/radius", 0.0, 10000.0), RangeRule("/arc", 0.0, 360.0, 0.25)}),
        MakeModuleEntry<VansInitPositionOffsetModule,EncodeInitPositionOffset,DecodeInitPositionOffset>(
            "InitPositionOffset",VansParticleModulePhase::Initialize,
            {RangeRule("/minOffset/*",-10000,10000),RangeRule("/maxOffset/*",-10000,10000)}),
        MakeModuleEntry<VansUpdateGravityModule, EncodeUpdateGravity, DecodeUpdateGravity>(
            "UpdateGravity", VansParticleModulePhase::Update),
        MakeModuleEntry<VansUpdateColorOverLifetime, EncodeUpdateColor, DecodeUpdateColor>(
            "UpdateColorOverLifetime", VansParticleModulePhase::Update,
            {RangeRule("/gradient/stops/*/t", 0.0, 1.0)}),
        MakeModuleEntry<VansUpdateSizeOverLifetime, EncodeUpdateSize, DecodeUpdateSize>(
            "UpdateSizeOverLifetime", VansParticleModulePhase::Update,
            {RangeRule("/curve/*/t", 0.0, 1.0)}),
        MakeModuleEntry<VansUpdateVelocityOverLifetime, EncodeUpdateVelocity, DecodeUpdateVelocity>(
            "UpdateVelocityOverLifetime", VansParticleModulePhase::Update,
            {RangeRule("/drag", 0.0, 100.0),
                RangeRule("/turbulence/strength", 0.0, 100.0),
                RangeRule("/turbulence/frequency", 0.0, 100.0),
                RangeRule("/turbulence/scrollSpeed", 0.0, 100.0)}),
        MakeModuleEntry<VansUpdateRotationOverLifetime, EncodeUpdateRotation, DecodeUpdateRotation>(
            "UpdateRotationOverLifetime", VansParticleModulePhase::Update,
            VansParticleEmitterJsonCodec::FloatCurveFields("/angularVelocity")),
        MakeModuleEntry<VansUpdateSpriteAnimModule, EncodeUpdateSprite, DecodeUpdateSprite>(
            "UpdateSpriteAnim", VansParticleModulePhase::Update,
            {RangeRule("/columns", 1.0, 256.0, 1.0), RangeRule("/rows", 1.0, 256.0, 1.0),
                RangeRule("/fps", 0.0, 1000.0, 0.1)}),
        MakeModuleEntry<VansUpdateAlphaFadeModule, EncodeUpdateAlphaFade, DecodeUpdateAlphaFade>(
            "UpdateAlphaFade", VansParticleModulePhase::Update,
            {RangeRule("/fadeInDelay", 0, 10000),
                RangeRule("/fadeIn/durationMin", 0.000001, 10000),
                RangeRule("/fadeIn/durationMax", 0.000001, 10000),
                RangeRule("/fadeOut/durationMin", 0.000001, 10000),
                RangeRule("/fadeOut/durationMax", 0.000001, 10000),
                RangeRule("/fadeIn/randomExponent", 0, 256, 0.25),
                RangeRule("/fadeOut/randomExponent", 0, 256, 0.25),
                ChoiceRule("/fadeIn/timeDomain", {"Lifetime", "Seconds"}),
                ChoiceRule("/fadeOut/timeDomain", {"Lifetime", "Seconds"}),
                ChoiceRule("/fadeIn/easing", {"Linear", "SmoothStep"}),
                ChoiceRule("/fadeOut/easing", {"Linear", "SmoothStep"})})
    };
    return entries;
}

ParticleJsonValue EncodeModule(const VansParticleModule& module)
{
    for (const ModuleEntry& entry : ModuleCatalog())
        if (entry.matches(module)) return entry.encode(module);
    throw std::invalid_argument("Unsupported particle module type during encode");
}

std::unique_ptr<VansParticleModule> DecodeModule(
    const ParticleJsonValue& root, VansParticleModulePhase phase)
{
    const std::string name = root.value("module", "");
    for (const ModuleEntry& entry : ModuleCatalog())
    {
        if (entry.schema.name != name || entry.schema.phase != phase) continue;
        auto module = entry.create();
        module->m_Enabled = root.value("enabled", true);
        entry.decode(root, *module);
        return module;
    }
    throw std::invalid_argument("Unknown particle module or incorrect phase: " + name);
}
}

std::vector<VansParticleFieldRule> VansParticleEmitterJsonCodec::FloatCurveFields(const std::string& path, double maximumTime)
{
    std::vector<VansParticleFieldRule> rules;
    rules.push_back(ChoiceRule(path + "/mode",
        {"Constant", "RandomBetween", "Curve", "RandomBetweenCurves"}));
    rules.push_back(ChoiceRule(path + "/randomDistribution", {"Uniform", "StandardBias", "ExponentialBias"}));
    rules.push_back(RangeRule(path + "/randomBias", -1, 1));
    for (const char* keys : {"keys", "minKeys", "maxKeys"})
        rules.push_back(RangeRule(path + "/" + keys + "/*/t", 0.0, maximumTime));
    return rules;
}

const std::vector<VansParticleModuleSchema>& VansParticleEmitterJsonCodec::ModuleSchemas()
{
    static const std::vector<VansParticleModuleSchema> schemas = []
    {
        std::vector<VansParticleModuleSchema> result;
        result.reserve(ModuleCatalog().size());
        for (const ModuleEntry& entry : ModuleCatalog()) result.push_back(entry.schema);
        return result;
    }();
    return schemas;
}

Vans::VansSerializedValue VansParticleEmitterJsonCodec::ModuleDefaults()
{
    auto result = ParticleJsonValue::array();
    for (const ModuleEntry& entry : ModuleCatalog())
    {
        auto module = entry.create();
        auto definition = EncodeModule(*module);
        definition["enabled"] = module->m_Enabled;
        const char* phase = entry.schema.phase == VansParticleModulePhase::Initialize
            ? "initialize" : "update";
        result.push_back({{"phase", phase}, {"definition", definition}});
    }
    return Vans::DecodeSerializedValueJson(result);
}

Vans::VansSerializedValue VansParticleEmitterJsonCodec::EncodeEmitter(const VansParticleEmitter& emitter)
{
    ParticleJsonValue root;
    root["name"] = emitter.m_Name;
    root["enabled"] = emitter.m_Enabled;
    root["maxParticles"] = emitter.m_MaxParticles;
    root["emissionChannel"] = emitter.m_EmissionChannel;
    root["spawn"] = EncodeSpawnConfig(emitter.m_SpawnConfig);

    auto initialize = ParticleJsonValue::array();
    for (const auto& module : emitter.m_InitModules)
    {
        if (!module)
            throw std::invalid_argument("Null particle module in initialize phase");
        auto encoded = EncodeModule(*module);
        encoded["enabled"] = module->m_Enabled;
        initialize.push_back(std::move(encoded));
    }
    root["initialize"] = std::move(initialize);

    auto update = ParticleJsonValue::array();
    for (const auto& module : emitter.m_UpdateModules)
    {
        if (!module)
            throw std::invalid_argument("Null particle module in update phase");
        auto encoded = EncodeModule(*module);
        encoded["enabled"] = module->m_Enabled;
        update.push_back(std::move(encoded));
    }
    root["update"] = std::move(update);

    root["renderer"] = EncodeRendererConfig(emitter.m_RendererConfig);
    return Vans::DecodeSerializedValueJson(root);
}

void VansParticleEmitterJsonCodec::DecodeEmitter(
    const Vans::VansSerializedValue& serializedRoot,
    VansParticleEmitter& emitter)
{
    const ParticleJsonValue root =
        Vans::EncodeSerializedValueJson<ParticleJsonValue>(serializedRoot);
    emitter.m_Name = root.value("name", "Emitter");
    emitter.m_Enabled = root.value("enabled", true);
    emitter.m_MaxParticles = root.value("maxParticles", 1000u);
    emitter.m_EmissionChannel = root.value("emissionChannel",0u);
    if (emitter.m_EmissionChannel>255) throw std::invalid_argument("emissionChannel must be in [0,255]");
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
            emitter.m_InitModules.push_back(
                DecodeModule(moduleJson, VansParticleModulePhase::Initialize));
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
            emitter.m_UpdateModules.push_back(
                DecodeModule(moduleJson, VansParticleModulePhase::Update));
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
