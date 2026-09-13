#include "VansParticleAuthoringSchema.h"
#include "../Serialization/VansParticleAssetJsonCodec.h"
#include "../Serialization/VansParticleEmitterJsonCodec.h"
#include "../../AssetCore/Serialization/VansSerializedPathPattern.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace VansGraphics
{
const std::vector<VansParticleAuthoringField>& VansParticleAuthoringSchema::Fields()
{
    static const auto fields = []
    {
        std::vector<VansParticleAuthoringField> result;
        const auto choice = [&](std::string path, std::vector<std::string> values)
        { result.push_back({std::move(path), std::move(values)}); };
        const auto range = [&](std::string path, double minimum, double maximum, double step = 0.01)
        {
            // 同时容纳作者输入的十进制端点和运行时 float 编码后的端点。
            minimum = std::min(minimum, double(float(minimum)));
            maximum = std::max(maximum, double(float(maximum)));
            result.push_back({std::move(path), {}, true, minimum, maximum, step});
        };
        choice("/global/emissionFrame", {"Source", "World"});
        range("/global/duration", 0.01, 3600);
        range("/global/startDelay", 0, 3600);
        range("/global/fixedStep", 0, 1.0 / 30, 0.0001);
        range("/global/maxSubsteps", 1, 16, 1);
        range("/global/drainFade", 0, 60);
        const std::string emitter = "/emitters/*/", renderer = emitter + "renderer/";
        range(emitter + "maxParticles", 1, 65536, 1);
        choice(emitter + "spawn/type", {"RateOverTime", "Burst"});
        range(emitter + "spawn/rate", 0, 65536, 0.1);
        range(emitter + "spawn/bursts/*/time", 0, 3600);
        range(emitter + "spawn/bursts/*/count", 1, 65536, 1);
        range(emitter + "spawn/bursts/*/cycles", 0, 100000, 1);
        range(emitter + "spawn/bursts/*/interval", 0, 3600);
        choice(renderer + "type", {"None", "Billboard", "Ribbon"});
        choice(renderer + "sortMode", {"None", "ByDistance", "OldestFirst", "NewestFirst"});
        choice(renderer + "lightingMode", {"UnlitFlipbook", "SixWayLit"});
        choice(renderer + "ribbon/rootMode", {"None", "FollowSource"});
        choice(renderer + "ribbon/stopAttachment", {"KeepUntilInvisible", "DetachOnStop"});
        range(renderer + "ribbon/rootWidth", 0, 100, 0.001);
        range(renderer + "ribbon/maxSegmentLength", 0.001, 100);
        range(renderer + "ribbon/uvFlowSpeed", -100, 100);
        range(renderer + "ribbon/softIntersection", 0, 10, 0.001);
        range(renderer + "ribbon/tipFadeDistance", 0, 10, 0.001);
        for (const char* dimension : {"columns", "rows"})
            range(renderer + "spriteSheet/" + dimension, 1, 256, 1);
        for (const char* field : {"lightIntensity", "ambientIntensity", "emissiveIntensity", "absorptionStrength"})
            range(renderer + "sixWayLighting/" + field, 0, 100);
        range(renderer + "sixWayLighting/lightmapRemapMin", 0, 1);
        range(renderer + "sixWayLighting/lightmapRemapMax", 0, 1);
        range(renderer + "volumetric/radiusScale", 0.01, 32);
        range(renderer + "volumetric/maxDistanceMeters", 0.1, 100000, 0.1);
        for (const char* field : {"densityMultiplier", "extinctionPerMeter", "directLightingScale", "skyLightingScale"})
            range(renderer + "volumetric/" + field, 0, 100);
        range(renderer + "volumetric/anisotropy", -0.9, 0.9);
        range(renderer + "volumetric/edgeSoftness", 0.001, 1);
        range(renderer + "volumetric/injectionPriority", 0, 255, 1);
        choice(emitter + "initialize/*/mode", {"Cone", "Random"});
        choice(emitter + "initialize/*/shape", {"Sphere", "Box", "Cone", "Disk", "Edge"});
        range(emitter + "initialize/*/speed", 0, 10000);
        range(emitter + "initialize/*/radius", 0, 10000);
        range(emitter + "initialize/*/angle", 0, 180, 0.25);
        range(emitter + "initialize/*/arc", 0, 360, 0.25);
        for (const char* phase : {"initialize", "update"})
        {
            const auto module = emitter + phase + "/*/";
            for (const char* curve : {"lifetime", "size", "angle", "angularVelocity"})
            {
                const auto path = module + curve + "/";
                choice(path + "mode", {"Constant", "RandomBetween", "Curve", "RandomBetweenCurves"});
                for (const char* keys : {"keys", "minKeys", "maxKeys"})
                    range(path + keys + "/*/t", 0, 1);
            }
        }
        range(emitter + "update/*/curve/*/t", 0, 1);
        range(emitter + "update/*/gradient/stops/*/t", 0, 1);
        range(emitter + "update/*/drag", 0, 100);
        for (const char* field : {"strength", "frequency", "scrollSpeed"})
            range(emitter + "update/*/turbulence/" + field, 0, 100);
        for (const char* dimension : {"columns", "rows"})
            range(emitter + "update/*/" + dimension, 1, 256, 1);
        range(emitter + "update/*/fps", 0, 1000, 0.1);
        return result;
    }();
    return fields;
}

Vans::ParticleJson VansParticleAuthoringSchema::Defaults()
{
    VansParticleAsset asset;
    VansParticleEmitter emitter;
    const BurstConfig burst;
    auto key = Vans::ParticleJson{{"t", 1.0}, {"value", 1.0}};
    auto stop = Vans::ParticleJson{{"t", 1.0}, {"color", {1.0, 1.0, 1.0, 0.0}}};
    return {{"asset", VansParticleAssetJsonCodec::Encode(asset)},
        {"emitter", VansParticleEmitterJsonCodec::EncodeEmitter(emitter)},
        {"modules", VansParticleEmitterJsonCodec::ModuleDefaults()},
        {"arrayElements", {{"curve", key}, {"keys", key}, {"minKeys", key}, {"maxKeys", key}, {"stops", stop},
            {"bursts", {{"time", burst.time}, {"count", burst.count}, {"cycles", burst.cycles}, {"interval", burst.interval}}}}}};
}

void VansParticleAuthoringSchema::ValidateFields(const Vans::ParticleJson& root)
{
    if (!root.is_object()) throw std::invalid_argument("Particle asset must be an object");
    std::function<void(const Vans::ParticleJson&, const std::string&)> visit;
    visit = [&](const auto& value, const auto& path)
    {
        for (const auto& field : Fields())
        {
            if (!Vans::MatchSerializedPathPattern(field.pathPattern, path)) continue;
            // angle 既可表示速度圆锥角，也可表示旋转曲线对象。
            if (field.hasLimits && !value.is_object())
            {
                if (!value.is_number()) throw std::invalid_argument(path + " must be numeric");
                const double numeric = value.template get<double>();
                if (!std::isfinite(numeric) || numeric < field.minimum || numeric > field.maximum)
                    throw std::invalid_argument(path + " is outside supported limits");
            }
            if (!field.choices.empty() && (!value.is_string() ||
                std::find(field.choices.begin(), field.choices.end(), value.template get<std::string>()) == field.choices.end()))
                throw std::invalid_argument(path + " contains an unsupported option");
        }
        if (value.is_number_float() && !std::isfinite(value.template get<double>()))
            throw std::invalid_argument(path + " must be finite");
        if (value.is_object())
            for (auto it = value.begin(); it != value.end(); ++it) visit(it.value(), path + "/" + it.key());
        if (value.is_array())
            for (size_t i = 0; i < value.size(); ++i) visit(value[i], path + "/" + std::to_string(i));
    };
    visit(root, "");
}
}
