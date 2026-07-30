#include "VansWaterSystem.h"
#include "VansWaterFFT.h"
#include "../../Util/VansLog.h"
#include "../../Configration/VansConfigration.h"
#include "../VansShaderManager.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../VulkanCore/VansRenderPass.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../VulkanCore/VansPipeline.h"
#include <cmath>
#include <string>
#include <algorithm>
#include <cstddef>

namespace VansGraphics
{

namespace
{
    // W-04: 閼奉亜濮╅悽鐔稿灇鐎佃鏆熼崚鍡楃閻?Gerstner 濞夈垹鍨庨柌?
    void AutoGenerateGerstnerWaves(std::vector<GerstnerWaveGPU>& waves,
                                    int count, const glm::vec2& windDir,
                                    float swellAmplitude, float windSpeed)
    {
        waves.clear();
        waves.reserve(count);
        glm::vec2 dir = glm::normalize(windDir);
        if (glm::length(dir) < 0.001f)
            dir = glm::vec2(0.7071f, 0.7071f);

        // 濞夈垽鏆辨禒?256m 閸?0.5m 鐎佃鏆熼崚鍡楃閿涘牊澧跨仦鏇氱瑐闂勬劒浜掔憰鍡欐磰缁?LOD Nyquist 鏉╁洦鎶ら敍?
        const float minWL = 0.5f;
        const float PI = 3.14159265358979323846f;
        const float GRAVITY = 9.81f;
        const float maxWL = std::clamp(2.0f * PI * windSpeed * windSpeed / GRAVITY, 8.0f, 512.0f);

        for (int i = 0; i < count; ++i)
        {
            float t = (count <= 1) ? 0.0f : static_cast<float>(i) / static_cast<float>(count - 1);
            float wavelength = maxWL * std::powf(minWL / maxWL, t);
            float k = 2.0f * PI / wavelength;
            float omega = std::sqrtf(GRAVITY * k);
            float speed = omega / k;  // 閻╂悂鈧喎瀹?

            // 閹割垰绠欓梾蹇斿皾闂€鍨櫤鐏忓骏绱欓惌顓熷皾閼充粙鍣虹亸蹇ョ礆閿涘苯濮為崗銉ょ鐎规岸娈㈤張鐑樷偓?
            float baseAmp = swellAmplitude * std::powf(wavelength / maxWL, 0.75f)
                * std::sqrt(2.0f / float((std::max)(count, 1)));
            // 閺傜懓鎮滈敍姘额棑閺傜懓鎮?+ 鐏忓繗顫楁惔锔藉⒖閺?
            float angleSpread = (static_cast<float>((i * 7 + 3) % 17) / 17.0f - 0.5f) * 0.6f;
            float angle = std::atan2f(dir.y, dir.x) + angleSpread;
            float dx = std::cosf(angle);
            float dy = std::sinf(angle);

            GerstnerWaveGPU wave = {};
            wave.amplitude  = baseAmp;
            wave.wavelength = wavelength;
            wave.directionX = dx;
            wave.directionY = dy;
            wave.phaseSpeed = speed;
            wave.steepness  = 0.05f + 0.55f * (1.0f - t);  // 闂€鎸庡皾閺囨挳娅?
            wave.padding0   = 0.0f;
            wave.padding1   = 0.0f;
            waves.push_back(wave);
        }
    }

    std::uint32_t HashU32(std::uint32_t value)
    {
        value ^= value >> 16;
        value *= 0x7feb352du;
        value ^= value >> 15;
        value *= 0x846ca68bu;
        value ^= value >> 16;
        return value;
    }

    float Hash01(std::uint32_t seed, std::uint32_t channel)
    {
        const std::uint32_t h = HashU32(seed ^ (channel * 0x9e3779b9u));
        return float(h & 0x00ffffffu) / float(0x01000000u);
    }

    float Fract01(float value)
    {
        return value - std::floor(value);
    }

    float ComputeWaterF0(float ior)
    {
        const float f = (ior - 1.0f) / (std::max)(ior + 1.0f, 0.0001f);
        return f * f;
    }

    PBRWaterParamsGPU BuildPBRWaterParams(const VansWaterConfig& config,
                                          const glm::vec3& cameraPos,
                                          const glm::mat4& viewMatrix,
                                          const glm::mat4& vpMatrix,
                                          const glm::vec3& mainLightDir,
                                          const glm::vec3& mainLightColor)
    {
        PBRWaterParamsGPU params = {};
        const float ior = config.m_Medium.m_IOR;
        params.absorptionCoeff = glm::vec4(config.m_Medium.m_AbsorptionCoeff, 1.0f);
        params.scatteringCoeff = glm::vec4(config.m_Medium.m_ScatteringCoeff, 1.0f);
        params.cameraPosition = glm::vec4(cameraPos, 1.0f);
        params.mainLightDir = glm::vec4(glm::normalize(mainLightDir), 0.0f);
        params.mainLightColor = glm::vec4(glm::max(mainLightColor, glm::vec3(0.0f)), 1.0f);
        params.surfaceParams = glm::vec4(
            config.m_Medium.m_WaterRoughness,
            ior,
            ComputeWaterF0(ior),
            config.m_SpecularIntensity);
        params.refractionParams = glm::vec4(
            config.m_Refraction.m_DistortionStrength,
            config.m_Optics.m_MaxRefractionCrossDistance,
            config.m_Optics.m_WaterDispersionStrength,
            1.5f);
        params.volumeParams = glm::vec4(
            config.m_Optics.m_MaxCrossDistance,
            float(config.m_Volume.m_SampleCount),
            config.m_Volume.m_ResolutionScale,
            config.m_Optics.m_MultiScatterScale);
        params.thinSSSParams = glm::vec4(
            config.m_Optics.m_SSSPathScale,
            config.m_Optics.m_SSSNonlinearStrength,
            config.m_Optics.m_SSSScatterBoost,
            config.m_Medium.m_Anisotropy);
        params.backlitParams = glm::vec4(
            config.m_Optics.m_BacklitPathScale,
            config.m_Optics.m_BacklitPhaseG,
            0.0f,
            0.0f);
        params.filterParams = glm::vec4(
            config.m_Volume.m_SpatialDepthSensitivity,
            float(config.m_Volume.m_SpatialFilterIterations),
            0.0f,
            0.0f);
        params.effectFlags = glm::ivec4(
            config.m_SSR.m_Enabled ? 1 : 0,
            config.m_Refraction.m_Enabled ? 1 : 0,
            config.m_Caustics.m_Enabled ? 1 : 0,
            config.m_SSS.m_Enabled ? 1 : 0);
        params.invViewProjMatrix = glm::inverse(vpMatrix);
        params.viewMatrix = viewMatrix;
        params.projMatrix = vpMatrix * glm::inverse(viewMatrix);
        return params;
    }

    void AutoGenerateWaveParticles(std::vector<WaveParticleGPU>& particles,
                                   const VansWaterConfig& config)
    {
        const auto& wp = config.m_WaveParticle;
        const auto& spectrum = config.m_Spectrum;
        const int count = std::clamp(wp.m_ParticleCount, 0, int(VansWaterConfig::MAX_WAVE_PARTICLE_COUNT));
        particles.clear();
        particles.reserve(count);

        glm::vec2 windDir = spectrum.m_WindDirection;
        if (glm::length(windDir) < 0.001f)
            windDir = glm::vec2(1.0f, 0.0f);
        else
            windDir = glm::normalize(windDir);

        const float PI = 3.14159265358979323846f;
        const float baseAngle = std::atan2f(windDir.y, windDir.x);
        const float radiusRatio = wp.m_MinRadius / (std::max)(wp.m_MaxRadius, wp.m_MinRadius + 0.001f);
        const float amplitudeNorm = 2.0f / std::sqrt((std::max)(count, 1));
        const float seedOffsetX = Hash01(wp.m_RandomSeed, 101);
        const float seedOffsetZ = Hash01(wp.m_RandomSeed, 102);
        const float seedOffsetR = Hash01(wp.m_RandomSeed, 103);
        for (int i = 0; i < count; ++i)
        {
            const std::uint32_t seed = wp.m_RandomSeed ^ HashU32(std::uint32_t(i) * 747796405u + 2891336453u);
            // Low-discrepancy placement avoids random clumps that leave
            // compact particle kernels uncovered elsewhere in the solve tile.
            const float index = float(i) + 0.5f;
            const float px = Fract01(seedOffsetX + index * 0.754877666f);
            const float pz = Fract01(seedOffsetZ + index * 0.569840291f);
            const float rt = Fract01(seedOffsetR + index * 0.381966011f);
            const float radius = wp.m_MaxRadius * std::pow(radiusRatio, rt);
            const float angle = baseAngle + (Hash01(seed, 4) * 2.0f - 1.0f) * wp.m_DirectionSpread;
            const glm::vec2 dir = glm::normalize(glm::vec2(std::cosf(angle), std::sinf(angle)));
            const float phase = Hash01(seed, 5) * 2.0f * PI;
            const float speed = wp.m_PhaseVelocity * std::sqrtf(9.81f * radius / PI);
            const float amplitude = std::pow(radius / (std::max)(wp.m_MaxRadius, 0.001f), 0.65f)
                * (0.65f + 0.7f * Hash01(seed, 6))
                * amplitudeNorm;

            WaveParticleGPU particle = {};
            particle.positionRadius = glm::vec4(px, pz, radius, amplitude);
            particle.directionPhase = glm::vec4(dir.x, dir.y, phase, speed);
            particle.lifetimeSeed = glm::vec4(wp.m_Lifetime, 0.0f, float(seed & 0xffffu), 0.0f);
            particles.push_back(particle);
        }
    }

    struct WaterSSRParamsGPU
    {
        glm::vec4 cameraPosition;
        glm::mat4 projMatrix;
        glm::mat4 invProjMatrix;
        glm::mat4 viewMatrix;
        float maxDistance;
        int   maxSteps;
        float thickness;
        float maxRoughness;
        glm::vec4 surfaceParams; // x=current water roughness
    };

    struct ThicknessParamsGPU
    {
        float maxThickness;
        float deepFallback;
        float pad0;
        float pad1;
    };

    static_assert(offsetof(PBRWaterParamsGPU, invViewProjMatrix) == 192,
        "PBRWaterParamsGPU must match std140 shader layout");
    static_assert(sizeof(PBRWaterParamsGPU) == 384,
        "PBRWaterParamsGPU size must match std140 shader layout");
    static_assert(offsetof(WaterCausticsParamsGPU, opticalParams) == 64,
        "WaterCausticsParamsGPU must match std140 shader layout");
    static_assert(sizeof(WaterCausticsParamsGPU) == 80,
        "WaterCausticsParamsGPU size must match std140 shader layout");
    static_assert(offsetof(WaterGBufferParamsGPU, geometryParams) == 144,
        "WaterGBufferParamsGPU geometry offset must match std140 shader layout");
    static_assert(offsetof(WaterGBufferParamsGPU, waveParticleParams0) == 224,
        "WaterGBufferParamsGPU wave particle offset must match std140 shader layout");
    static_assert(offsetof(WaterGBufferParamsGPU, flowMapParams) == 304,
        "WaterGBufferParamsGPU flow map offset must match std140 shader layout");
    static_assert(offsetof(WaterGBufferParamsGPU, surfaceOptics) == 336,
        "WaterGBufferParamsGPU optics offset must match std140 shader layout");
    static_assert(sizeof(WaterGBufferParamsGPU) == 384,
        "WaterGBufferParamsGPU size must match std140 shader layout");
    static_assert(sizeof(WaveParticleGPU) == 48,
        "WaveParticleGPU must match std430 shader layout");

    constexpr VkDeviceSize SSR_PARAMS_BUFFER_SIZE = 256;
}

// ============================================================
// Water buffer helpers
// ============================================================
bool VansWaterSystem::CreateWaterBuffer(
    VansVKBuffer& buffer, bool& created, VkDeviceSize size, VkBufferUsageFlags usage)
{
    if (m_Device == nullptr)
        return false;

    VkDevice logicDevice = m_Device->GetLogicDevice();
    created = buffer.CreatVulkanBuffer(logicDevice,
        size,
        VK_FORMAT_UNDEFINED,
        usage,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!created)
        VANS_LOG_ERROR("[VansWaterSystem] water buffer create failed, size=" << size);
    return created;
}

void VansWaterSystem::DestroyWaterBuffer(VansVKBuffer& buffer, bool& created, VkDevice logicDevice)
{
    if (!created)
        return;
    buffer.DestroyVulkanBuffer(logicDevice);
    created = false;
}

VkBuffer VansWaterSystem::GetNativeBuffer(const VansVKBuffer& buffer, bool created)
{
    return created ? buffer.GetNativeBuffer() : VK_NULL_HANDLE;
}

// ============================================================
// Initialize
// ============================================================
void VansWaterSystem::Initialize(VansVKDevice* device,
                                  uint32_t renderWidth,
                                  uint32_t renderHeight)
{
    m_Device       = device;
    m_RenderWidth  = renderWidth;
    m_RenderHeight = renderHeight;

    // V2 fixed 2:1 geometry clipmap.
    VansWaterGeometryConfig geometryConfig;
    if (m_WaterMaterial)
        geometryConfig = m_WaterMaterial->m_Config.m_Geometry;
    m_GeometryClipmap = new VansWaterGeometryClipmap();
    if (!m_GeometryClipmap->Initialize(device, geometryConfig))
        VANS_LOG_ERROR("[VansWaterSystem] geometry clipmap initialization failed");

    // 閳光偓閳光偓 2. 缂傛牞鐦ч惈鈧懝鎻掓珤 閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓
    auto*       cfg         = VansConfigration::GetInstance();
    std::string projectRoot = cfg->GetProjectRootPath();
    VkDevice    logicDev    = device->GetLogicDevice();

    auto& shaderManager = VansShaderManager::Get();
    m_WaterGBufferShader = shaderManager.FindGraphicsShader("WaterGBuffer");
    // 瀵偓閸氼垱绻佹惔锔界ゴ鐠?+ 濞ｅ崬瀹抽崘娆忓弳閿涙艾鍩勯悽銊у缁斿鎸夐棃銏＄箒鎼达妇绱﹂崘韫箽鐠?CDLOD 婢舵艾鐪伴柆顔藉皡妞ゅ搫绨?
    // depthTest: TRUE, depthWrite: TRUE, compareOp: LESS閿涘牐绻庢径?patch 闁喗灏呮潻婊冾槱 patch閿?
    
    // CDLOD 婵夘偄鍘栧Ο鈥崇础閿涘牏鏁撴禍褏骞嗘晶鍐跨礆

    // Wave compute shader閿涘湹-01: Texture2DArray + SSBO + Nyquist閿?
    m_WaveSimShader = shaderManager.FindComputeShader("WaterWave");
    m_WaveParticleShader = shaderManager.FindComputeShader("WaterWaveParticle");
    m_FlowMapShader = shaderManager.FindComputeShader("WaterFlowMap");
    m_WaterSSRShader = shaderManager.FindComputeShader("WaterSSR");
    m_WaterRefractionShader = shaderManager.FindComputeShader("WaterRefraction");
    m_WaterCausticsShader = shaderManager.FindComputeShader("WaterCaustics");
    m_WaterThicknessShader = shaderManager.FindComputeShader("WaterThickness");
    m_WaterVolumeShader = shaderManager.FindComputeShader("WaterVolume");
    m_WaterVolumeFilterShader = shaderManager.FindComputeShader("WaterVolumeFilter");
    m_WaterCompositeShader = shaderManager.FindGraphicsShader("WaterComposite");

    if (!m_WaterGBufferShader || !m_WaterCompositeShader || !m_WaveSimShader ||
        !m_WaveParticleShader || !m_FlowMapShader ||
        !m_WaterSSRShader || !m_WaterRefractionShader || !m_WaterCausticsShader ||
        !m_WaterThicknessShader || !m_WaterVolumeShader || !m_WaterVolumeFilterShader)
    {
        VANS_LOG_ERROR("[VansWaterSystem] One or more managed water shaders are unavailable");
    }

    // 閳光偓閳光偓 3. 閸掓稑缂?WaterGBufferParams UBO 閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓
    CreateWaterBuffer(m_GBufParamsBuffer, m_GBufParamsBufferCreated,
        sizeof(WaterGBufferParamsGPU),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    VansWaterConfig initialConfig;
    if (m_WaterMaterial)
        initialConfig = m_WaterMaterial->m_Config;
    initialConfig.Validate();

    WaterGBufferParamsGPU gbufParams = {};
    gbufParams.VPMatrix       = glm::mat4(1.0f);
    gbufParams.ViewMatrix     = glm::mat4(1.0f);
    gbufParams.cameraPosition = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    gbufParams.geometryParams = glm::ivec4(
        geometryConfig.m_LodCount, geometryConfig.m_MeshDim,
        initialConfig.m_Spectrum.m_CascadeCount, int(initialConfig.m_Spectrum.m_Mode));
    gbufParams.geometryScale = glm::vec4(geometryConfig.m_BasePatchSize, geometryConfig.m_MorphStartRatio, 1.0f, 2.0f);
    gbufParams.spectrumScale = glm::vec4(
        initialConfig.m_Spectrum.m_BaseCoverage,
        initialConfig.m_Spectrum.m_CascadeScale, 0.0f, 1.0f);
    gbufParams.windAndChop = glm::vec4(
        initialConfig.m_Spectrum.m_WindDirection,
        initialConfig.m_Spectrum.m_WindSpeed,
        initialConfig.m_Spectrum.m_Choppiness);
    gbufParams.simulationParams = glm::ivec4(
        initialConfig.m_Spectrum.m_GerstnerWaveCount,
        initialConfig.m_WaveParticle.m_ParticleCount,
        initialConfig.m_WaveParticle.m_OctaveCount,
        0);
    gbufParams.waveParticleParams0 = glm::vec4(
        initialConfig.m_WaveParticle.m_DomainSize,
        initialConfig.m_WaveParticle.m_Amplitude,
        initialConfig.m_WaveParticle.m_MinRadius,
        initialConfig.m_WaveParticle.m_MaxRadius);
    gbufParams.waveParticleParams1 = glm::vec4(
        initialConfig.m_WaveParticle.m_PhaseVelocity,
        initialConfig.m_WaveParticle.m_Damping,
        initialConfig.m_WaveParticle.m_DirectionSpread,
        float(initialConfig.m_WaveParticle.m_Profile));
    gbufParams.waveParticleParams2 = glm::vec4(
        initialConfig.m_WaveParticle.m_Lacunarity,
        initialConfig.m_WaveParticle.m_Persistence,
        initialConfig.m_WaveParticle.m_RadiusFalloff,
        initialConfig.m_WaveParticle.m_ProfileSharpness);
    gbufParams.waveParticleParams3 = glm::vec4(
        initialConfig.m_WaveParticle.m_FoamThreshold,
        initialConfig.m_WaveParticle.m_FoamSoftness,
        initialConfig.m_WaveParticle.m_Lifetime,
        float(initialConfig.m_WaveParticle.m_RandomSeed & 0xffffu));
    gbufParams.flowMapWorld = glm::vec4(
        initialConfig.m_FlowMap.m_WorldOrigin,
        initialConfig.m_FlowMap.m_WorldSize);
    gbufParams.flowMapParams = glm::vec4(
        initialConfig.m_FlowMap.m_Enabled ? 1.0f : 0.0f,
        initialConfig.m_FlowMap.m_Strength,
        initialConfig.m_FlowMap.m_Speed,
        initialConfig.m_FlowMap.m_PhaseLength);
    gbufParams.flowMapFallback = glm::vec4(
        initialConfig.m_FlowMap.m_FallbackDirection,
        initialConfig.m_FlowMap.m_NoiseAmount,
        0.0f);
    gbufParams.surfaceOptics = glm::vec4(
        initialConfig.m_Medium.m_WaterRoughness,
        initialConfig.m_Medium.m_IOR,
        ComputeWaterF0(initialConfig.m_Medium.m_IOR),
        initialConfig.m_SpecularIntensity);
    gbufParams.scatteringCoeff = glm::vec4(initialConfig.m_Medium.m_ScatteringCoeff, 1.0f);
    gbufParams.absorptionCoeff = glm::vec4(initialConfig.m_Medium.m_AbsorptionCoeff, 1.0f);
    m_GBufParamsCache = gbufParams;
    if (m_GBufParamsBufferCreated)
        m_GBufParamsBuffer.SetBufferData(&m_GBufParamsCache, 0, sizeof(WaterGBufferParamsGPU));

    // Macro displacement and derivative cascade arrays.
    // CLAMP_TO_EDGE閿涙俺鍒涢崶鎹愵洬閻?snappedOrigin 鍗?lodScale/2 閻ㄥ嫪绗橀悾宀冨瘱閸ヨ揪绱?
    // 鏉堝湱鏅径鏍畱 Patch 閻?CDLOD 鐠烘繄顬囬悳顖滃閺夌喍绻氱拠浣风瑝娴兼岸鍣伴弽宄板煂閿涘瓔LAMP 娴ｆ粈璐熺€瑰鍙忕純?
    m_WaveDisplacementImage.CreateVulkanImage(
        logicDev,
        { WAVE_TEXTURE_SIZE, WAVE_TEXTURE_SIZE, 1 },
        VK_FORMAT_R16G16B16A16_SFLOAT,
        1, VansWaterConfig::MAX_SPECTRUM_CASCADES,
        VK_IMAGE_TYPE_2D,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_SAMPLE_COUNT_1_BIT,
        false, false, true,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    // Surface derivative map: layers are paired as dPdx/dPdz per cascade.
    m_WaveDerivativeImage.CreateVulkanImage(
        logicDev,
        { WAVE_TEXTURE_SIZE, WAVE_TEXTURE_SIZE, 1 },
        VK_FORMAT_R16G16B16A16_SFLOAT,
        1, VansWaterConfig::MAX_SPECTRUM_CASCADES * 2,
        VK_IMAGE_TYPE_2D,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_SAMPLE_COUNT_1_BIT,
        false, false, true,
        VK_SAMPLER_ADDRESS_MODE_REPEAT);

    m_WaterFFT = new VansWaterFFT();
    if (!m_WaterFFT->Initialize(device, projectRoot, &m_WaveDisplacementImage, &m_WaveDerivativeImage))
    {
        VANS_LOG_WARN("[VansWaterSystem] FFT initialize failed; FFT mode will fall back to Gerstner");
        delete m_WaterFFT;
        m_WaterFFT = nullptr;
    }

    m_FlowMapImage.CreateVulkanImage(
        logicDev,
        { VansWaterConfig::FLOW_MAP_TEXTURE_SIZE, VansWaterConfig::FLOW_MAP_TEXTURE_SIZE, 1 },
        VK_FORMAT_R16G16B16A16_SFLOAT,
        1, 1,
        VK_IMAGE_TYPE_2D,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_SAMPLE_COUNT_1_BIT,
        false, false, true,
        VK_SAMPLER_ADDRESS_MODE_REPEAT);

    // 閳光偓閳光偓 5. 閸掓稑缂撳缈犵秼閺佸牊鐏夌拹鏉戞禈閿涘湑LAMP_TO_EDGE 闂冨弶顒涙潏鍦喘楠炴娊鎽垫导顏勫閿涘鏀㈤埞鈧?
    auto createEffectImage = [&](VansVKImage& image)
    {
        image.CreateVulkanImage(
            logicDev,
            { m_RenderWidth, m_RenderHeight, 1 },
            VK_FORMAT_R16G16B16A16_SFLOAT,
            1, 1,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_SAMPLE_COUNT_1_BIT,
            false, false, true,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);  // 鐏炲繐绠风粚娲？閺佸牊鐏夌拹鏉戞禈娑撳秴绨?repeat
    };
    createEffectImage(m_WaterReflectionImage);
    createEffectImage(m_WaterRefractionImage);
    createEffectImage(m_WaterCausticsImage);
    // Thickness is declared as r16f in water_thickness.comp.  Keeping the
    // Vulkan image format identical to the storage-image declaration avoids
    // undefined format reinterpretation and saves 3 unused channels.
    m_WaterThicknessImage.CreateVulkanImage(
        logicDev,
        { m_RenderWidth, m_RenderHeight, 1 },
        VK_FORMAT_R16_SFLOAT,
        1, 1,
        VK_IMAGE_TYPE_2D,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_SAMPLE_COUNT_1_BIT,
        false, false, true,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    m_VolumeWidth = (std::max)(1u, uint32_t(std::ceil(float(m_RenderWidth) * initialConfig.m_Volume.m_ResolutionScale)));
    m_VolumeHeight = (std::max)(1u, uint32_t(std::ceil(float(m_RenderHeight) * initialConfig.m_Volume.m_ResolutionScale)));
    auto createVolumeImage = [&](VansVKImage& image, VkFormat format)
    {
        image.CreateVulkanImage(
            logicDev,
            { m_VolumeWidth, m_VolumeHeight, 1 },
            format,
            1, 1,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_SAMPLE_COUNT_1_BIT,
            false, false, true,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    };
    createVolumeImage(m_WaterVolumeRawColorImage, VK_FORMAT_R16G16B16A16_SFLOAT);
    createVolumeImage(m_WaterVolumeRawTransmittanceImage, VK_FORMAT_R16G16B16A16_SFLOAT);
    createVolumeImage(m_WaterVolumeRawDepthImage, VK_FORMAT_R16_SFLOAT);
    createVolumeImage(m_WaterVolumeColorImage, VK_FORMAT_R16G16B16A16_SFLOAT);
    createVolumeImage(m_WaterVolumeTransmittanceImage, VK_FORMAT_R16G16B16A16_SFLOAT);
    createVolumeImage(m_WaterVolumeDepthImage, VK_FORMAT_R16_SFLOAT);

    // PBRWater shared optics/composite UBO.
    CreateWaterBuffer(m_CompParamsBuffer, m_CompParamsBufferCreated,
        sizeof(PBRWaterParamsGPU),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    PBRWaterParamsGPU compParams = BuildPBRWaterParams(
        initialConfig,
        glm::vec3(0.0f, 10.0f, 0.0f),
        glm::mat4(1.0f),
        glm::mat4(1.0f),
        glm::vec3(0.35f, 1.0f, 0.25f),
        glm::vec3(1.0f));
    // 閽冩繆澹婂缈犵秼姒涙顓婚崣鍌涙殶閿涘牏澧块悶鍡樻煙閸氭埊绱扮痪銏犲帨閸氬憡鏁归張鈧箛顐礉閽冩繂鍘滅粚鍧椻偓蹇旀付濞ｆ唻绱?
    if (m_CompParamsBufferCreated)
        m_CompParamsBuffer.SetBufferData(&compParams, 0, sizeof(PBRWaterParamsGPU));

    // 閳光偓閳光偓 SSR Params UBO閿涘湹-12閿涘鏀㈤埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧?
    CreateWaterBuffer(m_SSRParamsBuffer, m_SSRParamsBufferCreated,
        SSR_PARAMS_BUFFER_SIZE,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    // 閳光偓閳光偓 Caustics Params UBO閿涘湹-14閿涘鏀㈤埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧?
    CreateWaterBuffer(m_CausticsParamsBuffer, m_CausticsParamsBufferCreated,
        sizeof(WaterCausticsParamsGPU),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    // 閳光偓閳光偓 W-16: Thickness Params UBO 閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓
    {
        CreateWaterBuffer(m_ThicknessParamsBuffer, m_ThicknessParamsBufferCreated,
            sizeof(ThicknessParamsGPU),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        ThicknessParamsGPU tp = { 15.0f, 0.8f, 0.0f, 0.0f };
        if (m_ThicknessParamsBufferCreated)
            m_ThicknessParamsBuffer.SetBufferData(&tp, 0, sizeof(ThicknessParamsGPU));
    }
    // 閸掓稑缂?Gerstner 濞?SSBO閿涘湹-04閿涘鏀㈤埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧?
    {
        VkDeviceSize ssboSize = MAX_WAVE_COUNT * sizeof(GerstnerWaveGPU);
        CreateWaterBuffer(m_WaveSSBO, m_WaveSSBOCreated,
            ssboSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

        VansWaterConfig initialConfig;
        if (m_WaterMaterial)
            initialConfig = m_WaterMaterial->m_Config;
        initialConfig.Validate();
        const auto& spectrum = initialConfig.m_Spectrum;
        std::vector<GerstnerWaveGPU> waves;
        AutoGenerateGerstnerWaves(waves, spectrum.m_GerstnerWaveCount,
            spectrum.m_WindDirection, spectrum.m_SwellAmplitude, spectrum.m_WindSpeed);
        const std::size_t activeCount = waves.size();
        waves.resize(MAX_WAVE_COUNT, GerstnerWaveGPU{});
        if (m_WaveSSBOCreated)
            m_WaveSSBO.SetBufferData(waves.data(), 0, waves.size() * sizeof(GerstnerWaveGPU));

        VANS_LOG("[VansWaterSystem] Wave SSBO: " << activeCount << " active waves, " << ssboSize << " bytes");
    }

    {
        VkDeviceSize ssboSize = VansWaterConfig::MAX_WAVE_PARTICLE_COUNT * sizeof(WaveParticleGPU);
        CreateWaterBuffer(m_WaveParticleSSBO, m_WaveParticleSSBOCreated,
            ssboSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

        std::vector<WaveParticleGPU> particles;
        AutoGenerateWaveParticles(particles, initialConfig);
        const std::size_t activeCount = particles.size();
        particles.resize(VansWaterConfig::MAX_WAVE_PARTICLE_COUNT, WaveParticleGPU{});
        if (m_WaveParticleSSBOCreated)
            m_WaveParticleSSBO.SetBufferData(
                particles.data(), 0, particles.size() * sizeof(WaveParticleGPU));

        VANS_LOG("[VansWaterSystem] WaveParticle SSBO: " << activeCount
            << " active particles, " << ssboSize << " bytes");
    }

    m_Initialized = true;
    VANS_LOG("[VansWaterSystem] Initialize: " << renderWidth << "x" << renderHeight
             << " waterLevel=" << m_WaterLevel
             << " meshDim=" << m_GeometryClipmap->GetMeshDim());
}

// ============================================================
// SetGlobalDescriptorSet 閳?閸?CreateGlobalDescriptorSet 娑斿鎮楃拫鍐暏
// ============================================================
void VansWaterSystem::SetGlobalDescriptorSet(
    VkDescriptorSetLayout globalLayout,
    VkDescriptorSet        globalSet)
{
    if (globalLayout == VK_NULL_HANDLE || globalSet == VK_NULL_HANDLE)
    {
        VANS_LOG_ERROR("[VansWaterSystem] SetGlobalDescriptorSet received null global descriptor set!");
        return;
    }
    m_GlobalLayout = globalLayout;
    m_GlobalSet    = globalSet;
    VANS_LOG("[VansWaterSystem] Global descriptor set updated (SetGlobalDescriptorSet).");
}

// ============================================================
// SetupDescriptors
// ============================================================
void VansWaterSystem::SetupDescriptors(
    VansRenderPassManager* renderPassManager,
    VkDescriptorSetLayout  globalLayout,
    VkDescriptorSet        globalSet,
    VansVKImage*           sceneHZBImage)
{
    if (!m_Initialized)
    {
        VANS_LOG_WARN("[VansWaterSystem] SetupDescriptors called before Initialize");
        return;
    }

    m_GlobalLayout = globalLayout;
    m_GlobalSet    = globalSet;
    auto* descMgr  = VansVKDescriptorManager::GetInstance();

    // 閳光偓閳光偓 Water GBuffer Pass descriptor set閿涘湯et 1閿涘鏀㈤埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧?
    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterGBuffer(
            m_GBufPassLayout, sets, 1);
        m_GBufPassSet = sets[0];
        descMgr->BeginDescriptorUpdate();

        // binding 0閿涙瓙aterGBufferParams UBO
        descMgr->WriteBufferDescriptor(
            m_GBufPassSet,
            WATER_GBUF_BINDING_PARAMS,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { GetNativeBuffer(m_GBufParamsBuffer, m_GBufParamsBufferCreated), 0, sizeof(WaterGBufferParamsGPU) } });

        // binding 1閿涙矮缍呯粔鏄忓垱閸?Texture2DArray閿涘湹-01閿?
        descMgr->WriteImageDescriptor(
            m_GBufPassSet,
            WATER_GBUF_BINDING_DISPLACEMENT,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { {
                m_WaveDisplacementImage.GetSampler(),
                m_WaveDisplacementImage.GetImageView(),
                VK_IMAGE_LAYOUT_GENERAL
            } });

        // binding 4: FFT derivative Texture2DArray 閳?vertex shader reads slope normal.
        descMgr->WriteImageDescriptor(
            m_GBufPassSet,
            WATER_GBUF_BINDING_DERIVATIVE,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { {
                m_WaveDerivativeImage.GetSampler(),
                m_WaveDerivativeImage.GetImageView(),
                VK_IMAGE_LAYOUT_GENERAL
            } });

        descMgr->WriteImageDescriptor(
            m_GBufPassSet,
            WATER_GBUF_BINDING_FLOW_MAP,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { {
                m_FlowMapImage.GetSampler(),
                m_FlowMapImage.GetImageView(),
                VK_IMAGE_LAYOUT_GENERAL
            } });

        descMgr->CommitDescriptorUpdates();
    }

    // 閳光偓閳光偓 Water Wave Compute descriptor set閿涘湯et 0閿涘鏀㈤埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧?
    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterWaveCompute(
            m_WaveSimLayout, sets, 1);
        m_WaveSimSet = sets[0];
        descMgr->BeginDescriptorUpdate();

        descMgr->WriteBufferDescriptor(
            m_WaveSimSet,
            WATER_WAVE_BINDING_PARAMS,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { GetNativeBuffer(m_GBufParamsBuffer, m_GBufParamsBufferCreated), 0, sizeof(WaterGBufferParamsGPU) } });
        descMgr->WriteImageDescriptor(
            m_WaveSimSet,
            WATER_WAVE_BINDING_DISPLACEMENT,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { {
                m_WaveDisplacementImage.GetSampler(),
                m_WaveDisplacementImage.GetImageView(),
                VK_IMAGE_LAYOUT_GENERAL
            } });
        // binding 2閿涙erstnerWave SSBO 鏉堟挸鍙?
        descMgr->WriteBufferDescriptor(
            m_WaveSimSet,
            WATER_WAVE_BINDING_WAVE_SSBO,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            { { GetNativeBuffer(m_WaveSSBO, m_WaveSSBOCreated), 0, MAX_WAVE_COUNT * sizeof(GerstnerWaveGPU) } });
        descMgr->WriteImageDescriptor(
            m_WaveSimSet,
            WATER_WAVE_BINDING_DERIVATIVE,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaveDerivativeImage.GetSampler(), m_WaveDerivativeImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });

        descMgr->CommitDescriptorUpdates();
    }

    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterWaveCompute(
            m_WaveParticleLayout, sets, 1);
        m_WaveParticleSet = sets[0];
        descMgr->BeginDescriptorUpdate();

        descMgr->WriteBufferDescriptor(
            m_WaveParticleSet,
            WATER_WAVE_BINDING_PARAMS,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { GetNativeBuffer(m_GBufParamsBuffer, m_GBufParamsBufferCreated), 0, sizeof(WaterGBufferParamsGPU) } });
        descMgr->WriteImageDescriptor(
            m_WaveParticleSet,
            WATER_WAVE_BINDING_DISPLACEMENT,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { {
                m_WaveDisplacementImage.GetSampler(),
                m_WaveDisplacementImage.GetImageView(),
                VK_IMAGE_LAYOUT_GENERAL
            } });
        descMgr->WriteBufferDescriptor(
            m_WaveParticleSet,
            WATER_WAVE_BINDING_WAVE_SSBO,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            { { GetNativeBuffer(m_WaveParticleSSBO, m_WaveParticleSSBOCreated), 0,
                VansWaterConfig::MAX_WAVE_PARTICLE_COUNT * sizeof(WaveParticleGPU) } });
        descMgr->WriteImageDescriptor(
            m_WaveParticleSet,
            WATER_WAVE_BINDING_DERIVATIVE,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaveDerivativeImage.GetSampler(), m_WaveDerivativeImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });

        descMgr->CommitDescriptorUpdates();
    }

    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterFlowMapCompute(
            m_FlowMapLayout, sets, 1);
        m_FlowMapSet = sets[0];
        descMgr->BeginDescriptorUpdate();

        descMgr->WriteBufferDescriptor(
            m_FlowMapSet,
            WATER_FLOW_BINDING_PARAMS,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { GetNativeBuffer(m_GBufParamsBuffer, m_GBufParamsBufferCreated), 0, sizeof(WaterGBufferParamsGPU) } });
        descMgr->WriteImageDescriptor(
            m_FlowMapSet,
            WATER_FLOW_BINDING_OUTPUT,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_FlowMapImage.GetSampler(), m_FlowMapImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });

        descMgr->CommitDescriptorUpdates();
    }

    // 閳光偓閳光偓 Water Composite Pass descriptor set閿涘湯et 1閿涘鏀㈤埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧?
    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterComposite(
            m_CompPassLayout, sets, 1);
        m_CompPassSet = sets[0];
        descMgr->BeginDescriptorUpdate();

        descMgr->WriteImageDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_GBUF_NORMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufNormal().GetSampler(), renderPassManager->GetWaterGBufNormal().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_GBUF_DEPTH,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufLinearDepth().GetSampler(), renderPassManager->GetWaterGBufLinearDepth().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteBufferDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_PARAMS,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { GetNativeBuffer(m_CompParamsBuffer, m_CompParamsBufferCreated), 0, sizeof(PBRWaterParamsGPU) } });
        descMgr->WriteImageDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_SCENE_GBUF2,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetGbuffer2().GetSampler(), renderPassManager->GetGbuffer2().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_REFLECTION,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterReflectionImage.GetSampler(), m_WaterReflectionImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteImageDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_REFRACTION_DATA,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterRefractionImage.GetSampler(), m_WaterRefractionImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteImageDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_CAUSTICS,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterCausticsImage.GetSampler(), m_WaterCausticsImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteImageDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_GBUF_SCATTER,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufScatter().GetSampler(), renderPassManager->GetWaterGBufScatter().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_GBUF_ABSORPTION,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufAbsorption().GetSampler(), renderPassManager->GetWaterGBufAbsorption().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_VOLUME_COLOR,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterVolumeColorImage.GetSampler(), m_WaterVolumeColorImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteImageDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_VOLUME_TRANSMITTANCE,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterVolumeTransmittanceImage.GetSampler(), m_WaterVolumeTransmittanceImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteImageDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_VOLUME_DEPTH,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterVolumeDepthImage.GetSampler(), m_WaterVolumeDepthImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteImageDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_SCENE_COLOR,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetOpaqueSceneColor().GetSampler(), renderPassManager->GetOpaqueSceneColor().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->CommitDescriptorUpdates();
    }

    // 閳光偓閳光偓 Water SSR Compute descriptor set閿涘湯et 0, W-12閿涘鏀㈤埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧?
    // 瀵ゆ儼绻滈崚?HZB 閸欘垳鏁ら弮璺哄灡瀵ょ尨绱欑憴?EnsureSSRDescriptorSet閿?

    // 閳光偓閳光偓 Water Refraction Compute descriptor set閿涘湯et 0閿涘鏀㈤埞鈧埞鈧埞鈧埞鈧埞鈧?
    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterRefractionCompute(
            m_RefractionLayout, sets, 1);
        m_RefractionSet = sets[0];
        descMgr->BeginDescriptorUpdate();

        descMgr->WriteImageDescriptor(
            m_RefractionSet, WATER_REFRACTION_BINDING_GBUF_NORMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufNormal().GetSampler(), renderPassManager->GetWaterGBufNormal().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(
            m_RefractionSet, WATER_REFRACTION_BINDING_GBUF_DEPTH,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufLinearDepth().GetSampler(), renderPassManager->GetWaterGBufLinearDepth().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(
            m_RefractionSet, WATER_REFRACTION_BINDING_SCENE_GBUF2,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetGbuffer2().GetSampler(), renderPassManager->GetGbuffer2().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(
            m_RefractionSet, WATER_REFRACTION_BINDING_THICKNESS,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterThicknessImage.GetSampler(), m_WaterThicknessImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteBufferDescriptor(
            m_RefractionSet, WATER_REFRACTION_BINDING_PARAMS,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { GetNativeBuffer(m_CompParamsBuffer, m_CompParamsBufferCreated), 0, sizeof(PBRWaterParamsGPU) } });
        descMgr->WriteImageDescriptor(
            m_RefractionSet, WATER_REFRACTION_BINDING_REFRACTION_DATA_OUT,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaterRefractionImage.GetSampler(), m_WaterRefractionImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });

        descMgr->CommitDescriptorUpdates();
    }

    // 閳光偓閳光偓 Water Caustics Compute descriptor set閿涘湯et 0, W-14閿涘鏀㈤埞鈧埞鈧?
    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterCausticsCompute(
            m_CausticsLayout, sets, 1);
        m_CausticsSet = sets[0];
        descMgr->BeginDescriptorUpdate();

        descMgr->WriteImageDescriptor(
            m_CausticsSet, WATER_CAUSTICS_BINDING_GBUF_NORMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufNormal().GetSampler(), renderPassManager->GetWaterGBufNormal().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(
            m_CausticsSet, WATER_CAUSTICS_BINDING_GBUF_DEPTH,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufLinearDepth().GetSampler(), renderPassManager->GetWaterGBufLinearDepth().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(
            m_CausticsSet, WATER_CAUSTICS_BINDING_THICKNESS,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterThicknessImage.GetSampler(), m_WaterThicknessImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteBufferDescriptor(
            m_CausticsSet, WATER_CAUSTICS_BINDING_PARAMS,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { GetNativeBuffer(m_CausticsParamsBuffer, m_CausticsParamsBufferCreated), 0, sizeof(WaterCausticsParamsGPU) } });
        descMgr->WriteImageDescriptor(
            m_CausticsSet, WATER_CAUSTICS_BINDING_CAUSTICS_OUT,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaterCausticsImage.GetSampler(), m_WaterCausticsImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });

        descMgr->CommitDescriptorUpdates();
    }

    // 閳光偓閳光偓 W-16: Water Thickness Compute descriptor set 閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓
    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterThicknessCompute(
            m_ThicknessLayout, sets, 1);
        m_ThicknessSet = sets[0];
        descMgr->BeginDescriptorUpdate();

        descMgr->WriteImageDescriptor(
            m_ThicknessSet, WATER_THICKNESS_BINDING_GBUF_DEPTH,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufLinearDepth().GetSampler(),
                renderPassManager->GetWaterGBufLinearDepth().GetImageView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(
            m_ThicknessSet, WATER_THICKNESS_BINDING_SCENE_GBUF2,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetGbuffer2().GetSampler(),
                renderPassManager->GetGbuffer2().GetImageView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteBufferDescriptor(
            m_ThicknessSet, WATER_THICKNESS_BINDING_PARAMS,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { GetNativeBuffer(m_ThicknessParamsBuffer, m_ThicknessParamsBufferCreated), 0, sizeof(float) * 4 } });
        descMgr->WriteImageDescriptor(
            m_ThicknessSet, WATER_THICKNESS_BINDING_THICKNESS_OUT,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaterThicknessImage.GetSampler(),
                m_WaterThicknessImage.GetImageView(),
                VK_IMAGE_LAYOUT_GENERAL } });

        descMgr->CommitDescriptorUpdates();
    }
    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterVolumeCompute(
            m_VolumeLayout, sets, 1);
        m_VolumeSet = sets[0];
        descMgr->BeginDescriptorUpdate();
        descMgr->WriteImageDescriptor(m_VolumeSet, WATER_VOLUME_BINDING_GBUF_NORMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufNormal().GetSampler(), renderPassManager->GetWaterGBufNormal().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(m_VolumeSet, WATER_VOLUME_BINDING_GBUF_DEPTH, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufLinearDepth().GetSampler(), renderPassManager->GetWaterGBufLinearDepth().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(m_VolumeSet, WATER_VOLUME_BINDING_GBUF_SCATTER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufScatter().GetSampler(), renderPassManager->GetWaterGBufScatter().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(m_VolumeSet, WATER_VOLUME_BINDING_GBUF_ABSORPTION, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufAbsorption().GetSampler(), renderPassManager->GetWaterGBufAbsorption().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(m_VolumeSet, WATER_VOLUME_BINDING_THICKNESS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterThicknessImage.GetSampler(), m_WaterThicknessImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteImageDescriptor(m_VolumeSet, WATER_VOLUME_BINDING_SCENE_GBUF2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetGbuffer2().GetSampler(), renderPassManager->GetGbuffer2().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(m_VolumeSet, WATER_VOLUME_BINDING_REFRACTION_DATA, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterRefractionImage.GetSampler(), m_WaterRefractionImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteBufferDescriptor(m_VolumeSet, WATER_VOLUME_BINDING_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { GetNativeBuffer(m_CompParamsBuffer, m_CompParamsBufferCreated), 0, sizeof(PBRWaterParamsGPU) } });
        descMgr->WriteImageDescriptor(m_VolumeSet, WATER_VOLUME_BINDING_COLOR_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaterVolumeRawColorImage.GetSampler(), m_WaterVolumeRawColorImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteImageDescriptor(m_VolumeSet, WATER_VOLUME_BINDING_TRANSMITTANCE_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaterVolumeRawTransmittanceImage.GetSampler(), m_WaterVolumeRawTransmittanceImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteImageDescriptor(m_VolumeSet, WATER_VOLUME_BINDING_DEPTH_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaterVolumeRawDepthImage.GetSampler(), m_WaterVolumeRawDepthImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->CommitDescriptorUpdates();
    }

    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterVolumeFilterCompute(
            m_VolumeFilterLayout, sets, 1);
        m_VolumeFilterSet = sets[0];
        descMgr->BeginDescriptorUpdate();
        descMgr->WriteImageDescriptor(m_VolumeFilterSet, WATER_VOLUME_FILTER_BINDING_GBUF_DEPTH, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufLinearDepth().GetSampler(), renderPassManager->GetWaterGBufLinearDepth().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(m_VolumeFilterSet, WATER_VOLUME_FILTER_BINDING_RAW_COLOR, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterVolumeRawColorImage.GetSampler(), m_WaterVolumeRawColorImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteImageDescriptor(m_VolumeFilterSet, WATER_VOLUME_FILTER_BINDING_RAW_TRANSMITTANCE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterVolumeRawTransmittanceImage.GetSampler(), m_WaterVolumeRawTransmittanceImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteImageDescriptor(m_VolumeFilterSet, WATER_VOLUME_FILTER_BINDING_RAW_DEPTH, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterVolumeRawDepthImage.GetSampler(), m_WaterVolumeRawDepthImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteBufferDescriptor(m_VolumeFilterSet, WATER_VOLUME_FILTER_BINDING_PARAMS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { GetNativeBuffer(m_CompParamsBuffer, m_CompParamsBufferCreated), 0, sizeof(PBRWaterParamsGPU) } });
        descMgr->WriteImageDescriptor(m_VolumeFilterSet, WATER_VOLUME_FILTER_BINDING_COLOR_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaterVolumeColorImage.GetSampler(), m_WaterVolumeColorImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteImageDescriptor(m_VolumeFilterSet, WATER_VOLUME_FILTER_BINDING_TRANSMITTANCE_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaterVolumeTransmittanceImage.GetSampler(), m_WaterVolumeTransmittanceImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteImageDescriptor(m_VolumeFilterSet, WATER_VOLUME_FILTER_BINDING_DEPTH_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaterVolumeDepthImage.GetSampler(), m_WaterVolumeDepthImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->CommitDescriptorUpdates();
    }
    m_DescriptorsReady = true;
    VANS_LOG("[VansWaterSystem] SetupDescriptors completed");
}

// ============================================================
// Shutdown
// ============================================================
void VansWaterSystem::Shutdown()
{
    if (!m_Initialized)
        return;

    VkDevice dev = m_Device->GetLogicDevice();
    auto*  descMgr = VansVKDescriptorManager::GetInstance();

    // Geometry clipmap owns immutable patch mesh buffers.
    if (m_GeometryClipmap)
    {
        m_GeometryClipmap->Shutdown(dev);
        delete m_GeometryClipmap;
        m_GeometryClipmap = nullptr;
    }
    if (m_WaterFFT)
    {
        m_WaterFFT->Shutdown(dev);
        delete m_WaterFFT;
        m_WaterFFT = nullptr;
    }

    if (m_GBufPassLayout != VK_NULL_HANDLE)   { descMgr->DestroyDescriptorSetLayout(m_GBufPassLayout); m_GBufPassLayout = VK_NULL_HANDLE; }
    if (m_CompPassLayout != VK_NULL_HANDLE)   { descMgr->DestroyDescriptorSetLayout(m_CompPassLayout); m_CompPassLayout = VK_NULL_HANDLE; }
    if (m_WaveSimLayout != VK_NULL_HANDLE)    { descMgr->DestroyDescriptorSetLayout(m_WaveSimLayout);  m_WaveSimLayout  = VK_NULL_HANDLE; }
    if (m_WaveParticleLayout != VK_NULL_HANDLE) { descMgr->DestroyDescriptorSetLayout(m_WaveParticleLayout); m_WaveParticleLayout = VK_NULL_HANDLE; }
    if (m_FlowMapLayout != VK_NULL_HANDLE)    { descMgr->DestroyDescriptorSetLayout(m_FlowMapLayout);  m_FlowMapLayout  = VK_NULL_HANDLE; }
    if (m_SSRLayout != VK_NULL_HANDLE)        { descMgr->DestroyDescriptorSetLayout(m_SSRLayout);      m_SSRLayout      = VK_NULL_HANDLE; }
    if (m_RefractionLayout != VK_NULL_HANDLE) { descMgr->DestroyDescriptorSetLayout(m_RefractionLayout); m_RefractionLayout = VK_NULL_HANDLE; }
    if (m_ThicknessLayout != VK_NULL_HANDLE)   { descMgr->DestroyDescriptorSetLayout(m_ThicknessLayout); m_ThicknessLayout = VK_NULL_HANDLE; }
    if (m_VolumeLayout != VK_NULL_HANDLE) { descMgr->DestroyDescriptorSetLayout(m_VolumeLayout); m_VolumeLayout = VK_NULL_HANDLE; }
    if (m_VolumeFilterLayout != VK_NULL_HANDLE) { descMgr->DestroyDescriptorSetLayout(m_VolumeFilterLayout); m_VolumeFilterLayout = VK_NULL_HANDLE; }
    if (m_CausticsLayout != VK_NULL_HANDLE)  { descMgr->DestroyDescriptorSetLayout(m_CausticsLayout);  m_CausticsLayout  = VK_NULL_HANDLE; }

    DestroyWaterBuffer(m_GBufParamsBuffer, m_GBufParamsBufferCreated, dev);
    DestroyWaterBuffer(m_CompParamsBuffer, m_CompParamsBufferCreated, dev);
    DestroyWaterBuffer(m_SSRParamsBuffer, m_SSRParamsBufferCreated, dev);
    DestroyWaterBuffer(m_CausticsParamsBuffer, m_CausticsParamsBufferCreated, dev);
    DestroyWaterBuffer(m_ThicknessParamsBuffer, m_ThicknessParamsBufferCreated, dev);
    DestroyWaterBuffer(m_WaveSSBO, m_WaveSSBOCreated, dev);
    DestroyWaterBuffer(m_WaveParticleSSBO, m_WaveParticleSSBOCreated, dev);
    m_GBufParamsCache = {};

    m_WaveDisplacementImage.DestroyVulkanImage(dev);
    m_WaveDisplacementReady = false;
    m_WaveDerivativeImage.DestroyVulkanImage(dev);
    m_WaveDerivativeReady = false;
    m_FlowMapImage.DestroyVulkanImage(dev);
    m_FlowMapReady = false;
    m_WaterReflectionImage.DestroyVulkanImage(dev);
    m_WaterRefractionImage.DestroyVulkanImage(dev);
    m_WaterCausticsImage.DestroyVulkanImage(dev);
    m_WaterThicknessImage.DestroyVulkanImage(dev);
    m_WaterVolumeRawColorImage.DestroyVulkanImage(dev);
    m_WaterVolumeRawTransmittanceImage.DestroyVulkanImage(dev);
    m_WaterVolumeRawDepthImage.DestroyVulkanImage(dev);
    m_WaterVolumeColorImage.DestroyVulkanImage(dev);
    m_WaterVolumeTransmittanceImage.DestroyVulkanImage(dev);
    m_WaterVolumeDepthImage.DestroyVulkanImage(dev);
    m_ReflectionOutputReady = false;
    m_RefractionOutputReady = false;
    m_CausticsOutputReady = false;
    m_ThicknessOutputReady = false;
    m_VolumeOutputReady = false;
    m_VolumeFilterOutputReady = false;

    // Managed shader programs outlive scene-local water resources.
    m_WaterGBufferShader = nullptr;
    m_WaterCompositeShader = nullptr;
    m_WaveSimShader = nullptr;
    m_WaveParticleShader = nullptr;
    m_FlowMapShader = nullptr;
    m_WaterSSRShader = nullptr;
    m_WaterRefractionShader = nullptr;
    m_WaterCausticsShader = nullptr;
    m_WaterThicknessShader = nullptr;
    m_WaterVolumeShader = nullptr;
    m_WaterVolumeFilterShader = nullptr;

    m_Initialized      = false;
    m_DescriptorsReady = false;
    m_WaterMaterial    = nullptr;
    m_Device           = nullptr;
    VANS_LOG("[VansWaterSystem] Shutdown");
}

// ============================================================
// UpdateWaveSSBO 閳?鏉╂劘顢戦弮鍫曞櫢閺傛壆鏁撻幋鎰皾閸掑棝鍣洪獮鏈电瑐娴肩姴鍩?SSBO閿涘湹-04閿?
// 瑜版挾绱潏鎴濇珤娣囶喗鏁煎▔銏犲棘閺佸府绱欏▔銏℃殶閵嗕線顥撻柅鐔粹偓浣圭Ь濞搭亜绠欐惔锔剧搼閿涘妞傜拫鍐暏閵?
// ============================================================
void VansWaterSystem::UpdateWaveSSBO()
{
    if (!m_Initialized || !m_WaveSSBOCreated)
        return;
    if (!m_WaterMaterial)
        return;

    VansWaterConfig config = m_WaterMaterial->m_Config;
    config.Validate();
    const auto& spectrum = config.m_Spectrum;
    std::vector<GerstnerWaveGPU> waves;
    AutoGenerateGerstnerWaves(waves, spectrum.m_GerstnerWaveCount,
        spectrum.m_WindDirection, spectrum.m_SwellAmplitude, spectrum.m_WindSpeed);
    const std::size_t activeCount = waves.size();
    waves.resize(MAX_WAVE_COUNT, GerstnerWaveGPU{}); // clear stale SSBO tail
    m_WaveSSBO.SetBufferData(waves.data(), 0, waves.size() * sizeof(GerstnerWaveGPU));
    VANS_LOG("[VansWaterSystem] Gerstner spectrum regenerated: " << activeCount << " active waves");
}

void VansWaterSystem::UpdateWaveParticleSSBO()
{
    if (!m_Initialized || !m_WaveParticleSSBOCreated || !m_WaterMaterial)
        return;

    VansWaterConfig config = m_WaterMaterial->m_Config;
    config.Validate();
    std::vector<WaveParticleGPU> particles;
    AutoGenerateWaveParticles(particles, config);
    const std::size_t activeCount = particles.size();
    particles.resize(VansWaterConfig::MAX_WAVE_PARTICLE_COUNT, WaveParticleGPU{});
    m_WaveParticleSSBO.SetBufferData(
        particles.data(), 0, particles.size() * sizeof(WaveParticleGPU));
    VANS_LOG("[VansWaterSystem] WaveParticle spectrum regenerated: " << activeCount << " active particles");
}

// ============================================================
// Update 閳?濮ｅ繐鎶?CPU 缁旑垳濮搁幀浣规纯閺?
// ============================================================
void VansWaterSystem::Update(float deltaTime, const glm::vec3& cameraPos,
                             const glm::mat4& viewMatrix, const glm::mat4& vpMatrix,
                             const glm::vec3& mainLightDir,
                             const glm::vec3& mainLightColor)
{
    m_Time += deltaTime;
    VansWaterConfig config;
    if (m_WaterMaterial)
    {
        config = m_WaterMaterial->m_Config;
        config.Validate();
        m_WaterMaterial->m_Config = config;
    }
    const auto& geometry = config.m_Geometry;
    const auto& spectrum = config.m_Spectrum;
    const auto& particle = config.m_WaveParticle;
    const auto& flowMap = config.m_FlowMap;
    m_WaterLevel = config.m_WaterLevel;

    const float windLength = spectrum.m_WindSpeed * spectrum.m_WindSpeed / 9.81f;
    const float spectralFourSigma = 5.5f * windLength
        * std::sqrt((std::max)(spectrum.m_SpectrumAmplitude, 0.0f));
    const float particleBound = spectrum.m_Mode == VansWaveMode::WaveParticle
        ? particle.m_Amplitude * 3.0f : 0.0f;
    const float displacementBound = spectrum.m_SwellAmplitude * 2.0f
        + spectralFourSigma + particleBound;
    if (m_GeometryClipmap)
    {
        m_GeometryClipmap->ApplyConfig(geometry);
        m_GeometryClipmap->GeneratePatches(cameraPos);
        m_GeometryClipmap->FrustumCullPatches(vpMatrix, m_WaterLevel, displacementBound);
    }

    WaterGBufferParamsGPU gbufParams = {};
    gbufParams.VPMatrix = vpMatrix;
    gbufParams.ViewMatrix = viewMatrix;
    gbufParams.cameraPosition = glm::vec4(cameraPos, 1.0f);
    gbufParams.geometryParams = glm::ivec4(
        geometry.m_LodCount, geometry.m_MeshDim, spectrum.m_CascadeCount, int(spectrum.m_Mode));
    gbufParams.geometryScale = glm::vec4(
        geometry.m_BasePatchSize, geometry.m_MorphStartRatio, displacementBound,
        VansWaterConfig::GEOMETRY_LOD_RATIO);
    gbufParams.spectrumScale = glm::vec4(
        spectrum.m_BaseCoverage, spectrum.m_CascadeScale, m_Time, 1.0f);
    gbufParams.windAndChop = glm::vec4(
        spectrum.m_WindDirection, spectrum.m_WindSpeed, spectrum.m_Choppiness);
    gbufParams.simulationParams = glm::ivec4(
        spectrum.m_GerstnerWaveCount, particle.m_ParticleCount,
        particle.m_OctaveCount, 0);
    gbufParams.waveParticleParams0 = glm::vec4(
        particle.m_DomainSize, particle.m_Amplitude,
        particle.m_MinRadius, particle.m_MaxRadius);
    gbufParams.waveParticleParams1 = glm::vec4(
        particle.m_PhaseVelocity, particle.m_Damping,
        particle.m_DirectionSpread, float(particle.m_Profile));
    gbufParams.waveParticleParams2 = glm::vec4(
        particle.m_Lacunarity, particle.m_Persistence,
        particle.m_RadiusFalloff, particle.m_ProfileSharpness);
    gbufParams.waveParticleParams3 = glm::vec4(
        particle.m_FoamThreshold, particle.m_FoamSoftness,
        particle.m_Lifetime, float(particle.m_RandomSeed & 0xffffu));
    gbufParams.flowMapWorld = glm::vec4(flowMap.m_WorldOrigin, flowMap.m_WorldSize);
    gbufParams.flowMapParams = glm::vec4(
        flowMap.m_Enabled ? 1.0f : 0.0f,
        flowMap.m_Strength,
        flowMap.m_Speed,
        flowMap.m_PhaseLength);
    gbufParams.flowMapFallback = glm::vec4(
        flowMap.m_FallbackDirection,
        flowMap.m_NoiseAmount,
        0.0f);
    gbufParams.surfaceOptics = glm::vec4(
        config.m_Medium.m_WaterRoughness,
        config.m_Medium.m_IOR,
        ComputeWaterF0(config.m_Medium.m_IOR),
        config.m_SpecularIntensity);
    gbufParams.scatteringCoeff = glm::vec4(config.m_Medium.m_ScatteringCoeff, 1.0f);
    gbufParams.absorptionCoeff = glm::vec4(config.m_Medium.m_AbsorptionCoeff, 1.0f);

    if (m_WaterFFT)
    {
        VansWaterFFT::Params fp;
        fp.resolution = VansWaterFFT::FFT_RESOLUTION;
        fp.cascadeCount = std::uint32_t(spectrum.m_CascadeCount);
        fp.windDirection = spectrum.m_WindDirection;
        fp.windSpeed = spectrum.m_WindSpeed;
        fp.spectrumAmplitude = spectrum.m_SpectrumAmplitude;
        fp.choppiness = spectrum.m_Choppiness;
        fp.smallWaveDamping = spectrum.m_SmallWaveDamping;
        fp.windDependency = spectrum.m_WindDependency;
        fp.depth = spectrum.m_Depth;
        fp.repeatPeriod = spectrum.m_RepeatPeriod;
        fp.randomSeed = spectrum.m_RandomSeed;
        fp.capillaryCoefficient = 0.000074f;
        float previousCoverage = spectrum.m_MinWavelength;
        for (uint32_t cascade = 0; cascade < VansWaterFFT::MAX_CASCADE_COUNT; ++cascade)
        {
            const float coverage = spectrum.m_BaseCoverage
                * std::pow(spectrum.m_CascadeScale, float(cascade));
            fp.domainCoverage[cascade] = coverage;
            fp.minWavelength[cascade] = cascade == 0
                ? spectrum.m_MinWavelength : previousCoverage;
            fp.maxWavelength[cascade] = coverage;
            previousCoverage = coverage;
        }
        m_WaterFFT->SetParams(fp);
    }

    m_GBufParamsCache = gbufParams;
    if (m_Device != nullptr && m_GBufParamsBufferCreated)
        m_GBufParamsBuffer.SetBufferData(&m_GBufParamsCache, 0, sizeof(WaterGBufferParamsGPU));

    // 濮ｅ繐鎶氶崗銊╁櫤閺囧瓨鏌?composite UBO閿涙矮绮?VansWaterMaterial 鐠囪褰囬幍鈧張澶夌矙鐠愩劌寮弫?+
    // 閻╁憡婧€娴ｅ秶鐤?+ invViewProj + 娑撹鍘滈弬鐟版倻閿涘牏鈥樻穱?Editor 娣囶喗鏁肩€圭偞妞傞悽鐔告櫏閿?
    if (m_Device != nullptr && m_CompParamsBufferCreated)
    {
        PBRWaterParamsGPU compParams = BuildPBRWaterParams(
            config, cameraPos, viewMatrix, vpMatrix, mainLightDir, mainLightColor);
        m_CompParamsBuffer.SetBufferData(&compParams, 0, sizeof(PBRWaterParamsGPU));
    }

    // 閳光偓閳光偓 閺囧瓨鏌?SSR Params UBO閿涘湹-12閿涘鏀㈤埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧?
    if (m_Device != nullptr && m_SSRParamsBufferCreated)
    {
        WaterSSRParamsGPU ssrParams = {};
        ssrParams.cameraPosition = glm::vec4(cameraPos, 1.0f);
        ssrParams.projMatrix     = vpMatrix * glm::inverse(viewMatrix);
        ssrParams.invProjMatrix  = glm::inverse(ssrParams.projMatrix);
        ssrParams.viewMatrix     = viewMatrix;
        ssrParams.maxDistance = config.m_SSR.m_MaxDistance;
        ssrParams.maxSteps = 64;
        ssrParams.thickness = 1.0f;
        ssrParams.maxRoughness = config.m_SSR.m_MaxRoughness;
        ssrParams.surfaceParams = glm::vec4(config.m_Medium.m_WaterRoughness, 0.0f, 0.0f, 0.0f);

        m_SSRParamsBuffer.SetBufferData(&ssrParams, 0, sizeof(WaterSSRParamsGPU));
    }

    // 閳光偓閳光偓 閺囧瓨鏌?Caustics Params UBO閿涘湹-14閿涘鏀㈤埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧埞鈧?
    if (m_Device != nullptr && m_CausticsParamsBufferCreated)
    {
        WaterCausticsParamsGPU causticParams = {};
        causticParams.sunDirection     = glm::vec4(glm::normalize(mainLightDir), 0.0f);
        causticParams.mainLightColor   = glm::vec4(mainLightColor, 1.0f);
        const glm::vec3 extinction = config.m_Medium.m_AbsorptionCoeff + config.m_Medium.m_ScatteringCoeff;
        causticParams.extinctionCoeff = glm::vec4(extinction, 0.0f);
        causticParams.causticsIntensity = config.m_Caustics.m_Enabled ? config.m_Caustics.m_Intensity : 0.0f;
        causticParams.causticsScale = config.m_Caustics.m_Scale;
        causticParams.shoreFadeStart = 0.015f;
        causticParams.maxDepth   = config.m_SSS.m_MaxThicknessDistance;
        causticParams.opticalParams = glm::vec4(config.m_Medium.m_IOR, 0.0f, 0.0f, 0.0f);

        m_CausticsParamsBuffer.SetBufferData(&causticParams, 0, sizeof(WaterCausticsParamsGPU));
    }

    // 閳光偓閳光偓 W-16: 閺囧瓨鏌?Thickness Params UBO 閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓
    if (m_Device != nullptr && m_ThicknessParamsBufferCreated)
    {
        ThicknessParamsGPU tp = {};
        tp.maxThickness = config.m_SSS.m_MaxThicknessDistance;
        tp.deepFallback = config.m_SSS.m_DeepWaterThicknessFallback;
        m_ThicknessParamsBuffer.SetBufferData(&tp, 0, sizeof(ThicknessParamsGPU));
    }

}

// ============================================================
// UpdateWaveSimulation 閳?Compute 閻㈢喐鍨?Gerstner 濞夈垹鑸版担宥囆╃拹鏉戞禈閿涘湹-01: 闁?LOD閿?
// ============================================================
void VansWaterSystem::UpdateWaveSimulation(VansVKCommandBuffer& cmd, float /*deltaTime*/)
{
    if (!m_Initialized || !m_DescriptorsReady)
        return;

    VansWaterConfig config;
    if (m_WaterMaterial)
        config = m_WaterMaterial->m_Config;
    config.Validate();
    const VansWaveMode mode = config.m_Spectrum.m_Mode;
    const int cascadeCount = config.m_Spectrum.m_CascadeCount;

    auto runFlowMap = [&]()
    {
        if (m_FlowMapShader == nullptr || m_FlowMapSet == VK_NULL_HANDLE)
            return;

        VkImageMemoryBarrier beforeCompute = {};
        beforeCompute.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        beforeCompute.srcAccessMask = m_FlowMapReady
            ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT : 0;
        beforeCompute.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        beforeCompute.oldLayout = m_FlowMapReady ? VK_IMAGE_LAYOUT_GENERAL : m_FlowMapImage.GetImageLayout();
        beforeCompute.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        beforeCompute.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        beforeCompute.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        beforeCompute.image = m_FlowMapImage.GetImage();
        beforeCompute.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        cmd.PipelineBarrier(
            m_FlowMapReady ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            {}, {}, { beforeCompute });
        m_FlowMapImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);

        cmd.EnsureComputeShader(*m_FlowMapShader, { m_FlowMapLayout });
        const uint32_t groups = (VansWaterConfig::FLOW_MAP_TEXTURE_SIZE + 7u) / 8u;
        cmd.DispatchCompute(*m_FlowMapShader, groups, groups, 1, { m_FlowMapSet });

        VkImageMemoryBarrier afterCompute = {};
        afterCompute.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        afterCompute.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        afterCompute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        afterCompute.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        afterCompute.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        afterCompute.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        afterCompute.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        afterCompute.image = m_FlowMapImage.GetImage();
        afterCompute.subresourceRange = beforeCompute.subresourceRange;
        cmd.PipelineBarrier(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            {}, {}, { afterCompute });
        m_FlowMapReady = true;
    };

    auto runSurfaceCompute = [&](VansComputeShader* shader, VkDescriptorSetLayout layout, VkDescriptorSet set)
    {
        if (shader == nullptr || layout == VK_NULL_HANDLE || set == VK_NULL_HANDLE)
            return;

        const VkImageLayout currentLayout = m_WaveDisplacementReady
            ? VK_IMAGE_LAYOUT_GENERAL
            : m_WaveDisplacementImage.GetImageLayout();

        VkImageMemoryBarrier beforeCompute = {};
        beforeCompute.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        beforeCompute.srcAccessMask       = m_WaveDisplacementReady
            ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT : 0;
        beforeCompute.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        beforeCompute.oldLayout           = currentLayout;
        beforeCompute.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        beforeCompute.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        beforeCompute.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        beforeCompute.image               = m_WaveDisplacementImage.GetImage();
        beforeCompute.subresourceRange    = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
            0, uint32_t(VansWaterConfig::MAX_SPECTRUM_CASCADES)
        };
        VkImageMemoryBarrier beforeDerivative = beforeCompute;
        beforeDerivative.srcAccessMask = m_WaveDerivativeReady ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT : 0;
        beforeDerivative.image = m_WaveDerivativeImage.GetImage();
        beforeDerivative.subresourceRange.layerCount = uint32_t(VansWaterConfig::MAX_SPECTRUM_CASCADES * 2);
        cmd.PipelineBarrier(
            (m_WaveDisplacementReady || m_WaveDerivativeReady) ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            {}, {}, { beforeCompute, beforeDerivative });
        m_WaveDisplacementImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
        m_WaveDerivativeImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);

        cmd.EnsureComputeShader(*shader, { layout });
        const uint32_t groups = (WAVE_TEXTURE_SIZE + 7u) / 8u;
        cmd.DispatchCompute(*shader, groups, groups, uint32_t(cascadeCount), { set });

        VkImageMemoryBarrier afterCompute = {};
        afterCompute.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        afterCompute.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        afterCompute.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        afterCompute.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
        afterCompute.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        afterCompute.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        afterCompute.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        afterCompute.image               = m_WaveDisplacementImage.GetImage();
        afterCompute.subresourceRange    = beforeCompute.subresourceRange;
        VkImageMemoryBarrier afterDerivative = afterCompute;
        afterDerivative.image = m_WaveDerivativeImage.GetImage();
        afterDerivative.subresourceRange = beforeDerivative.subresourceRange;
        cmd.PipelineBarrier(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            {}, {}, { afterCompute, afterDerivative });

        m_WaveDisplacementReady = true;
        m_WaveDerivativeReady = true;
    };

    auto runGerstner = [&]()
    {
        runSurfaceCompute(m_WaveSimShader, m_WaveSimLayout, m_WaveSimSet);
    };

    auto runWaveParticle = [&]()
    {
        runSurfaceCompute(m_WaveParticleShader, m_WaveParticleLayout, m_WaveParticleSet);
    };

    runFlowMap();

    bool fftReady = false;
    if (mode == VansWaveMode::FFT && m_WaterFFT && m_WaterFFT->IsReady())
    {
        m_WaterFFT->UpdateFFT(cmd, m_Time);
        m_WaveDisplacementReady = true;
        m_WaveDerivativeReady = true;
        fftReady = true;
    }

    if (mode == VansWaveMode::FFT && !fftReady)
    {
        static bool s_FFTFallbackLogged = false;
        if (!s_FFTFallbackLogged)
        {
            VANS_LOG_WARN("[VansWaterSystem] FFT requested but not ready; falling back to Gerstner");
            s_FFTFallbackLogged = true;
        }
        runGerstner();
    }
    else if (mode == VansWaveMode::Gerstner)
        runGerstner();
    else if (mode == VansWaveMode::WaveParticle)
    {
        if (m_WaveParticleShader && m_WaveParticleSet != VK_NULL_HANDLE)
            runWaveParticle();
        else
        {
            static bool s_WaveParticleFallbackLogged = false;
            if (!s_WaveParticleFallbackLogged)
            {
                VANS_LOG_WARN("[VansWaterSystem] WaveParticle requested but not ready; falling back to Gerstner");
                s_WaveParticleFallbackLogged = true;
            }
            runGerstner();
        }
    }
}

// ============================================================
// RenderWaterGBuffer 閳?鐠佹崘顓搁弬鍥ㄣ€?Pass 7
// ============================================================
void VansWaterSystem::RenderWaterGBuffer(VansVKCommandBuffer& cmd, GlobalStateData& globalState)
{
    static int s_DbgFrame = 0;
    bool dbgLog = (s_DbgFrame++ % 120) == 0;

    if (!m_Initialized || !m_DescriptorsReady || !m_GeometryClipmap || m_GeometryClipmap->GetPatchCount() == 0)
    {
        if (dbgLog)
            VANS_LOG_WARN("[WaterGBuffer] EARLY RETURN: init/descReady/patches not ready");
        return;
    }
    if (m_WaterGBufferShader == nullptr || m_GeometryClipmap->GetVertexBuffer() == VK_NULL_HANDLE)
    {
        if (dbgLog)
            VANS_LOG_WARN("[WaterGBuffer] EARLY RETURN: shader or vertex buffer is null");
        return;
    }

    if (dbgLog)
    {
        VANS_LOG("[WaterGBuffer] Render: init=" << m_Initialized
                 << " descReady=" << m_DescriptorsReady
                 << " patches=" << m_GeometryClipmap->GetPatchCount()
                 << " shader=" << (m_WaterGBufferShader != nullptr)
                 << " waterLevel=" << m_WaterLevel);
    }

    // 閳光偓閳光偓 鐠佸墽鐤嗘い鍓佸仯鏉堟挸鍙嗙敮鍐ㄧ湰 閳光偓閳光偓
    globalState.vertexInputBindingDescriptions   = &m_GeometryClipmap->GetVertexBindings();
    globalState.vertexInputAttributeDescriptions = &m_GeometryClipmap->GetVertexAttributes();

    // 閳光偓閳光偓 Descriptor Set 鐢啫鐪弫鎵矋閿涙瓟Set 0: Global, Set 1: WaterGBuf Pass]
    std::vector<VkDescriptorSetLayout> layouts = { m_GlobalLayout, m_GBufPassLayout };
    std::vector<VkDescriptorSet>       sets    = { m_GlobalSet,    m_GBufPassSet    };

    cmd.EnsureGraphicsShader(*m_WaterGBufferShader, globalState, layouts);
    cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
        *m_WaterGBufferShader, 0, sets, {});
    cmd.BindGraphicsPipeline(*m_WaterGBufferShader->GetGraphicsPipeline());

    // Bind shared immutable geometry clipmap mesh.
    VkDeviceSize offset = 0;
    VkBuffer vbuf = m_GeometryClipmap->GetVertexBuffer();
    VkBuffer ibuf = m_GeometryClipmap->GetIndexBuffer();
    cmd.BindVertexBuffers(0, 1, &vbuf, &offset);
    cmd.BindIndexBuffer(ibuf, 0, VK_INDEX_TYPE_UINT32);

    const std::vector<WaterGeometryPatch>& patches = m_GeometryClipmap->GetPatches();
    uint32_t indexCount = m_GeometryClipmap->GetIndexCount();

    // 閳光偓閳光偓 闁?Patch 閹恒劑鈧礁鐖堕柌?+ DrawIndexed 閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓
    for (auto patchIter = patches.rbegin(); patchIter != patches.rend(); ++patchIter)
    {
        const WaterGeometryPatch& patch = *patchIter;
        WaterPatchPushConstant pc = {};
        pc.patchWorldOrigin = patch.worldOrigin;
        pc.patchWorldSize   = patch.worldSize;
        pc.lodLevel         = patch.lodLevel;
        pc.waterLevel       = m_WaterLevel;
        pc.outerEdgeMask    = patch.outerEdgeMask;

        cmd.UpdatePushConstants(
            *m_WaterGBufferShader->GetGraphicsPipeline(),
            VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(WaterPatchPushConstant), &pc);

        cmd.DrawIndexed(indexCount, 1, 0, 0, 0);
    }
}

// ============================================================
// DispatchWaterSSR / DispatchRefractionCS / DispatchCausticsCS
// ============================================================
void VansWaterSystem::DispatchWaterSSR(VansVKCommandBuffer& cmd)
{
    if (!m_Initialized || !m_DescriptorsReady)
        return;

    // Inspector optimization: SSR enable guard
    if (m_WaterMaterial && !m_WaterMaterial->m_Config.m_SSR.m_Enabled)
        return;

    // 閳光偓閳光偓 Water SSR (HZB Ray March) 閳?Reflection 閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓閳光偓
    if (m_WaterSSRShader != nullptr && m_SSRSet != VK_NULL_HANDLE)
    {
        VkImageMemoryBarrier beforeSSR = {};
        beforeSSR.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        beforeSSR.srcAccessMask = m_ReflectionOutputReady ? VK_ACCESS_SHADER_READ_BIT : 0;
        beforeSSR.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        beforeSSR.oldLayout = m_WaterReflectionImage.GetImageLayout();
        beforeSSR.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        beforeSSR.image = m_WaterReflectionImage.GetImage();
        beforeSSR.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        m_WaterReflectionImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
        cmd.PipelineBarrier(m_ReflectionOutputReady ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                                    : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {}, {}, { beforeSSR });

        cmd.EnsureComputeShader(*m_WaterSSRShader, { m_SSRLayout });
        cmd.DispatchCompute(*m_WaterSSRShader,
            (m_RenderWidth + 7u) / 8u,
            (m_RenderHeight + 7u) / 8u,
            1, { m_SSRSet });

        VkImageMemoryBarrier afterSSR = {};
        afterSSR.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        afterSSR.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        afterSSR.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        afterSSR.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        afterSSR.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        afterSSR.image = m_WaterReflectionImage.GetImage();
        afterSSR.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, {}, {}, { afterSSR });
    }

    m_ReflectionOutputReady = true;
}

// ============================================================
// EnsureSSRDescriptorSet 閳?HZB 閸欘垳鏁ら弮璺烘鏉╃喎鍨卞?SSR descriptor set
// ============================================================
void VansWaterSystem::EnsureSSRDescriptorSet(VansVKImage* hzbImage)
{
    if (m_SSRSet != VK_NULL_HANDLE) return;  // already created
    if (hzbImage == nullptr) return;
    if (!m_Initialized || !m_DescriptorsReady) return;

    auto* descMgr = VansVKDescriptorManager::GetInstance();
    auto* rp = VansRenderPassManager::GetInstance();
    if (!rp) return;

    // Create layout + allocate set
    std::vector<VkDescriptorSet> sets;
    VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterSSRCompute(
        m_SSRLayout, sets, 1);
    m_SSRSet = sets[0];

    descMgr->BeginDescriptorUpdate();
    descMgr->WriteImageDescriptor(
        m_SSRSet, WATER_SSR_BINDING_GBUF_NORMAL,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        { { rp->GetWaterGBufNormal().GetSampler(), rp->GetWaterGBufNormal().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
    descMgr->WriteImageDescriptor(
        m_SSRSet, WATER_SSR_BINDING_GBUF_DEPTH,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        { { rp->GetWaterGBufLinearDepth().GetSampler(), rp->GetWaterGBufLinearDepth().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
    descMgr->WriteImageDescriptor(
        m_SSRSet, WATER_SSR_BINDING_SCENE_HZB,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        { { hzbImage->GetSampler(), hzbImage->GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
    descMgr->WriteImageDescriptor(
        m_SSRSet, WATER_SSR_BINDING_SCENE_GBUF2,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        { { rp->GetGbuffer2().GetSampler(), rp->GetGbuffer2().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
    descMgr->WriteImageDescriptor(
        m_SSRSet, WATER_SSR_BINDING_SCENE_COLOR,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        { { rp->GetColor().GetSampler(), rp->GetColor().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
    descMgr->WriteBufferDescriptor(
        m_SSRSet, WATER_SSR_BINDING_PARAMS,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        { { GetNativeBuffer(m_SSRParamsBuffer, m_SSRParamsBufferCreated), 0, SSR_PARAMS_BUFFER_SIZE } });
    descMgr->WriteImageDescriptor(
        m_SSRSet, WATER_SSR_BINDING_REFLECTION_OUT,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        { { m_WaterReflectionImage.GetSampler(), m_WaterReflectionImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });

    descMgr->CommitDescriptorUpdates();
    VANS_LOG("[VansWaterSystem] SSR descriptor set created with HZB.");
}

void VansWaterSystem::DispatchRefractionCS(VansVKCommandBuffer& cmd)
{
    if (!m_Initialized || !m_DescriptorsReady)
        return;

    // Inspector optimization: Refraction enable guard
    if (m_WaterMaterial && !m_WaterMaterial->m_Config.m_Refraction.m_Enabled)
        return;

    if (m_WaterRefractionShader == nullptr || m_RefractionSet == VK_NULL_HANDLE)
        return;

    // Barrier: refraction image 閳?compute write
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask       = m_RefractionOutputReady ? VK_ACCESS_SHADER_READ_BIT : 0;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.oldLayout           = m_RefractionOutputReady ? VK_IMAGE_LAYOUT_GENERAL : m_WaterRefractionImage.GetImageLayout();
        barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = m_WaterRefractionImage.GetImage();
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        m_WaterRefractionImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
        cmd.PipelineBarrier(
            m_RefractionOutputReady ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            {}, {}, { barrier });
    }

    cmd.EnsureComputeShader(*m_WaterRefractionShader, { m_RefractionLayout });
    cmd.DispatchCompute(*m_WaterRefractionShader,
        (m_RenderWidth + 7u) / 8u,
        (m_RenderHeight + 7u) / 8u,
        1,
        { m_RefractionSet });

    // Barrier: refraction image 閳?fragment read (for composite pass)
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = m_WaterRefractionImage.GetImage();
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        cmd.PipelineBarrier(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            {}, {}, { barrier });
    }
    m_RefractionOutputReady = true;
}
void VansWaterSystem::DispatchCausticsCS(VansVKCommandBuffer& cmd)
{
    if (!m_Initialized || !m_DescriptorsReady) return;

    // Caustics are opt-in.  Do not leave the compute pass running when the
    // material is unavailable or the effect is disabled in the Inspector.
    if (!m_WaterMaterial || !m_WaterMaterial->m_Config.m_Caustics.m_Enabled)
        return;

    if (m_WaterCausticsShader == nullptr || m_CausticsSet == VK_NULL_HANDLE) return;

    // Barrier: caustics image 閳?compute write
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask       = m_CausticsOutputReady ? VK_ACCESS_SHADER_READ_BIT : 0;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.oldLayout           = m_CausticsOutputReady ? VK_IMAGE_LAYOUT_GENERAL
                                                          : m_WaterCausticsImage.GetImageLayout();
        barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.image               = m_WaterCausticsImage.GetImage();
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        m_WaterCausticsImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
        cmd.PipelineBarrier(
            m_CausticsOutputReady ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            {}, {}, { barrier });
    }

    cmd.EnsureComputeShader(*m_WaterCausticsShader, { m_CausticsLayout });
    cmd.DispatchCompute(*m_WaterCausticsShader,
        (m_RenderWidth  + 7u) / 8u,
        (m_RenderHeight + 7u) / 8u,
        1, { m_CausticsSet });

    // Barrier: caustics image 閳?fragment read
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.image               = m_WaterCausticsImage.GetImage();
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        cmd.PipelineBarrier(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            {}, {}, { barrier });
    }
    m_CausticsOutputReady = true;
}

// ============================================================
// DispatchWaterThicknessCS 閳?W-16 闂冭埖顔?: 閸樻艾瀹抽崶鎾呯礄鐠佹崘顓搁弬鍥ㄣ€?鎼?.2閿?
// ============================================================
void VansWaterSystem::DispatchWaterThicknessCS(VansVKCommandBuffer& cmd)
{
    if (!m_Initialized || !m_DescriptorsReady)
        return;

    if (m_WaterThicknessShader == nullptr || m_ThicknessSet == VK_NULL_HANDLE)
        return;

    // Barrier: thickness image 閳?compute write
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask       = m_ThicknessOutputReady ? VK_ACCESS_SHADER_READ_BIT : 0;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.oldLayout           = m_ThicknessOutputReady ? VK_IMAGE_LAYOUT_GENERAL : m_WaterThicknessImage.GetImageLayout();
        barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = m_WaterThicknessImage.GetImage();
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        m_WaterThicknessImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
        cmd.PipelineBarrier(
            m_ThicknessOutputReady
                ? (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
                : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            {}, {}, { barrier });
    }

    cmd.EnsureComputeShader(*m_WaterThicknessShader, { m_ThicknessLayout });
    cmd.DispatchCompute(*m_WaterThicknessShader,
        (m_RenderWidth  + 7u) / 8u,
        (m_RenderHeight + 7u) / 8u,
        1, { m_ThicknessSet });

    // Barrier: thickness image 閳?fragment/compute read (for SSS scatter + composite)
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = m_WaterThicknessImage.GetImage();
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        cmd.PipelineBarrier(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            {}, {}, { barrier });
    }
    m_ThicknessOutputReady = true;
}

// ============================================================
// DispatchWaterVolumeCS / DispatchWaterVolumeFilterCS
// ============================================================
void VansWaterSystem::DispatchWaterVolumeCS(VansVKCommandBuffer& cmd)
{
    if (!m_Initialized || !m_DescriptorsReady)
        return;
    if (m_WaterVolumeShader == nullptr || m_VolumeSet == VK_NULL_HANDLE)
        return;

    std::vector<VkImageMemoryBarrier> before;
    auto addBefore = [&](VansVKImage& image)
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = m_VolumeOutputReady ? VK_ACCESS_SHADER_READ_BIT : 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.oldLayout = m_VolumeOutputReady ? VK_IMAGE_LAYOUT_GENERAL : image.GetImageLayout();
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.GetImage();
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        image.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
        before.push_back(barrier);
    };
    addBefore(m_WaterVolumeRawColorImage);
    addBefore(m_WaterVolumeRawTransmittanceImage);
    addBefore(m_WaterVolumeRawDepthImage);
    cmd.PipelineBarrier(
        m_VolumeOutputReady ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        {}, {}, before);

    cmd.EnsureComputeShader(*m_WaterVolumeShader, { m_VolumeLayout });
    cmd.DispatchCompute(*m_WaterVolumeShader,
        (m_VolumeWidth + 7u) / 8u,
        (m_VolumeHeight + 7u) / 8u,
        1, { m_VolumeSet });

    std::vector<VkImageMemoryBarrier> after;
    auto addAfter = [&](VansVKImage& image)
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.GetImage();
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        after.push_back(barrier);
    };
    addAfter(m_WaterVolumeRawColorImage);
    addAfter(m_WaterVolumeRawTransmittanceImage);
    addAfter(m_WaterVolumeRawDepthImage);
    cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        {}, {}, after);
    m_VolumeOutputReady = true;
}

void VansWaterSystem::DispatchWaterVolumeFilterCS(VansVKCommandBuffer& cmd)
{
    if (!m_Initialized || !m_DescriptorsReady)
        return;
    if (m_WaterVolumeFilterShader == nullptr || m_VolumeFilterSet == VK_NULL_HANDLE)
        return;

    std::vector<VkImageMemoryBarrier> before;
    auto addBefore = [&](VansVKImage& image)
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = m_VolumeFilterOutputReady ? VK_ACCESS_SHADER_READ_BIT : 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.oldLayout = m_VolumeFilterOutputReady ? VK_IMAGE_LAYOUT_GENERAL : image.GetImageLayout();
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.GetImage();
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        image.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
        before.push_back(barrier);
    };
    addBefore(m_WaterVolumeColorImage);
    addBefore(m_WaterVolumeTransmittanceImage);
    addBefore(m_WaterVolumeDepthImage);
    cmd.PipelineBarrier(
        m_VolumeFilterOutputReady ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        {}, {}, before);

    cmd.EnsureComputeShader(*m_WaterVolumeFilterShader, { m_VolumeFilterLayout });
    cmd.DispatchCompute(*m_WaterVolumeFilterShader,
        (m_VolumeWidth + 7u) / 8u,
        (m_VolumeHeight + 7u) / 8u,
        1, { m_VolumeFilterSet });

    std::vector<VkImageMemoryBarrier> after;
    auto addAfter = [&](VansVKImage& image)
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.GetImage();
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        after.push_back(barrier);
    };
    addAfter(m_WaterVolumeColorImage);
    addAfter(m_WaterVolumeTransmittanceImage);
    addAfter(m_WaterVolumeDepthImage);
    cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        {}, {}, after);
    m_VolumeFilterOutputReady = true;
}
// ============================================================
// RenderWaterComposite 閳?鐠佹崘顓搁弬鍥ㄣ€?Pass 9閿涘牆鍙忕仦?1閳?瀵ゆ儼绻滈崥鍫熷灇閿?
// ============================================================
void VansWaterSystem::RenderWaterComposite(VansVKCommandBuffer& cmd, GlobalStateData& globalState)
{
    if (!m_Initialized || !m_DescriptorsReady)
        return;
    if (m_WaterCompositeShader == nullptr)
        return;

    globalState.vertexInputBindingDescriptions   = nullptr;
    globalState.vertexInputAttributeDescriptions = nullptr;

    std::vector<VkDescriptorSetLayout> layouts = { m_GlobalLayout, m_CompPassLayout };
    std::vector<VkDescriptorSet>       sets    = { m_GlobalSet,    m_CompPassSet    };

    cmd.EnsureGraphicsShader(*m_WaterCompositeShader, globalState, layouts);
    cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
        *m_WaterCompositeShader, 0, sets, {});
    cmd.BindGraphicsPipeline(*m_WaterCompositeShader->GetGraphicsPipeline());

    cmd.Draw(3, 1, 0, 0);
}

} // namespace VansGraphics
