#include "VansWaterSystem.h"
#include "VansWaterFFT.h"
#include "VansWaterWaveSystem.h"
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
    // W-04: 鑷姩鐢熸垚瀵规暟鍒嗗竷鐨?Gerstner 娉㈠垎閲?
    void AutoGenerateGerstnerWaves(std::vector<GerstnerWaveGPU>& waves,
                                    int count, const glm::vec2& windDir,
                                    float swellAmplitude, float windSpeed)
    {
        waves.clear();
        waves.reserve(count);
        glm::vec2 dir = glm::normalize(windDir);
        if (glm::length(dir) < 0.001f)
            dir = glm::vec2(0.7071f, 0.7071f);

        // 娉㈤暱浠?256m 鍒?0.5m 瀵规暟鍒嗗竷锛堟墿灞曚笂闄愪互瑕嗙洊绮?LOD Nyquist 杩囨护锛?
        const float minWL = 0.5f;
        const float maxWL = 256.0f;
        const float PI = 3.14159265358979323846f;
        const float GRAVITY = 9.81f;

        for (int i = 0; i < count; ++i)
        {
            float t = (count <= 1) ? 0.0f : static_cast<float>(i) / static_cast<float>(count - 1);
            float wavelength = maxWL * std::powf(minWL / maxWL, t);
            float k = 2.0f * PI / wavelength;
            float omega = std::sqrtf(GRAVITY * k);
            float speed = omega / k;  // 鐩搁€熷害

            // 鎸箙闅忔尝闀垮噺灏忥紙鐭尝鑳介噺灏忥級锛屽姞鍏ヤ竴瀹氶殢鏈烘€?
            float baseAmp = swellAmplitude * std::powf(wavelength / maxWL, 0.75f);
            // 鏂瑰悜锛氶鏂瑰悜 + 灏忚搴︽墿鏁?
            float angleSpread = (static_cast<float>((i * 7 + 3) % 17) / 17.0f - 0.5f) * 0.6f;
            float angle = std::atan2f(dir.y, dir.x) + angleSpread;
            float dx = std::cosf(angle);
            float dy = std::sinf(angle);

            GerstnerWaveGPU wave = {};
            wave.amplitude  = baseAmp;
            wave.wavelength = wavelength;
            wave.directionX = dx;
            wave.directionY = dy;
            wave.speed      = speed;
            wave.steepness  = 0.05f + 0.55f * (1.0f - t);  // 闀挎尝鏇撮櫋
            wave.pad0       = 0.0f;
            wave.pad1       = 0.0f;
            waves.push_back(wave);
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
    };

    struct ThicknessParamsGPU
    {
        float maxThickness;
        float deepFallback;
        float pad0;
        float pad1;
    };

    struct SSSParamsGPU
    {
        glm::vec4 absorptionCoeff;
        glm::vec4 scatteringCoeff;
        float maxThickness;
        float anisotropy;
        float pad0;
        float pad1;
    };

    constexpr VkDeviceSize SSR_PARAMS_BUFFER_SIZE = 256;
    constexpr VkDeviceSize SSS_PARAMS_DESCRIPTOR_SIZE = sizeof(float) * 16;
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

    // 鈹€鈹€ 1. 鍒涘缓 CDLOD 绠＄悊鍣紙W-02: VansWaterLOD 鐙珛绫伙級鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    m_WaterLOD = new VansWaterLOD();
    m_WaterLOD->Initialize(device,
        VansWaterLOD::MAX_LOD_COUNT,
        VansWaterLOD::MIN_LOD_DIST,
        VansWaterLOD::WATER_MESH_DIM,
        VansWaterLOD::BASE_PATCH_SIZE);

    // 鈹€鈹€ 2. 缂栬瘧鐫€鑹插櫒 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    auto*       cfg         = VansConfigration::GetInstance();
    std::string projectRoot = cfg->GetProjectRootPath();
    VkDevice    logicDev    = device->GetLogicDevice();

    m_WaterGBufferShader = new VansGraphicsShader();
    const std::string waterGBufferShaderPath = projectRoot + "EngineAssets/Shaders/Water/WaterGBuffer";
    m_WaterGBufferShader->InitShader(logicDev, waterGBufferShaderPath.c_str());
    VansShaderManager::Get().ConfigureGraphicsShader(*m_WaterGBufferShader, "WaterGBuffer", waterGBufferShaderPath);
    // 寮€鍚繁搴︽祴璇?+ 娣卞害鍐欏叆锛氬埄鐢ㄧ嫭绔嬫按闈㈡繁搴︾紦鍐蹭繚璇?CDLOD 澶氬眰閬尅椤哄簭
    // depthTest: TRUE, depthWrite: TRUE, compareOp: LESS锛堣繎澶?patch 閬尅杩滃 patch锛?
    
    // CDLOD 濉厖妯″紡锛堢敓浜х幆澧冿級

    // Wave compute shader锛圵-01: Texture2DArray + SSBO + Nyquist锛?
    m_WaveSimShader = new VansComputeShader();
    m_WaveSimShader->InitShader(logicDev,
        (projectRoot + "EngineAssets/Shaders/Water/WaterWave").c_str());

    m_WaterEffectsShader = new VansComputeShader();
    m_WaterEffectsShader->InitShader(logicDev,
        (projectRoot + "EngineAssets/Shaders/Water/WaterEffects").c_str());

    m_WaterSSRShader = new VansComputeShader();
    m_WaterSSRShader->InitShader(logicDev,
        (projectRoot + "EngineAssets/Shaders/Water/SSR").c_str());

    m_WaterRefractionShader = new VansComputeShader();
    m_WaterRefractionShader->InitShader(logicDev,
        (projectRoot + "EngineAssets/Shaders/Water/Refraction").c_str());

    m_WaterCausticsShader = new VansComputeShader();
    m_WaterCausticsShader->InitShader(logicDev,
        (projectRoot + "EngineAssets/Shaders/Water/Caustics").c_str());

    // N-01: Detail Normal compute shader锛堢嫭绔嬬洰褰曪紝閬垮厤涓?water_wave_spectrum.comp 鍐茬獊锛?
    m_DetailNormalShader = new VansComputeShader();
    m_DetailNormalShader->InitShader(logicDev,
        (projectRoot + "EngineAssets/Shaders/Water/WaterDetailNormal").c_str());

    // W-16: Water Thickness compute shader
    m_WaterThicknessShader = new VansComputeShader();
    m_WaterThicknessShader->InitShader(logicDev,
        (projectRoot + "EngineAssets/Shaders/Water/SSS").c_str());

    // W-16 Phase 2: Water SSS Scatter compute shader
    m_WaterSSSScatterShader = new VansComputeShader();
    m_WaterSSSScatterShader->InitShader(logicDev,
        (projectRoot + "EngineAssets/Shaders/Water/SSSScatter").c_str());

    m_WaterCompositeShader = new VansGraphicsShader();
    const std::string waterCompositeShaderPath = projectRoot + "EngineAssets/Shaders/Water/WaterComposite";
    m_WaterCompositeShader->InitShader(logicDev, waterCompositeShaderPath.c_str());
    VansShaderManager::Get().ConfigureGraphicsShader(*m_WaterCompositeShader, "WaterComposite", waterCompositeShaderPath);

    // 鈹€鈹€ 3. 鍒涘缓 WaterGBufferParams UBO 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    CreateWaterBuffer(m_GBufParamsBuffer, m_GBufParamsBufferCreated,
        sizeof(WaterGBufferParamsGPU),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    WaterGBufferParamsGPU gbufParams = {};
    gbufParams.VPMatrix       = glm::mat4(1.0f);
    gbufParams.ViewMatrix     = glm::mat4(1.0f);
    gbufParams.cameraPosition = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    gbufParams.minLodDist     = VansWaterLOD::BASE_PATCH_SIZE;
    gbufParams.lodLevels      = m_WaterLOD ? m_WaterLOD->GetLodLevels() : VansWaterLOD::MAX_LOD_COUNT;
    gbufParams.meshDim        = m_WaterLOD ? m_WaterLOD->GetMeshDim() : VansWaterLOD::WATER_MESH_DIM;
    gbufParams.clipmapBaseScale = 4.0f * VansWaterLOD::BASE_PATCH_SIZE;
    gbufParams.maxWaveAmp      = 0.6f;  // swellAmplitude(0.2) * 3
    gbufParams.detailBalance   = m_WaterLOD ? m_WaterLOD->GetDetailBalance() : 2.0f;
    gbufParams.morphStartRatio = 0.5f;
    gbufParams.waveTimeAndScale = glm::vec4(0.0f, 0.2f, 1.5f, 1.0f);  // Initialize() initial UBO
    m_GBufParamsCache = gbufParams;
    if (m_GBufParamsBufferCreated)
        m_GBufParamsBuffer.SetBufferData(&m_GBufParamsCache, 0, sizeof(WaterGBufferParamsGPU));

    // 鈹€鈹€ 4. 鍒涘缓娉㈠舰浣嶇Щ璐村浘锛圵-01: Texture2DArray, 256虏 脳 MAX_LOD_COUNT锛夆攢鈹€
    // CLAMP_TO_EDGE锛氳创鍥捐鐩?snappedOrigin 卤 lodScale/2 鐨勪笘鐣岃寖鍥达紝
    // 杈圭晫澶栫殑 Patch 鐢?CDLOD 璺濈鐜害鏉熶繚璇佷笉浼氶噰鏍峰埌锛孋LAMP 浣滀负瀹夊叏缃?
    m_WaveDisplacementImage.CreateVulkanImage(
        logicDev,
        { WAVE_TEXTURE_SIZE, WAVE_TEXTURE_SIZE, 1 },
        VK_FORMAT_R16G16B16A16_SFLOAT,
        1, VansWaterLOD::MAX_LOD_COUNT,  // 10 layers
        VK_IMAGE_TYPE_2D,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_SAMPLE_COUNT_1_BIT,
        false, false, true,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    // Tessendorf FFT derivative map: R=slopeX, G=slopeZ, B=foam/fold, A=reserved.
    m_WaveDerivativeImage.CreateVulkanImage(
        logicDev,
        { WAVE_TEXTURE_SIZE, WAVE_TEXTURE_SIZE, 1 },
        VK_FORMAT_R16G16B16A16_SFLOAT,
        1, VansWaterLOD::MAX_LOD_COUNT,
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

    // 鈹€鈹€ N-01: Detail Normal Texture2DArray锛?024虏 脳 1 layer RGBA16F锛屼笘鐣岀┖闂村钩閾猴級鈹€鈹€
    m_DetailNormalImage.CreateVulkanImage(
        logicDev,
        { DETAIL_TEXTURE_SIZE, DETAIL_TEXTURE_SIZE, 1 },
        VK_FORMAT_R16G16B16A16_SFLOAT,
        1, VansWaterLOD::MAX_LOD_COUNT,
        VK_IMAGE_TYPE_2D,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_SAMPLE_COUNT_1_BIT,
        false, false, true,
        VK_SAMPLER_ADDRESS_MODE_REPEAT);

    // 鈹€鈹€ 5. 鍒涘缓姘翠綋鏁堟灉璐村浘锛圕LAMP_TO_EDGE 闃叉杈圭紭骞抽摵浼奖锛夆攢鈹€
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
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);  // 灞忓箷绌洪棿鏁堟灉璐村浘涓嶅簲 repeat
    };
    createEffectImage(m_WaterReflectionImage);
    createEffectImage(m_WaterRefractionImage);
    createEffectImage(m_WaterCausticsImage);
    createEffectImage(m_WaterThicknessImage);  // W-16: CLAMP_TO_EDGE 鍚屼笂
    createEffectImage(m_WaterSSSScatterImage); // W-16 Phase 2: SSS 鏁ｅ皠杈撳嚭

    // 鈹€鈹€ 6. 鍒涘缓 WaterCompositeParams UBO 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    CreateWaterBuffer(m_CompParamsBuffer, m_CompParamsBufferCreated,
        sizeof(WaterCompositeParamsGPU),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    WaterCompositeParamsGPU compParams = {};
    // 钃濊壊姘翠綋榛樿鍙傛暟锛堢墿鐞嗘柟鍚戯細绾㈠厜鍚告敹鏈€蹇紝钃濆厜绌块€忔渶娣憋級
    compParams.deepWaterColor    = glm::vec4(0.01f, 0.04f, 0.18f, 1.0f);   // 娣辨按鏆楄摑
    compParams.shallowWaterColor = glm::vec4(0.05f, 0.18f, 0.55f, 1.0f);   // 娴呮按浜摑锛堟暎灏勮壊锛?
    compParams.fresnelPower      = 5.0f;
    compParams.waterLevel        = m_WaterLevel;
    compParams.specularIntensity = 0.6f;
    compParams.foamIntensity     = 1.0f;
    compParams.absorptionCoeff   = glm::vec4(0.25f, 0.08f, 0.02f, 1.0f);  // R>G>B, 娑堝厜 0.27>0.12>0.08
    compParams.scatteringCoeff   = glm::vec4(0.02f, 0.04f, 0.06f, 1.0f);  // B>G>R, 钃濆厜绌块€忔渶娣?
    compParams.sssAnisotropy     = 0.85f;
    compParams.waterRoughness    = 0.02f;
    compParams.waterIOR          = 1.33f;
    compParams.cameraPosition    = glm::vec4(0.0f, 10.0f, 0.0f, 1.0f);
    compParams.viewMatrix        = glm::mat4(1.0f);
    compParams.projMatrix        = glm::mat4(1.0f);
    if (m_CompParamsBufferCreated)
        m_CompParamsBuffer.SetBufferData(&compParams, 0, sizeof(WaterCompositeParamsGPU));

    // 鈹€鈹€ SSR Params UBO锛圵-12锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    CreateWaterBuffer(m_SSRParamsBuffer, m_SSRParamsBufferCreated,
        SSR_PARAMS_BUFFER_SIZE,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    // 鈹€鈹€ Caustics Params UBO锛圵-14锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    CreateWaterBuffer(m_CausticsParamsBuffer, m_CausticsParamsBufferCreated,
        sizeof(WaterCausticsParamsGPU),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    // 鈹€鈹€ W-16: Thickness Params UBO 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    {
        CreateWaterBuffer(m_ThicknessParamsBuffer, m_ThicknessParamsBufferCreated,
            sizeof(ThicknessParamsGPU),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        ThicknessParamsGPU tp = { 15.0f, 0.8f, 0.0f, 0.0f };
        if (m_ThicknessParamsBufferCreated)
            m_ThicknessParamsBuffer.SetBufferData(&tp, 0, sizeof(ThicknessParamsGPU));
    }

    // 鈹€鈹€ W-16 Phase 2: SSS Params UBO 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    {
        CreateWaterBuffer(m_SSSParamsBuffer, m_SSSParamsBufferCreated,
            SSS_PARAMS_DESCRIPTOR_SIZE,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        SSSParamsGPU sp = {
            glm::vec4(0.25f, 0.08f, 0.02f, 1.0f),
            glm::vec4(0.02f, 0.04f, 0.06f, 1.0f),
            15.0f, 0.85f, 0.0f, 0.0f
        };
        if (m_SSSParamsBufferCreated)
            m_SSSParamsBuffer.SetBufferData(&sp, 0, sizeof(SSSParamsGPU));
    }

    // 鈹€鈹€ 7. 鍒涘缓 Gerstner 娉?SSBO锛圵-04锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    {
        VkDeviceSize ssboSize = MAX_WAVE_COUNT * sizeof(GerstnerWaveGPU);
        CreateWaterBuffer(m_WaveSSBO, m_WaveSSBOCreated,
            ssboSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

        // 鑷姩鐢熸垚榛樿娉㈠垎閲?
        std::vector<GerstnerWaveGPU> waves;
        AutoGenerateGerstnerWaves(waves, 128, glm::vec2(0.7071f, 0.7071f), 0.2f, 12.0f);
        if (m_WaveSSBOCreated)
            m_WaveSSBO.SetBufferData(waves.data(), 0, waves.size() * sizeof(GerstnerWaveGPU));

        VANS_LOG("[VansWaterSystem] Wave SSBO: " << waves.size() << " waves, " << ssboSize << " bytes");
    }

    m_Initialized = true;
    VANS_LOG("[VansWaterSystem] Initialize: " << renderWidth << "x" << renderHeight
             << " waterLevel=" << m_WaterLevel
             << " meshDim=" << m_WaterLOD->GetMeshDim());
}

// ============================================================
// SetGlobalDescriptorSet 鈥?鍦?CreateGlobalDescriptorSet 涔嬪悗璋冪敤
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

    // 鈹€鈹€ Water GBuffer Pass descriptor set锛圫et 1锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterGBuffer(
            m_GBufPassLayout, sets, 1);
        m_GBufPassSet = sets[0];
        descMgr->BeginDescriptorUpdate();

        // binding 0锛歐aterGBufferParams UBO
        descMgr->WriteBufferDescriptor(
            m_GBufPassSet,
            WATER_GBUF_BINDING_PARAMS,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { GetNativeBuffer(m_GBufParamsBuffer, m_GBufParamsBufferCreated), 0, sizeof(WaterGBufferParamsGPU) } });

        // binding 1锛氫綅绉昏创鍥?Texture2DArray锛圵-01锛?
        descMgr->WriteImageDescriptor(
            m_GBufPassSet,
            WATER_GBUF_BINDING_DISPLACEMENT,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { {
                m_WaveDisplacementImage.GetSampler(),
                m_WaveDisplacementImage.GetImageView(),
                VK_IMAGE_LAYOUT_GENERAL
            } });

        // binding 2锛欸erstnerWave SSBO锛圵-04锛夆€?椤剁偣鐫€鑹插櫒璇诲彇
        descMgr->WriteBufferDescriptor(
            m_GBufPassSet,
            WATER_GBUF_BINDING_WAVE_SSBO,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            { { GetNativeBuffer(m_WaveSSBO, m_WaveSSBOCreated), 0, MAX_WAVE_COUNT * sizeof(GerstnerWaveGPU) } });

        // binding 3: Detail Normal Texture2DArray锛圢-01锛夆€?澶嶇敤鍘?normal map binding slot
        descMgr->WriteImageDescriptor(
            m_GBufPassSet,
            WATER_GBUF_BINDING_NORMAL_MAP,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { {
                m_DetailNormalImage.GetSampler(),
                m_DetailNormalImage.GetImageView(),
                VK_IMAGE_LAYOUT_GENERAL
            } });

        // binding 4: FFT derivative Texture2DArray 鈥?vertex shader reads slope normal.
        descMgr->WriteImageDescriptor(
            m_GBufPassSet,
            WATER_GBUF_BINDING_DERIVATIVE,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { {
                m_WaveDerivativeImage.GetSampler(),
                m_WaveDerivativeImage.GetImageView(),
                VK_IMAGE_LAYOUT_GENERAL
            } });

        descMgr->CommitDescriptorUpdates();
    }

    // 鈹€鈹€ Water Wave Compute descriptor set锛圫et 0锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
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
        // binding 2锛欸erstnerWave SSBO 杈撳叆
        descMgr->WriteBufferDescriptor(
            m_WaveSimSet,
            WATER_WAVE_BINDING_WAVE_SSBO,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            { { GetNativeBuffer(m_WaveSSBO, m_WaveSSBOCreated), 0, MAX_WAVE_COUNT * sizeof(GerstnerWaveGPU) } });

        descMgr->CommitDescriptorUpdates();
    }

    // 鈹€鈹€ Water Composite Pass descriptor set锛圫et 1锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
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
            { { GetNativeBuffer(m_CompParamsBuffer, m_CompParamsBufferCreated), 0, sizeof(WaterCompositeParamsGPU) } });
        descMgr->WriteImageDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_SCENE_GBUF2,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetGbuffer2().GetSampler(), renderPassManager->GetGbuffer2().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_REFLECTION,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterReflectionImage.GetSampler(), m_WaterReflectionImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteImageDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_REFRACTION,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterRefractionImage.GetSampler(), m_WaterRefractionImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteImageDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_CAUSTICS,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterCausticsImage.GetSampler(), m_WaterCausticsImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        // W-15: 娉℃搏绾圭悊 鈥?浣跨敤鍙嶅皠璐村浘浣滀负 placeholder锛屽悗缁粠 VansWaterMaterial::m_FoamTexture 缁戝畾鐪熷疄绾圭悊
        descMgr->WriteImageDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_FOAM_TEXTURE,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterReflectionImage.GetSampler(), m_WaterReflectionImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        // W-16: 鍘氬害鍥?
        descMgr->WriteImageDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_THICKNESS,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterThicknessImage.GetSampler(), m_WaterThicknessImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        // W-16 Phase 2: SSS 鏁ｅ皠杈撳嚭
        descMgr->WriteImageDescriptor(
            m_CompPassSet, WATER_COMP_BINDING_SSS_SCATTER,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterSSSScatterImage.GetSampler(), m_WaterSSSScatterImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->CommitDescriptorUpdates();
    }

    // 鈹€鈹€ Water Effects Compute descriptor set锛圫et 0锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterEffectsCompute(
            m_EffectsLayout, sets, 1);
        m_EffectsSet = sets[0];
        descMgr->BeginDescriptorUpdate();

        descMgr->WriteImageDescriptor(
            m_EffectsSet, WATER_EFFECT_BINDING_GBUF_NORMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufNormal().GetSampler(), renderPassManager->GetWaterGBufNormal().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(
            m_EffectsSet, WATER_EFFECT_BINDING_GBUF_DEPTH,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufLinearDepth().GetSampler(), renderPassManager->GetWaterGBufLinearDepth().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(
            m_EffectsSet, WATER_EFFECT_BINDING_SCENE_GBUF2,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetGbuffer2().GetSampler(), renderPassManager->GetGbuffer2().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(
            m_EffectsSet, WATER_EFFECT_BINDING_SCENE_COLOR,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetColor().GetSampler(), renderPassManager->GetColor().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteBufferDescriptor(
            m_EffectsSet, WATER_EFFECT_BINDING_PARAMS,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { GetNativeBuffer(m_CompParamsBuffer, m_CompParamsBufferCreated), 0, sizeof(WaterCompositeParamsGPU) } });
        descMgr->WriteImageDescriptor(
            m_EffectsSet, WATER_EFFECT_BINDING_REFLECTION_OUT,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaterReflectionImage.GetSampler(), m_WaterReflectionImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteImageDescriptor(
            m_EffectsSet, WATER_EFFECT_BINDING_REFRACTION_OUT,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaterRefractionImage.GetSampler(), m_WaterRefractionImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteImageDescriptor(
            m_EffectsSet, WATER_EFFECT_BINDING_CAUSTICS_OUT,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaterCausticsImage.GetSampler(), m_WaterCausticsImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->CommitDescriptorUpdates();
    }

    // 鈹€鈹€ Water SSR Compute descriptor set锛圫et 0, W-12锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€
    // 寤惰繜鍒?HZB 鍙敤鏃跺垱寤猴紙瑙?EnsureSSRDescriptorSet锛?

    // 鈹€鈹€ Water Refraction Compute descriptor set锛圫et 0锛夆攢鈹€鈹€鈹€鈹€鈹€
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
            m_RefractionSet, WATER_REFRACTION_BINDING_SCENE_COLOR,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetColor().GetSampler(), renderPassManager->GetColor().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteBufferDescriptor(
            m_RefractionSet, WATER_REFRACTION_BINDING_PARAMS,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { GetNativeBuffer(m_CompParamsBuffer, m_CompParamsBufferCreated), 0, sizeof(WaterCompositeParamsGPU) } });
        descMgr->WriteImageDescriptor(
            m_RefractionSet, WATER_REFRACTION_BINDING_REFRACTION_OUT,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaterRefractionImage.GetSampler(), m_WaterRefractionImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } });

        descMgr->CommitDescriptorUpdates();
    }

    // 鈹€鈹€ Water Caustics Compute descriptor set锛圫et 0, W-14锛夆攢鈹€鈹€
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
            m_CausticsSet, WATER_CAUSTICS_BINDING_SCENE_GBUF2,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetGbuffer2().GetSampler(), renderPassManager->GetGbuffer2().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
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

    // 鈹€鈹€ W-16: Water Thickness Compute descriptor set 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
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

    // 鈹€鈹€ W-16 Phase 2: Water SSS Scatter Compute descriptor set 鈹€鈹€
    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterSSSScatterCompute(
            m_SSSScatterLayout, sets, 1);
        m_SSSScatterSet = sets[0];
        descMgr->BeginDescriptorUpdate();

        descMgr->WriteImageDescriptor(
            m_SSSScatterSet, WATER_SSS_SCATTER_BINDING_GBUF_NORMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufNormal().GetSampler(),
                renderPassManager->GetWaterGBufNormal().GetImageView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(
            m_SSSScatterSet, WATER_SSS_SCATTER_BINDING_GBUF_DEPTH,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufLinearDepth().GetSampler(),
                renderPassManager->GetWaterGBufLinearDepth().GetImageView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteImageDescriptor(
            m_SSSScatterSet, WATER_SSS_SCATTER_BINDING_THICKNESS_MAP,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterThicknessImage.GetSampler(),
                m_WaterThicknessImage.GetImageView(),
                VK_IMAGE_LAYOUT_GENERAL } });
        descMgr->WriteImageDescriptor(
            m_SSSScatterSet, WATER_SSS_SCATTER_BINDING_SCENE_GBUF2,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetGbuffer2().GetSampler(),
                renderPassManager->GetGbuffer2().GetImageView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });
        descMgr->WriteBufferDescriptor(
            m_SSSScatterSet, WATER_SSS_SCATTER_BINDING_PARAMS,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { GetNativeBuffer(m_SSSParamsBuffer, m_SSSParamsBufferCreated), 0, sizeof(float) * 16 } });
        descMgr->WriteImageDescriptor(
            m_SSSScatterSet, WATER_SSS_SCATTER_BINDING_SCATTER_OUT,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaterSSSScatterImage.GetSampler(),
                m_WaterSSSScatterImage.GetImageView(),
                VK_IMAGE_LAYOUT_GENERAL } });

        descMgr->CommitDescriptorUpdates();
    }

    // 鈹€鈹€ N-01: Detail Normal Compute descriptor set 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterDetailNormalCompute(
            m_DetailNormalLayout, sets, 1);
        m_DetailNormalSet = sets[0];
        descMgr->BeginDescriptorUpdate();

        // binding 0: WaterGBufferParams UBO锛堝鐢?m_GBufParamsBuffer锛?
        descMgr->WriteBufferDescriptor(
            m_DetailNormalSet, WATER_DETAIL_BINDING_PARAMS,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { GetNativeBuffer(m_GBufParamsBuffer, m_GBufParamsBufferCreated), 0, sizeof(WaterGBufferParamsGPU) } });
        // binding 1: Detail normal output storage image array
        descMgr->WriteImageDescriptor(
            m_DetailNormalSet, WATER_DETAIL_BINDING_OUTPUT,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_DetailNormalImage.GetSampler(),
                m_DetailNormalImage.GetImageView(),
                VK_IMAGE_LAYOUT_GENERAL } });
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

    // W-02: 濮旀墭 VansWaterLOD 娓呯悊缃戞牸缂撳啿
    if (m_WaterLOD)
    {
        m_WaterLOD->Shutdown(dev);
        delete m_WaterLOD;
        m_WaterLOD = nullptr;
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
    if (m_EffectsLayout != VK_NULL_HANDLE)    { descMgr->DestroyDescriptorSetLayout(m_EffectsLayout);  m_EffectsLayout  = VK_NULL_HANDLE; }
    if (m_SSRLayout != VK_NULL_HANDLE)        { descMgr->DestroyDescriptorSetLayout(m_SSRLayout);      m_SSRLayout      = VK_NULL_HANDLE; }
    if (m_RefractionLayout != VK_NULL_HANDLE) { descMgr->DestroyDescriptorSetLayout(m_RefractionLayout); m_RefractionLayout = VK_NULL_HANDLE; }
    if (m_ThicknessLayout != VK_NULL_HANDLE)   { descMgr->DestroyDescriptorSetLayout(m_ThicknessLayout); m_ThicknessLayout = VK_NULL_HANDLE; }
    if (m_SSSScatterLayout != VK_NULL_HANDLE) { descMgr->DestroyDescriptorSetLayout(m_SSSScatterLayout); m_SSSScatterLayout = VK_NULL_HANDLE; }
    if (m_CausticsLayout != VK_NULL_HANDLE)  { descMgr->DestroyDescriptorSetLayout(m_CausticsLayout);  m_CausticsLayout  = VK_NULL_HANDLE; }
    if (m_DetailNormalLayout != VK_NULL_HANDLE) { descMgr->DestroyDescriptorSetLayout(m_DetailNormalLayout); m_DetailNormalLayout = VK_NULL_HANDLE; }

    DestroyWaterBuffer(m_GBufParamsBuffer, m_GBufParamsBufferCreated, dev);
    DestroyWaterBuffer(m_CompParamsBuffer, m_CompParamsBufferCreated, dev);
    DestroyWaterBuffer(m_SSRParamsBuffer, m_SSRParamsBufferCreated, dev);
    DestroyWaterBuffer(m_CausticsParamsBuffer, m_CausticsParamsBufferCreated, dev);
    DestroyWaterBuffer(m_ThicknessParamsBuffer, m_ThicknessParamsBufferCreated, dev);
    DestroyWaterBuffer(m_SSSParamsBuffer, m_SSSParamsBufferCreated, dev);
    DestroyWaterBuffer(m_WaveSSBO, m_WaveSSBOCreated, dev);
    m_GBufParamsCache = {};

    m_WaveDisplacementImage.DestroyVulkanImage(dev);
    m_WaveDisplacementReady = false;
    m_WaveDerivativeImage.DestroyVulkanImage(dev);
    m_WaveDerivativeReady = false;
    m_WaterReflectionImage.DestroyVulkanImage(dev);
    m_WaterRefractionImage.DestroyVulkanImage(dev);
    m_WaterCausticsImage.DestroyVulkanImage(dev);
    m_WaterThicknessImage.DestroyVulkanImage(dev);
    m_WaterSSSScatterImage.DestroyVulkanImage(dev);
    m_DetailNormalImage.DestroyVulkanImage(dev);
    m_DetailNormalReady = false;
    m_WaterEffectsReady = false;

    delete m_WaterGBufferShader;    m_WaterGBufferShader   = nullptr;
    delete m_WaterCompositeShader;  m_WaterCompositeShader = nullptr;
    delete m_WaveSimShader;         m_WaveSimShader        = nullptr;
    delete m_WaterEffectsShader;    m_WaterEffectsShader   = nullptr;
    delete m_WaterSSRShader;        m_WaterSSRShader       = nullptr;
    delete m_WaterRefractionShader; m_WaterRefractionShader = nullptr;
    delete m_WaterCausticsShader;   m_WaterCausticsShader   = nullptr;
    delete m_DetailNormalShader;    m_DetailNormalShader    = nullptr;
    delete m_WaterThicknessShader;  m_WaterThicknessShader  = nullptr;
    delete m_WaterSSSScatterShader; m_WaterSSSScatterShader = nullptr;
    delete m_WaveSystem;    m_WaveSystem    = nullptr;

    m_Initialized      = false;
    m_DescriptorsReady = false;
    m_WaterMaterial    = nullptr;
    m_Device           = nullptr;
    VANS_LOG("[VansWaterSystem] Shutdown");
}

// ============================================================
// UpdateWaveSSBO 鈥?杩愯鏃堕噸鏂扮敓鎴愭尝鍒嗛噺骞朵笂浼犲埌 SSBO锛圵-04锛?
// 褰撶紪杈戝櫒淇敼娉㈠弬鏁帮紙娉㈡暟銆侀閫熴€佹秾娴箙搴︾瓑锛夋椂璋冪敤銆?
// ============================================================
void VansWaterSystem::UpdateWaveSSBO()
{
    if (!m_Initialized || !m_WaveSSBOCreated)
        return;
    if (!m_WaterMaterial)
        return;

    std::vector<GerstnerWaveGPU> waves;
    int   count  = m_WaterMaterial->m_GerstnerWaveCount;
    glm::vec2 windDir = m_WaterMaterial->m_WindDirection;
    float swell  = m_WaterMaterial->m_SwellAmplitude;
    float windSp = m_WaterMaterial->m_WindSpeed;

    AutoGenerateGerstnerWaves(waves, count, windDir, swell, windSp);

    VkDeviceSize size = waves.size() * sizeof(GerstnerWaveGPU);
    m_WaveSSBO.SetBufferData(waves.data(), 0, size);

    VANS_LOG("[VansWaterSystem] UpdateWaveSSBO: " << waves.size() << " waves regenerated");
}

// ============================================================
// Update 鈥?姣忓抚 CPU 绔姸鎬佹洿鏂?
// ============================================================
void VansWaterSystem::Update(float deltaTime, const glm::vec3& cameraPos,
                             const glm::mat4& viewMatrix, const glm::mat4& vpMatrix,
                             const glm::vec3& mainLightDir,
                             const glm::vec3& mainLightColor)
{
    m_Time += deltaTime;

    // 浠?WaterMaterial 璇诲彇杩愯鏃跺弬鏁帮紙鏀寔缂栬緫鍣ㄥ疄鏃惰皟鏁达級
    float ampScale   = 0.2f;
    float chopScale  = 1.5f;
    float baseScale  = 4.0f * VansWaterLOD::BASE_PATCH_SIZE;
    float maxAmp     = 0.6f;  // swellAmplitude(0.2) * 3
    if (m_WaterMaterial)
    {
        ampScale  = m_WaterMaterial->m_SwellAmplitude;
        chopScale = m_WaterMaterial->m_ChopScale;
        baseScale = (m_WaterMaterial->m_OceanBaseScale > 0.0f)
            ? m_WaterMaterial->m_OceanBaseScale
            : 4.0f * m_WaterMaterial->m_LODBasePatchSize;
        maxAmp    = m_WaterMaterial->m_SwellAmplitude * 3.0f;
        if (m_WaterMaterial->m_Config.m_Waves.m_Mode == VansWaveMode::FFT ||
            m_WaterMaterial->m_Config.m_Waves.m_Mode == VansWaveMode::Hybrid)
        {
            maxAmp = (std::max)(maxAmp,
                m_WaterMaterial->m_Config.m_Waves.m_FFT.m_SpectrumAmplitude * 3.0f);
        }
    }

    // W-02: 濮旀墭 VansWaterLOD 鐢熸垚 Patch
    if (m_WaterLOD)
    {
        VansWaterLODConfig lodConfig;
        if (m_WaterMaterial)
        {
            lodConfig.m_MaxLOD = m_WaterMaterial->m_MaxLODCount;
            lodConfig.m_BasePatchSize = m_WaterMaterial->m_LODBasePatchSize;
            lodConfig.m_MeshDim = m_WaterMaterial->m_LODMeshDim;
            lodConfig.m_DetailBalance = m_WaterMaterial->m_LODDetailBalance;
            lodConfig.m_MorphWidthRatio = m_WaterMaterial->m_LODMorphWidthRatio;
        }
        m_WaterLOD->SetLodConfig(lodConfig);
        m_WaterLOD->GeneratePatches(cameraPos);
    }

    // 姣忓抚鍐欏叆姘撮潰 pass 鑷湁鐩告満鏁版嵁
    WaterGBufferParamsGPU gbufParams = {};
    gbufParams.VPMatrix       = vpMatrix;
    gbufParams.ViewMatrix     = viewMatrix;
    gbufParams.cameraPosition = glm::vec4(cameraPos, 1.0f);
    gbufParams.minLodDist     = m_WaterLOD ? m_WaterLOD->GetBasePatchSize() : VansWaterLOD::BASE_PATCH_SIZE;
    gbufParams.lodLevels      = m_WaterLOD ? m_WaterLOD->GetLodLevels() : VansWaterLOD::MAX_LOD_COUNT;
    gbufParams.meshDim        = m_WaterLOD ? m_WaterLOD->GetMeshDim() : VansWaterLOD::WATER_MESH_DIM;
    gbufParams.clipmapBaseScale = baseScale;
    gbufParams.maxWaveAmp     = maxAmp;
    gbufParams.detailBalance   = m_WaterLOD ? m_WaterLOD->GetDetailBalance() : 2.0f;  // CPU/GPU 鍚屾锛氫粠 VansWaterLOD 杩愯鏃惰鍙?
    gbufParams.morphStartRatio = m_WaterLOD ? m_WaterLOD->GetMorphWidthRatio() : 0.5f;
    gbufParams.waveTimeAndScale = glm::vec4(m_Time, ampScale, chopScale, 1.0f);

    VansWaveMode waveMode = m_WaterMaterial ? m_WaterMaterial->m_Config.m_Waves.m_Mode : VansWaveMode::Gerstner;
    int fftLODCount = m_WaterMaterial ? m_WaterMaterial->m_Config.m_Waves.m_FFT.m_LODCount : 4;
    if (m_WaterMaterial && m_WaterMaterial->m_FftLODCount > 0)
        fftLODCount = m_WaterMaterial->m_FftLODCount;
    fftLODCount = std::clamp(fftLODCount, 1, (std::max)(gbufParams.lodLevels, 1));
    const bool useDerivativeNormal = m_WaterMaterial
        ? m_WaterMaterial->m_Config.m_Waves.m_FFT.m_UseDerivativeNormal
        : true;

    // N-01: Detail normal 鍙傛暟鍐欏叆 UBO padding
    if (m_WaterMaterial)
    {
        gbufParams.waveParamsPad[0] = glm::vec4(
            m_WaterMaterial->m_DetailNormalIntensity,
            m_WaterMaterial->m_DetailNormalScale,
            m_WaterMaterial->m_WindDirection.x,
            m_WaterMaterial->m_WindDirection.y
        );
        gbufParams.waveParamsPad[1] = glm::vec4(
            m_WaterMaterial->m_DetailNormalTimeOffset,
            float(m_WaterMaterial->m_DetailNormalOctaves),
            m_WaterMaterial->m_DetailNormalBaseScale,
            0.0f
        );
    }

    // FFT/Hybrid mode parameters for water_prepass.vert and partial Gerstner dispatch.
    gbufParams.waveParamsPad[2] = glm::vec4(
        float(static_cast<int>(waveMode)),
        useDerivativeNormal ? 1.0f : 0.0f,
        float(fftLODCount),
        float(m_WaterMaterial ? m_WaterMaterial->m_FftResolution : VansWaterFFT::FFT_RESOLUTION));
    gbufParams.waveParamsPad[3] = glm::vec4(0.0f, float(gbufParams.lodLevels), 0.0f, 0.0f);

    if (m_WaterFFT && m_WaterMaterial)
    {
        VansWaterFFT::Params fp;
        fp.resolution = VansWaterFFT::FFT_RESOLUTION;
        fp.lodCount = uint32_t(waveMode == VansWaveMode::Hybrid ? fftLODCount : gbufParams.lodLevels);
        fp.baseScale = baseScale;
        fp.detailBalance = gbufParams.detailBalance;
        fp.windDirection = m_WaterMaterial->m_WindDirection;
        fp.windSpeed = m_WaterMaterial->m_WindSpeed;
        fp.spectrumAmplitude = m_WaterMaterial->m_Config.m_Waves.m_FFT.m_SpectrumAmplitude;
        fp.choppiness = m_WaterMaterial->m_Config.m_Waves.m_FFT.m_Choppiness;
        fp.smallWaveDamping = m_WaterMaterial->m_Config.m_Waves.m_FFT.m_SmallWaveDamping;
        fp.windDependency = m_WaterMaterial->m_Config.m_Waves.m_FFT.m_WindDependency;
        fp.depth = m_WaterMaterial->m_Config.m_Waves.m_FFT.m_Depth;
        fp.repeatPeriod = m_WaterMaterial->m_Config.m_Waves.m_FFT.m_RepeatPeriod;
        fp.maxWaveAmp = maxAmp;
        fp.normalScale = gbufParams.waveTimeAndScale.w;
        fp.foamSlopeScale = m_WaterMaterial->m_Config.m_Waves.m_FFT.m_FoamSlopeScale;
        fp.foamFoldScale = m_WaterMaterial->m_Config.m_Waves.m_FFT.m_FoamFoldScale;
        fp.foamFoldThreshold = m_WaterMaterial->m_Config.m_Waves.m_FFT.m_FoamFoldThreshold;
        fp.randomSeed = m_WaterMaterial->m_Config.m_Waves.m_FFT.m_RandomSeed;
        m_WaterFFT->SetParams(fp);
    }

    m_GBufParamsCache = gbufParams;
    if (m_Device != nullptr && m_GBufParamsBufferCreated)
        m_GBufParamsBuffer.SetBufferData(&m_GBufParamsCache, 0, sizeof(WaterGBufferParamsGPU));

    // 姣忓抚鍏ㄩ噺鏇存柊 composite UBO锛氫粠 VansWaterMaterial 璇诲彇鎵€鏈変粙璐ㄥ弬鏁?+
    // 鐩告満浣嶇疆 + invViewProj + 涓诲厜鏂瑰悜锛堢‘淇?Editor 淇敼瀹炴椂鐢熸晥锛?
    if (m_Device != nullptr && m_CompParamsBufferCreated)
    {
        WaterCompositeParamsGPU compParams = {};
        if (m_WaterMaterial)
        {
            compParams.deepWaterColor    = m_WaterMaterial->m_DeepWaterColor;
            compParams.shallowWaterColor = m_WaterMaterial->m_ShallowWaterColor;
            compParams.fresnelPower      = m_WaterMaterial->m_FresnelPower;
            compParams.waterLevel        = m_WaterLevel;
            compParams.specularIntensity = m_WaterMaterial->m_SpecularIntensity;
            compParams.foamIntensity     = m_WaterMaterial->m_FoamIntensity;
            compParams.absorptionCoeff   = glm::vec4(m_WaterMaterial->m_AbsorptionCoeffs, 1.0f);
            compParams.scatteringCoeff   = glm::vec4(m_WaterMaterial->m_ScatteringCoeffs, 1.0f);
            compParams.sssAnisotropy     = m_WaterMaterial->m_Anisotropy;
            compParams.waterRoughness    = m_WaterMaterial->m_WaterRoughness;
            compParams.waterIOR          = m_WaterMaterial->m_WaterIOR;
        }
        else
        {
            // fallback 榛樿鍙傛暟
            compParams.deepWaterColor    = glm::vec4(0.01f, 0.04f, 0.18f, 1.0f);
            compParams.shallowWaterColor = glm::vec4(0.05f, 0.18f, 0.55f, 1.0f);
            compParams.fresnelPower      = 5.0f;
            compParams.waterLevel        = m_WaterLevel;
            compParams.specularIntensity = 0.6f;
            compParams.foamIntensity     = 1.0f;
            compParams.absorptionCoeff   = glm::vec4(0.25f, 0.08f, 0.02f, 1.0f);
            compParams.scatteringCoeff   = glm::vec4(0.02f, 0.04f, 0.06f, 1.0f);
            compParams.sssAnisotropy     = 0.85f;
            compParams.waterRoughness    = 0.02f;
            compParams.waterIOR          = 1.33f;
        }
        compParams.cameraPosition  = glm::vec4(cameraPos, 1.0f);
        compParams.invViewProjMatrix = glm::inverse(vpMatrix);
        compParams.mainLightDir    = glm::vec4(glm::normalize(mainLightDir), 0.0f);
        compParams.viewMatrix      = viewMatrix;
        compParams.projMatrix      = vpMatrix * glm::inverse(viewMatrix);  // proj = VP * inv(View)

        m_CompParamsBuffer.SetBufferData(&compParams, 0, sizeof(WaterCompositeParamsGPU));
    }

    // 鈹€鈹€ 鏇存柊 SSR Params UBO锛圵-12锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    if (m_Device != nullptr && m_SSRParamsBufferCreated)
    {
        WaterSSRParamsGPU ssrParams = {};
        ssrParams.cameraPosition = glm::vec4(cameraPos, 1.0f);
        ssrParams.projMatrix     = vpMatrix * glm::inverse(viewMatrix);
        ssrParams.invProjMatrix  = glm::inverse(ssrParams.projMatrix);
        ssrParams.viewMatrix     = viewMatrix;
        // Inspector optimization: read SSR params from material
        if (m_WaterMaterial)
        {
            ssrParams.maxDistance  = m_WaterMaterial->m_SSRMaxDistance;
            ssrParams.maxSteps     = 64;
            ssrParams.thickness    = 1.0f;
            ssrParams.maxRoughness = m_WaterMaterial->m_SSRMaxRoughness;
        }
        else
        {
            ssrParams.maxDistance  = 500.0f;
            ssrParams.maxSteps     = 64;
            ssrParams.thickness    = 1.0f;
            ssrParams.maxRoughness = 0.3f;
        }

        m_SSRParamsBuffer.SetBufferData(&ssrParams, 0, sizeof(WaterSSRParamsGPU));
    }

    // 鈹€鈹€ 鏇存柊 Caustics Params UBO锛圵-14锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    if (m_Device != nullptr && m_CausticsParamsBufferCreated)
    {
        WaterCausticsParamsGPU causticParams = {};
        causticParams.sunDirection     = glm::vec4(glm::normalize(mainLightDir), 0.0f);
        causticParams.mainLightColor   = glm::vec4(mainLightColor, 1.0f);
        if (m_WaterMaterial)
        {
            glm::vec3 ext = m_WaterMaterial->m_AbsorptionCoeffs + m_WaterMaterial->m_ScatteringCoeffs;
            causticParams.extinctionCoeff = glm::vec4(ext, 0.0f);
            causticParams.causticsIntensity = m_WaterMaterial->m_CausticsIntensity;
            causticParams.causticsScale     = m_WaterMaterial->m_CausticsScale;
        }
        else
        {
            causticParams.extinctionCoeff  = glm::vec4(0.27f, 0.12f, 0.08f, 0.0f);
            causticParams.causticsIntensity = 1.0f;
            causticParams.causticsScale     = 0.5f;
        }
        causticParams.waterLevel = m_WaterLevel;
        causticParams.maxDepth   = 15.0f;

        m_CausticsParamsBuffer.SetBufferData(&causticParams, 0, sizeof(WaterCausticsParamsGPU));
    }

    // 鈹€鈹€ W-16: 鏇存柊 Thickness Params UBO 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    if (m_Device != nullptr && m_ThicknessParamsBufferCreated)
    {
        ThicknessParamsGPU tp = {};
        tp.maxThickness = 15.0f;
        tp.deepFallback = 0.8f;
        if (m_WaterMaterial)
        {
            tp.maxThickness = m_WaterMaterial->m_MaxThicknessDistance;
            tp.deepFallback = m_WaterMaterial->m_DeepWaterThicknessFallback;
        }
        m_ThicknessParamsBuffer.SetBufferData(&tp, 0, sizeof(ThicknessParamsGPU));
    }

    // 鈹€鈹€ W-16 Phase 2: 鏇存柊 SSS Params UBO 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    if (m_Device != nullptr && m_SSSParamsBufferCreated)
    {
        SSSParamsGPU sp = {};
        sp.absorptionCoeff = glm::vec4(0.25f, 0.08f, 0.02f, 1.0f);
        sp.scatteringCoeff = glm::vec4(0.02f, 0.04f, 0.06f, 1.0f);
        sp.maxThickness = 15.0f;
        sp.anisotropy = 0.85f;
        if (m_WaterMaterial)
        {
            sp.absorptionCoeff = glm::vec4(m_WaterMaterial->m_AbsorptionCoeffs, 1.0f);
            sp.scatteringCoeff = glm::vec4(m_WaterMaterial->m_ScatteringCoeffs, 1.0f);
            sp.maxThickness    = m_WaterMaterial->m_MaxThicknessDistance;
            sp.anisotropy      = m_WaterMaterial->m_Anisotropy;
        }
        m_SSSParamsBuffer.SetBufferData(&sp, 0, sizeof(SSSParamsGPU));
    }
}

// ============================================================
// UpdateWaveSimulation 鈥?Compute 鐢熸垚 Gerstner 娉㈠舰浣嶇Щ璐村浘锛圵-01: 閫?LOD锛?
// ============================================================
void VansWaterSystem::UpdateWaveSimulation(VansVKCommandBuffer& cmd, float /*deltaTime*/)
{
    if (!m_Initialized || !m_DescriptorsReady)
        return;

    const VansWaveMode mode = m_WaterMaterial
        ? m_WaterMaterial->m_Config.m_Waves.m_Mode
        : VansWaveMode::Gerstner;
    const int lodLevels = m_WaterLOD ? m_WaterLOD->GetLodLevels() : int(VansWaterLOD::MAX_LOD_COUNT);
    const int fftLODCount = std::clamp(
        m_WaterMaterial ? m_WaterMaterial->m_Config.m_Waves.m_FFT.m_LODCount : 4,
        1,
        (std::max)(lodLevels, 1));

    auto updateGerstnerDispatchParams = [&](int outputBaseLod, int outputLodCount, bool disableSpectrumApprox)
    {
        if (!m_GBufParamsBufferCreated)
            return;

        m_GBufParamsCache.waveParamsPad[3] = glm::vec4(
            float(outputBaseLod),
            float(outputLodCount),
            disableSpectrumApprox ? 1.0f : 0.0f,
            0.0f);
        m_GBufParamsBuffer.SetBufferData(&m_GBufParamsCache, 0, sizeof(WaterGBufferParamsGPU));
    };

    auto runGerstner = [&](int outputBaseLod, int outputLodCount, bool disableSpectrumApprox)
    {
        if (outputLodCount <= 0 || m_WaveSimShader == nullptr || m_WaveSimSet == VK_NULL_HANDLE)
            return;

        updateGerstnerDispatchParams(outputBaseLod, outputLodCount, disableSpectrumApprox);

        const VkImageLayout currentLayout = m_WaveDisplacementReady
            ? VK_IMAGE_LAYOUT_GENERAL
            : m_WaveDisplacementImage.GetImageLayout();

        VkImageMemoryBarrier beforeCompute = {};
        beforeCompute.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        beforeCompute.srcAccessMask       = m_WaveDisplacementReady ? VK_ACCESS_SHADER_READ_BIT : 0;
        beforeCompute.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        beforeCompute.oldLayout           = currentLayout;
        beforeCompute.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        beforeCompute.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        beforeCompute.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        beforeCompute.image               = m_WaveDisplacementImage.GetImage();
        beforeCompute.subresourceRange    = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
            uint32_t((std::max)(outputBaseLod, 0)),
            uint32_t((std::max)(outputLodCount, 0))
        };
        cmd.PipelineBarrier(
            m_WaveDisplacementReady ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            {}, {}, { beforeCompute });
        m_WaveDisplacementImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);

        cmd.EnsureComputeShader(*m_WaveSimShader, { m_WaveSimLayout });
        const uint32_t groups = (WAVE_TEXTURE_SIZE + 7u) / 8u;
        cmd.DispatchCompute(*m_WaveSimShader, groups, groups, uint32_t(outputLodCount), { m_WaveSimSet });

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
        cmd.PipelineBarrier(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            {}, {}, { afterCompute });

        m_WaveDisplacementReady = true;
    };

    if (mode == VansWaveMode::FFT)
    {
        if (m_WaterFFT && m_WaterFFT->IsReady())
        {
            m_WaterFFT->UpdateFFT(cmd, m_Time);
            m_WaveDisplacementReady = true;
            m_WaveDerivativeReady = true;
            return;
        }

        static bool s_FFTFallbackLogged = false;
        if (!s_FFTFallbackLogged)
        {
            VANS_LOG_WARN("[VansWaterSystem] FFT requested but not ready; falling back to Gerstner");
            s_FFTFallbackLogged = true;
        }
        runGerstner(0, lodLevels, false);
        return;
    }

    if (mode == VansWaveMode::Hybrid)
    {
        bool fftOk = false;
        if (m_WaterFFT && m_WaterFFT->IsReady() && fftLODCount > 0)
        {
            m_WaterFFT->UpdateFFT(cmd, m_Time);
            fftOk = true;
            m_WaveDisplacementReady = true;
            m_WaveDerivativeReady = true;
        }

        if (fftOk && fftLODCount < lodLevels)
            runGerstner(fftLODCount, lodLevels - fftLODCount, true);
        else if (!fftOk)
            runGerstner(0, lodLevels, false);
        return;
    }

    runGerstner(0, lodLevels, false);
}

// ============================================================
// UpdateDetailNormalCompute 鈥?N-01: Compute 鐢熸垚缁嗚妭娉曠嚎璐村浘
// ============================================================
void VansWaterSystem::UpdateDetailNormalCompute(VansVKCommandBuffer& cmd)
{
    if (!m_Initialized || !m_DescriptorsReady ||
        m_DetailNormalShader == nullptr || m_DetailNormalSet == VK_NULL_HANDLE)
        return;

    const uint32_t detailLayerCount = uint32_t((std::max)(
        m_WaterLOD ? m_WaterLOD->GetLodLevels() : int(VansWaterLOD::MAX_LOD_COUNT),
        1));

    // Barrier: FRAGMENT read -> COMPUTE write.
    const VkImageLayout currentLayout = m_DetailNormalReady
        ? VK_IMAGE_LAYOUT_GENERAL
        : m_DetailNormalImage.GetImageLayout();

    VkImageMemoryBarrier beforeCompute = {};
    beforeCompute.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    beforeCompute.srcAccessMask       = m_DetailNormalReady ? VK_ACCESS_SHADER_READ_BIT : 0;
    beforeCompute.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    beforeCompute.oldLayout           = currentLayout;
    beforeCompute.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    beforeCompute.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beforeCompute.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beforeCompute.image               = m_DetailNormalImage.GetImage();
    beforeCompute.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, detailLayerCount };
    cmd.PipelineBarrier(
        m_DetailNormalReady ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        {}, {}, { beforeCompute });
    m_DetailNormalImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);

    // Dispatch one layer per active water LOD, local 8x8.
    cmd.EnsureComputeShader(*m_DetailNormalShader, { m_DetailNormalLayout });
    const uint32_t groups = (DETAIL_TEXTURE_SIZE + 7u) / 8u;
    cmd.DispatchCompute(*m_DetailNormalShader, groups, groups, detailLayerCount, { m_DetailNormalSet });

    // Barrier: COMPUTE write -> FRAGMENT read.
    VkImageMemoryBarrier afterCompute = {};
    afterCompute.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    afterCompute.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    afterCompute.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    afterCompute.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
    afterCompute.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    afterCompute.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    afterCompute.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    afterCompute.image               = m_DetailNormalImage.GetImage();
    afterCompute.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, detailLayerCount };
    cmd.PipelineBarrier(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        {}, {}, { afterCompute });

    m_DetailNormalReady = true;
}

// ============================================================
// RenderWaterGBuffer 鈥?璁捐鏂囨。 Pass 7
// ============================================================
void VansWaterSystem::RenderWaterGBuffer(VansVKCommandBuffer& cmd, GlobalStateData& globalState)
{
    static int s_DbgFrame = 0;
    bool dbgLog = (s_DbgFrame++ % 120) == 0;

    if (!m_Initialized || !m_DescriptorsReady || !m_WaterLOD || m_WaterLOD->GetPatchCount() == 0)
    {
        if (dbgLog)
            VANS_LOG_WARN("[WaterGBuffer] EARLY RETURN: init/descReady/patches not ready");
        return;
    }
    if (m_WaterGBufferShader == nullptr || m_WaterLOD->GetVertexBuffer() == VK_NULL_HANDLE)
    {
        if (dbgLog)
            VANS_LOG_WARN("[WaterGBuffer] EARLY RETURN: shader or vertex buffer is null");
        return;
    }

    if (dbgLog)
    {
        VANS_LOG("[WaterGBuffer] Render: init=" << m_Initialized
                 << " descReady=" << m_DescriptorsReady
                 << " patches=" << m_WaterLOD->GetPatchCount()
                 << " shader=" << (m_WaterGBufferShader != nullptr)
                 << " waterLevel=" << m_WaterLevel);
    }

    // 鈹€鈹€ 璁剧疆椤剁偣杈撳叆甯冨眬 鈹€鈹€
    globalState.vertexInputBindingDescriptions   = &m_WaterLOD->GetVertexBindings();
    globalState.vertexInputAttributeDescriptions = &m_WaterLOD->GetVertexAttributes();

    // 鈹€鈹€ Descriptor Set 甯冨眬鏁扮粍锛歔Set 0: Global, Set 1: WaterGBuf Pass]
    std::vector<VkDescriptorSetLayout> layouts = { m_GlobalLayout, m_GBufPassLayout };
    std::vector<VkDescriptorSet>       sets    = { m_GlobalSet,    m_GBufPassSet    };

    cmd.EnsureGraphicsShader(*m_WaterGBufferShader, globalState, layouts);
    cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
        *m_WaterGBufferShader, 0, sets, {});
    cmd.BindGraphicsPipeline(*m_WaterGBufferShader->GetGraphicsPipeline());

    // 鈹€鈹€ 缁戝畾鍏辩敤椤剁偣 / 绱㈠紩缂撳啿锛堟潵鑷?VansWaterLOD锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    VkDeviceSize offset = 0;
    VkBuffer vbuf = m_WaterLOD->GetVertexBuffer();
    VkBuffer ibuf = m_WaterLOD->GetIndexBuffer();
    cmd.BindVertexBuffers(0, 1, &vbuf, &offset);
    cmd.BindIndexBuffer(ibuf, 0, VK_INDEX_TYPE_UINT32);

    const std::vector<CDLODPatch>& patches = m_WaterLOD->GetPatches();
    uint32_t indexCount = m_WaterLOD->GetIndexCount();

    // 鈹€鈹€ 閫?Patch 鎺ㄩ€佸父閲?+ DrawIndexed 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    for (auto patchIter = patches.rbegin(); patchIter != patches.rend(); ++patchIter)
    {
        const CDLODPatch& patch = *patchIter;
        WaterPatchPushConstant pc = {};
        pc.patchWorldOrigin = patch.worldOrigin;
        pc.patchWorldSize   = patch.worldSize;
        pc.lodLevel         = patch.lodLevel;
        pc.waterLevel       = m_WaterLevel;
        pc.outerEdgeMask    = patch.outerEdgeMask;
        pc.innerEdgeMask    = patch.innerEdgeMask;

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
    if (m_WaterMaterial && !m_WaterMaterial->m_EnableSSR)
        return;

    // 鈹€鈹€ Water SSR (HZB Ray March) 鈫?Reflection 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    if (m_WaterSSRShader != nullptr && m_SSRSet != VK_NULL_HANDLE)
    {
        VkImageMemoryBarrier beforeSSR = {};
        beforeSSR.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        beforeSSR.srcAccessMask = 0;
        beforeSSR.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        beforeSSR.oldLayout = m_WaterReflectionImage.GetImageLayout();
        beforeSSR.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        beforeSSR.image = m_WaterReflectionImage.GetImage();
        beforeSSR.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        m_WaterReflectionImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
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

    m_WaterEffectsReady = true;
}

// ============================================================
// EnsureSSRDescriptorSet 鈥?HZB 鍙敤鏃跺欢杩熷垱寤?SSR descriptor set
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
    if (m_WaterMaterial && !m_WaterMaterial->m_EnableRefraction)
        return;

    if (m_WaterRefractionShader == nullptr || m_RefractionSet == VK_NULL_HANDLE)
        return;

    // Barrier: refraction image 鈫?compute write
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask       = m_WaterEffectsReady ? VK_ACCESS_SHADER_READ_BIT : 0;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.oldLayout           = m_WaterEffectsReady ? VK_IMAGE_LAYOUT_GENERAL : m_WaterRefractionImage.GetImageLayout();
        barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = m_WaterRefractionImage.GetImage();
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        m_WaterRefractionImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
        cmd.PipelineBarrier(
            m_WaterEffectsReady ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            {}, {}, { barrier });
    }

    cmd.EnsureComputeShader(*m_WaterRefractionShader, { m_RefractionLayout });
    cmd.DispatchCompute(*m_WaterRefractionShader,
        (m_RenderWidth + 7u) / 8u,
        (m_RenderHeight + 7u) / 8u,
        1,
        { m_RefractionSet });

    // Barrier: refraction image 鈫?fragment read (for composite pass)
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
}
void VansWaterSystem::DispatchCausticsCS(VansVKCommandBuffer& cmd)
{
    if (!m_Initialized || !m_DescriptorsReady) return;

    // Inspector optimization: Caustics enable guard
    if (m_WaterMaterial && !m_WaterMaterial->m_EnableCaustics)
        return;

    if (m_WaterCausticsShader == nullptr || m_CausticsSet == VK_NULL_HANDLE) return;

    // Barrier: caustics image 鈫?compute write
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask       = m_WaterEffectsReady ? VK_ACCESS_SHADER_READ_BIT : 0;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.oldLayout           = m_WaterEffectsReady ? VK_IMAGE_LAYOUT_GENERAL
                                                          : m_WaterCausticsImage.GetImageLayout();
        barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.image               = m_WaterCausticsImage.GetImage();
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        m_WaterCausticsImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
        cmd.PipelineBarrier(
            m_WaterEffectsReady ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            {}, {}, { barrier });
    }

    cmd.EnsureComputeShader(*m_WaterCausticsShader, { m_CausticsLayout });
    cmd.DispatchCompute(*m_WaterCausticsShader,
        (m_RenderWidth  + 7u) / 8u,
        (m_RenderHeight + 7u) / 8u,
        1, { m_CausticsSet });

    // Barrier: caustics image 鈫?fragment read
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
}

// ============================================================
// DispatchWaterThicknessCS 鈥?W-16 闃舵1: 鍘氬害鍥撅紙璁捐鏂囨。 搂3.2锛?
// ============================================================
void VansWaterSystem::DispatchWaterThicknessCS(VansVKCommandBuffer& cmd)
{
    if (!m_Initialized || !m_DescriptorsReady)
        return;

    // Inspector optimization: SSS enable guard
    if (m_WaterMaterial && !m_WaterMaterial->m_SSSEnabled)
        return;

    if (m_WaterThicknessShader == nullptr || m_ThicknessSet == VK_NULL_HANDLE)
        return;

    // Barrier: thickness image 鈫?compute write
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask       = m_WaterEffectsReady ? VK_ACCESS_SHADER_READ_BIT : 0;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.oldLayout           = m_WaterEffectsReady ? VK_IMAGE_LAYOUT_GENERAL : m_WaterThicknessImage.GetImageLayout();
        barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = m_WaterThicknessImage.GetImage();
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        m_WaterThicknessImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
        cmd.PipelineBarrier(
            m_WaterEffectsReady ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            {}, {}, { barrier });
    }

    cmd.EnsureComputeShader(*m_WaterThicknessShader, { m_ThicknessLayout });
    cmd.DispatchCompute(*m_WaterThicknessShader,
        (m_RenderWidth  + 7u) / 8u,
        (m_RenderHeight + 7u) / 8u,
        1, { m_ThicknessSet });

    // Barrier: thickness image 鈫?fragment/compute read (for SSS scatter + composite)
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
}

// ============================================================
// DispatchWaterSSSScatterCS 鈥?W-16 闃舵2: SSS 鍗曟鏁ｅ皠锛堣璁℃枃妗?搂3.3锛?
// ============================================================
void VansWaterSystem::DispatchWaterSSSScatterCS(VansVKCommandBuffer& cmd)
{
    if (!m_Initialized || !m_DescriptorsReady)
        return;

    // Inspector optimization: SSS enable guard
    if (m_WaterMaterial && !m_WaterMaterial->m_SSSEnabled)
        return;

    if (m_WaterSSSScatterShader == nullptr || m_SSSScatterSet == VK_NULL_HANDLE)
        return;

    // Barrier: SSS scatter image 鈫?compute write
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask       = m_WaterEffectsReady ? VK_ACCESS_SHADER_READ_BIT : 0;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.oldLayout           = m_WaterEffectsReady ? VK_IMAGE_LAYOUT_GENERAL : m_WaterSSSScatterImage.GetImageLayout();
        barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = m_WaterSSSScatterImage.GetImage();
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        m_WaterSSSScatterImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
        cmd.PipelineBarrier(
            m_WaterEffectsReady ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            {}, {}, { barrier });
    }

    cmd.EnsureComputeShader(*m_WaterSSSScatterShader, { m_SSSScatterLayout });
    cmd.DispatchCompute(*m_WaterSSSScatterShader,
        (m_RenderWidth  + 7u) / 8u,
        (m_RenderHeight + 7u) / 8u,
        1, { m_SSSScatterSet });

    // Barrier: SSS scatter image 鈫?fragment read (for composite pass)
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = m_WaterSSSScatterImage.GetImage();
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        cmd.PipelineBarrier(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            {}, {}, { barrier });
    }
}

// ============================================================
// RenderWaterComposite 鈥?璁捐鏂囨。 Pass 9锛堝叏灞?1鈻?寤惰繜鍚堟垚锛?
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
