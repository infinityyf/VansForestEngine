#include "VansParticleAssetJsonCodec.h"
#include "VansParticleEmitterJsonCodec.h"
#include "../Authoring/VansParticleAuthoringSchema.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <memory>
#include <utility>
#include <cmath>
#include <stdexcept>

namespace VansGraphics
{
Vans::ParticleJson VansParticleAssetJsonCodec::Encode(const VansParticleAsset& asset)
{
    Vans::ParticleJson root;
    root["name"] = asset.m_Name;

    Vans::ParticleJson global;
    global["duration"] = asset.m_Duration;
    global["loop"] = asset.m_Loop;
    global["prewarm"] = asset.m_Prewarm;
    global["startDelay"] = asset.m_StartDelay;
    global["emissionFrame"] = asset.m_EmissionFrame == VansParticleEmissionFrame::World ? "World" : "Source";
    global["fixedStep"] = asset.m_FixedStep;
    global["maxSubsteps"] = asset.m_MaxSubsteps;
    global["drainFade"] = asset.m_DrainFade;
    root["global"] = std::move(global);

    Vans::ParticleJson emitters = Vans::ParticleJson::array();
    for (const auto& emitter : asset.m_Emitters)
    {
        if (emitter)
            emitters.push_back(VansParticleEmitterJsonCodec::EncodeEmitter(*emitter));
    }
    root["emitters"] = std::move(emitters);
    return root;
}

bool VansParticleAssetJsonCodec::Decode(
    const Vans::ParticleJson& root,
    const std::filesystem::path& filePath,
    VansParticleAsset& asset,
    std::string& error)
{
    try
    {
        if (root.contains("version")) throw std::invalid_argument("Particle assets have a single current schema without version fields");
        VansParticleAuthoringSchema::ValidateFields(root);
        VansParticleAsset decoded;
        decoded.m_Name = root.value("name", "");

        if (root.contains("global") && root["global"].is_object())
        {
            const auto& global = root["global"];
            decoded.m_Duration = global.value("duration", 5.0f);
            decoded.m_Loop = global.value("loop", true);
            decoded.m_Prewarm = global.value("prewarm", false);
            decoded.m_StartDelay = global.value("startDelay", 0.0f);
            if (!std::isfinite(decoded.m_StartDelay) || decoded.m_StartDelay < 0)
                throw std::runtime_error("Particle startDelay must be finite and nonnegative");
            if (global.contains("worldAligned") || global.contains("simulationSpace"))
                throw std::invalid_argument("Particle uses emissionFrame; simulation is always world-space");
            const auto emissionFrame = global.value("emissionFrame", "Source");
            if (emissionFrame != "World" && emissionFrame != "Source") throw std::invalid_argument("Invalid emissionFrame");
            decoded.m_EmissionFrame = emissionFrame == "World" ? VansParticleEmissionFrame::World : VansParticleEmissionFrame::Source;
            decoded.m_FixedStep = global.value("fixedStep", 0.0f);
            decoded.m_MaxSubsteps = global.value("maxSubsteps", 8u);
            decoded.m_DrainFade = global.value("drainFade", 0.0f);
            if (!std::isfinite(decoded.m_Duration) || decoded.m_Duration < 0.01f || decoded.m_Duration > 3600
                || !std::isfinite(decoded.m_FixedStep) || decoded.m_FixedStep < 0
                || (decoded.m_FixedStep > 0 && (decoded.m_FixedStep < 1.0f/240 || decoded.m_FixedStep > 1.0f/30))
                || decoded.m_MaxSubsteps < 1 || decoded.m_MaxSubsteps > 16
                || !std::isfinite(decoded.m_DrainFade) || decoded.m_DrainFade < 0 || decoded.m_DrainFade > 60
                || (decoded.m_Prewarm && decoded.m_Duration > 30))
                throw std::runtime_error("Particle global timing is outside supported finite limits");
        }

        if (root.contains("emitters") && root["emitters"].is_array())
        {
            if (root["emitters"].size() > 16) throw std::invalid_argument("Particle assets support at most 16 emitters");
            for (const auto& emitterJson : root["emitters"])
            {
                auto emitter = std::make_unique<VansParticleEmitter>();
                VansParticleEmitterJsonCodec::DecodeEmitter(emitterJson, *emitter);
                decoded.m_Emitters.push_back(std::move(emitter));
            }
        }

        asset = std::move(decoded);
        return true;
    }
    catch (const std::exception& exception)
    {
        error = "Invalid particle asset JSON " + filePath.string() + ": " + exception.what();
        return false;
    }
}
}
