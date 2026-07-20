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

    // ── WaterGBufferParams GPU struct（对应 water_prepass.vert set=1 binding=0）
    // W-04: wave0-3 + waveSpeedSteepness01/23 字段已移除，改为 SSBO 管理
    struct alignas(16) WaterGBufferParamsGPU
    {
        glm::mat4 VPMatrix;       // 水面专用 VP 矩阵
        glm::mat4 ViewMatrix;     // 水面专用 View 矩阵，用于计算线性深度
        glm::vec4 cameraPosition; // 水面专用相机位置
        glm::ivec4 geometryParams; // x=lodCount, y=meshDim, z=cascadeCount, w=waveMode
        glm::vec4 geometryScale;   // x=basePatchSize, y=morphStart, z=maxDisplacement, w=lodRatio
        glm::vec4 spectrumScale;   // x=baseCoverage, y=cascadeScale, z=time, w=normalStrength
        glm::vec4 windAndChop;     // xy=windDirection, z=windSpeed, w=choppiness
        glm::ivec4 simulationParams; // x=gerstnerCount, y=microSlopeFieldCount, z=microEnabled, w=hybridAdd
        glm::vec4 microSlopeParams;  // x=intensity, y=min wavelength, z=band split, w=macro split
        glm::vec4 microDomainParams; // x=primary coverage, y=secondary coverage, z=rotation radians
    };

    // ── WaterCompositeParams GPU struct（对应 water_composite.frag set=1 binding=2）
    struct alignas(16) WaterCompositeParamsGPU
    {
        glm::vec4 deepWaterColor;      // 深水颜色
        glm::vec4 shallowWaterColor;   // 浅水颜色
        float     fresnelPower;        // Fresnel 指数
        float     waterLevel;          // 水面 Y 高度
        float     specularIntensity;   // 高光强度
        float     refractionStrength;   // screen-height UV displacement at thickness=1
        glm::vec4 absorptionCoeff;     // W-16: 吸收系数 (RGB)
        glm::vec4 scatteringCoeff;     // W-16: 散射系数 (RGB)
        float     sssAnisotropy;       // W-16: 各向异性 g
        float     waterRoughness;      // 水面微面元粗糙度（0=镜面, 1=漫反射）
        float     waterIOR;            // Inspector optimization: IOR → GPU，动态计算 F0
        float     maxOpticalDepth;      // normalized thickness -> metres
        glm::vec4 cameraPosition;       // offset  96: 相机世界位置
        glm::mat4 invViewProjMatrix;    // offset 112: 逆 VP 矩阵
        glm::vec4 mainLightDir;         // offset 176: 主光方向
        glm::mat4 viewMatrix;           // offset 192: 视图矩阵（SSR HZB 追踪）
        glm::mat4 projMatrix;           // offset 256: 投影矩阵（SSR HZB 追踪）
        glm::ivec4 effectFlags;         // offset 320: SSR/refraction/caustics/SSS enabled
    };

    // ── WaterCausticsParams GPU struct（对应 water_caustics.comp set=0 binding=3, W-14）
    struct alignas(16) WaterCausticsParamsGPU
    {
        glm::vec4 sunDirection;       // offset  0: xyz=归一化太阳方向, w=unused
        glm::vec4 mainLightColor;     // offset 16: rgb=颜色, a=强度乘数
        glm::vec4 extinctionCoeff;    // offset 32: rgb=消光系数 (m⁻¹), a=unused
        float     causticsIntensity;  // offset 48: 焦散强度 [0, 2]
        float     causticsScale;      // offset 52: 焦散缩放
        float     shoreFadeStart;     // offset 56: normalized thickness
        float     maxDepth;           // offset 60: 最大深度 (m)
        glm::vec4 opticalParams;      // offset 64: x=water IOR
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
        void DispatchWaterThicknessCS(VansVKCommandBuffer& cmd);   // W-16: 阶段1 厚度图
        void DispatchWaterSSSScatterCS(VansVKCommandBuffer& cmd);  // W-16: 阶段2 SSS 散射
        void DispatchWaterSSR(VansVKCommandBuffer& cmd);
        void DispatchRefractionCS(VansVKCommandBuffer& cmd);
        void DispatchCausticsCS(VansVKCommandBuffer& cmd);

        // ── N-01: Detail Normal compute ───────────────────────────

        // W-12: SSR HZB 延迟绑定（在 HZB 创建后调用）
        void EnsureSSRDescriptorSet(VansVKImage* hzbImage);

        // ── Water Composite（Pass 9）─────────────────────────────
        void RenderWaterComposite(VansVKCommandBuffer& cmd, GlobalStateData& globalState);

        // ── 参数 ─────────────────────────────────────────────────
        void SetWaterLevel(float y)                        { m_WaterLevel = y; }
        void SetWaterMaterial(VansWaterMaterial* mat)      { m_WaterMaterial = mat; }
        float GetWaterLevel() const                        { return m_WaterLevel; }
        bool  IsInitialized() const                        { return m_Initialized; }
        bool  IsDescriptorsReady() const                   { return m_DescriptorsReady; }
        VansWaterGeometryClipmap* GetGeometryClipmap() const { return m_GeometryClipmap; }
        VansWaterFFT* GetFFT() const                       { return m_WaterFFT; }

        // W-04: 运行时更新波分量 SSBO（editor 修改参数后调用）
        void UpdateWaveSSBO();

        // 纹理访问器（供 Editor 纹理预览，W-17）
        VansVKImage& GetDisplacementImage()      { return m_WaveDisplacementImage; }
        VansVKImage& GetDerivativeImage()        { return m_WaveDerivativeImage; }
        VansVKImage& GetReflectionImage()        { return m_WaterReflectionImage; }
        VansVKImage& GetRefractionImage()        { return m_WaterRefractionImage; }
        VansVKImage& GetCausticsImage()          { return m_WaterCausticsImage; }
        VansVKImage& GetThicknessImage()         { return m_WaterThicknessImage; }
        VansVKImage& GetMicroSlopeImage()        { return m_MicroSlopeImage; }
        VansVKImage& GetSSSScatterImage()         { return m_WaterSSSScatterImage; }  // W-16

    private:
        // ── 原始 Vulkan 缓冲分配 ────────────────────────────────
        bool CreateWaterBuffer(VansVKBuffer& buffer, bool& created,
                               VkDeviceSize size, VkBufferUsageFlags usage);
        void DestroyWaterBuffer(VansVKBuffer& buffer, bool& created, VkDevice logicDevice);
        static VkBuffer GetNativeBuffer(const VansVKBuffer& buffer, bool created);

        // ── 引擎设备 ──────────────────────────────────────────────
        VansVKDevice*      m_Device        = nullptr;
        VansWaterMaterial* m_WaterMaterial = nullptr;

        float m_WaterLevel       = 3.4f;
        float m_Time             = 0.0f;
        bool  m_Initialized      = false;
        bool  m_DescriptorsReady = false;

        uint32_t m_RenderWidth  = 0;
        uint32_t m_RenderHeight = 0;

        // ── 拆分类（设计文档 W-02, W-03, W-09）───────────────────
        VansWaterGeometryClipmap* m_GeometryClipmap = nullptr;
        VansWaterFFT*        m_WaterFFT  = nullptr;  // Tessendorf FFT ocean
        VansWaterFFT*        m_MicroFFT  = nullptr;  // short-wave spectral slope FFT

        // ── 着色器 ───────────────────────────────────────────────
        VansGraphicsShader* m_WaterGBufferShader   = nullptr;  // water_prepass.vert/.frag
        VansGraphicsShader* m_WaterCompositeShader = nullptr;  // water_composite.vert/.frag
        VansComputeShader*  m_WaterSSRShader       = nullptr;  // water_ssr.comp (HZB ray march)
        VansComputeShader*  m_WaveSimShader        = nullptr;  // water_wave_spectrum.comp (→ W-03)
        VansComputeShader*  m_WaterRefractionShader = nullptr;  // water_refraction.comp
        VansComputeShader*  m_WaterCausticsShader   = nullptr;  // water_caustics.comp (W-14)
        VansComputeShader*  m_WaterThicknessShader  = nullptr;  // water_thickness.comp (W-16)
        VansComputeShader*  m_WaterSSSScatterShader = nullptr;  // water_sss_scatter.comp (W-16)

        // ── Descriptor Sets：Water GBuffer Pass（Set 1）──────────
        VkDescriptorSetLayout m_GBufPassLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_GBufPassSet    = VK_NULL_HANDLE;

        // ── Descriptor Sets：Water Wave Compute（Set 0）──────────
        VkDescriptorSetLayout m_WaveSimLayout  = VK_NULL_HANDLE;
        VkDescriptorSet       m_WaveSimSet     = VK_NULL_HANDLE;

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

        // ── W-16: SSS Scatter Compute ──────────────────────────────
        VkDescriptorSetLayout m_SSSScatterLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_SSSScatterSet    = VK_NULL_HANDLE;

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
        VansVKBuffer m_SSSParamsBuffer;         // W-16: SSS 散射参数 UBO
        bool m_GBufParamsBufferCreated = false;
        bool m_CompParamsBufferCreated = false;
        bool m_SSRParamsBufferCreated = false;
        bool m_CausticsParamsBufferCreated = false;
        bool m_ThicknessParamsBufferCreated = false;
        bool m_SSSParamsBufferCreated = false;
        WaterGBufferParamsGPU m_GBufParamsCache = {};

        // ── 波形贴图：Compute 写入，WaterGBuffer 顶点采样 ─────────
        // Macro displacement cascades: Texture2DArray 256² × MAX_SPECTRUM_CASCADES.
        VansVKImage m_WaveDisplacementImage;
        bool        m_WaveDisplacementReady = false;
        VansVKImage m_WaveDerivativeImage;
        bool        m_WaveDerivativeReady = false;

        // ── 水体效果贴图：Pre-Water Compute 输出，Composite 采样 ───
        VansVKImage m_WaterReflectionImage;
        VansVKImage m_WaterRefractionImage;
        VansVKImage m_WaterCausticsImage;
        VansVKImage m_WaterThicknessImage;    // W-16: SSS 厚度图
        VansVKImage m_WaterSSSScatterImage;   // W-16: SSS 散射输出
        bool        m_ReflectionOutputReady = false;
        bool        m_RefractionOutputReady = false;
        bool        m_CausticsOutputReady = false;
        bool        m_ThicknessOutputReady = false;
        bool        m_SSSOutputReady = false;

        // Two wavelength bands, each with two decorrelated periodic fields.
        // RG stores exact spectral dh/dx and dh/dz.
        VansVKImage m_MicroSlopeImage;
        bool        m_MicroSlopeReady = false;

        // ── SSBO：Gerstner 波分量（W-04）───────────────────────────
        VansVKBuffer   m_WaveSSBO;
        bool           m_WaveSSBOCreated = false;
        static constexpr uint32_t MAX_WAVE_COUNT = 64;

        // 全局 descriptor set（从 VansScene 传入，不拥有）
        VkDescriptorSetLayout m_GlobalLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_GlobalSet    = VK_NULL_HANDLE;
    };

} // namespace VansGraphics
