#include "VansParticleAssetJsonCodec.h"
#include "VansParticleEmitterJsonCodec.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <memory>
#include <utility>

namespace VansGraphics
{
Vans::ParticleJson VansParticleAssetJsonCodec::Encode(const VansParticleAsset& asset)
{
    Vans::ParticleJson root;
    root["version"] = asset.m_Version;
    root["name"] = asset.m_Name;

    Vans::ParticleJson global;
    global["duration"] = asset.m_Duration;
    global["loop"] = asset.m_Loop;
    global["prewarm"] = asset.m_Prewarm;
    global["simulationSpace"] = asset.m_SimSpace;
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
        VansParticleAsset decoded;
        decoded.m_FilePath = filePath.string();
        decoded.m_Version = root.value("version", 1);
        decoded.m_Name = root.value("name", "");

        if (root.contains("global") && root["global"].is_object())
        {
            const auto& global = root["global"];
            decoded.m_Duration = global.value("duration", 5.0f);
            decoded.m_Loop = global.value("loop", true);
            decoded.m_Prewarm = global.value("prewarm", false);
            decoded.m_SimSpace = global.value("simulationSpace", std::string("Local"));
        }

        if (root.contains("emitters") && root["emitters"].is_array())
        {
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
