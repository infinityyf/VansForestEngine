#include "VansSceneEnvironmentNodeBuilder.h"

#include "../VansScene.h"
#include "../VulkanCore/VansMesh.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansRenderPass.h"
#include "../TerrainCore/VansTerrain.h"
#include "../VegetationCore/VansVegetationSystem.h"
#include "../WaterCore/VansWaterMaterial.h"
#include "../WaterCore/VansWaterSystem.h"
#include "../../PhysicsCore/VansPhysics.h"
#include "../../PhysicsCore/VansCollisionLayerManager.h"
#include "../../PhysicsCore/VansTerrainPhysicsNode.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../Util/VansLog.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <memory>
#include <optional>
#include <random>

namespace VansGraphics
{

bool VansSceneEnvironmentNodeBuilder::BuildTerrainNode(
    VansScene& scene,
    VansVKDevice& device,
    const Vans::VansSceneTerrainNodeConfig& terrainData,
    std::string& error)
{
    if (!terrainData.valid || !terrainData.asset)
    {
        error = "Terrain node requires a resolved terrain asset snapshot";
        return false;
    }

    TerrainConfig config;
    if (!Vans::VansAssetGuid::TryParse(terrainData.assetGuid, config.assetGuid))
    {
        error = "Terrain node has an invalid asset GUID '" + terrainData.assetGuid + "'";
        return false;
    }
    config.asset = terrainData.asset;
    const auto resolveLoadedTexture = [&scene, &error](Vans::VansAssetGuid guid)
    {
        auto* texture = static_cast<VansTexture*>(scene.GetTextureAsset(guid.ToString()));
        if (!texture)
            error = "Terrain layer texture was not preloaded: " + guid.ToString();
        return texture;
    };
    config.layers.reserve(terrainData.asset->layers.size());
    for (const Vans::VansTerrainLayerAsset& layerAsset : terrainData.asset->layers)
    {
        TerrainLayerConfig layer;
        layer.albedo = resolveLoadedTexture(layerAsset.albedo);
        if (!layer.albedo) return false;
        layer.normal = resolveLoadedTexture(layerAsset.normal);
        if (!layer.normal) return false;
        layer.roughness = resolveLoadedTexture(layerAsset.roughness);
        if (!layer.roughness) return false;
        layer.tiling = layerAsset.tiling;
        config.layers.push_back(std::move(layer));
    }

    std::optional<VansEngine::TerrainPhysicsProperties> terrainPhysics;
    if (terrainData.collision && terrainData.collision->enabled.value_or(false))
    {
        const Vans::VansSceneTerrainCollisionConfig& collision = *terrainData.collision;
        if (!collision.layer || collision.layer->empty())
        {
            error = "Enabled terrain collision requires an explicit layer";
            return false;
        }
        int layerIndex = -1;
        if (!VansEngine::VansCollisionLayerManager::Get().TryGetLayerIndex(
            *collision.layer, layerIndex))
        {
            error = "Terrain collision references unknown layer '" + *collision.layer + "'";
            return false;
        }

        terrainPhysics.emplace();
        terrainPhysics->enabled = true;
        terrainPhysics->surface = terrainData.asset;
        terrainPhysics->layerName = *collision.layer;
        if (collision.material.staticFriction)
            terrainPhysics->material.staticFriction = *collision.material.staticFriction;
        if (collision.material.dynamicFriction)
            terrainPhysics->material.dynamicFriction = *collision.material.dynamicFriction;
        if (collision.material.restitution)
            terrainPhysics->material.restitution = *collision.material.restitution;
    }

    RenderNodeType type = RenderNodeType::TERRAIN_NODE;
    auto renderNode = std::make_unique<VansTerrainRenderNode>(&device, config, type);
    std::unique_ptr<VansEngine::VansTerrainPhysicsNode> terrainPhysicsNode;

    // Terrain 物理碰撞是可选项，只由 terrain.collision.enabled 控制。
    // 所有可失败步骤先在局部 owner 中完成，再发布 Render/Physics 节点。
    if (terrainPhysics)
    {
        auto& physicsSystem = VansEngine::VansPhysicsSystem::GetInstance();
        std::lock_guard<std::mutex> simLock(physicsSystem.GetSimulationMutex());

        terrainPhysicsNode = std::make_unique<VansEngine::VansTerrainPhysicsNode>();
        if (!terrainPhysicsNode->Initialize(*terrainPhysics))
        {
            error = "Terrain collision initialization failed";
            return false;
        }
    }

    renderNode->SetName(terrainData.name.value_or("TerrainNode"));
    scene.RegistRenderNode(renderNode.release(), type);
    if (terrainPhysicsNode)
    {
        auto& physicsSystem = VansEngine::VansPhysicsSystem::GetInstance();
        std::lock_guard<std::mutex> simLock(physicsSystem.GetSimulationMutex());
        scene.SetTerrainPhysicsNode(terrainPhysicsNode.release());
    }
    return true;
}

bool VansSceneEnvironmentNodeBuilder::BuildWaterNode(
    VansScene& scene,
    VansVKDevice& device,
    const Vans::VansSceneWaterNodeConfig& waterData,
    std::shared_ptr<const Vans::VansTerrainAsset> effectiveTerrain,
    std::string& error)
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
        error = "Water configuration was rejected by the current scene schema";
        return false;
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

    const Vans::VansSceneWaterDetailNormalConfig& detailNormalConfig = waterData.detailNormal;
    if (detailNormalConfig.enabled) config.m_DetailNormal.m_Enabled = *detailNormalConfig.enabled;
    if (detailNormalConfig.decodeMode)
        config.m_DetailNormal.m_DecodeMode = VansWaterNormalDecodeMode::RGReconstructZ;
    if (detailNormalConfig.flipGreen) config.m_DetailNormal.m_FlipGreen = *detailNormalConfig.flipGreen;
    if (detailNormalConfig.globalStrength) config.m_DetailNormal.m_GlobalStrength = *detailNormalConfig.globalStrength;
    if (detailNormalConfig.maxSlope) config.m_DetailNormal.m_MaxSlope = *detailNormalConfig.maxSlope;
    if (detailNormalConfig.mipBias) config.m_DetailNormal.m_MipBias = *detailNormalConfig.mipBias;
    if (detailNormalConfig.anisotropy) config.m_DetailNormal.m_Anisotropy = *detailNormalConfig.anisotropy;
    for (std::size_t layerIndex = 0; layerIndex < detailNormalConfig.layers.size(); ++layerIndex)
    {
        const Vans::VansSceneWaterDetailNormalLayerConfig& source = detailNormalConfig.layers[layerIndex];
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

    VansMesh* planeMesh = static_cast<VansMesh*>(scene.FindMeshAsset("plane"));
    VansTexture* detailNormalTexture = static_cast<VansTexture*>(
        scene.GetTextureAsset("waterDetailWaveNormal"));
    VansTexture* neutralNormalTexture = static_cast<VansTexture*>(
        scene.GetTextureAsset("defaultNormal"));
    if (!detailNormalTexture && !neutralNormalTexture)
    {
        error = "Neither 'waterDetailWaveNormal' nor 'defaultNormal' texture is available";
        return false;
    }

    // 创建只持有当前配置模型的 WaterMaterial。
    VansWaterMaterial* mat = new VansWaterMaterial();
    mat->m_MaterialType = VansMaterialType::VAN_WATER;
    mat->m_Config       = config;

    // ── 注册到场景 ─────────────────────────────────────────────────────────
    mat->SetName(waterData.name.value_or("WaterMaterial"));
    scene.AddMaterialAsset(mat);

    // 记录完整配置供 VansWaterSystem 初始化时读取
    scene.SetWaterRuntimeConfig(config, mat);

    // ── 创建 VansWaterRenderNode，使用引擎内置 "plane" 网格作为水面几何体 ──
    // 该节点保留现有兼容路径；当前 Water GBuffer 由 WaterSystem geometry clipmap 绘制。
    {
        if (!planeMesh)
        {
            VANS_LOG_WARN("[BuildWaterNode] Built-in mesh 'plane' is unavailable; "
                "the compatibility render node was skipped");
        }
        else
        {
            VkDevice& nativeDevice = device.GetLogicDevice();
            VansWaterRenderNode* waterNode = new VansWaterRenderNode(nativeDevice, WATER_NODE);
            waterNode->m_Mesh     = planeMesh;
            waterNode->m_Material = mat;

            // 有地形时覆盖范围只来自同次 SceneBuild 的有效 Terrain；无地形水场保留既有兼容平面默认。
            const float terrainHalfSize = effectiveTerrain
                ? effectiveTerrain->settings.terrainSize * 0.5f
                : DefaultWaterCompatibilityPlaneHalfExtent;
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

    VANS_LOG("[BuildWaterNode] Water loaded: level=" << config.m_WaterLevel
        << " geometryLod=" << config.m_Geometry.m_LodCount
        << " spectrumCascades=" << config.m_Spectrum.m_CascadeCount
        << " ssr=" << (config.m_SSR.m_Enabled ? "on" : "off"));

    // ── 创建 VansWaterSystem（设计文档 §12.1）────────────────────────────────
    // VansWaterSystem 管理 Water GBuffer 纹理、波形仿真、Pre-Water Compute 和 Composite pass。
    // 通过 m_Scene->GetWaterSystem() 供 VansVKRenderer 在渲染循环中调度。
    {
        VansWaterSystem* waterSystem = new VansWaterSystem();
        waterSystem->SetWaterLevel(config.m_WaterLevel);
        waterSystem->SetWaterMaterial(mat);
        waterSystem->SetDetailNormalTextures(detailNormalTexture, neutralNormalTexture);
        waterSystem->Initialize(&device,
            static_cast<uint32_t>(device.GetRenderWidth()),
            static_cast<uint32_t>(device.GetRenderHeight()));

        // SetupDescriptors：绑定 WaterGBuf 纹理到合成集（在 SetupVansWaterGBufferPass 之后调用）
        auto* rp = VansRenderPassManager::GetInstance();
        waterSystem->SetupDescriptors(
            rp,
            scene.GetGlobalDescriptorSetLayout(),
            scene.GetGlobalDescriptorSet());

        scene.SetWaterSystem(waterSystem);
    }
    return true;
}

}
