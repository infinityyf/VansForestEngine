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
#include <mutex>
#include <random>

namespace VansGraphics
{

void VansSceneEnvironmentNodeBuilder::AddTerrainNode(
    VansScene& scene,
    VansVKDevice* device,
    const Vans::VansSceneTerrainNodeConfig& terrainData)
{
    if (!terrainData.valid || !terrainData.asset)
        throw std::invalid_argument("Terrain scene node requires a resolved terrain asset snapshot.");

    TerrainConfig config;
    Vans::VansAssetGuid::TryParse(terrainData.assetGuid, config.assetGuid);
    config.asset = terrainData.asset;
    const auto resolveLoadedTexture = [&scene](Vans::VansAssetGuid guid)
    {
        auto* texture = static_cast<VansTexture*>(scene.GetTextureAsset(guid.ToString()));
        if (!texture)
            throw std::invalid_argument(
                "Terrain layer texture was not preloaded: " + guid.ToString());
        return texture;
    };
    config.layers.reserve(terrainData.asset->layers.size());
    for (const Vans::VansTerrainLayerAsset& layerAsset : terrainData.asset->layers)
    {
        TerrainLayerConfig layer;
        layer.albedo = resolveLoadedTexture(layerAsset.albedo);
        layer.normal = resolveLoadedTexture(layerAsset.normal);
        layer.roughness = resolveLoadedTexture(layerAsset.roughness);
        layer.tiling = layerAsset.tiling;
        config.layers.push_back(std::move(layer));
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
        terrainPhysicsProps.surface = terrainData.asset;
        terrainPhysicsProps.terrainSize = terrainData.asset->settings.terrainSize;
        terrainPhysicsProps.maxHeight = terrainData.asset->settings.maxHeight;
        terrainPhysicsProps.heightOffset = terrainData.asset->settings.heightOffset;
        if (collision.layer) terrainPhysicsProps.layerName = *collision.layer;
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

    if (!waterData.valid)
    {
        VANS_LOG_ERROR("[AddWaterNode] Water configuration was rejected by the current scene schema.");
        return;
    }

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

    if(waterData.river.maxHeight)config.m_River.m_MaxHeight=*waterData.river.maxHeight;
    if(waterData.river.wavelength)config.m_River.m_Wavelength=*waterData.river.wavelength;
    if(waterData.river.lifetime)config.m_River.m_Lifetime=*waterData.river.lifetime;
    if(waterData.river.flowGridSize)config.m_River.m_FlowGridSize=*waterData.river.flowGridSize;
    if(waterData.river.fineDetailStrength)config.m_River.m_FineDetailStrength=*waterData.river.fineDetailStrength;

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


    const Vans::VansSceneWaterRefractionConfig& refraction = waterData.refraction;
    if (refraction.enabled) config.m_Refraction.m_Enabled = *refraction.enabled;
    if (refraction.distortionStrength) config.m_Refraction.m_DistortionStrength = *refraction.distortionStrength;

    const Vans::VansSceneWaterDetailNormalConfig& detailNormal = waterData.detailNormal;
    if (detailNormal.enabled) config.m_DetailNormal.m_Enabled = *detailNormal.enabled;
    if (detailNormal.decodeMode)
        config.m_DetailNormal.m_DecodeMode = VansWaterNormalDecodeMode::RGReconstructZ;
    if (detailNormal.flipGreen) config.m_DetailNormal.m_FlipGreen = *detailNormal.flipGreen;
    if (detailNormal.globalStrength) config.m_DetailNormal.m_GlobalStrength = *detailNormal.globalStrength;
    if (detailNormal.maxSlope) config.m_DetailNormal.m_MaxSlope = *detailNormal.maxSlope;
    if (detailNormal.mipBias) config.m_DetailNormal.m_MipBias = *detailNormal.mipBias;
    if (detailNormal.anisotropy) config.m_DetailNormal.m_Anisotropy = *detailNormal.anisotropy;
    for (std::size_t layerIndex = 0; layerIndex < detailNormal.layers.size(); ++layerIndex)
    {
        const Vans::VansSceneWaterDetailNormalLayerConfig& source = detailNormal.layers[layerIndex];
        VansWaterDetailNormalLayerConfig& destination = config.m_DetailNormal.m_Layers[layerIndex];
        if (source.enabled) destination.m_Enabled = *source.enabled;
        if (source.tileSizeMeters) destination.m_TileSizeMeters = *source.tileSizeMeters;
        if (source.direction) destination.m_Direction = toVec2(*source.direction);
        if (source.speedMetersPerSecond) destination.m_SpeedMetersPerSecond = *source.speedMetersPerSecond;
        if (source.phase) destination.m_Phase = *source.phase;
        if (source.strength) destination.m_Strength = *source.strength;
        if (source.fadeStartMeters) destination.m_FadeStartMeters = *source.fadeStartMeters;
        if (source.fadeEndMeters) destination.m_FadeEndMeters = *source.fadeEndMeters;
    }

    const Vans::VansSceneWaterEffectiveRoughnessConfig& effectiveRoughness = waterData.effectiveRoughness;
    if (effectiveRoughness.mode)
    {
        config.m_EffectiveRoughness.m_Mode =
            *effectiveRoughness.mode == Vans::VansSceneWaterEffectiveRoughnessMode::DistanceHeuristic
            ? VansWaterEffectiveRoughnessMode::DistanceHeuristic
            : VansWaterEffectiveRoughnessMode::BaseOnly;
    }
    if (effectiveRoughness.distanceStartMeters)
        config.m_EffectiveRoughness.m_DistanceStartMeters = *effectiveRoughness.distanceStartMeters;
    if (effectiveRoughness.distanceEndMeters)
        config.m_EffectiveRoughness.m_DistanceEndMeters = *effectiveRoughness.distanceEndMeters;
    if (effectiveRoughness.distanceStrength)
        config.m_EffectiveRoughness.m_DistanceStrength = *effectiveRoughness.distanceStrength;

    const Vans::VansSceneWaterColorMipConfig& colorMip = waterData.colorMip;
    if (colorMip.refractionScatterScale)
        config.m_ColorMip.m_RefractionScatterScale = *colorMip.refractionScatterScale;
    if (colorMip.refractionRoughnessScale)
        config.m_ColorMip.m_RefractionRoughnessScale = *colorMip.refractionRoughnessScale;
    if (colorMip.forwardScatterMipScale)
        config.m_ColorMip.m_ForwardScatterMipScale = *colorMip.forwardScatterMipScale;
    if (colorMip.backgroundScatterScale)
        config.m_ColorMip.m_BackgroundScatterScale = *colorMip.backgroundScatterScale;
    if (colorMip.lodBias) config.m_ColorMip.m_LodBias = *colorMip.lodBias;

    const Vans::VansSceneWaterShadowConfig& shadow = waterData.shadow;
    if (shadow.enabled) config.m_Shadow.m_Enabled = *shadow.enabled;
    if (shadow.quality) config.m_Shadow.m_Quality = *shadow.quality;
    if (shadow.depthBias) config.m_Shadow.m_DepthBias = *shadow.depthBias;
    if (shadow.normalBias) config.m_Shadow.m_NormalBias = *shadow.normalBias;
    if (shadow.volumeStepStride) config.m_Shadow.m_VolumeStepStride = *shadow.volumeStepStride;

    const Vans::VansSceneWaterSSRConfig& ssr = waterData.ssr;
    if (ssr.enabled) config.m_SSR.m_Enabled = *ssr.enabled;
    if (ssr.maxDistance) config.m_SSR.m_MaxDistance = *ssr.maxDistance;
    if (ssr.maxRoughness) config.m_SSR.m_MaxRoughness = *ssr.maxRoughness;
    if (ssr.roughnessFadeStart) config.m_SSR.m_RoughnessFadeStart = *ssr.roughnessFadeStart;
    if (ssr.colorMipConeScale) config.m_SSR.m_ColorMipConeScale = *ssr.colorMipConeScale;
    if (ssr.colorMipBias) config.m_SSR.m_ColorMipBias = *ssr.colorMipBias;
    if (ssr.edgeFadePixels) config.m_SSR.m_EdgeFadePixels = *ssr.edgeFadePixels;

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
            VansTexture* detailNormal = static_cast<VansTexture*>(
                scene.GetTextureAsset("waterDetailWaveNormal"));
            VansTexture* neutralNormal = static_cast<VansTexture*>(
                scene.GetTextureAsset("defaultNormal"));
            if (detailNormal == nullptr)
                VANS_LOG_ERROR("[AddWaterNode] Required built-in texture 'waterDetailWaveNormal' is missing");
            waterSystem->SetDetailNormalTextures(detailNormal, neutralNormal);
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

}
