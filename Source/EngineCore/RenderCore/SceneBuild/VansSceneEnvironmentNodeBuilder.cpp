#include "VansSceneEnvironmentNodeBuilder.h"

#include "../VansScene.h"
#include "../VulkanCore/VansMesh.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansRenderPass.h"
#include "../TerrainCore/VansTerrain.h"
#include "../VegetationCore/VansVegetationSystem.h"
#include "../WaterCore/VansWaterMaterial.h"
#include "../WaterCore/VansWaterSystem.h"
#include "../VansGraphicsDevice.h"
#include "../../Configration/VansConfigration.h"
#include "../../PhysicsCore/VansPhysics.h"
#include "../../PhysicsCore/VansTerrainPhysicsNode.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../Util/VansLog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>

namespace VansGraphics
{
namespace
{
std::string ReadStringField(const json& object, const char* key)
{
    if (!object.is_object())
        return {};
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

glm::vec2 ReadVec2Field(const json& object, const char* key, glm::vec2 fallback)
{
    if (!object.is_object() || !object.contains(key))
        return fallback;

    const auto& value = object[key];
    if (value.is_array() && value.size() >= 2)
        return { value[0].get<float>(), value[1].get<float>() };
    if (value.is_object())
        return {
            value.value("x", fallback.x),
            value.value("y", fallback.y)
        };
    return fallback;
}

VansWaveMode ReadWaterWaveMode(const json& object, const char* key, VansWaveMode fallback)
{
    std::string modeStr = ReadStringField(object, key);
    std::transform(modeStr.begin(), modeStr.end(), modeStr.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (modeStr == "fft") return VansWaveMode::FFT;
    if (modeStr == "gerstner") return VansWaveMode::Gerstner;
    if (modeStr == "waveparticle" || modeStr == "wave_particle" || modeStr == "particle")
        return VansWaveMode::WaveParticle;
    if (modeStr == "hybrid")
    {
        VANS_LOG_WARN("[AddWaterNode] Deprecated water wave mode 'hybrid' mapped to FFT.");
        return VansWaveMode::FFT;
    }
    return fallback;
}

std::string ReadVegetationConfigPath(const json& vegetationData)
{
    if (vegetationData.is_string())
        return vegetationData.get<std::string>();
    if (!vegetationData.is_object())
        return {};
    std::string path = ReadStringField(vegetationData, "config");
    if (path.empty()) path = ReadStringField(vegetationData, "configPath");
    if (path.empty()) path = ReadStringField(vegetationData, "path");
    return path;
}

json LoadVegetationConfigFromReference(const json& vegetationData, const std::string& projectRoot)
{
    const std::string configPath = ReadVegetationConfigPath(vegetationData);
    if (configPath.empty())
        return vegetationData;

    std::filesystem::path resolved = std::filesystem::path(configPath);
    if (resolved.is_relative())
        resolved = std::filesystem::path(projectRoot) / resolved;

    std::ifstream input(resolved);
    if (!input)
    {
        VANS_LOG_ERROR("[VegetationConfig] Cannot open config file: " << resolved.string());
        return json::object();
    }

    json loaded = json::parse(input, nullptr, false);
    if (loaded.is_discarded() || !loaded.is_object())
    {
        VANS_LOG_ERROR("[VegetationConfig] Invalid JSON config file: " << resolved.string());
        return json::object();
    }
    VANS_LOG("[VegetationConfig] Loaded config file: " << resolved.string());

    if (vegetationData.is_object())
    {
        for (const auto& [key, value] : vegetationData.items())
        {
            if (key == "config" || key == "configPath" || key == "path")
                continue;
            loaded[key] = value;
        }
    }
    return loaded;
}
}

void VansSceneEnvironmentNodeBuilder::AddTerrainNode(VansScene& scene, VansVKDevice* device, json& terrainData)
{
    auto vansConfigration = VansConfigration::GetInstance();
    auto& projectMgr = Vans::VansProjectManager::Get();
    std::string projectRoot = projectMgr.IsProjectLoaded()
        ? projectMgr.GetProjectRootPath()
        : vansConfigration->GetProjectRootPath();

    TerrainConfig config;

    // Heightmap (required)
    config.heightmapPath = projectRoot + terrainData["heightmap"].get<std::string>();
    config.terrainSize = terrainData.value("terrainSize", config.terrainSize);
    config.maxHeight = terrainData.value("maxHeight", config.maxHeight);
    config.heightOffset = terrainData.value("heightOffset", config.heightOffset);
    config.splitDistMult = terrainData.value("splitDistMult", config.splitDistMult);
    config.lodDistanceRatio = terrainData.value("lodDistanceRatio", config.lodDistanceRatio);
    config.morphStartRatio = terrainData.value("morphStartRatio", config.morphStartRatio);
    config.maxPatchInstances = terrainData.value("maxPatchInstances", config.maxPatchInstances);

    // Tessellation (optional, defaults from TerrainConfig)
    if (terrainData.contains("tessellation") && terrainData["tessellation"].is_object())
    {
        auto& tessJson = terrainData["tessellation"];
        config.enableTessellation   = tessJson.value("enabled",  config.enableTessellation);
        config.tessellationDistance = tessJson.value("distance", config.tessellationDistance);
        config.maxTessellationLevel = tessJson.value("maxLevel", config.maxTessellationLevel);
        config.tessellationPower        = tessJson.value("power",               config.tessellationPower);
        config.tessLodBias              = tessJson.value("lodBias",             config.tessLodBias);
        config.tessDisplacementStrength = tessJson.value("displacementStrength", config.tessDisplacementStrength);

        // 程序化噪声细节（替代 displacementStrength）
        if (tessJson.contains("noiseDetail") && tessJson["noiseDetail"].is_object())
        {
            auto& noiseJson = tessJson["noiseDetail"];
            config.enableNoiseDetail = noiseJson.value("enabled",       config.enableNoiseDetail);
            config.noiseStrength     = noiseJson.value("strength",      config.noiseStrength);
            config.noiseFrequency    = noiseJson.value("frequency",     config.noiseFrequency);
            config.noiseLacunarity   = noiseJson.value("lacunarity",    config.noiseLacunarity);
            config.noiseGain         = noiseJson.value("gain",          config.noiseGain);
            config.noiseOctaves      = noiseJson.value("octaves",       config.noiseOctaves);
            config.noiseWarpStrength = noiseJson.value("warpStrength",  config.noiseWarpStrength);
            config.noiseFadeStart    = noiseJson.value("fadeStart",     config.noiseFadeStart);
        }
    }

    // Splatmaps (required, array of 2)
    if (terrainData.contains("splatmaps") && terrainData["splatmaps"].is_array())
    {
        auto& splatmaps = terrainData["splatmaps"];
        if (splatmaps.size() >= 1)
            config.splatmap0Path = projectRoot + splatmaps[0].get<std::string>();
        if (splatmaps.size() >= 2)
            config.splatmap1Path = projectRoot + splatmaps[1].get<std::string>();
    }

    // Layers (up to 8)
    if (terrainData.contains("layers") && terrainData["layers"].is_array())
    {
        for (auto& layerJson : terrainData["layers"])
        {
            TerrainLayerConfig layer;

            // Support texture name references (look up from scene texture manager)
            if (layerJson.contains("albedo_texture"))
            {
                std::string texName = layerJson["albedo_texture"].get<std::string>();
                layer.albedoTex = static_cast<VansTexture*>(scene.GetTextureAsset(texName));
            }
            else if (layerJson.contains("albedo"))
                layer.albedoPath = projectRoot + layerJson["albedo"].get<std::string>();

            if (layerJson.contains("normal_texture"))
            {
                std::string texName = layerJson["normal_texture"].get<std::string>();
                layer.normalTex = static_cast<VansTexture*>(scene.GetTextureAsset(texName));
            }
            else if (layerJson.contains("normal"))
                layer.normalPath = projectRoot + layerJson["normal"].get<std::string>();

            if (layerJson.contains("roughness_texture"))
            {
                std::string texName = layerJson["roughness_texture"].get<std::string>();
                layer.roughnessTex = static_cast<VansTexture*>(scene.GetTextureAsset(texName));
            }
            else if (layerJson.contains("roughness"))
                layer.roughnessPath = projectRoot + layerJson["roughness"].get<std::string>();

            if (layerJson.contains("tiling"))
                layer.tiling = layerJson["tiling"].get<float>();
            config.layers.push_back(layer);
        }
    }

    RenderNodeType type = RenderNodeType::TERRAIN_NODE;
    VansRenderNode* renderNode = new VansTerrainRenderNode(device, config, type);

    // Read optional name
    std::string name = "TerrainNode";
    if (terrainData.contains("name"))
    {
        name = terrainData["name"].get<std::string>();
    }
    renderNode->SetName(name);
    scene.RegistRenderNode(renderNode, type);

    // Terrain 物理碰撞是可选项，只由 terrain.collision.enabled 控制。
    if (terrainData.contains("collision") && terrainData["collision"].is_object())
    {
        auto& collisionJson = terrainData["collision"];
        VansEngine::TerrainPhysicsProperties terrainPhysicsProps;
        terrainPhysicsProps.enabled = collisionJson.value("enabled", false);
        terrainPhysicsProps.heightmapPath = config.heightmapPath;
        terrainPhysicsProps.terrainSize = config.terrainSize;
        terrainPhysicsProps.maxHeight = config.maxHeight;
        terrainPhysicsProps.heightOffset = config.heightOffset;

        if (collisionJson.contains("terrainSize"))
            terrainPhysicsProps.terrainSize = collisionJson["terrainSize"].get<float>();
        if (collisionJson.contains("maxHeight"))
            terrainPhysicsProps.maxHeight = collisionJson["maxHeight"].get<float>();
        if (collisionJson.contains("heightOffset"))
            terrainPhysicsProps.heightOffset = collisionJson["heightOffset"].get<float>();
        if (collisionJson.contains("layer"))
            terrainPhysicsProps.layerName = collisionJson["layer"].get<std::string>();
        if (collisionJson.contains("flipX"))
            terrainPhysicsProps.flipX = collisionJson["flipX"].get<bool>();
        if (collisionJson.contains("flipZ"))
            terrainPhysicsProps.flipZ = collisionJson["flipZ"].get<bool>();

        if (collisionJson.contains("material") && collisionJson["material"].is_object())
        {
            auto& materialJson = collisionJson["material"];
            if (materialJson.contains("staticFriction"))
                terrainPhysicsProps.material.staticFriction = materialJson["staticFriction"].get<float>();
            if (materialJson.contains("dynamicFriction"))
                terrainPhysicsProps.material.dynamicFriction = materialJson["dynamicFriction"].get<float>();
            if (materialJson.contains("restitution"))
                terrainPhysicsProps.material.restitution = materialJson["restitution"].get<float>();
        }

        if (terrainPhysicsProps.enabled)
        {
            auto& physicsSystem = VansEngine::VansPhysicsSystem::GetInstance();
            std::lock_guard<std::mutex> simLock(physicsSystem.GetSimulationMutex());

            scene.SetTerrainPhysicsNode(nullptr);
            auto* terrainPhysicsNode = new VansEngine::VansTerrainPhysicsNode();
            if (!terrainPhysicsNode->Initialize(terrainPhysicsProps))
            {
                delete terrainPhysicsNode;
                VANS_LOG_WARN("[VansScene] Terrain collision initialization failed.");
            }
            else
            {
                scene.SetTerrainPhysicsNode(terrainPhysicsNode);
            }
        }
    }
}

void VansSceneEnvironmentNodeBuilder::AddWaterNode(VansScene& scene, VkDevice& device, json& waterData)
{
    VansWaterConfig config;
    const std::uint32_t schemaVersion = waterData.value("schemaVersion", 0u);
    if (schemaVersion != VansWaterConfig::SCHEMA_VERSION)
        VANS_LOG_WARN("[AddWaterNode] Water data is not schema V4; deprecated fields are read through compatibility paths.");

    config.m_WaterLevel        = waterData.value("level", 3.4f);
    config.m_SpecularIntensity = waterData.value("specularIntensity", 1.0f);

    // ── medium 块 ──────────────────────────────────────────────────────────
    if (waterData.contains("medium") && waterData["medium"].is_object())
    {
        auto& m = waterData["medium"];

        // absorption — 支持数组 [r,g,b] 或对象 {r,g,b}
        if (m.contains("absorption"))
        {
            auto& a = m["absorption"];
            if (a.is_array() && a.size() >= 3)
                config.m_Medium.m_AbsorptionCoeff = {a[0], a[1], a[2]};
            else if (a.is_object())
                config.m_Medium.m_AbsorptionCoeff = {
                    a.value("r", 0.05f), a.value("g", 0.08f), a.value("b", 0.20f)};
        }

        if (m.contains("scattering"))
        {
            auto& s = m["scattering"];
            if (s.is_array() && s.size() >= 3)
                config.m_Medium.m_ScatteringCoeff = {s[0], s[1], s[2]};
            else if (s.is_object())
                config.m_Medium.m_ScatteringCoeff = {
                    s.value("r", 0.03f), s.value("g", 0.05f), s.value("b", 0.08f)};
        }

        config.m_Medium.m_IOR          = m.value("ior",          config.m_Medium.m_IOR);
        config.m_Medium.m_FresnelPower = m.value("fresnelPower",  config.m_Medium.m_FresnelPower);
        config.m_Medium.m_Anisotropy   = m.value("anisotropy",   config.m_Medium.m_Anisotropy);
        config.m_Medium.m_WaterRoughness = m.value("roughness",  config.m_Medium.m_WaterRoughness);

        if (m.contains("deepColor"))
        {
            auto& d = m["deepColor"];
            if (d.is_array() && d.size() >= 3)
                config.m_Medium.m_DeepColor = {d[0], d[1], d[2], 1.0f};
            else if (d.is_object())
                config.m_Medium.m_DeepColor = {
                    d.value("r", 0.0f), d.value("g", 0.05f), d.value("b", 0.2f), 1.0f};
        }

        if (m.contains("shallowColor"))
        {
            auto& s = m["shallowColor"];
            if (s.is_array() && s.size() >= 3)
                config.m_Medium.m_ShallowColor = {s[0], s[1], s[2], 1.0f};
            else if (s.is_object())
                config.m_Medium.m_ShallowColor = {
                    s.value("r", 0.1f), s.value("g", 0.3f), s.value("b", 0.4f), 1.0f};
        }
    }

    // V2 spectral cascades are independent of geometry clipmap LODs.
    if (waterData.contains("spectrum") && waterData["spectrum"].is_object())
    {
        auto& w = waterData["spectrum"];

        config.m_Spectrum.m_Mode = ReadWaterWaveMode(w, "mode", config.m_Spectrum.m_Mode);
        config.m_Spectrum.m_BaseCoverage = w.value("baseCoverage", config.m_Spectrum.m_BaseCoverage);
        config.m_Spectrum.m_CascadeScale = w.value("cascadeScale", config.m_Spectrum.m_CascadeScale);
        config.m_Spectrum.m_CascadeCount = w.value("cascadeCount", config.m_Spectrum.m_CascadeCount);
        config.m_Spectrum.m_WindSpeed = w.value("windSpeed", config.m_Spectrum.m_WindSpeed);
        config.m_Spectrum.m_SwellAmplitude = w.value("swellAmplitude", config.m_Spectrum.m_SwellAmplitude);
        config.m_Spectrum.m_Choppiness = w.value("choppiness", config.m_Spectrum.m_Choppiness);
        config.m_Spectrum.m_GerstnerWaveCount = w.value("gerstnerWaveCount", config.m_Spectrum.m_GerstnerWaveCount);

        config.m_Spectrum.m_WindDirection = ReadVec2Field(
            w, "windDirection", config.m_Spectrum.m_WindDirection);

        config.m_Spectrum.m_SpectrumAmplitude = w.value("spectrumAmplitude", config.m_Spectrum.m_SpectrumAmplitude);
        config.m_Spectrum.m_MinWavelength = w.value("minWavelength", config.m_Spectrum.m_MinWavelength);
        config.m_Spectrum.m_SmallWaveDamping = w.value("smallWaveDamping", config.m_Spectrum.m_SmallWaveDamping);
        config.m_Spectrum.m_WindDependency = w.value("windDependency", config.m_Spectrum.m_WindDependency);
        config.m_Spectrum.m_Depth = w.value("depth", config.m_Spectrum.m_Depth);
        config.m_Spectrum.m_RepeatPeriod = w.value("repeatPeriod", config.m_Spectrum.m_RepeatPeriod);
        config.m_Spectrum.m_RandomSeed = w.value("randomSeed", config.m_Spectrum.m_RandomSeed);
    }

    if (waterData.contains("waves") && waterData["waves"].is_object())
    {
        auto& w = waterData["waves"];
        config.m_Spectrum.m_Mode = ReadWaterWaveMode(w, "mode", config.m_Spectrum.m_Mode);
        config.m_Spectrum.m_CascadeCount = w.value("cascadeCount", config.m_Spectrum.m_CascadeCount);
        config.m_Spectrum.m_BaseCoverage = w.value("baseCoverage", config.m_Spectrum.m_BaseCoverage);
        config.m_Spectrum.m_CascadeScale = w.value("cascadeScale", config.m_Spectrum.m_CascadeScale);
        config.m_Spectrum.m_WindDirection = ReadVec2Field(
            w, "windDirection", config.m_Spectrum.m_WindDirection);
        config.m_Spectrum.m_WindSpeed = w.value("windSpeed", config.m_Spectrum.m_WindSpeed);
        config.m_Spectrum.m_Choppiness = w.value("choppiness", config.m_Spectrum.m_Choppiness);

        if (w.contains("gerstner") && w["gerstner"].is_object())
        {
            auto& g = w["gerstner"];
            config.m_Spectrum.m_SwellAmplitude = g.value(
                "swellAmplitude", config.m_Spectrum.m_SwellAmplitude);
            config.m_Spectrum.m_GerstnerWaveCount = g.value(
                "waveCount", config.m_Spectrum.m_GerstnerWaveCount);
            config.m_Spectrum.m_GerstnerWaveCount = g.value(
                "gerstnerWaveCount", config.m_Spectrum.m_GerstnerWaveCount);
        }

        if (w.contains("fft") && w["fft"].is_object())
        {
            auto& f = w["fft"];
            config.m_Spectrum.m_SpectrumAmplitude = f.value(
                "spectrumAmplitude", config.m_Spectrum.m_SpectrumAmplitude);
            config.m_Spectrum.m_MinWavelength = f.value(
                "minWavelength", config.m_Spectrum.m_MinWavelength);
            config.m_Spectrum.m_SmallWaveDamping = f.value(
                "smallWaveDamping", config.m_Spectrum.m_SmallWaveDamping);
            config.m_Spectrum.m_WindDependency = f.value(
                "windDependency", config.m_Spectrum.m_WindDependency);
            config.m_Spectrum.m_Depth = f.value("depth", config.m_Spectrum.m_Depth);
            config.m_Spectrum.m_RepeatPeriod = f.value(
                "repeatPeriod", config.m_Spectrum.m_RepeatPeriod);
            config.m_Spectrum.m_RandomSeed = f.value(
                "randomSeed", config.m_Spectrum.m_RandomSeed);
        }

        if (w.contains("waveParticle") && w["waveParticle"].is_object())
        {
            auto& p = w["waveParticle"];
            config.m_WaveParticle.m_ParticleCount = p.value(
                "particleCount", config.m_WaveParticle.m_ParticleCount);
            config.m_WaveParticle.m_OctaveCount = p.value(
                "octaveCount", config.m_WaveParticle.m_OctaveCount);
            config.m_WaveParticle.m_Profile = p.value(
                "profile", config.m_WaveParticle.m_Profile);
            config.m_WaveParticle.m_DomainSize = p.value(
                "domainSize", config.m_WaveParticle.m_DomainSize);
            config.m_WaveParticle.m_Amplitude = p.value(
                "amplitude", config.m_WaveParticle.m_Amplitude);
            config.m_WaveParticle.m_MinRadius = p.value(
                "minRadius", config.m_WaveParticle.m_MinRadius);
            config.m_WaveParticle.m_MaxRadius = p.value(
                "maxRadius", config.m_WaveParticle.m_MaxRadius);
            config.m_WaveParticle.m_PhaseVelocity = p.value(
                "phaseVelocity", config.m_WaveParticle.m_PhaseVelocity);
            config.m_WaveParticle.m_Damping = p.value(
                "damping", config.m_WaveParticle.m_Damping);
            config.m_WaveParticle.m_DirectionSpread = p.value(
                "directionSpread", config.m_WaveParticle.m_DirectionSpread);
            config.m_WaveParticle.m_Lacunarity = p.value(
                "lacunarity", config.m_WaveParticle.m_Lacunarity);
            config.m_WaveParticle.m_Persistence = p.value(
                "persistence", config.m_WaveParticle.m_Persistence);
            config.m_WaveParticle.m_RadiusFalloff = p.value(
                "radiusFalloff", config.m_WaveParticle.m_RadiusFalloff);
            config.m_WaveParticle.m_ProfileSharpness = p.value(
                "profileSharpness", config.m_WaveParticle.m_ProfileSharpness);
            config.m_WaveParticle.m_FoamThreshold = p.value(
                "foamThreshold", config.m_WaveParticle.m_FoamThreshold);
            config.m_WaveParticle.m_FoamSoftness = p.value(
                "foamSoftness", config.m_WaveParticle.m_FoamSoftness);
            config.m_WaveParticle.m_Lifetime = p.value(
                "lifetime", config.m_WaveParticle.m_Lifetime);
            config.m_WaveParticle.m_RandomSeed = p.value(
                "randomSeed", config.m_WaveParticle.m_RandomSeed);
        }
    }

    if (waterData.contains("flowMap") && waterData["flowMap"].is_object())
    {
        auto& f = waterData["flowMap"];
        config.m_FlowMap.m_Enabled = f.value("enabled", config.m_FlowMap.m_Enabled);
        config.m_FlowMap.m_Strength = f.value("strength", config.m_FlowMap.m_Strength);
        config.m_FlowMap.m_Speed = f.value("speed", config.m_FlowMap.m_Speed);
        config.m_FlowMap.m_PhaseLength = f.value("phaseLength", config.m_FlowMap.m_PhaseLength);
        config.m_FlowMap.m_NoiseAmount = f.value("noiseAmount", config.m_FlowMap.m_NoiseAmount);
        config.m_FlowMap.m_WorldOrigin = ReadVec2Field(
            f, "worldOrigin", config.m_FlowMap.m_WorldOrigin);
        config.m_FlowMap.m_WorldSize = ReadVec2Field(
            f, "worldSize", config.m_FlowMap.m_WorldSize);
        config.m_FlowMap.m_FallbackDirection = ReadVec2Field(
            f, "fallbackDirection", config.m_FlowMap.m_FallbackDirection);
    }

    // ── caustics 块 ────────────────────────────────────────────────────────
    if (waterData.contains("caustics") && waterData["caustics"].is_object())
    {
        auto& c = waterData["caustics"];
        config.m_Caustics.m_Enabled   = c.value("enabled", config.m_Caustics.m_Enabled);
        config.m_Caustics.m_Intensity = c.value("intensity", 1.0f);
        config.m_Caustics.m_Scale     = c.value("scale",     0.5f);
    }

    // ── refraction 块 ─────────────────────────────────────────────────────
    if (waterData.contains("refraction") && waterData["refraction"].is_object())
    {
        auto& r = waterData["refraction"];
        config.m_Refraction.m_Enabled     = r.value("enabled",     true);
        config.m_Refraction.m_DistortionStrength = r.value(
            "distortionStrength", config.m_Refraction.m_DistortionStrength);
    }

    // ── ssr 块 ────────────────────────────────────────────────────────────
    if (waterData.contains("ssr") && waterData["ssr"].is_object())
    {
        auto& s = waterData["ssr"];
        config.m_SSR.m_Enabled      = s.value("enabled",      true);
        config.m_SSR.m_MaxDistance   = s.value("maxDistance",  500.0f);
        config.m_SSR.m_MaxRoughness  = s.value("maxRoughness", 0.3f);
    }

    // ── sss 块（W-16: 次表面散射）─────────────────────────────────────────
    if (waterData.contains("sss") && waterData["sss"].is_object())
    {
        auto& s = waterData["sss"];
        config.m_SSS.m_Enabled                   = s.value("enabled",        true);
        config.m_SSS.m_MaxThicknessDistance       = s.value("maxThickness",  15.0f);
        config.m_SSS.m_DeepWaterThicknessFallback = s.value("deepFallback",  0.8f);
    }

    if (waterData.contains("geometry") && waterData["geometry"].is_object())
    {
        auto& l = waterData["geometry"];
        config.m_Geometry.m_LodCount = l.value("lodCount", config.m_Geometry.m_LodCount);
        config.m_Geometry.m_BasePatchSize = l.value("basePatchSize", config.m_Geometry.m_BasePatchSize);
        config.m_Geometry.m_MeshDim = l.value("meshDim", config.m_Geometry.m_MeshDim);
        config.m_Geometry.m_MorphStartRatio = l.value("morphStartRatio", config.m_Geometry.m_MorphStartRatio);
    }

    config.Validate();
    // ── 创建只持有单一 V2 配置的 WaterMaterial ─────────────────────────────
    VansWaterMaterial* mat = new VansWaterMaterial();
    mat->m_MaterialType = VansMaterialType::VAN_WATER;
    mat->m_Config       = config;

    // ── 注册到场景 ─────────────────────────────────────────────────────────
    mat->SetName(waterData.value("name", "WaterMaterial"));
    scene.AddMaterialAsset(mat);

    // 记录完整配置供 VansWaterSystem 初始化时读取
    scene.SetWaterRuntimeConfig(config, mat);

    // ── 创建 VansWaterRenderNode，使用引擎内置 "plane" 网格作为水面几何体 ──
    {
        // "plane" is an engine runtime binding for the unit plane mesh.
        VansMesh* planeMesh = static_cast<VansMesh*>(scene.FindMeshAsset("plane"));
        if (planeMesh == nullptr)
        {
            VANS_LOG_WARN("[AddWaterNode] 网格 'plane' 未找到，水面渲染节点将不可见。");
        }
        else
        {
            VansWaterRenderNode* waterNode = new VansWaterRenderNode(device, WATER_NODE);
            waterNode->m_Mesh     = planeMesh;
            waterNode->m_Material = mat;

            // 水面铺满整个地形范围（与 terrain.terrainSize 一致，使用 config.m_WaterLevel 为 Y 高度）
            const float terrainHalfSize = 512.0f; // 默认 1024×1024 地形的半径
            waterNode->SetTransformData(
                glm::vec3(0.0f, config.m_WaterLevel, 0.0f),  // 位置（Y = water level）
                glm::vec3(-90.0f, 0.0f, 0.0f),               // 旋转（plane 默认朝 Z，绕 X 旋转 -90° 使其水平）
                glm::vec3(terrainHalfSize, terrainHalfSize, 1.0f) // 缩放铺满地形
            );

            const std::string nodeName = waterData.value("name", "WaterNode");
            waterNode->SetName(nodeName);
            scene.RegistRenderNode(waterNode, WATER_NODE);
        }
    }

    VANS_LOG("[AddWaterNode] Water V2 loaded: level=" << config.m_WaterLevel
        << " geometryLod=" << config.m_Geometry.m_LodCount
        << " spectrumCascades=" << config.m_Spectrum.m_CascadeCount
        << " ssr=" << (config.m_SSR.m_Enabled ? "on" : "off"));

    // ── 创建 VansWaterSystem（设计文档 §12.1）────────────────────────────────
    // VansWaterSystem 管理 Water GBuffer 纹理、波形仿真、Pre-Water Compute 和 Composite pass。
    // 通过 m_Scene->GetWaterSystem() 供 VansVKRenderer 在渲染循环中调度。
    {
        VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
        if (vkDevice)
        {
            VansWaterSystem* waterSystem = new VansWaterSystem();
            waterSystem->SetWaterLevel(config.m_WaterLevel);
            waterSystem->SetWaterMaterial(mat);
            waterSystem->Initialize(vkDevice,
                static_cast<uint32_t>(vkDevice->GetRenderWidth()),
                static_cast<uint32_t>(vkDevice->GetRenderHeight()));

            // SetupDescriptors：绑定 WaterGBuf 纹理到合成集（在 SetupVansWaterGBufferPass 之后调用）
            auto* rp = VansRenderPassManager::GetInstance();
            waterSystem->SetupDescriptors(
                rp,
                scene.GetGlobalDescriptorSetLayout(),
                scene.GetGlobalDescriptorSet());

            scene.SetWaterSystem(waterSystem);
        }
        else
        {
            VANS_LOG_WARN("[AddWaterNode] 无法获取 VansVKDevice，VansWaterSystem 未初始化。");
        }
    }
}

void VansSceneEnvironmentNodeBuilder::AddVegetationNode(VansScene& scene, VkDevice& device, json& vegetationData, const std::string& projectRoot)
{
    json resolvedVegetationData = LoadVegetationConfigFromReference(vegetationData, projectRoot);
    if (!resolvedVegetationData.is_object())
    {
        VANS_LOG_ERROR("[SceneLoader] Vegetation config must be an object or config path.");
        return;
    }
    vegetationData = resolvedVegetationData;

    // Read optional parameters from JSON
    uint32_t instanceCount = vegetationData.value("instanceCount", 2000000u);
    uint32_t boneCount     = vegetationData.value("boneCount", 6u);
    float    bladeHeight   = vegetationData.value("bladeHeight", 0.5f);
    float    windDirX      = vegetationData.value("windDirX", 1.0f);
    float    windDirZ      = vegetationData.value("windDirZ", 0.0f);
    float    leanDeviation = vegetationData.value("leanDeviation", 35.0f);  // degrees
    std::string materialName = vegetationData.value("material", "grassMaterial");
    std::string name         = vegetationData.value("name", "VegetationNode");

    // Read per-frame simulation parameters (all configurable from JSON)
    uint32_t subBladeCount           = vegetationData.value("subBladeCount",          10u);
    float    subBladeScatterRadiusMin = vegetationData.value("subBladeScatterRadiusMin", 0.15f);
    float    subBladeScatterRadiusMax = vegetationData.value("subBladeScatterRadiusMax", 0.45f);
    float windStrength  = vegetationData.value("windStrength",  4.0f);   // overall wind force
    float windFrequency = vegetationData.value("windFrequency", 0.5f);   // spatial noise frequency
    float windSpeed     = vegetationData.value("windSpeed",     1.5f);   // noise scroll rate (animation speed)
    float windBendMult  = vegetationData.value("windBendMult",  5.0f);   // bend amplification
    float stiffness     = vegetationData.value("stiffness",    15.0f);
    float damping       = vegetationData.value("damping",       0.92f);
    float softness      = vegetationData.value("softness",      0.2f);
    float lodFullDist   = vegetationData.value("lodFullDist",  15.0f);
    float lodFadeDist   = vegetationData.value("lodFadeDist",  20.0f);

    glm::vec2 placementMinXZ(-100.0f, -100.0f);
    glm::vec2 placementMaxXZ( 100.0f,  100.0f);
    float grassScaleMin = vegetationData.value("grassScaleMin", 0.4f);
    float grassScaleMax = vegetationData.value("grassScaleMax", 1.5f);
    auto readVec2FromJson = [](const json& value, const glm::vec2& fallback) -> glm::vec2
    {
        if (value.is_array() && value.size() >= 2)
            return glm::vec2(value[0].get<float>(), value[1].get<float>());
        return fallback;
    };
    if (vegetationData.contains("placement") && vegetationData["placement"].is_object())
    {
        const json& placementJson = vegetationData["placement"];
        placementMinXZ = readVec2FromJson(placementJson.value("boundsMin", json::array()), placementMinXZ);
        placementMaxXZ = readVec2FromJson(placementJson.value("boundsMax", json::array()), placementMaxXZ);
        grassScaleMin = placementJson.value("grassScaleMin", grassScaleMin);
        grassScaleMax = placementJson.value("grassScaleMax", grassScaleMax);
    }

    // Create the vegetation system
    VansVegetationSystem* vegetationSystem = new VansVegetationSystem();
    scene.SetVegetationSystem(vegetationSystem);
    vegetationSystem->SetBladeHeight(bladeHeight);   // must be set before Init()
    vegetationSystem->SetInitWindDirection(glm::vec2(windDirX, windDirZ), leanDeviation);
    vegetationSystem->SetSubBladeParams(subBladeCount, subBladeScatterRadiusMin, subBladeScatterRadiusMax);  // must be set before Init()
    vegetationSystem->SetPlacementBounds(placementMinXZ, placementMaxXZ);
    vegetationSystem->SetGrassScaleRange(grassScaleMin, grassScaleMax);

    if (vegetationData.contains("trees") && vegetationData["trees"].is_object())
    {
        const json& treesJson = vegetationData["trees"];
        TreeVegetationConfig treeConfig;
        treeConfig.enabled = treesJson.value("enabled", false);
        treeConfig.cullDistance = treesJson.value("cullDistance", 800.0f);
        treeConfig.cullEnabled = treesJson.value("cullEnabled", true);
        treeConfig.hizEnabled = treesJson.value("hizEnabled", true);

        auto parsePartType = [](const std::string& type) -> TreePartType {
            if (type == "trunk") return TreePartType::Trunk;
            if (type == "leaves" || type == "leaf") return TreePartType::Leaves;
            return TreePartType::Custom;
        };
        auto readSubmeshIndex = [](const json& value) -> int32_t {
            if (value.contains("submeshIndex") && value["submeshIndex"].is_number_integer())
                return value["submeshIndex"].get<int32_t>();
            if (value.contains("submesh") && value["submesh"].is_number_integer())
                return value["submesh"].get<int32_t>();
            if (value.contains("submesh") && value["submesh"].is_object() &&
                value["submesh"].contains("index") && value["submesh"]["index"].is_number_integer())
            {
                return value["submesh"]["index"].get<int32_t>();
            }
            return -1;
        };

        if (treesJson.contains("species") && treesJson["species"].is_array())
        {
            for (const auto& spJson : treesJson["species"])
            {
                TreeSpeciesConfig species;
                species.name = spJson.value("name", std::string("TreeSpecies"));
                species.boundsRadius = spJson.value("boundsRadius", 1.0f);
                if (spJson.contains("parts") && spJson["parts"].is_array())
                {
                    for (const auto& partJson : spJson["parts"])
                    {
                        TreePartConfig part;
                        part.type = parsePartType(partJson.value("type", std::string("custom")));
                        part.meshName = partJson.value("mesh", std::string(""));
                        part.materialName = partJson.value("material", std::string(""));
                        part.submeshIndex = readSubmeshIndex(partJson);
                        species.parts.push_back(part);
                    }
                }
                treeConfig.species.push_back(species);
            }
        }

        if (treesJson.contains("instances") && treesJson["instances"].is_array())
        {
            for (const auto& instJson : treesJson["instances"])
            {
                TreeInstanceConfig inst;
                inst.speciesName = instJson.value("species", treeConfig.species.empty() ? std::string("") : treeConfig.species[0].name);
                if (instJson.contains("position") && instJson["position"].is_array() && instJson["position"].size() >= 3)
                {
                    inst.position = glm::vec3(
                        instJson["position"][0].get<float>(),
                        instJson["position"][1].get<float>(),
                        instJson["position"][2].get<float>());
                }
                inst.yawDeg = instJson.value("yaw", 0.0f);
                inst.scale = instJson.value("scale", 1.0f);
                inst.submeshIndex = readSubmeshIndex(instJson);
                treeConfig.instances.push_back(inst);
            }
        }
        else if (treesJson.contains("randomInstances") && treesJson["randomInstances"].is_object() && !treeConfig.species.empty())
        {
            const json& randomJson = treesJson["randomInstances"];
            uint32_t treeCount = randomJson.value("count", 80u);
            uint32_t seed = randomJson.value("seed", 1337u);
            float scaleMin = randomJson.value("scaleMin", 0.9f);
            float scaleMax = randomJson.value("scaleMax", 1.15f);
            glm::vec2 treeMinXZ = readVec2FromJson(randomJson.value("boundsMin", json::array()), placementMinXZ);
            glm::vec2 treeMaxXZ = readVec2FromJson(randomJson.value("boundsMax", json::array()), placementMaxXZ);
            const glm::vec2 normalizedMinXZ = glm::min(treeMinXZ, treeMaxXZ);
            const glm::vec2 normalizedMaxXZ = glm::max(treeMinXZ, treeMaxXZ);
            treeMinXZ = normalizedMinXZ;
            treeMaxXZ = normalizedMaxXZ;
            std::mt19937 rng(seed);
            std::uniform_real_distribution<float> xDist(treeMinXZ.x, treeMaxXZ.x);
            std::uniform_real_distribution<float> zDist(treeMinXZ.y, treeMaxXZ.y);
            std::uniform_real_distribution<float> yawDist(0.0f, 360.0f);
            std::uniform_real_distribution<float> scaleDist(std::min(scaleMin, scaleMax), std::max(scaleMin, scaleMax));

            for (uint32_t i = 0; i < treeCount; ++i)
            {
                TreeInstanceConfig inst;
                inst.speciesName = randomJson.value("species", treeConfig.species[0].name);
                inst.position = glm::vec3(xDist(rng), 0.0f, zDist(rng));
                inst.yawDeg = yawDist(rng);
                inst.scale = scaleDist(rng);
                inst.submeshIndex = readSubmeshIndex(randomJson);
                treeConfig.instances.push_back(inst);
            }
        }
        else if (!treeConfig.species.empty())
        {
            uint32_t treeCount = treesJson.value("count", 10u);
            float radius = treesJson.value("placementRadius", 12.0f);
            glm::vec3 center(0.0f);
            if (treesJson.contains("center") && treesJson["center"].is_array() && treesJson["center"].size() >= 3)
            {
                center = glm::vec3(
                    treesJson["center"][0].get<float>(),
                    treesJson["center"][1].get<float>(),
                    treesJson["center"][2].get<float>());
            }
            for (uint32_t i = 0; i < treeCount; ++i)
            {
                float angle = (static_cast<float>(i) / std::max(1u, treeCount)) * 6.28318530718f;
                float ring = radius * (0.55f + 0.45f * (static_cast<float>((i * 37u) % 100u) / 100.0f));
                TreeInstanceConfig inst;
                inst.speciesName = treeConfig.species[0].name;
                inst.position = center + glm::vec3(cosf(angle) * ring, 0.0f, sinf(angle) * ring);
                inst.yawDeg = angle * 57.2957795f;
                inst.scale = 1.0f;
                treeConfig.instances.push_back(inst);
            }
        }

        vegetationSystem->SetTreeConfig(treeConfig);
    }

    // ── Parse render configs (multi-mesh/material support) ─────────────────
    if (vegetationData.contains("renderConfigs") && vegetationData["renderConfigs"].is_array())
    {
        std::vector<GrassRenderConfig> configs;
        for (auto& rc : vegetationData["renderConfigs"])
        {
            GrassRenderConfig cfg;
            cfg.meshName     = rc.value("mesh", std::string(""));
            cfg.materialName = rc.value("material", materialName);
            cfg.percent      = rc.value("percent", 1.0f);
            configs.push_back(cfg);
        }
        vegetationSystem->SetRenderConfigs(configs);
    }
    else
    {
        // Backward compatible: single material, procedural blade
        GrassRenderConfig defaultCfg;
        defaultCfg.meshName     = "";
        defaultCfg.materialName = materialName;
        defaultCfg.percent      = 1.0f;
        vegetationSystem->SetRenderConfigs({ defaultCfg });
    }

    vegetationSystem->Init(device, instanceCount, boneCount);

    // Apply runtime simulation parameters loaded from JSON
    vegetationSystem->SetSimParams(
        glm::vec2(windDirX, windDirZ),
        windStrength, windFrequency, windSpeed, windBendMult,
        stiffness, damping, softness,
        lodFullDist, lodFadeDist);

    // ── Connect terrain heightmap for ground placement ──────────────────────
    if (scene.GetTerrainRenderNode() != nullptr)
    {
        VansTerrainRenderNode* terrainNode = dynamic_cast<VansTerrainRenderNode*>(scene.GetTerrainRenderNode());
        if (terrainNode && terrainNode->GetTerrain())
        {
            VansTerrain* terrain = terrainNode->GetTerrain();
            VansTexture* heightMap = terrain->GetHeightMap();
            if (heightMap)
            {
                // 读取植被覆盖参数；未配置时复用 terrain 运行时参数，避免高度不一致
                float terrainMaxHeight = vegetationData.value("terrainMaxHeight", terrain->GetMaxHeight());
                float terrainHeightOffset = vegetationData.value("terrainHeightOffset", terrain->GetHeightOffset());

                vegetationSystem->SetTerrainHeightmap(
                    heightMap->GetImage().GetImageView(),
                    heightMap->GetImage().GetSampler(),
                    terrain->GetTerrainSize(),
                    terrainMaxHeight,
                    terrainHeightOffset);
            }
        }
    }

    // ── 连接 Hi-Z depth pyramid 进行保守遮挡剔除 ──────────────────────────
    {
        VansMaterialManager* materialManager = scene.GetMaterialManager();
        VansTexture* hzbTexture = materialManager->GetRuntimeRenderTexture(
            VansMaterialManager::RT_HZB_RESULT);
        if (hzbTexture != nullptr)
        {
            float hizSampleBias = vegetationData.value("hizSampleBias", 0.2f);
            vegetationSystem->SetHiZDepth(
                hzbTexture->GetImage().GetImageView(),
                hzbTexture->GetImage().GetSampler(),
                static_cast<uint32_t>(materialManager->m_HIZMipCount),
                hizSampleBias);
        }
        else
        {
            VANS_LOG_WARN("[SceneLoader] HZB texture not available — Hi-Z vegetation cull disabled.");
        }
    }

    // ── Build per-config GPU resources (allocates bone weights, remap, indirect draw) ──
    auto meshLookup = [&scene](const std::string& name) -> VansMesh* {
        return static_cast<VansMesh*>(scene.FindMeshAsset(name));
    };
    auto materialLookup = [&scene](const std::string& name) -> VansMaterial* {
        return static_cast<VansMaterial*>(scene.FindMaterialAsset(name));
    };
    vegetationSystem->BuildRenderConfigs(meshLookup, materialLookup);
    vegetationSystem->BuildTreeResources(meshLookup, materialLookup);

    // Create the vegetation render node
    RenderNodeType type = RenderNodeType::VEGETATION_NODE;
    VansVegetationRenderNode* renderNode = new VansVegetationRenderNode(device, type);
    renderNode->SetVegetationSystem(vegetationSystem);

    // Assign the default grass material (kept for backward compat / fallback)
    VansMaterial* material = static_cast<VansMaterial*>(scene.FindMaterialAsset(materialName));
    if (material)
        renderNode->m_Material = material;
    renderNode->m_Mesh = nullptr;  // No mesh asset — geometry is procedural / per-config
    renderNode->SetName(name);

    scene.RegistRenderNode(renderNode, type);
    VANS_LOG("[AddVegetationNode] Vegetation node '" << name << "' created with " << instanceCount << " instances.");
}
}
