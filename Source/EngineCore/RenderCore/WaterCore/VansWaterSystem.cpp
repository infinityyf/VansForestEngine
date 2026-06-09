#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansWaterSystem.h"
#include "VansWaterWaveSystem.h"
#include "../../Util/VansLog.h"
#include "../../Configration/VansConfigration.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../VulkanCore/VansRenderPass.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../VulkanCore/VansPipeline.h"
#include <cmath>
#include <cstring>
#include <string>
#include <algorithm>
#include <cstddef>

namespace VansGraphics
{

namespace
{
    // W-04: 自动生成对数分布的 Gerstner 波分量
    void AutoGenerateGerstnerWaves(std::vector<GerstnerWaveGPU>& waves,
                                    int count, const glm::vec2& windDir,
                                    float swellAmplitude, float windSpeed)
    {
        waves.clear();
        waves.reserve(count);
        glm::vec2 dir = glm::normalize(windDir);
        if (glm::length(dir) < 0.001f)
            dir = glm::vec2(0.7071f, 0.7071f);

        // 波长从 256m 到 0.5m 对数分布（扩展上限以覆盖粗 LOD Nyquist 过滤）
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
            float speed = omega / k;  // 相速度

            // 振幅随波长减小（短波能量小），加入一定随机性
            float baseAmp = swellAmplitude * std::powf(wavelength / maxWL, 0.75f);
            // 方向：风方向 + 小角度扩散
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
            wave.steepness  = 0.05f + 0.55f * (1.0f - t);  // 长波更陡
            wave.pad0       = 0.0f;
            wave.pad1       = 0.0f;
            waves.push_back(wave);
        }
    }
}

// ============================================================
// AllocateBuffer — 封装 vkCreateBuffer + vkAllocateMemory
// ============================================================
bool VansWaterSystem::AllocateBuffer(
    VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
    VkBuffer& outBuffer, VkDeviceMemory& outMemory)
{
    VkDevice device = m_Device->GetLogicDevice();

    VkBufferCreateInfo ci = {};
    ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size        = size;
    ci.usage       = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &ci, nullptr, &outBuffer) != VK_SUCCESS)
    {
        VANS_LOG_ERROR("[VansWaterSystem] vkCreateBuffer failed");
        return false;
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, outBuffer, &memReq);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_Device->GetPhysicalDevice(), &memProps);
    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    {
        bool typeBit  = (memReq.memoryTypeBits >> i) & 1u;
        bool propsFit = (memProps.memoryTypes[i].propertyFlags & props) == props;
        if (typeBit && propsFit)
        {
            memTypeIndex = i;
            break;
        }
    }
    if (memTypeIndex == UINT32_MAX)
    {
        VANS_LOG_ERROR("[VansWaterSystem] no suitable memory type");
        vkDestroyBuffer(device, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo ai = {};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = memReq.size;
    ai.memoryTypeIndex = memTypeIndex;
    if (vkAllocateMemory(device, &ai, nullptr, &outMemory) != VK_SUCCESS)
    {
        VANS_LOG_ERROR("[VansWaterSystem] vkAllocateMemory failed");
        vkDestroyBuffer(device, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(device, outBuffer, outMemory, 0);
    return true;
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

    // ── 1. 创建 CDLOD 管理器（W-02: VansWaterLOD 独立类）────────
    m_WaterLOD = new VansWaterLOD();
    m_WaterLOD->Initialize(device,
        VansWaterLOD::MAX_LOD_COUNT,
        VansWaterLOD::MIN_LOD_DIST,
        VansWaterLOD::WATER_MESH_DIM,
        VansWaterLOD::BASE_PATCH_SIZE);

    // ── 2. 编译着色器 ─────────────────────────────────────────
    auto*       cfg         = VansConfigration::GetInstance();
    std::string projectRoot = cfg->GetProjectRootPath();
    VkDevice    logicDev    = device->GetLogicDevice();

    m_WaterGBufferShader = new VansGraphicsShader();
    m_WaterGBufferShader->InitShader(logicDev,
        (projectRoot + "EngineAssets/Shaders/Water/WaterGBuffer").c_str());
    m_WaterGBufferShader->SetPushConstant(sizeof(WaterPatchPushConstant));
    m_WaterGBufferShader->SetDrawStateData(
        VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS, VK_CULL_MODE_NONE);
    // CDLOD 填充模式（生产环境）
    m_WaterGBufferShader->SetPolygonMode(VK_POLYGON_MODE_FILL);
    m_WaterGBufferShader->SetColorAttachmentCount(2);

    // Wave compute shader（W-01: Texture2DArray + SSBO + Nyquist）
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

    m_WaterCompositeShader = new VansGraphicsShader();
    m_WaterCompositeShader->InitShader(logicDev,
        (projectRoot + "EngineAssets/Shaders/Water/WaterComposite").c_str());
    m_WaterCompositeShader->SetDrawStateData(
        VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS, VK_CULL_MODE_NONE);
    m_WaterCompositeShader->SetColorAttachmentCount(1);

    // ── 3. 创建 WaterGBufferParams UBO ─────────────────────────
    AllocateBuffer(sizeof(WaterGBufferParamsGPU),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_GBufParamsBuffer, m_GBufParamsMemory);

    WaterGBufferParamsGPU gbufParams = {};
    gbufParams.VPMatrix       = glm::mat4(1.0f);
    gbufParams.ViewMatrix     = glm::mat4(1.0f);
    gbufParams.cameraPosition = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    gbufParams.minLodDist     = VansWaterLOD::MIN_LOD_DIST;
    gbufParams.lodLevels      = VansWaterLOD::MAX_LOD_COUNT;
    gbufParams.meshDim        = VansWaterLOD::WATER_MESH_DIM;
    gbufParams.clipmapBaseScale = 128.0f;
    gbufParams.maxWaveAmp      = 0.6f;  // swellAmplitude(0.2) * 3
    gbufParams.detailBalance   = m_WaterLOD ? m_WaterLOD->GetDetailBalance() : 2.0f;
    gbufParams.morphStartRatio = 0.6f;
    gbufParams.waveTimeAndScale = glm::vec4(0.0f, 0.2f, 1.5f, 1.0f);  // Initialize() initial UBO
    {
        void* data = nullptr;
        vkMapMemory(logicDev, m_GBufParamsMemory, 0, sizeof(WaterGBufferParamsGPU), 0, &data);
        std::memcpy(data, &gbufParams, sizeof(WaterGBufferParamsGPU));
        vkUnmapMemory(logicDev, m_GBufParamsMemory);
    }

    // ── 4. 创建波形位移贴图（W-01: Texture2DArray, 256² × MAX_LOD_COUNT）──
    // CLAMP_TO_EDGE：贴图覆盖 snappedOrigin ± lodScale/2 的世界范围，
    // 边界外的 Patch 由 CDLOD 距离环约束保证不会采样到，CLAMP 作为安全网
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

    // ── 5. 创建水体效果贴图（CLAMP_TO_EDGE 防止边缘平铺伪影）──
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
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);  // 屏幕空间效果贴图不应 repeat
    };
    createEffectImage(m_WaterReflectionImage);
    createEffectImage(m_WaterRefractionImage);
    createEffectImage(m_WaterCausticsImage);
    createEffectImage(m_WaterThicknessImage);  // W-16: CLAMP_TO_EDGE 同上

    // ── 6. 创建 WaterCompositeParams UBO ──────────────────────
    AllocateBuffer(sizeof(WaterCompositeParamsGPU),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_CompParamsBuffer, m_CompParamsMemory);

    WaterCompositeParamsGPU compParams = {};
    // 蓝色水体默认参数（物理方向：红光吸收最快，蓝光穿透最深）
    compParams.deepWaterColor    = glm::vec4(0.01f, 0.04f, 0.18f, 1.0f);   // 深水暗蓝
    compParams.shallowWaterColor = glm::vec4(0.05f, 0.18f, 0.55f, 1.0f);   // 浅水亮蓝（散射色）
    compParams.fresnelPower      = 5.0f;
    compParams.waterLevel        = m_WaterLevel;
    compParams.specularIntensity = 0.6f;
    compParams.foamIntensity     = 1.0f;
    compParams.absorptionCoeff   = glm::vec4(0.25f, 0.08f, 0.02f, 1.0f);  // R>G>B, 消光 0.27>0.12>0.08
    compParams.scatteringCoeff   = glm::vec4(0.02f, 0.04f, 0.06f, 1.0f);  // B>G>R, 蓝光穿透最深
    compParams.sssAnisotropy     = 0.85f;
    compParams.waterRoughness    = 0.02f;
    compParams.cameraPosition    = glm::vec4(0.0f, 10.0f, 0.0f, 1.0f);
    compParams.viewMatrix        = glm::mat4(1.0f);
    compParams.projMatrix        = glm::mat4(1.0f);
    {
        void* data = nullptr;
        vkMapMemory(logicDev, m_CompParamsMemory, 0, sizeof(WaterCompositeParamsGPU), 0, &data);
        std::memcpy(data, &compParams, sizeof(WaterCompositeParamsGPU));
        vkUnmapMemory(logicDev, m_CompParamsMemory);
    }

    // ── SSR Params UBO（W-12）────────────────────────────────
    AllocateBuffer(256,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_SSRParamsBuffer, m_SSRParamsMemory);

    // ── Caustics Params UBO（W-14）────────────────────────────
    AllocateBuffer(sizeof(WaterCausticsParamsGPU),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_CausticsParamsBuffer, m_CausticsParamsMemory);

    // ── 7. 创建 Gerstner 波 SSBO（W-04）────────────────────────
    {
        VkDeviceSize ssboSize = MAX_WAVE_COUNT * sizeof(GerstnerWaveGPU);
        AllocateBuffer(ssboSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_WaveSSBO, m_WaveSSBOMemory);

        // 自动生成默认波分量
        std::vector<GerstnerWaveGPU> waves;
        AutoGenerateGerstnerWaves(waves, 128, glm::vec2(0.7071f, 0.7071f), 0.2f, 12.0f);
        void* data = nullptr;
        vkMapMemory(logicDev, m_WaveSSBOMemory, 0, ssboSize, 0, &data);
        std::memcpy(data, waves.data(), waves.size() * sizeof(GerstnerWaveGPU));
        vkUnmapMemory(logicDev, m_WaveSSBOMemory);

        VANS_LOG("[VansWaterSystem] Wave SSBO: " << waves.size() << " waves, " << ssboSize << " bytes");
    }

    m_Initialized = true;
    VANS_LOG("[VansWaterSystem] Initialize: " << renderWidth << "x" << renderHeight
             << " waterLevel=" << m_WaterLevel
             << " meshDim=" << m_WaterLOD->GetMeshDim());
}

// ============================================================
// SetGlobalDescriptorSet — 在 CreateGlobalDescriptorSet 之后调用
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

    // ── Water GBuffer Pass descriptor set（Set 1）────────────
    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterGBuffer(
            m_GBufPassLayout, sets, 1);
        m_GBufPassSet = sets[0];

        // binding 0：WaterGBufferParams UBO
        descMgr->m_BufferDescInfos.push_back({
            m_GBufPassSet, WATER_GBUF_BINDING_PARAMS, 0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { m_GBufParamsBuffer, 0, sizeof(WaterGBufferParamsGPU) } }
        });

        // binding 1：位移贴图 Texture2DArray（W-01）
        descMgr->m_ImageDescInfos.push_back({
            m_GBufPassSet, WATER_GBUF_BINDING_DISPLACEMENT, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { {
                m_WaveDisplacementImage.GetSampler(),
                m_WaveDisplacementImage.GetImageView(),
                VK_IMAGE_LAYOUT_GENERAL
            } }
        });

        // binding 2：GerstnerWave SSBO（W-04）— 顶点着色器读取
        descMgr->m_BufferDescInfos.push_back({
            m_GBufPassSet, WATER_GBUF_BINDING_WAVE_SSBO, 0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            { { m_WaveSSBO, 0, MAX_WAVE_COUNT * sizeof(GerstnerWaveGPU) } }
        });

        // binding 3: 法线贴图（W-08）— 使用位移贴图作为 placeholder，后续从 VansWaterMaterial 加载真实纹理
        descMgr->m_ImageDescInfos.push_back({
            m_GBufPassSet, WATER_GBUF_BINDING_NORMAL_MAP, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { {
                m_WaveDisplacementImage.GetSampler(),
                m_WaveDisplacementImage.GetImageView(),
                VK_IMAGE_LAYOUT_GENERAL
            } }
        });

        descMgr->UpdateDescriptorSets();
    }

    // ── Water Wave Compute descriptor set（Set 0）────────────
    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterWaveCompute(
            m_WaveSimLayout, sets, 1);
        m_WaveSimSet = sets[0];

        descMgr->m_BufferDescInfos.push_back({
            m_WaveSimSet, WATER_WAVE_BINDING_PARAMS, 0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { m_GBufParamsBuffer, 0, sizeof(WaterGBufferParamsGPU) } }
        });
        descMgr->m_ImageDescInfos.push_back({
            m_WaveSimSet, WATER_WAVE_BINDING_DISPLACEMENT, 0,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { {
                m_WaveDisplacementImage.GetSampler(),
                m_WaveDisplacementImage.GetImageView(),
                VK_IMAGE_LAYOUT_GENERAL
            } }
        });
        // binding 2：GerstnerWave SSBO 输入
        descMgr->m_BufferDescInfos.push_back({
            m_WaveSimSet, WATER_WAVE_BINDING_WAVE_SSBO, 0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            { { m_WaveSSBO, 0, MAX_WAVE_COUNT * sizeof(GerstnerWaveGPU) } }
        });

        descMgr->UpdateDescriptorSets();
    }

    // ── Water Composite Pass descriptor set（Set 1）──────────
    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterComposite(
            m_CompPassLayout, sets, 1);
        m_CompPassSet = sets[0];

        descMgr->m_ImageDescInfos.push_back({
            m_CompPassSet, WATER_COMP_BINDING_GBUF_NORMAL, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufNormal().GetSampler(), renderPassManager->GetWaterGBufNormal().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
        });
        descMgr->m_ImageDescInfos.push_back({
            m_CompPassSet, WATER_COMP_BINDING_GBUF_DEPTH, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufLinearDepth().GetSampler(), renderPassManager->GetWaterGBufLinearDepth().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
        });
        descMgr->m_BufferDescInfos.push_back({
            m_CompPassSet, WATER_COMP_BINDING_PARAMS, 0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { m_CompParamsBuffer, 0, sizeof(WaterCompositeParamsGPU) } }
        });
        descMgr->m_ImageDescInfos.push_back({
            m_CompPassSet, WATER_COMP_BINDING_SCENE_GBUF2, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetGbuffer2().GetSampler(), renderPassManager->GetGbuffer2().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
        });
        descMgr->m_ImageDescInfos.push_back({
            m_CompPassSet, WATER_COMP_BINDING_REFLECTION, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterReflectionImage.GetSampler(), m_WaterReflectionImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }
        });
        descMgr->m_ImageDescInfos.push_back({
            m_CompPassSet, WATER_COMP_BINDING_REFRACTION, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterRefractionImage.GetSampler(), m_WaterRefractionImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }
        });
        descMgr->m_ImageDescInfos.push_back({
            m_CompPassSet, WATER_COMP_BINDING_CAUSTICS, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterCausticsImage.GetSampler(), m_WaterCausticsImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }
        });
        // W-15: 泡沫纹理 — 使用反射贴图作为 placeholder，后续从 VansWaterMaterial::m_FoamTexture 绑定真实纹理
        descMgr->m_ImageDescInfos.push_back({
            m_CompPassSet, WATER_COMP_BINDING_FOAM_TEXTURE, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterReflectionImage.GetSampler(), m_WaterReflectionImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }
        });
        // W-16: 厚度图
        descMgr->m_ImageDescInfos.push_back({
            m_CompPassSet, WATER_COMP_BINDING_THICKNESS, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_WaterThicknessImage.GetSampler(), m_WaterThicknessImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }
        });
        descMgr->UpdateDescriptorSets();
    }

    // ── Water Effects Compute descriptor set（Set 0）──────────
    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterEffectsCompute(
            m_EffectsLayout, sets, 1);
        m_EffectsSet = sets[0];

        descMgr->m_ImageDescInfos.push_back({
            m_EffectsSet, WATER_EFFECT_BINDING_GBUF_NORMAL, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufNormal().GetSampler(), renderPassManager->GetWaterGBufNormal().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
        });
        descMgr->m_ImageDescInfos.push_back({
            m_EffectsSet, WATER_EFFECT_BINDING_GBUF_DEPTH, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufLinearDepth().GetSampler(), renderPassManager->GetWaterGBufLinearDepth().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
        });
        descMgr->m_ImageDescInfos.push_back({
            m_EffectsSet, WATER_EFFECT_BINDING_SCENE_GBUF2, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetGbuffer2().GetSampler(), renderPassManager->GetGbuffer2().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
        });
        descMgr->m_ImageDescInfos.push_back({
            m_EffectsSet, WATER_EFFECT_BINDING_SCENE_COLOR, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetColor().GetSampler(), renderPassManager->GetColor().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
        });
        descMgr->m_BufferDescInfos.push_back({
            m_EffectsSet, WATER_EFFECT_BINDING_PARAMS, 0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { m_CompParamsBuffer, 0, sizeof(WaterCompositeParamsGPU) } }
        });
        descMgr->m_ImageDescInfos.push_back({
            m_EffectsSet, WATER_EFFECT_BINDING_REFLECTION_OUT, 0,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaterReflectionImage.GetSampler(), m_WaterReflectionImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }
        });
        descMgr->m_ImageDescInfos.push_back({
            m_EffectsSet, WATER_EFFECT_BINDING_REFRACTION_OUT, 0,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaterRefractionImage.GetSampler(), m_WaterRefractionImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }
        });
        descMgr->m_ImageDescInfos.push_back({
            m_EffectsSet, WATER_EFFECT_BINDING_CAUSTICS_OUT, 0,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaterCausticsImage.GetSampler(), m_WaterCausticsImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }
        });
        descMgr->UpdateDescriptorSets();
    }

    // ── Water SSR Compute descriptor set（Set 0, W-12）───────
    // 延迟到 HZB 可用时创建（见 EnsureSSRDescriptorSet）

    // ── Water Refraction Compute descriptor set（Set 0）──────
    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterRefractionCompute(
            m_RefractionLayout, sets, 1);
        m_RefractionSet = sets[0];

        descMgr->m_ImageDescInfos.push_back({
            m_RefractionSet, WATER_REFRACTION_BINDING_GBUF_NORMAL, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufNormal().GetSampler(), renderPassManager->GetWaterGBufNormal().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
        });
        descMgr->m_ImageDescInfos.push_back({
            m_RefractionSet, WATER_REFRACTION_BINDING_GBUF_DEPTH, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufLinearDepth().GetSampler(), renderPassManager->GetWaterGBufLinearDepth().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
        });
        descMgr->m_ImageDescInfos.push_back({
            m_RefractionSet, WATER_REFRACTION_BINDING_SCENE_GBUF2, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetGbuffer2().GetSampler(), renderPassManager->GetGbuffer2().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
        });
        descMgr->m_ImageDescInfos.push_back({
            m_RefractionSet, WATER_REFRACTION_BINDING_SCENE_COLOR, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetColor().GetSampler(), renderPassManager->GetColor().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
        });
        descMgr->m_BufferDescInfos.push_back({
            m_RefractionSet, WATER_REFRACTION_BINDING_PARAMS, 0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { m_CompParamsBuffer, 0, sizeof(WaterCompositeParamsGPU) } }
        });
        descMgr->m_ImageDescInfos.push_back({
            m_RefractionSet, WATER_REFRACTION_BINDING_REFRACTION_OUT, 0,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaterRefractionImage.GetSampler(), m_WaterRefractionImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }
        });

        descMgr->UpdateDescriptorSets();
    }

    // ── Water Caustics Compute descriptor set（Set 0, W-14）───
    {
        std::vector<VkDescriptorSet> sets;
        VansDescriptorSetLayoutFactory::CreateAndAllocate_WaterCausticsCompute(
            m_CausticsLayout, sets, 1);
        m_CausticsSet = sets[0];

        descMgr->m_ImageDescInfos.push_back({
            m_CausticsSet, WATER_CAUSTICS_BINDING_GBUF_NORMAL, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufNormal().GetSampler(), renderPassManager->GetWaterGBufNormal().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
        });
        descMgr->m_ImageDescInfos.push_back({
            m_CausticsSet, WATER_CAUSTICS_BINDING_GBUF_DEPTH, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetWaterGBufLinearDepth().GetSampler(), renderPassManager->GetWaterGBufLinearDepth().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
        });
        descMgr->m_ImageDescInfos.push_back({
            m_CausticsSet, WATER_CAUSTICS_BINDING_SCENE_GBUF2, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { renderPassManager->GetGbuffer2().GetSampler(), renderPassManager->GetGbuffer2().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
        });
        descMgr->m_BufferDescInfos.push_back({
            m_CausticsSet, WATER_CAUSTICS_BINDING_PARAMS, 0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { m_CausticsParamsBuffer, 0, sizeof(WaterCausticsParamsGPU) } }
        });
        descMgr->m_ImageDescInfos.push_back({
            m_CausticsSet, WATER_CAUSTICS_BINDING_CAUSTICS_OUT, 0,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { { m_WaterCausticsImage.GetSampler(), m_WaterCausticsImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }
        });

        descMgr->UpdateDescriptorSets();
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

    // W-02: 委托 VansWaterLOD 清理网格缓冲
    if (m_WaterLOD)
    {
        m_WaterLOD->Shutdown(dev);
        delete m_WaterLOD;
        m_WaterLOD = nullptr;
    }

    if (m_GBufPassLayout != VK_NULL_HANDLE)   { descMgr->DestroyDescriptorSetLayout(m_GBufPassLayout); m_GBufPassLayout = VK_NULL_HANDLE; }
    if (m_CompPassLayout != VK_NULL_HANDLE)   { descMgr->DestroyDescriptorSetLayout(m_CompPassLayout); m_CompPassLayout = VK_NULL_HANDLE; }
    if (m_WaveSimLayout != VK_NULL_HANDLE)    { descMgr->DestroyDescriptorSetLayout(m_WaveSimLayout);  m_WaveSimLayout  = VK_NULL_HANDLE; }
    if (m_EffectsLayout != VK_NULL_HANDLE)    { descMgr->DestroyDescriptorSetLayout(m_EffectsLayout);  m_EffectsLayout  = VK_NULL_HANDLE; }
    if (m_SSRLayout != VK_NULL_HANDLE)        { descMgr->DestroyDescriptorSetLayout(m_SSRLayout);      m_SSRLayout      = VK_NULL_HANDLE; }
    if (m_RefractionLayout != VK_NULL_HANDLE) { descMgr->DestroyDescriptorSetLayout(m_RefractionLayout); m_RefractionLayout = VK_NULL_HANDLE; }
    if (m_CausticsLayout != VK_NULL_HANDLE)  { descMgr->DestroyDescriptorSetLayout(m_CausticsLayout);  m_CausticsLayout  = VK_NULL_HANDLE; }

    auto destroyBuf = [&](VkBuffer& buf, VkDeviceMemory& mem)
    {
        if (buf != VK_NULL_HANDLE) { vkDestroyBuffer(dev, buf, nullptr); buf = VK_NULL_HANDLE; }
        if (mem != VK_NULL_HANDLE) { vkFreeMemory(dev, mem, nullptr);    mem = VK_NULL_HANDLE; }
    };
    destroyBuf(m_GBufParamsBuffer, m_GBufParamsMemory);
    destroyBuf(m_CompParamsBuffer, m_CompParamsMemory);
    destroyBuf(m_SSRParamsBuffer,  m_SSRParamsMemory);
    destroyBuf(m_CausticsParamsBuffer, m_CausticsParamsMemory);
    destroyBuf(m_WaveSSBO,         m_WaveSSBOMemory);

    m_WaveDisplacementImage.DestroyVulkanImage(dev);
    m_WaveDisplacementReady = false;
    m_WaterReflectionImage.DestroyVulkanImage(dev);
    m_WaterRefractionImage.DestroyVulkanImage(dev);
    m_WaterCausticsImage.DestroyVulkanImage(dev);
    m_WaterThicknessImage.DestroyVulkanImage(dev);
    m_WaterEffectsReady = false;

    delete m_WaterGBufferShader;    m_WaterGBufferShader   = nullptr;
    delete m_WaterCompositeShader;  m_WaterCompositeShader = nullptr;
    delete m_WaveSimShader;         m_WaveSimShader        = nullptr;
    delete m_WaterEffectsShader;    m_WaterEffectsShader   = nullptr;
    delete m_WaterSSRShader;        m_WaterSSRShader       = nullptr;
    delete m_WaterRefractionShader; m_WaterRefractionShader = nullptr;
    delete m_WaterCausticsShader;   m_WaterCausticsShader   = nullptr;
    delete m_WaveSystem;    m_WaveSystem    = nullptr;

    m_Initialized      = false;
    m_DescriptorsReady = false;
    m_WaterMaterial    = nullptr;
    m_Device           = nullptr;
    VANS_LOG("[VansWaterSystem] Shutdown");
}

// ============================================================
// UpdateWaveSSBO — 运行时重新生成波分量并上传到 SSBO（W-04）
// 当编辑器修改波参数（波数、风速、涌浪幅度等）时调用。
// ============================================================
void VansWaterSystem::UpdateWaveSSBO()
{
    if (!m_Initialized || m_WaveSSBO == VK_NULL_HANDLE || m_WaveSSBOMemory == VK_NULL_HANDLE)
        return;
    if (!m_WaterMaterial)
        return;

    std::vector<GerstnerWaveGPU> waves;
    int   count  = m_WaterMaterial->m_GerstnerWaveCount;
    glm::vec2 windDir = m_WaterMaterial->m_WindDirection;
    float swell  = m_WaterMaterial->m_SwellAmplitude;
    float windSp = m_WaterMaterial->m_WindSpeed;

    AutoGenerateGerstnerWaves(waves, count, windDir, swell, windSp);

    VkDevice logicDev = m_Device->GetLogicDevice();
    VkDeviceSize size = waves.size() * sizeof(GerstnerWaveGPU);
    VkDeviceSize ssboSize = MAX_WAVE_COUNT * sizeof(GerstnerWaveGPU);
    void* data = nullptr;
    vkMapMemory(logicDev, m_WaveSSBOMemory, 0, ssboSize, 0, &data);
    std::memcpy(data, waves.data(), static_cast<size_t>(size));
    vkUnmapMemory(logicDev, m_WaveSSBOMemory);

    VANS_LOG("[VansWaterSystem] UpdateWaveSSBO: " << waves.size() << " waves regenerated");
}

// ============================================================
// Update — 每帧 CPU 端状态更新
// ============================================================
void VansWaterSystem::Update(float deltaTime, const glm::vec3& cameraPos,
                             const glm::mat4& viewMatrix, const glm::mat4& vpMatrix,
                             const glm::vec3& mainLightDir,
                             const glm::vec3& mainLightColor)
{
    m_Time += deltaTime;

    // 从 WaterMaterial 读取运行时参数（支持编辑器实时调整）
    float ampScale   = 0.2f;
    float chopScale  = 1.5f;
    float baseScale  = 128.0f;
    float maxAmp     = 0.6f;  // swellAmplitude(0.2) * 3
    if (m_WaterMaterial)
    {
        ampScale  = m_WaterMaterial->m_SwellAmplitude;
        chopScale = m_WaterMaterial->m_ChopScale;
        baseScale = m_WaterMaterial->m_OceanBaseScale;
        maxAmp    = m_WaterMaterial->m_SwellAmplitude * 3.0f;
    }

    // W-02: 委托 VansWaterLOD 生成 Patch
    if (m_WaterLOD)
        m_WaterLOD->GeneratePatches(cameraPos);

    // 每帧写入水面 pass 自有相机数据
    WaterGBufferParamsGPU gbufParams = {};
    gbufParams.VPMatrix       = vpMatrix;
    gbufParams.ViewMatrix     = viewMatrix;
    gbufParams.cameraPosition = glm::vec4(cameraPos, 1.0f);
    gbufParams.minLodDist     = VansWaterLOD::MIN_LOD_DIST;
    gbufParams.lodLevels      = VansWaterLOD::MAX_LOD_COUNT;
    gbufParams.meshDim        = m_WaterLOD ? m_WaterLOD->GetMeshDim() : VansWaterLOD::WATER_MESH_DIM;
    gbufParams.clipmapBaseScale = baseScale;
    gbufParams.maxWaveAmp     = maxAmp;
    gbufParams.detailBalance   = m_WaterLOD ? m_WaterLOD->GetDetailBalance() : 2.0f;  // CPU/GPU 同步：从 VansWaterLOD 运行时读取
    gbufParams.morphStartRatio = 0.6f;  // morph zone 起点比例：外侧 40% 才开始 morph
    gbufParams.waveTimeAndScale = glm::vec4(m_Time, ampScale, chopScale, 1.0f);

    if (m_Device != nullptr && m_GBufParamsMemory != VK_NULL_HANDLE)
    {
        void* data = nullptr;
        vkMapMemory(m_Device->GetLogicDevice(), m_GBufParamsMemory, 0, sizeof(WaterGBufferParamsGPU), 0, &data);
        std::memcpy(data, &gbufParams, sizeof(WaterGBufferParamsGPU));
        vkUnmapMemory(m_Device->GetLogicDevice(), m_GBufParamsMemory);
    }

    // 每帧全量更新 composite UBO：从 VansWaterMaterial 读取所有介质参数 +
    // 相机位置 + invViewProj + 主光方向（确保 Editor 修改实时生效）
    if (m_Device != nullptr && m_CompParamsMemory != VK_NULL_HANDLE)
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
        }
        else
        {
            // fallback 默认参数
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
        }
        compParams.cameraPosition  = glm::vec4(cameraPos, 1.0f);
        compParams.invViewProjMatrix = glm::inverse(vpMatrix);
        compParams.mainLightDir    = glm::vec4(glm::normalize(mainLightDir), 0.0f);
        compParams.viewMatrix      = viewMatrix;
        compParams.projMatrix      = vpMatrix * glm::inverse(viewMatrix);  // proj = VP * inv(View)

        void* data = nullptr;
        vkMapMemory(m_Device->GetLogicDevice(), m_CompParamsMemory, 0,
            sizeof(WaterCompositeParamsGPU), 0, &data);
        std::memcpy(data, &compParams, sizeof(WaterCompositeParamsGPU));
        vkUnmapMemory(m_Device->GetLogicDevice(), m_CompParamsMemory);
    }

    // ── 更新 SSR Params UBO（W-12）──────────────────────
    if (m_Device != nullptr && m_SSRParamsMemory != VK_NULL_HANDLE)
    {
        struct WaterSSRParamsGPU {
            glm::vec4 cameraPosition;
            glm::mat4 projMatrix;
            glm::mat4 invProjMatrix;
            glm::mat4 viewMatrix;
            float maxDistance;
            int   maxSteps;
            float thickness;
            float pad;
        } ssrParams;
        ssrParams.cameraPosition = glm::vec4(cameraPos, 1.0f);
        ssrParams.projMatrix     = vpMatrix * glm::inverse(viewMatrix);
        ssrParams.invProjMatrix  = glm::inverse(ssrParams.projMatrix);
        ssrParams.viewMatrix     = viewMatrix;
        ssrParams.maxDistance    = 500.0f;   // 500m 最大追踪距离
        ssrParams.maxSteps       = 64;       // 64 步
        ssrParams.thickness      = 1.0f;     // 1m 深度容差

        void* data = nullptr;
        vkMapMemory(m_Device->GetLogicDevice(), m_SSRParamsMemory, 0, sizeof(WaterSSRParamsGPU), 0, &data);
        std::memcpy(data, &ssrParams, sizeof(WaterSSRParamsGPU));
        vkUnmapMemory(m_Device->GetLogicDevice(), m_SSRParamsMemory);
    }

    // ── 更新 Caustics Params UBO（W-14）──────────────────
    if (m_Device != nullptr && m_CausticsParamsMemory != VK_NULL_HANDLE)
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

        void* data = nullptr;
        vkMapMemory(m_Device->GetLogicDevice(), m_CausticsParamsMemory, 0, sizeof(WaterCausticsParamsGPU), 0, &data);
        std::memcpy(data, &causticParams, sizeof(WaterCausticsParamsGPU));
        vkUnmapMemory(m_Device->GetLogicDevice(), m_CausticsParamsMemory);
    }
}

// ============================================================
// UpdateWaveSimulation — Compute 生成 Gerstner 波形位移贴图（W-01: 逐 LOD）
// ============================================================
void VansWaterSystem::UpdateWaveSimulation(VansVKCommandBuffer& cmd, float /*deltaTime*/)
{
    if (!m_Initialized || !m_DescriptorsReady || m_WaveSimShader == nullptr || m_WaveSimSet == VK_NULL_HANDLE)
        return;

    // W-11: 波形模式检查 — FFT/Hybrid 未实现，回退到 Gerstner
    if (m_WaterMaterial)
    {
        VansWaveMode mode = m_WaterMaterial->m_Config.m_Waves.m_Mode;
        if (mode != VansWaveMode::Gerstner)
        {
            static int logSkip = 0;
            if (++logSkip % 120 == 1)
                VANS_LOG_WARN("[VansWaterSystem] Wave mode " << static_cast<int>(mode)
                    << " not implemented, falling back to Gerstner");
        }
    }

    const VkImageLayout currentLayout = m_WaveDisplacementReady
        ? VK_IMAGE_LAYOUT_GENERAL
        : m_WaveDisplacementImage.GetImageLayout();

    // W-01: barrier 覆盖全部 layers
    VkImageMemoryBarrier beforeCompute = {};
    beforeCompute.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    beforeCompute.srcAccessMask       = m_WaveDisplacementReady ? VK_ACCESS_SHADER_READ_BIT : 0;
    beforeCompute.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    beforeCompute.oldLayout           = currentLayout;
    beforeCompute.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    beforeCompute.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beforeCompute.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beforeCompute.image               = m_WaveDisplacementImage.GetImage();
    beforeCompute.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, VansWaterLOD::MAX_LOD_COUNT };
    cmd.PipelineBarrier(
        m_WaveDisplacementReady ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        {}, {}, { beforeCompute });
    m_WaveDisplacementImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);

    // W-01: 3D Dispatch — gl_WorkGroupID.z = LOD index
    // 每层 32×32 groups (256/8=32)，10 layers total
    cmd.EnsureComputeShader(*m_WaveSimShader, { m_WaveSimLayout });
    const uint32_t groups = (WAVE_TEXTURE_SIZE + 7u) / 8u;
    cmd.DispatchCompute(*m_WaveSimShader, groups, groups, VansWaterLOD::MAX_LOD_COUNT, { m_WaveSimSet });

    VkImageMemoryBarrier afterCompute = {};
    afterCompute.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    afterCompute.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    afterCompute.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    afterCompute.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
    afterCompute.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    afterCompute.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    afterCompute.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    afterCompute.image               = m_WaveDisplacementImage.GetImage();
    afterCompute.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, VansWaterLOD::MAX_LOD_COUNT };
    cmd.PipelineBarrier(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        {}, {}, { afterCompute });

    m_WaveDisplacementReady = true;
}

// ============================================================
// RenderWaterGBuffer — 设计文档 Pass 7
// ============================================================
void VansWaterSystem::RenderWaterGBuffer(VansVKCommandBuffer& cmd, GlobalStateData& globalState)
{
    static int s_DbgFrame = 0;
    bool dbgLog = (s_DbgFrame++ % 120) == 0;

    if (!m_Initialized || !m_DescriptorsReady || !m_WaterLOD || m_WaterLOD->GetPatchCount() == 0)
    {
        if (dbgLog)
            VANS_LOG_WARN("[WaterGBuffer] EARLY RETURN: init/descReady/patches 条件不满足");
        return;
    }
    if (m_WaterGBufferShader == nullptr || m_WaterLOD->GetVertexBuffer() == VK_NULL_HANDLE)
    {
        if (dbgLog)
            VANS_LOG_WARN("[WaterGBuffer] EARLY RETURN: shader/vbuf 为空");
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

    // ── 设置顶点输入布局 ──
    globalState.vertexInputBindingDescriptions   = &m_WaterLOD->GetVertexBindings();
    globalState.vertexInputAttributeDescriptions = &m_WaterLOD->GetVertexAttributes();

    // ── Descriptor Set 布局数组：[Set 0: Global, Set 1: WaterGBuf Pass]
    std::vector<VkDescriptorSetLayout> layouts = { m_GlobalLayout, m_GBufPassLayout };
    std::vector<VkDescriptorSet>       sets    = { m_GlobalSet,    m_GBufPassSet    };

    cmd.EnsureGraphicsShader(*m_WaterGBufferShader, globalState, layouts);
    cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
        *m_WaterGBufferShader, 0, sets, {});
    cmd.BindGraphicsPipeline(*m_WaterGBufferShader->GetGraphicsPipeline());

    // ── 绑定共用顶点 / 索引缓冲（来自 VansWaterLOD）─────────────
    VkDeviceSize offset = 0;
    VkBuffer vbuf = m_WaterLOD->GetVertexBuffer();
    VkBuffer ibuf = m_WaterLOD->GetIndexBuffer();
    cmd.BindVertexBuffers(0, 1, &vbuf, &offset);
    cmd.BindIndexBuffer(ibuf, 0, VK_INDEX_TYPE_UINT32);

    const std::vector<CDLODPatch>& patches = m_WaterLOD->GetPatches();
    uint32_t indexCount = m_WaterLOD->GetIndexCount();

    // ── 逐 Patch 推送常量 + DrawIndexed ──────────────────────
    for (auto patchIter = patches.rbegin(); patchIter != patches.rend(); ++patchIter)
    {
        const CDLODPatch& patch = *patchIter;
        WaterPatchPushConstant pc = {};
        pc.patchWorldOrigin = patch.worldCenter - glm::vec2(patch.worldHalfSize);
        pc.patchWorldSize   = patch.worldHalfSize * 2.0f;
        pc.lodLevel         = patch.lodLevel;
        pc.waterLevel       = m_WaterLevel;

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

    // ── Water SSR (HZB Ray March) → Reflection ───────────────
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
// EnsureSSRDescriptorSet — HZB 可用时延迟创建 SSR descriptor set
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

    descMgr->m_ImageDescInfos.push_back({
        m_SSRSet, WATER_SSR_BINDING_GBUF_NORMAL, 0,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        { { rp->GetWaterGBufNormal().GetSampler(), rp->GetWaterGBufNormal().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
    });
    descMgr->m_ImageDescInfos.push_back({
        m_SSRSet, WATER_SSR_BINDING_GBUF_DEPTH, 0,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        { { rp->GetWaterGBufLinearDepth().GetSampler(), rp->GetWaterGBufLinearDepth().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
    });
    descMgr->m_ImageDescInfos.push_back({
        m_SSRSet, WATER_SSR_BINDING_SCENE_HZB, 0,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        { { hzbImage->GetSampler(), hzbImage->GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }
    });
    descMgr->m_ImageDescInfos.push_back({
        m_SSRSet, WATER_SSR_BINDING_SCENE_GBUF2, 0,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        { { rp->GetGbuffer2().GetSampler(), rp->GetGbuffer2().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
    });
    descMgr->m_ImageDescInfos.push_back({
        m_SSRSet, WATER_SSR_BINDING_SCENE_COLOR, 0,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        { { rp->GetColor().GetSampler(), rp->GetColor().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
    });
    descMgr->m_BufferDescInfos.push_back({
        m_SSRSet, WATER_SSR_BINDING_PARAMS, 0,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        { { m_SSRParamsBuffer, 0, 256 } }
    });
    descMgr->m_ImageDescInfos.push_back({
        m_SSRSet, WATER_SSR_BINDING_REFLECTION_OUT, 0,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        { { m_WaterReflectionImage.GetSampler(), m_WaterReflectionImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL } }
    });

    descMgr->UpdateDescriptorSets();
    VANS_LOG("[VansWaterSystem] SSR descriptor set created with HZB.");
}

void VansWaterSystem::DispatchRefractionCS(VansVKCommandBuffer& cmd)
{
    if (!m_Initialized || !m_DescriptorsReady)
        return;
    if (m_WaterRefractionShader == nullptr || m_RefractionSet == VK_NULL_HANDLE)
        return;

    // Barrier: refraction image → compute write
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

    // Barrier: refraction image → fragment read (for composite pass)
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
    if (m_WaterCausticsShader == nullptr || m_CausticsSet == VK_NULL_HANDLE) return;

    // Barrier: caustics image → compute write
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

    // Barrier: caustics image → fragment read
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
// RenderWaterComposite — 设计文档 Pass 9（全屏 1△ 延迟合成）
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
