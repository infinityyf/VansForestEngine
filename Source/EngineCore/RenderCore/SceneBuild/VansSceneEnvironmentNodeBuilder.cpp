#include "VansSceneEnvironmentNodeBuilder.h"

#include "../VansScene.h"
#include "../VulkanCore/VansMesh.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansRenderPass.h"
#include "../TerrainCore/VansTerrain.h"
#include "../VegetationCore/VansVegetationSystem.h"
#include "../PcgCore/VansPcgMask.h"
#include "../PcgCore/VansPcgSceneConfigAdapter.h"
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
#include <mutex>
#include <random>

namespace VansGraphics
{
namespace
{
glm::vec3 ToVec3(const Vans::VansSceneFloat3& value)
{
    return glm::vec3(value[0], value[1], value[2]);
}

TreePartType ToTreePartType(Vans::VansSceneVegetationTreePartType type)
{
    switch (type)
    {
    case Vans::VansSceneVegetationTreePartType::Trunk: return TreePartType::Trunk;
    case Vans::VansSceneVegetationTreePartType::Leaves: return TreePartType::Leaves;
    case Vans::VansSceneVegetationTreePartType::Custom: return TreePartType::Custom;
    }
    return TreePartType::Custom;
}
}

void VansSceneEnvironmentNodeBuilder::AddTerrainNode(
    VansScene& scene,
    VansVKDevice* device,
    const Vans::VansSceneTerrainNodeConfig& terrainData)
{
    auto vansConfigration = VansConfigration::GetInstance();
    auto& projectMgr = Vans::VansProjectManager::Get();
    std::string projectRoot = projectMgr.IsProjectLoaded()
        ? projectMgr.GetProjectRootPath()
        : vansConfigration->GetProjectRootPath();

    TerrainConfig config;

    // Heightmap (required)
    if (terrainData.heightmap)
        config.heightmapPath = projectRoot + *terrainData.heightmap;
    if (terrainData.terrainSize) config.terrainSize = *terrainData.terrainSize;
    if (terrainData.maxHeight) config.maxHeight = *terrainData.maxHeight;
    if (terrainData.heightOffset) config.heightOffset = *terrainData.heightOffset;
    if (terrainData.splitDistMult) config.splitDistMult = *terrainData.splitDistMult;
    if (terrainData.lodDistanceRatio) config.lodDistanceRatio = *terrainData.lodDistanceRatio;
    if (terrainData.morphStartRatio) config.morphStartRatio = *terrainData.morphStartRatio;
    if (terrainData.maxPatchInstances) config.maxPatchInstances = *terrainData.maxPatchInstances;

    // Tessellation (optional, defaults from TerrainConfig)
    const Vans::VansSceneTerrainTessellationConfig& tess = terrainData.tessellation;
    if (tess.enabled) config.enableTessellation = *tess.enabled;
    if (tess.distance) config.tessellationDistance = *tess.distance;
    if (tess.maxLevel) config.maxTessellationLevel = *tess.maxLevel;
    if (tess.power) config.tessellationPower = *tess.power;
    if (tess.lodBias) config.tessLodBias = *tess.lodBias;
    if (tess.displacementStrength) config.tessDisplacementStrength = *tess.displacementStrength;

    // 程序化噪声细节（替代 displacementStrength）
    const Vans::VansSceneTerrainNoiseDetailConfig& noise = tess.noiseDetail;
    if (noise.enabled) config.enableNoiseDetail = *noise.enabled;
    if (noise.strength) config.noiseStrength = *noise.strength;
    if (noise.frequency) config.noiseFrequency = *noise.frequency;
    if (noise.lacunarity) config.noiseLacunarity = *noise.lacunarity;
    if (noise.gain) config.noiseGain = *noise.gain;
    if (noise.octaves) config.noiseOctaves = *noise.octaves;
    if (noise.warpStrength) config.noiseWarpStrength = *noise.warpStrength;
    if (noise.fadeStart) config.noiseFadeStart = *noise.fadeStart;

    // Splatmaps (required, array of 2)
    if (terrainData.splatmaps.size() >= 1)
        config.splatmap0Path = projectRoot + terrainData.splatmaps[0];
    if (terrainData.splatmaps.size() >= 2)
        config.splatmap1Path = projectRoot + terrainData.splatmaps[1];

    // Layers (up to 8)
    for (const Vans::VansSceneTerrainLayerConfig& layerConfig : terrainData.layers)
    {
        TerrainLayerConfig layer;

        // Support texture name references (look up from scene texture manager)
        if (layerConfig.albedoTexture)
            layer.albedoTex = static_cast<VansTexture*>(scene.GetTextureAsset(*layerConfig.albedoTexture));
        else if (layerConfig.albedoPath)
            layer.albedoPath = projectRoot + *layerConfig.albedoPath;

        if (layerConfig.normalTexture)
            layer.normalTex = static_cast<VansTexture*>(scene.GetTextureAsset(*layerConfig.normalTexture));
        else if (layerConfig.normalPath)
            layer.normalPath = projectRoot + *layerConfig.normalPath;

        if (layerConfig.roughnessTexture)
            layer.roughnessTex = static_cast<VansTexture*>(scene.GetTextureAsset(*layerConfig.roughnessTexture));
        else if (layerConfig.roughnessPath)
            layer.roughnessPath = projectRoot + *layerConfig.roughnessPath;

        if (layerConfig.tiling)
            layer.tiling = *layerConfig.tiling;
        config.layers.push_back(layer);
    }

    RenderNodeType type = RenderNodeType::TERRAIN_NODE;
    VansRenderNode* renderNode = new VansTerrainRenderNode(device, config, type);

    // Read optional name
    std::string name = terrainData.name.value_or("TerrainNode");
    renderNode->SetName(name);
    scene.RegistRenderNode(renderNode, type);

    // Terrain 物理碰撞是可选项，只由 terrain.collision.enabled 控制。
    if (terrainData.collision)
    {
        const Vans::VansSceneTerrainCollisionConfig& collision = *terrainData.collision;
        VansEngine::TerrainPhysicsProperties terrainPhysicsProps;
        terrainPhysicsProps.enabled = collision.enabled.value_or(false);
        terrainPhysicsProps.heightmapPath = config.heightmapPath;
        terrainPhysicsProps.terrainSize = config.terrainSize;
        terrainPhysicsProps.maxHeight = config.maxHeight;
        terrainPhysicsProps.heightOffset = config.heightOffset;

        if (collision.terrainSize) terrainPhysicsProps.terrainSize = *collision.terrainSize;
        if (collision.maxHeight) terrainPhysicsProps.maxHeight = *collision.maxHeight;
        if (collision.heightOffset) terrainPhysicsProps.heightOffset = *collision.heightOffset;
        if (collision.layer) terrainPhysicsProps.layerName = *collision.layer;
        if (collision.flipX) terrainPhysicsProps.flipX = *collision.flipX;
        if (collision.flipZ) terrainPhysicsProps.flipZ = *collision.flipZ;
        if (collision.material.staticFriction)
            terrainPhysicsProps.material.staticFriction = *collision.material.staticFriction;
        if (collision.material.dynamicFriction)
            terrainPhysicsProps.material.dynamicFriction = *collision.material.dynamicFriction;
        if (collision.material.restitution)
            terrainPhysicsProps.material.restitution = *collision.material.restitution;

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

void VansSceneEnvironmentNodeBuilder::AddWaterNode(
    VansScene& scene,
    VkDevice& device,
    const Vans::VansSceneWaterNodeConfig& waterData)
{
    auto toVec2 = [](const Vans::VansSceneFloat2& value) {
        return glm::vec2(value[0], value[1]);
    };
    auto toVec3 = [](const Vans::VansSceneFloat3& value) {
        return glm::vec3(value[0], value[1], value[2]);
    };
    auto toWaveMode = [](Vans::VansSceneWaterWaveMode value) {
        switch (value)
        {
        case Vans::VansSceneWaterWaveMode::Gerstner: return VansWaveMode::Gerstner;
        case Vans::VansSceneWaterWaveMode::FFT: return VansWaveMode::FFT;
        case Vans::VansSceneWaterWaveMode::WaveParticle: return VansWaveMode::WaveParticle;
        }
        return VansWaveMode::WaveParticle;
    };

    VansWaterConfig config;
    if (waterData.level) config.m_WaterLevel = *waterData.level;
    if (waterData.specularIntensity) config.m_SpecularIntensity = *waterData.specularIntensity;

    const Vans::VansSceneWaterMediumConfig& medium = waterData.medium;
    if (medium.absorptionCoeff) config.m_Medium.m_AbsorptionCoeff = toVec3(*medium.absorptionCoeff);
    if (medium.scatteringCoeff) config.m_Medium.m_ScatteringCoeff = toVec3(*medium.scatteringCoeff);
    if (medium.ior) config.m_Medium.m_IOR = *medium.ior;
    if (medium.anisotropy) config.m_Medium.m_Anisotropy = *medium.anisotropy;
    if (medium.waterRoughness) config.m_Medium.m_WaterRoughness = *medium.waterRoughness;

    const Vans::VansSceneWaterSpectrumConfig& spectrum = waterData.spectrum;
    if (spectrum.mode) config.m_Spectrum.m_Mode = toWaveMode(*spectrum.mode);
    if (spectrum.baseCoverage) config.m_Spectrum.m_BaseCoverage = *spectrum.baseCoverage;
    if (spectrum.cascadeScale) config.m_Spectrum.m_CascadeScale = *spectrum.cascadeScale;
    if (spectrum.cascadeCount) config.m_Spectrum.m_CascadeCount = *spectrum.cascadeCount;
    if (spectrum.windSpeed) config.m_Spectrum.m_WindSpeed = *spectrum.windSpeed;
    if (spectrum.swellAmplitude) config.m_Spectrum.m_SwellAmplitude = *spectrum.swellAmplitude;
    if (spectrum.choppiness) config.m_Spectrum.m_Choppiness = *spectrum.choppiness;
    if (spectrum.gerstnerWaveCount) config.m_Spectrum.m_GerstnerWaveCount = *spectrum.gerstnerWaveCount;
    if (spectrum.windDirection) config.m_Spectrum.m_WindDirection = toVec2(*spectrum.windDirection);
    if (spectrum.spectrumAmplitude) config.m_Spectrum.m_SpectrumAmplitude = *spectrum.spectrumAmplitude;
    if (spectrum.minWavelength) config.m_Spectrum.m_MinWavelength = *spectrum.minWavelength;
    if (spectrum.smallWaveDamping) config.m_Spectrum.m_SmallWaveDamping = *spectrum.smallWaveDamping;
    if (spectrum.windDependency) config.m_Spectrum.m_WindDependency = *spectrum.windDependency;
    if (spectrum.depth) config.m_Spectrum.m_Depth = *spectrum.depth;
    if (spectrum.repeatPeriod) config.m_Spectrum.m_RepeatPeriod = *spectrum.repeatPeriod;
    if (spectrum.randomSeed) config.m_Spectrum.m_RandomSeed = *spectrum.randomSeed;

    const Vans::VansSceneWaterWaveParticleConfig& waveParticle = waterData.waveParticle;
    if (waveParticle.particlesPerCascade) config.m_WaveParticle.m_ParticlesPerCascade = *waveParticle.particlesPerCascade;
    if (waveParticle.rmsAmplitude) config.m_WaveParticle.m_RmsAmplitude = *waveParticle.rmsAmplitude;
    if (waveParticle.packetWidth) config.m_WaveParticle.m_PacketWidth = *waveParticle.packetWidth;
    if (waveParticle.dispersionScale) config.m_WaveParticle.m_DispersionScale = *waveParticle.dispersionScale;
    if (waveParticle.directionSpread) config.m_WaveParticle.m_DirectionSpread = *waveParticle.directionSpread;
    if (waveParticle.cascadeAmplitudeFalloff) config.m_WaveParticle.m_CascadeAmplitudeFalloff = *waveParticle.cascadeAmplitudeFalloff;
    if (waveParticle.foamThreshold) config.m_WaveParticle.m_FoamThreshold = *waveParticle.foamThreshold;
    if (waveParticle.foamSoftness) config.m_WaveParticle.m_FoamSoftness = *waveParticle.foamSoftness;
    if (waveParticle.randomSeed) config.m_WaveParticle.m_RandomSeed = *waveParticle.randomSeed;

    const Vans::VansSceneWaterFlowMapConfig& flowMap = waterData.flowMap;
    if (flowMap.enabled) config.m_FlowMap.m_Enabled = *flowMap.enabled;
    if (flowMap.strength) config.m_FlowMap.m_Strength = *flowMap.strength;
    if (flowMap.speed) config.m_FlowMap.m_Speed = *flowMap.speed;
    if (flowMap.phaseLength) config.m_FlowMap.m_PhaseLength = *flowMap.phaseLength;
    if (flowMap.noiseAmount) config.m_FlowMap.m_NoiseAmount = *flowMap.noiseAmount;
    if (flowMap.worldOrigin) config.m_FlowMap.m_WorldOrigin = toVec2(*flowMap.worldOrigin);
    if (flowMap.worldSize) config.m_FlowMap.m_WorldSize = toVec2(*flowMap.worldSize);
    if (flowMap.fallbackDirection) config.m_FlowMap.m_FallbackDirection = toVec2(*flowMap.fallbackDirection);

    const Vans::VansSceneWaterCausticsConfig& caustics = waterData.caustics;
    if (caustics.enabled) config.m_Caustics.m_Enabled = *caustics.enabled;
    if (caustics.intensity) config.m_Caustics.m_Intensity = *caustics.intensity;
    if (caustics.maxDistance) config.m_Caustics.m_MaxDistance = *caustics.maxDistance;
    if (caustics.maxGain) config.m_Caustics.m_MaxGain = *caustics.maxGain;
    if (caustics.filterRadius) config.m_Caustics.m_FilterRadius = *caustics.filterRadius;

    const Vans::VansSceneWaterRefractionConfig& refraction = waterData.refraction;
    if (refraction.enabled) config.m_Refraction.m_Enabled = *refraction.enabled;
    if (refraction.distortionStrength) config.m_Refraction.m_DistortionStrength = *refraction.distortionStrength;

    const Vans::VansSceneWaterSSRConfig& ssr = waterData.ssr;
    if (ssr.enabled) config.m_SSR.m_Enabled = *ssr.enabled;
    if (ssr.maxDistance) config.m_SSR.m_MaxDistance = *ssr.maxDistance;
    if (ssr.maxRoughness) config.m_SSR.m_MaxRoughness = *ssr.maxRoughness;

    const Vans::VansSceneWaterSSSConfig& sss = waterData.sss;
    if (sss.enabled) config.m_SSS.m_Enabled = *sss.enabled;
    if (sss.maxThickness) config.m_SSS.m_MaxThicknessDistance = *sss.maxThickness;
    if (sss.deepFallback) config.m_SSS.m_DeepWaterThicknessFallback = *sss.deepFallback;

    const Vans::VansSceneWaterOpticsConfig& optics = waterData.optics;
    if (optics.maxCrossDistance) config.m_Optics.m_MaxCrossDistance = *optics.maxCrossDistance;
    if (optics.maxRefractionCrossDistance)
        config.m_Optics.m_MaxRefractionCrossDistance = *optics.maxRefractionCrossDistance;
    if (optics.multiScatterScale) config.m_Optics.m_MultiScatterScale = *optics.multiScatterScale;
    if (optics.waterDispersionStrength)
        config.m_Optics.m_WaterDispersionStrength = *optics.waterDispersionStrength;
    if (optics.sssPathScale) config.m_Optics.m_SSSPathScale = *optics.sssPathScale;
    if (optics.sssNonlinearStrength)
        config.m_Optics.m_SSSNonlinearStrength = *optics.sssNonlinearStrength;
    if (optics.sssScatterBoost) config.m_Optics.m_SSSScatterBoost = *optics.sssScatterBoost;
    if (optics.backlitPathScale) config.m_Optics.m_BacklitPathScale = *optics.backlitPathScale;
    if (optics.backlitPhaseG) config.m_Optics.m_BacklitPhaseG = *optics.backlitPhaseG;

    const Vans::VansSceneWaterVolumeConfig& volume = waterData.volume;
    if (volume.resolutionScale) config.m_Volume.m_ResolutionScale = *volume.resolutionScale;
    if (volume.sampleCount) config.m_Volume.m_SampleCount = *volume.sampleCount;
    if (volume.spatialFilterIterations)
        config.m_Volume.m_SpatialFilterIterations = *volume.spatialFilterIterations;
    if (volume.spatialDepthSensitivity)
        config.m_Volume.m_SpatialDepthSensitivity = *volume.spatialDepthSensitivity;

    const Vans::VansSceneWaterGeometryConfig& geometry = waterData.geometry;
    if (geometry.lodCount) config.m_Geometry.m_LodCount = *geometry.lodCount;
    if (geometry.basePatchSize) config.m_Geometry.m_BasePatchSize = *geometry.basePatchSize;
    if (geometry.meshDim) config.m_Geometry.m_MeshDim = *geometry.meshDim;
    if (geometry.morphStartRatio) config.m_Geometry.m_MorphStartRatio = *geometry.morphStartRatio;

    config.Validate();
    // ── 创建只持有单一 V2 配置的 WaterMaterial ─────────────────────────────
    VansWaterMaterial* mat = new VansWaterMaterial();
    mat->m_MaterialType = VansMaterialType::VAN_WATER;
    mat->m_Config       = config;

    // ── 注册到场景 ─────────────────────────────────────────────────────────
    mat->SetName(waterData.name.value_or("WaterMaterial"));
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

            const std::string nodeName = waterData.name.value_or("WaterNode");
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

void VansSceneEnvironmentNodeBuilder::AddVegetationNode(
    VansScene& scene,
    VkDevice& device,
    const Vans::VansSceneVegetationNodeConfig& vegetationData,
    const std::string& projectRoot)
{
    if (!vegetationData.valid)
    {
        return;
    }

    const uint32_t instanceCount = vegetationData.instanceCount.value_or(2000000u);
    const uint32_t boneCount = vegetationData.boneCount.value_or(6u);
    const float bladeHeight = vegetationData.bladeHeight.value_or(0.5f);
    const float windDirX = vegetationData.windDirX.value_or(1.0f);
    const float windDirZ = vegetationData.windDirZ.value_or(0.0f);
    const float leanDeviation = vegetationData.leanDeviation.value_or(35.0f);
    const std::string materialName = vegetationData.material.value_or("grassMaterial");
    const std::string name = vegetationData.name.value_or("VegetationNode");

    const uint32_t subBladeCount = vegetationData.subBladeCount.value_or(10u);
    const float subBladeScatterRadiusMin = vegetationData.subBladeScatterRadiusMin.value_or(0.15f);
    const float subBladeScatterRadiusMax = vegetationData.subBladeScatterRadiusMax.value_or(0.45f);
    const float windStrength = vegetationData.windStrength.value_or(4.0f);
    const float windFrequency = vegetationData.windFrequency.value_or(0.5f);
    const float windSpeed = vegetationData.windSpeed.value_or(1.5f);
    const float windBendMult = vegetationData.windBendMult.value_or(5.0f);
    const float stiffness = vegetationData.stiffness.value_or(15.0f);
    const float damping = vegetationData.damping.value_or(0.92f);
    const float softness = vegetationData.softness.value_or(0.2f);
    const float lodFullDist = vegetationData.lodFullDist.value_or(15.0f);
    const float lodFadeDist = vegetationData.lodFadeDist.value_or(20.0f);

    glm::vec2 placementMinXZ(-100.0f, -100.0f);
    glm::vec2 placementMaxXZ(100.0f, 100.0f);
    float grassScaleMin = vegetationData.grassScaleMin.value_or(0.4f);
    float grassScaleMax = vegetationData.grassScaleMax.value_or(1.5f);
    if (vegetationData.placement)
    {
        const Vans::VansSceneVegetationPlacementConfig& placement = *vegetationData.placement;
        if (placement.boundsMin) placementMinXZ = ToPcgVec2(*placement.boundsMin);
        if (placement.boundsMax) placementMaxXZ = ToPcgVec2(*placement.boundsMax);
        if (placement.grassScaleMin) grassScaleMin = *placement.grassScaleMin;
        if (placement.grassScaleMax) grassScaleMax = *placement.grassScaleMax;
    }

    VansPcgSystem pcgSystem;
    pcgSystem.Configure(ToPcgMaskConfigs(vegetationData.pcgMasks), projectRoot, placementMinXZ, placementMaxXZ);
    PcgPlacementMask grassMask;
    if (vegetationData.placement && vegetationData.placement->mask)
    {
        grassMask = pcgSystem.ResolvePlacementMask(
            ToPcgMaskReference(*vegetationData.placement->mask),
            "grass",
            placementMinXZ,
            placementMaxXZ);
    }

    // Create the vegetation system
    VansVegetationSystem* vegetationSystem = new VansVegetationSystem();
    scene.SetVegetationSystem(vegetationSystem);
    vegetationSystem->SetBladeHeight(bladeHeight);   // must be set before Init()
    vegetationSystem->SetInitWindDirection(glm::vec2(windDirX, windDirZ), leanDeviation);
    vegetationSystem->SetSubBladeParams(subBladeCount, subBladeScatterRadiusMin, subBladeScatterRadiusMax);  // must be set before Init()
    vegetationSystem->SetPlacementBounds(placementMinXZ, placementMaxXZ);
    vegetationSystem->SetGrassScaleRange(grassScaleMin, grassScaleMax);
    vegetationSystem->SetPlacementMask(grassMask);

    if (vegetationData.trees)
    {
        const Vans::VansSceneVegetationTreesConfig& treesConfig = *vegetationData.trees;
        TreeVegetationConfig treeConfig;
        treeConfig.enabled = treesConfig.enabled.value_or(false);
        treeConfig.cullDistance = treesConfig.cullDistance.value_or(800.0f);
        treeConfig.cullEnabled = treesConfig.cullEnabled.value_or(true);
        treeConfig.hizEnabled = treesConfig.hizEnabled.value_or(true);

        for (const Vans::VansSceneVegetationTreeSpeciesConfig& speciesConfig : treesConfig.species)
        {
            TreeSpeciesConfig species;
            species.name = speciesConfig.name.empty() ? std::string("TreeSpecies") : speciesConfig.name;
            species.boundsRadius = speciesConfig.boundsRadius.value_or(1.0f);
            for (const Vans::VansSceneVegetationTreePartConfig& partConfig : speciesConfig.parts)
            {
                TreePartConfig part;
                part.type = ToTreePartType(partConfig.type);
                part.meshName = partConfig.mesh;
                part.materialName = partConfig.material;
                part.submeshIndex = partConfig.submeshIndex.value_or(-1);
                species.parts.push_back(part);
            }
            treeConfig.species.push_back(species);
        }

        if (!treesConfig.instances.empty())
        {
            for (const Vans::VansSceneVegetationTreeInstanceConfig& instanceConfig : treesConfig.instances)
            {
                TreeInstanceConfig inst;
                inst.speciesName = instanceConfig.species.value_or(treeConfig.species.empty() ? std::string("") : treeConfig.species[0].name);
                if (instanceConfig.position) inst.position = ToVec3(*instanceConfig.position);
                inst.yawDeg = instanceConfig.yaw.value_or(0.0f);
                inst.scale = instanceConfig.scale.value_or(1.0f);
                inst.submeshIndex = instanceConfig.submeshIndex.value_or(-1);
                treeConfig.instances.push_back(inst);
            }
        }
        else if (treesConfig.randomInstances && !treeConfig.species.empty())
        {
            const Vans::VansSceneVegetationRandomTreeConfig& randomConfig = *treesConfig.randomInstances;
            uint32_t treeCount = randomConfig.count.value_or(80u);
            uint32_t seed = randomConfig.seed.value_or(1337u);
            float scaleMin = randomConfig.scaleMin.value_or(0.9f);
            float scaleMax = randomConfig.scaleMax.value_or(1.15f);
            glm::vec2 treeMinXZ = randomConfig.boundsMin ? ToPcgVec2(*randomConfig.boundsMin) : placementMinXZ;
            glm::vec2 treeMaxXZ = randomConfig.boundsMax ? ToPcgVec2(*randomConfig.boundsMax) : placementMaxXZ;
            const glm::vec2 normalizedMinXZ = glm::min(treeMinXZ, treeMaxXZ);
            const glm::vec2 normalizedMaxXZ = glm::max(treeMinXZ, treeMaxXZ);
            treeMinXZ = normalizedMinXZ;
            treeMaxXZ = normalizedMaxXZ;
            std::mt19937 rng(seed);
            std::uniform_real_distribution<float> xDist(treeMinXZ.x, treeMaxXZ.x);
            std::uniform_real_distribution<float> zDist(treeMinXZ.y, treeMaxXZ.y);
            std::uniform_real_distribution<float> yawDist(0.0f, 360.0f);
            std::uniform_real_distribution<float> scaleDist(std::min(scaleMin, scaleMax), std::max(scaleMin, scaleMax));
            std::uniform_real_distribution<float> acceptDist(0.0f, 1.0f);
            PcgPlacementMask treeMask;
            if (randomConfig.mask)
            {
                treeMask = pcgSystem.ResolvePlacementMask(
                    ToPcgMaskReference(*randomConfig.mask),
                    "tree",
                    treeMinXZ,
                    treeMaxXZ);
            }
            const uint32_t maxAttempts = treeMask.enabled
                ? std::max(treeCount * 32u, treeCount)
                : treeCount;
            const size_t startInstanceCount = treeConfig.instances.size();

            for (uint32_t attempt = 0;
                 attempt < maxAttempts && (treeConfig.instances.size() - startInstanceCount) < treeCount;
                 ++attempt)
            {
                const float px = xDist(rng);
                const float pz = zDist(rng);
                if (treeMask.enabled && !pcgSystem.AcceptMask(treeMask, glm::vec2(px, pz), acceptDist(rng)))
                    continue;

                TreeInstanceConfig inst;
                inst.speciesName = randomConfig.species.value_or(treeConfig.species[0].name);
                inst.position = glm::vec3(px, 0.0f, pz);
                inst.yawDeg = yawDist(rng);
                inst.scale = scaleDist(rng);
                inst.submeshIndex = randomConfig.submeshIndex.value_or(-1);
                treeConfig.instances.push_back(inst);
            }

            if (treeMask.enabled && (treeConfig.instances.size() - startInstanceCount) < treeCount)
            {
                VANS_LOG_WARN("[PCG] Tree mask '" << treeMask.name << "' produced "
                    << (treeConfig.instances.size() - startInstanceCount) << "/" << treeCount
                    << " requested random tree instances.");
            }
        }
        else if (!treeConfig.species.empty())
        {
            uint32_t treeCount = treesConfig.fallbackCount.value_or(10u);
            float radius = treesConfig.placementRadius.value_or(12.0f);
            glm::vec3 center = treesConfig.center ? ToVec3(*treesConfig.center) : glm::vec3(0.0f);
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
    if (!vegetationData.renderConfigs.empty())
    {
        std::vector<GrassRenderConfig> configs;
        configs.reserve(vegetationData.renderConfigs.size());
        for (const Vans::VansSceneVegetationRenderConfig& renderConfig : vegetationData.renderConfigs)
        {
            GrassRenderConfig cfg;
            cfg.meshName = renderConfig.mesh.value_or(std::string());
            cfg.materialName = renderConfig.material.value_or(materialName);
            cfg.percent = renderConfig.percent.value_or(1.0f);
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
                float terrainMaxHeight = vegetationData.terrainMaxHeight.value_or(terrain->GetMaxHeight());
                float terrainHeightOffset = vegetationData.terrainHeightOffset.value_or(terrain->GetHeightOffset());

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
            VansMaterialManager::RT_HZB_OCCLUSION_RESULT);
        if (hzbTexture != nullptr)
        {
            float hizSampleBias = vegetationData.hizSampleBias.value_or(0.2f);
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
