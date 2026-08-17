#pragma once
#include "vulkan/vulkan.h"
#include "glm/glm.hpp"
#include "VansWaterMaterial.h"
#include "VansWaterConfig.h"
#include "VansWaterGeometryClipmap.h"
#include "../VulkanCore/VansVKImage.h"
#include "../VulkanCore/VansVKBuffer.h"
#include <vector>

// ============================================================
// VansWaterSystem — 水面渲染系统主类
//
// 渲染管线流程（与设计文档 §6.1 一致）：
//   Pass 7:  Water GBuffer Pass  → 写 WaterGBuf_Normal + WaterGBuf_LinearDepth
//   Pass 8:  Pre-Water Compute   → SSR、折射、焦散（Phase 2 实现）
//   Pass 9:  Water Composite     → 全屏合成，写回 SceneColor（在 DeferredSkyboxPass 内）
//
// 类拆分（设计文档 W-02, W-03, W-09）：
//   VansWaterGeometryClipmap → fixed 2:1 ring geometry and culling
//   VansWaterFFT       → FFT 频谱计算（W-09）
// ============================================================

namespace VansGraphics
{
    class VansVKCommandBuffer;
    class VansVKDevice;
    class VansGraphicsShader;
    class VansComputeShader;
    class VansRenderPassManager;
    class VansWaterGeometryClipmap;
    class VansWaterFFT;
    struct GlobalStateData;

    struct alignas(16) GerstnerWaveGPU
    {
        float amplitude;
        float wavelength;
        float directionX;
        float directionY;
        float phaseSpeed;
        float steepness;
        float padding0;
        float padding1;
    };

    struct alignas(16) WaveParticleGPU
    {
        // xy=周期域归一化初始位置，z=归一化包络半径，w=单位 RMS 振幅权重。
        glm::vec4 positionRadius;
        // xy=传播方向，z=归一化载波波长，w=初始相位。
        glm::vec4 directionWave;
        // x=紧支撑波包的解析零均值补偿系数；其余分量为 std430 对齐填充。
        glm::vec4 meanCompensation;
    };

    // WaterGBufferParams GPU struct. Matches water_prepass.vert set=1 binding=0.
    // Wave data is stored in SSBOs; this UBO contains matrices and simulation controls.
    struct alignas(16) WaterGBufferParamsGPU
    {
        glm::mat4 VPMatrix;
        glm::mat4 ViewMatrix;
        glm::vec4 cameraPosition;
        glm::ivec4 geometryParams;
        glm::vec4 geometryScale;
        glm::vec4 spectrumScale;
        glm::vec4 windAndChop;
        glm::ivec4 simulationParams;
        glm::vec4 waveParticleParams0;
        glm::vec4 waveParticleParams1;
        glm::vec4 flowMapWorld;
        glm::vec4 flowMapParams;
        glm::vec4 flowMapFallback;
        glm::vec4 surfaceOptics;     // x=roughness, y=IOR, z=F0, w=specular intensity
        glm::vec4 scatteringCoeff;   // rgb=sigma_s
        glm::vec4 absorptionCoeff;   // rgb=sigma_a
    };

    // PBRWaterParams GPU struct. Matches PBRWater surface/refraction/volume/composite shaders.
    struct alignas(16) PBRWaterParamsGPU
    {
        glm::vec4 absorptionCoeff;
        glm::vec4 scatteringCoeff;
        glm::vec4 cameraPosition;
        glm::vec4 mainLightDir;
        glm::vec4 mainLightColor;
        glm::vec4 surfaceParams;      // x=roughness, y=IOR, z=F0, w=specular intensity
        glm::vec4 refractionParams;   // x=strength, y=max distance, z=dispersion, w=edge fade
        glm::vec4 volumeParams;       // x=max distance, y=sample count, z=resolution scale, w=multi scatter
        glm::vec4 thinSSSParams;      // x=path scale, y=nonlinear strength, z=scatter boost, w=phase g
        glm::vec4 backlitParams;      // x=path scale, y=phase g, zw=padding
        glm::vec4 filterParams;       // x=spatial depth sensitivity, y=filter iterations, zw=padding
        glm::ivec4 effectFlags;       // x=SSR, y=refraction, z=caustics, w=thin SSS
        glm::mat4 invViewProjMatrix;
        glm::mat4 viewMatrix;
        glm::mat4 projMatrix;
    };

    // WaterCausticsParams GPU struct. Matches water_caustics.comp set=0 binding=9.
    struct alignas(16) WaterCausticsParamsGPU
    {
        glm::vec4 sunDirection;
        glm::vec4 mainLightColor;     // rgb = color, a = intensity multiplier
        glm::vec4 extinctionCoeff;
        glm::vec4 mediumParams;       // x=IOR, y=water level, z=intensity, w=max path distance
        glm::vec4 shapingParams;      // x=max gain, y=filter radius, z=use refraction data
    };

    class VansWaterSystem
    {
    public:
        // Compute 生成的周期位移贴图分辨率（W-01: 改为 256² per LOD layer）
        static constexpr uint32_t WAVE_TEXTURE_SIZE = 256;
        static constexpr uint32_t DETAIL_TEXTURE_SIZE = 256;

        VansWaterSystem()  = default;
        ~VansWaterSystem() = default;

        // ── 生命周期 ─────────────────────────────────────────────
        void Initialize(VansVKDevice* device,
                        uint32_t renderWidth, uint32_t renderHeight);

        // SetupDescriptors：在 SetupVansWaterGBufferPass 之后（BeforeRendering 中）调用。
        // 注意：globalLayout/globalSet 可能尚未创建（在 LoadSceneForRendering 时序中
        // CreateGlobalDescriptorSet 在 AddWaterNode 之后执行），此时可传 VK_NULL_HANDLE。
        // 后续通过 SetGlobalDescriptorSet 在 CreateGlobalDescriptorSet 之后补设。
        void SetupDescriptors(VansRenderPassManager* renderPassManager,
                              VkDescriptorSetLayout  globalLayout,
                              VkDescriptorSet        globalSet,
                              VansVKImage*           sceneHZBImage = nullptr);

        // 在 CreateGlobalDescriptorSet 之后调用，将全局 descriptor set 同步到 WaterSystem。
        // 必须在首次渲染之前调用。
        void SetGlobalDescriptorSet(VkDescriptorSetLayout globalLayout,
                                     VkDescriptorSet        globalSet);

        void Shutdown();

        // ── 每帧更新 ─────────────────────────────────────────────
        void Update(float deltaTime, const glm::vec3& cameraPos,
                const glm::mat4& viewMatrix, const glm::mat4& vpMatrix,
                const glm::vec3& mainLightDir = glm::vec3(0.35f, 1.0f, 0.25f),
                const glm::vec3& mainLightColor = glm::vec3(1.0f));

        // ── 波形模拟 ──────────────────────────────────────────────
        void UpdateWaveSimulation(VansVKCommandBuffer& cmd, float deltaTime);

        // ── Water GBuffer Pass（Pass 7）──────────────────────────
        void RenderWaterGBuffer(VansVKCommandBuffer& cmd, GlobalStateData& globalState);

        // ── Pre-Water Compute（Pass 8）───────────────────────────
        void DispatchWaterThicknessCS(VansVKCommandBuffer& cmd);   // W-16: thickness/cross distance
        void DispatchWaterVolumeCS(VansVKCommandBuffer& cmd);
        void DispatchWaterVolumeFilterCS(VansVKCommandBuffer& cmd);
        void DispatchWaterSSR(VansVKCommandBuffer& cmd);
        void DispatchRefractionCS(VansVKCommandBuffer& cmd);
        void DispatchCausticsCS(VansVKCommandBuffer& cmd);

        // ── N-01: Detail Normal compute ───────────────────────────

        // W-12: SSR HZB 延迟绑定（在 HZB 创建后调用）
        void EnsureSSRDescriptorSet(VansVKImage* hzbImage);

        // ── Water Composite（Pass 9）─────────────────────────────
        void RenderWaterComposite(VansVKCommandBuffer& cmd, GlobalStateData& globalState);

        // ── 参数 ─────────────────────────────────────────────────
        float GetWaterLevel() const { return m_WaterLevel; }
        void SetWaterMaterial(VansWaterMaterial* mat)      { m_WaterMaterial = mat; }
        void SetWaterLevel(float waterLevel) { m_WaterLevel = waterLevel; }
        bool  IsInitialized() const                        { return m_Initialized; }
        bool  IsDescriptorsReady() const                   { return m_DescriptorsReady; }
        VansWaterGeometryClipmap* GetGeometryClipmap() const { return m_GeometryClipmap; }
        VansWaterFFT* GetFFT() const                       { return m_WaterFFT; }

    // Wave data is stored in SSBOs; this UBO contains matrices and simulation controls.
        void UpdateWaveSSBO();
        void UpdateWaveParticleSSBO();

        // 纹理访问器（供 Editor 纹理预览，W-17）
        VansVKImage& GetDisplacementImage()      { return m_WaveDisplacementImage; }
        VansVKImage& GetDerivativeImage()        { return m_WaveDerivativeImage; }
        VansVKImage& GetFlowMapImage()           { return m_FlowMapImage; }
        VansVKImage& GetReflectionImage()        { return m_WaterReflectionImage; }
        VansVKImage& GetRefractionImage()        { return m_WaterRefractionImage; }
        VansVKImage& GetCausticsImage()          { return m_WaterCausticsImage; }
        VansVKImage& GetThicknessImage()         { return m_WaterThicknessImage; }
        VansVKImage& GetVolumeColorImage()       { return m_WaterVolumeColorImage; }
        VansVKImage& GetVolumeTransmittanceImage() { return m_WaterVolumeTransmittanceImage; }
        VansVKImage& GetVolumeDepthImage()       { return m_WaterVolumeDepthImage; }

    private:
        // ── 原始 Vulkan 缓冲分配 ────────────────────────────────
        bool CreateWaterBuffer(VansVKBuffer& buffer, bool& created,
                               VkDeviceSize size, VkBufferUsageFlags usage);
        void DestroyWaterBuffer(VansVKBuffer& buffer, bool& created, VkDevice logicDevice);
        static VkBuffer GetNativeBuffer(const VansVKBuffer& buffer, bool created);

        // ── 引擎设备 ──────────────────────────────────────────────
        VansVKDevice*      m_Device        = nullptr;
        VansWaterMaterial* m_WaterMaterial = nullptr;

        float m_WaterLevel       = 0.0f;
        float m_Time             = 0.0f;
        bool  m_Initialized      = false;
        bool  m_DescriptorsReady = false;

        uint32_t m_RenderWidth  = 0;
        uint32_t m_RenderHeight = 0;

        // ── 拆分类（设计文档 W-02, W-03, W-09）───────────────────
        VansWaterGeometryClipmap* m_GeometryClipmap = nullptr;
        VansWaterFFT*        m_WaterFFT  = nullptr;  // Tessendorf FFT ocean

        // ── 着色器 ───────────────────────────────────────────────
        VansGraphicsShader* m_WaterGBufferShader   = nullptr;  // water_prepass.vert/.frag
        VansGraphicsShader* m_WaterCompositeShader = nullptr;  // water_composite.vert/.frag
        VansComputeShader*  m_WaterSSRShader       = nullptr;  // water_ssr.comp (HZB ray march)
        VansComputeShader*  m_WaveSimShader        = nullptr;  // water_wave_spectrum.comp (→ W-03)
        VansComputeShader*  m_WaterRefractionShader = nullptr;  // water_refraction.comp
        VansComputeShader*  m_WaterCausticsShader   = nullptr;  // water_caustics.comp (W-14)
        VansComputeShader*  m_WaterThicknessShader  = nullptr;  // water_thickness.comp (W-16)
        VansComputeShader*  m_WaterVolumeShader     = nullptr;  // water_volume.comp
        VansComputeShader*  m_WaterVolumeFilterShader = nullptr; // water_volume_filter.comp
        VansComputeShader*  m_WaveParticleShader    = nullptr;  // water_wave_particle.comp
        VansComputeShader*  m_FlowMapShader         = nullptr;  // water_flowmap.comp

        // ── Descriptor Sets：Water GBuffer Pass（Set 1）──────────
        VkDescriptorSetLayout m_GBufPassLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_GBufPassSet    = VK_NULL_HANDLE;

        // ── Descriptor Sets：Water Wave Compute（Set 0）──────────
        VkDescriptorSetLayout m_WaveSimLayout  = VK_NULL_HANDLE;
        VkDescriptorSet       m_WaveSimSet     = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_WaveParticleLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_WaveParticleSet    = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_FlowMapLayout      = VK_NULL_HANDLE;
        VkDescriptorSet       m_FlowMapSet         = VK_NULL_HANDLE;

        // ── Descriptor Sets：Water SSR Compute（Set 0, W-12）─────
        VkDescriptorSetLayout m_SSRLayout      = VK_NULL_HANDLE;
        VkDescriptorSet       m_SSRSet         = VK_NULL_HANDLE;

        // ── Descriptor Sets：Water Refraction Compute（Set 0）─────
        VkDescriptorSetLayout m_RefractionLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_RefractionSet    = VK_NULL_HANDLE;
        // ── Descriptor Sets：Water Caustics Compute（Set 0, W-14）───
        VkDescriptorSetLayout m_CausticsLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_CausticsSet    = VK_NULL_HANDLE;

        // ── W-16: Thickness Compute ───────────────────────────────
        VkDescriptorSetLayout m_ThicknessLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_ThicknessSet    = VK_NULL_HANDLE;

        // ── PBRWater Volume Compute ──────────────────────────────
        VkDescriptorSetLayout m_VolumeLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_VolumeSet    = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_VolumeFilterLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_VolumeFilterSet    = VK_NULL_HANDLE;

        // ── N-01: Detail Normal compute ───────────────────────────

        // ── Descriptor Sets：Water Composite Pass（Set 1）────────
        VkDescriptorSetLayout m_CompPassLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_CompPassSet    = VK_NULL_HANDLE;

        // ── GPU Params UBO ───────────────────────────────────────
        VansVKBuffer m_GBufParamsBuffer;
        VansVKBuffer m_CompParamsBuffer;
        VansVKBuffer m_SSRParamsBuffer;
        VansVKBuffer m_CausticsParamsBuffer;
        VansVKBuffer m_ThicknessParamsBuffer;   // W-16: 厚度图参数 UBO
        bool m_GBufParamsBufferCreated = false;
        bool m_CompParamsBufferCreated = false;
        bool m_SSRParamsBufferCreated = false;
        bool m_CausticsParamsBufferCreated = false;
        bool m_ThicknessParamsBufferCreated = false;
        WaterGBufferParamsGPU m_GBufParamsCache = {};
        uint32_t m_VolumeWidth = 1;
        uint32_t m_VolumeHeight = 1;

        // ── 波形贴图：Compute 写入，WaterGBuffer 顶点采样 ─────────
        // Macro displacement cascades: Texture2DArray 256² × MAX_SPECTRUM_CASCADES.
        VansVKImage m_WaveDisplacementImage;
        bool        m_WaveDisplacementReady = false;
        VansVKImage m_WaveDerivativeImage;
        bool        m_WaveDerivativeReady = false;
        VansVKImage m_FlowMapImage;
        bool        m_FlowMapReady = false;

        // ── 水体效果贴图：Pre-Water Compute 输出，Composite 采样 ───
        VansVKImage m_WaterReflectionImage;
        VansVKImage m_WaterRefractionImage;
        VansVKImage m_WaterCausticsImage;
        VansVKImage m_WaterThicknessImage;    // W-16: SSS 厚度图
        VansVKImage m_WaterVolumeRawColorImage;
        VansVKImage m_WaterVolumeRawTransmittanceImage;
        VansVKImage m_WaterVolumeRawDepthImage;
        VansVKImage m_WaterVolumeColorImage;
        VansVKImage m_WaterVolumeTransmittanceImage;
        VansVKImage m_WaterVolumeDepthImage;
        bool        m_ReflectionOutputReady = false;
        bool        m_RefractionOutputReady = false;
        bool        m_CausticsOutputReady = false;
        bool        m_ThicknessOutputReady = false;
        bool        m_VolumeOutputReady = false;
        bool        m_VolumeFilterOutputReady = false;

        // ── SSBO：Gerstner 波分量（W-04）───────────────────────────
        VansVKBuffer   m_WaveSSBO;
        bool           m_WaveSSBOCreated = false;
        static constexpr uint32_t MAX_WAVE_COUNT = 64;
        VansVKBuffer   m_WaveParticleSSBO;
        bool           m_WaveParticleSSBOCreated = false;

        // 全局 descriptor set（从 VansScene 传入，不拥有）
        VkDescriptorSetLayout m_GlobalLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_GlobalSet    = VK_NULL_HANDLE;
    };

} // namespace VansGraphics
