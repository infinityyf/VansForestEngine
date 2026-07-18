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
    // ── 类型解析 ───────────────────────────────────────────────────────────
    VansWaterConfig config;

    const std::string typeStr = waterData.value("type", "ocean");
    if      (typeStr == "ocean")  config.m_Type = VansWaterType::Ocean;
    else if (typeStr == "lake")   config.m_Type = VansWaterType::Lake;
    else if (typeStr == "river")  config.m_Type = VansWaterType::River;
    else if (typeStr == "pool")   config.m_Type = VansWaterType::Pool;
    else
        VANS_LOG_WARN("[AddWaterNode] Unknown water type '" << typeStr << "', defaulting to ocean.");

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

    // ── waves 块 ───────────────────────────────────────────────────────────
    if (waterData.contains("waves") && waterData["waves"].is_object())
    {
        auto& w = waterData["waves"];

        const std::string modeStr = w.value("mode", "gerstner");
        if      (modeStr == "fft")     config.m_Waves.m_Mode = VansWaveMode::FFT;
        else if (modeStr == "hybrid")  config.m_Waves.m_Mode = VansWaveMode::Hybrid;
        else                           config.m_Waves.m_Mode = VansWaveMode::Gerstner;

        config.m_Waves.m_BaseScale        = w.value("baseScale",         config.m_Waves.m_BaseScale);
        config.m_Waves.m_MaxLOD           = w.value("maxLOD",            config.m_Waves.m_MaxLOD);
        config.m_Waves.m_WindSpeed        = w.value("windSpeed",         config.m_Waves.m_WindSpeed);
        config.m_Waves.m_SwellAmplitude   = w.value("swellAmplitude",    config.m_Waves.m_SwellAmplitude);
        config.m_Waves.m_ChopScale        = w.value("chopScale",         config.m_Waves.m_ChopScale);
        config.m_Waves.m_GerstnerWaveCount = w.value("gerstnerWaveCount", config.m_Waves.m_GerstnerWaveCount);
        config.m_Waves.m_FftLODCount      = w.value("fftLODCount",       config.m_Waves.m_FftLODCount);
        config.m_Waves.m_FftResolution    = w.value("fftResolution",     config.m_Waves.m_FftResolution);
        config.m_Waves.m_FFT.m_LODCount   = config.m_Waves.m_FftLODCount;
        config.m_Waves.m_FFT.m_Resolution = config.m_Waves.m_FftResolution;

        if (w.contains("windDirection"))
        {
            auto& d = w["windDirection"];
            if (d.is_array() && d.size() >= 2)
                config.m_Waves.m_WindDirection = {d[0], d[1]};
            else if (d.is_object())
                config.m_Waves.m_WindDirection = {
                    d.value("x", 0.7071f), d.value("y", 0.7071f)};
        }

        if (w.contains("fft") && w["fft"].is_object())
        {
            auto& fft = w["fft"];
            config.m_Waves.m_FFT.m_UseDerivativeNormal = fft.value("useDerivativeNormal", config.m_Waves.m_FFT.m_UseDerivativeNormal);
            config.m_Waves.m_FFT.m_Resolution          = fft.value("resolution",          config.m_Waves.m_FFT.m_Resolution);
            config.m_Waves.m_FFT.m_LODCount            = fft.value("lodCount",            config.m_Waves.m_FFT.m_LODCount);
            config.m_Waves.m_FFT.m_SpectrumAmplitude   = fft.value("spectrumAmplitude",   config.m_Waves.m_FFT.m_SpectrumAmplitude);
            config.m_Waves.m_FFT.m_Choppiness          = fft.value("choppiness",          config.m_Waves.m_FFT.m_Choppiness);
            config.m_Waves.m_FFT.m_SmallWaveDamping    = fft.value("smallWaveDamping",    config.m_Waves.m_FFT.m_SmallWaveDamping);
            config.m_Waves.m_FFT.m_WindDependency      = fft.value("windDependency",      config.m_Waves.m_FFT.m_WindDependency);
            config.m_Waves.m_FFT.m_Depth               = fft.value("depth",               config.m_Waves.m_FFT.m_Depth);
            config.m_Waves.m_FFT.m_RepeatPeriod        = fft.value("repeatPeriod",        config.m_Waves.m_FFT.m_RepeatPeriod);
            config.m_Waves.m_FFT.m_FoamSlopeScale      = fft.value("foamSlopeScale",      config.m_Waves.m_FFT.m_FoamSlopeScale);
            config.m_Waves.m_FFT.m_FoamFoldScale       = fft.value("foamFoldScale",       config.m_Waves.m_FFT.m_FoamFoldScale);
            config.m_Waves.m_FFT.m_FoamFoldThreshold   = fft.value("foamFoldThreshold",   config.m_Waves.m_FFT.m_FoamFoldThreshold);
            config.m_Waves.m_FFT.m_RandomSeed          = fft.value("randomSeed",          config.m_Waves.m_FFT.m_RandomSeed);
            config.m_Waves.m_FftLODCount               = config.m_Waves.m_FFT.m_LODCount;
            config.m_Waves.m_FftResolution             = config.m_Waves.m_FFT.m_Resolution;
        }

        // N-01: detailNormal 子块
        if (w.contains("detailNormal") && w["detailNormal"].is_object())
        {
            auto& dn = w["detailNormal"];
            config.m_Waves.m_DetailNormal.m_Enabled         = dn.value("enabled",    true);
            config.m_Waves.m_DetailNormal.m_Intensity       = dn.value("intensity",  1.0f);
            config.m_Waves.m_DetailNormal.m_Scale           = dn.value("scale",      1.0f);
            config.m_Waves.m_DetailNormal.m_OctaveCount     = dn.value("octaves",    4);
            config.m_Waves.m_DetailNormal.m_TimeOffset      = dn.value("timeOffset", 0.0f);
            config.m_Waves.m_DetailNormal.m_DetailBaseScale = dn.value("baseScale",  32.0f);
        }

        // maxLOD 范围校验
        if (config.m_Waves.m_MaxLOD < 1 || config.m_Waves.m_MaxLOD > 10)
        {
            VANS_LOG_WARN("[AddWaterNode] waves.maxLOD = " << config.m_Waves.m_MaxLOD
                << " 越界（合法范围 1-10），截断至合法值。");
            config.m_Waves.m_MaxLOD = glm::clamp(config.m_Waves.m_MaxLOD, 1, 10);
        }
    }

    // ── foam 块 ────────────────────────────────────────────────────────────
    if (waterData.contains("foam") && waterData["foam"].is_object())
    {
        auto& f = waterData["foam"];
        config.m_Foam.m_Enabled     = f.value("enabled",   true);
        config.m_Foam.m_TextureName = f.value("texture",   std::string{});
        config.m_Foam.m_Intensity   = f.value("intensity", 1.0f);
    }

    // ── normalMap 块 ───────────────────────────────────────────────────────
    if (waterData.contains("normalMap") && waterData["normalMap"].is_object())
    {
        auto& n = waterData["normalMap"];
        config.m_NormalMap.m_TextureName = n.value("texture", std::string{});
        if (n.contains("tiling"))
        {
            auto& t = n["tiling"];
            if (t.is_array() && t.size() >= 2)
                config.m_NormalMap.m_Tiling = {t[0], t[1]};
        }
    }

    // ── caustics 块 ────────────────────────────────────────────────────────
    if (waterData.contains("caustics") && waterData["caustics"].is_object())
    {
        auto& c = waterData["caustics"];
        config.m_Caustics.m_Enabled   = c.value("enabled",   true);
        config.m_Caustics.m_Intensity = c.value("intensity", 1.0f);
        config.m_Caustics.m_Scale     = c.value("scale",     0.5f);
    }

    // ── refraction 块 ─────────────────────────────────────────────────────
    if (waterData.contains("refraction") && waterData["refraction"].is_object())
    {
        auto& r = waterData["refraction"];
        config.m_Refraction.m_Enabled     = r.value("enabled",     true);
        config.m_Refraction.m_MaxDistance = r.value("maxDistance", 50.0f);
        config.m_Refraction.m_Scale       = r.value("scale",       0.5f);
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

    // ── lod 块（W-07）─────────────────────────────────────────────────────
    if (waterData.contains("lod") && waterData["lod"].is_object())
    {
        auto& l = waterData["lod"];
        config.m_LOD.m_MaxLOD          = l.value("levels",          config.m_LOD.m_MaxLOD);
        config.m_LOD.m_BasePatchSize   = l.value("basePatchSize",   config.m_LOD.m_BasePatchSize);
        config.m_LOD.m_MeshDim         = l.value("meshDim",         config.m_LOD.m_MeshDim);
        config.m_LOD.m_DetailBalance   = l.value("detailBalance",   config.m_LOD.m_DetailBalance);
        config.m_LOD.m_MorphWidthRatio = l.value("morphWidthRatio", config.m_LOD.m_MorphWidthRatio);
        if (!waterData.contains("waves") || !waterData["waves"].contains("baseScale"))
            config.m_Waves.m_BaseScale = l.value("minDistance", config.m_Waves.m_BaseScale);

        config.m_LOD.m_MaxLOD = glm::clamp(config.m_LOD.m_MaxLOD, 1, 10);
        if (config.m_LOD.m_MeshDim < 3)
            config.m_LOD.m_MeshDim = 65;
        if (((config.m_LOD.m_MeshDim - 1) % 2) != 0)
            ++config.m_LOD.m_MeshDim;

        VANS_LOG("[AddWaterNode] lod block: levels=" << config.m_LOD.m_MaxLOD
            << " basePatchSize=" << config.m_LOD.m_BasePatchSize
            << " meshDim=" << config.m_LOD.m_MeshDim);
    }

    // ── debug 块（W-07）───────────────────────────────────────────────────
    if (waterData.contains("debug") && waterData["debug"].is_object())
    {
        auto& d = waterData["debug"];
        bool showWire   = d.value("showLODWireframe", false);
        bool freezeLOD  = d.value("freezeLOD",        false);
        bool visMorph   = d.value("visualizeMorph",   false);
        VANS_LOG("[AddWaterNode] debug: wireframe=" << showWire
            << " freezeLOD=" << freezeLOD << " visualizeMorph=" << visMorph);
        // Debug 标志存储在 VansWaterConfig 中（可后续添加 m_Debug 子结构）
    }

    // ── 创建 VansWaterMaterial 并展开所有字段 ──────────────────────────────
    VansWaterMaterial* mat = new VansWaterMaterial();
    mat->m_MaterialType = VansMaterialType::VAN_WATER;
    mat->m_Config       = config;

    // 参与介质参数
    mat->m_AbsorptionCoeffs  = config.m_Medium.m_AbsorptionCoeff;
    mat->m_ScatteringCoeffs  = config.m_Medium.m_ScatteringCoeff;
    mat->m_WaterIOR          = config.m_Medium.m_IOR;
    mat->m_FresnelPower      = config.m_Medium.m_FresnelPower;
    mat->m_Anisotropy        = config.m_Medium.m_Anisotropy;
    mat->m_WaterRoughness    = config.m_Medium.m_WaterRoughness;
    mat->m_SpecularIntensity = config.m_SpecularIntensity;
    mat->m_DeepWaterColor    = config.m_Medium.m_DeepColor;
    mat->m_ShallowWaterColor = config.m_Medium.m_ShallowColor;

    // 波形参数
    mat->m_OceanBaseScale      = config.m_Waves.m_BaseScale;
    mat->m_MaxLODCount         = config.m_LOD.m_MaxLOD;
    mat->m_LODBasePatchSize    = config.m_LOD.m_BasePatchSize;
    mat->m_LODMeshDim          = config.m_LOD.m_MeshDim;
    mat->m_LODDetailBalance    = config.m_LOD.m_DetailBalance;
    mat->m_LODMorphWidthRatio  = config.m_LOD.m_MorphWidthRatio;
    mat->m_GerstnerWaveCount   = config.m_Waves.m_GerstnerWaveCount;
    mat->m_FftLODCount         = config.m_Waves.m_FftLODCount;
    mat->m_FftResolution       = config.m_Waves.m_FftResolution;
    mat->m_FFTUseDerivativeNormal = config.m_Waves.m_FFT.m_UseDerivativeNormal;
    mat->m_FFTSpectrumAmplitude = config.m_Waves.m_FFT.m_SpectrumAmplitude;
    mat->m_FFTChoppiness       = config.m_Waves.m_FFT.m_Choppiness;
    mat->m_FFTSmallWaveDamping = config.m_Waves.m_FFT.m_SmallWaveDamping;
    mat->m_FFTWindDependency   = config.m_Waves.m_FFT.m_WindDependency;
    mat->m_FFTDepth            = config.m_Waves.m_FFT.m_Depth;
    mat->m_FFTRepeatPeriod     = config.m_Waves.m_FFT.m_RepeatPeriod;
    mat->m_FFTFoamSlopeScale   = config.m_Waves.m_FFT.m_FoamSlopeScale;
    mat->m_FFTFoamFoldScale    = config.m_Waves.m_FFT.m_FoamFoldScale;
    mat->m_FFTFoamFoldThreshold = config.m_Waves.m_FFT.m_FoamFoldThreshold;
    mat->m_FFTRandomSeed       = config.m_Waves.m_FFT.m_RandomSeed;
    mat->m_WindSpeed           = config.m_Waves.m_WindSpeed;
    mat->m_SwellAmplitude      = config.m_Waves.m_SwellAmplitude;
    mat->m_ChopScale           = config.m_Waves.m_ChopScale;
    mat->m_WindDirection       = config.m_Waves.m_WindDirection;

    // 泡沫
    mat->m_EnableFoam    = config.m_Foam.m_Enabled;
    mat->m_FoamIntensity = config.m_Foam.m_Intensity;

    // 法线贴图平铺
    mat->m_NormalMapTiling = config.m_NormalMap.m_Tiling;

    // 焦散
    mat->m_EnableCaustics    = config.m_Caustics.m_Enabled;
    mat->m_CausticsIntensity = config.m_Caustics.m_Intensity;
    mat->m_CausticsScale     = config.m_Caustics.m_Scale;

    // 折射
    mat->m_EnableRefraction  = config.m_Refraction.m_Enabled;
    mat->m_RefractionMaxDist = config.m_Refraction.m_MaxDistance;
    mat->m_RefractionScale   = config.m_Refraction.m_Scale;

    // SSR
    mat->m_EnableSSR       = config.m_SSR.m_Enabled;
    mat->m_SSRMaxDistance   = config.m_SSR.m_MaxDistance;
    mat->m_SSRMaxRoughness  = config.m_SSR.m_MaxRoughness;

    // SSS（W-16: 次表面散射）
    mat->m_SSSEnabled                = config.m_SSS.m_Enabled;
    mat->m_MaxThicknessDistance      = config.m_SSS.m_MaxThicknessDistance;
    mat->m_DeepWaterThicknessFallback = config.m_SSS.m_DeepWaterThicknessFallback;

    // N-01: 细节法线
    mat->m_DetailNormalEnabled     = config.m_Waves.m_DetailNormal.m_Enabled;
    mat->m_DetailNormalIntensity   = config.m_Waves.m_DetailNormal.m_Intensity;
    mat->m_DetailNormalScale       = config.m_Waves.m_DetailNormal.m_Scale;
    mat->m_DetailNormalOctaves     = config.m_Waves.m_DetailNormal.m_OctaveCount;
    mat->m_DetailNormalTimeOffset  = config.m_Waves.m_DetailNormal.m_TimeOffset;
    mat->m_DetailNormalBaseScale   = config.m_Waves.m_DetailNormal.m_DetailBaseScale;

    // ── 纹理绑定（通过名称查找已加载资产）─────────────────────────────────
    if (!config.m_Foam.m_TextureName.empty())
    {
        mat->m_FoamTexture = static_cast<VansTexture*>(
            scene.GetTextureAsset(config.m_Foam.m_TextureName));
        if (!mat->m_FoamTexture)
            VANS_LOG_WARN("[AddWaterNode] foam texture '" << config.m_Foam.m_TextureName
                << "' not found in the AssetDatabase dependency closure.");
    }

    if (!config.m_NormalMap.m_TextureName.empty())
    {
        mat->m_WaterNormalTexture = static_cast<VansTexture*>(
            scene.GetTextureAsset(config.m_NormalMap.m_TextureName));
        if (!mat->m_WaterNormalTexture)
            VANS_LOG_WARN("[AddWaterNode] normalMap texture '" << config.m_NormalMap.m_TextureName
                << "' not found in the AssetDatabase dependency closure.");
    }

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

    VANS_LOG("[AddWaterNode] 水面配置加载完成: type=" << typeStr
        << " level=" << config.m_WaterLevel
        << " lod=" << config.m_LOD.m_MaxLOD
        << " waveLOD=" << config.m_Waves.m_MaxLOD
        << " foam=" << (config.m_Foam.m_Enabled ? "on" : "off")
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
