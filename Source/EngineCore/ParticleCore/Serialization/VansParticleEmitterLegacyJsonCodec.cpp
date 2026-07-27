#include "VansParticleEmitterLegacyJsonCodec.h"

#include <nlohmann/json.hpp>

#include <memory>
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
    if (auto* initLifetime = dynamic_cast<VansInitLifetimeModule*>(&module))
    {
        if (root.contains("lifetime"))
            DecodeFloatCurve(root["lifetime"], initLifetime->m_Lifetime);
    }
    else if (auto* initVelocity = dynamic_cast<VansInitVelocityModule*>(&module))
    {
        const std::string mode = root.value("mode", "Cone");
        initVelocity->m_VelocityMode = mode == "Random" ? VansInitVelocityMode::Random : VansInitVelocityMode::Cone;
        initVelocity->m_ConeAngle = root.value("angle", 25.0f);
        initVelocity->m_Speed = root.value("speed", 2.0f);
    }
    else if (auto* initSize = dynamic_cast<VansInitSizeModule*>(&module))
    {
        if (root.contains("size"))
            DecodeFloatCurve(root["size"], initSize->m_Size);
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
            DecodeColorGradient(root["gradient"], updateColor->m_Gradient);
    }
    else if (auto* updateSize = dynamic_cast<VansUpdateSizeOverLifetime*>(&module))
    {
        if (root.contains("curve"))
            DecodeCurveKeys(root["curve"], updateSize->m_Curve);
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

Vans::ParticleJson VansParticleEmitterLegacyJsonCodec::EncodeEmitter(const VansParticleEmitter& emitter)
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
                initialize.push_back(std::move(encoded));
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
                update.push_back(std::move(encoded));
        }
    }
    root["update"] = std::move(update);

    root["renderer"] = EncodeRendererConfig(emitter.m_RendererConfig);
    return root;
}

void VansParticleEmitterLegacyJsonCodec::DecodeEmitter(
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
