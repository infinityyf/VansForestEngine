#pragma once
#include "vulkan/vulkan.h"
#include "glm/glm.hpp"
#include "VansWaterMaterial.h"
#include "VansWaterConfig.h"
#include "VansWaterLOD.h"
#include "../VulkanCore/VansVKImage.h"
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
//   VansWaterLOD       → CDLOD 网格 + Patch 选取（W-02）
//   VansWaterWaveSystem → 波形模拟 + 位移贴图（W-03）
//   VansWaterFFT       → FFT 频谱计算（W-09）
// ============================================================

namespace VansGraphics
{
    class VansVKCommandBuffer;
    class VansVKDevice;
    class VansGraphicsShader;
    class VansComputeShader;
    class VansRenderPassManager;
    class VansWaterLOD;
    class VansWaterWaveSystem;
    class VansWaterFFT;
    struct GlobalStateData;

    // ── WaterGBufferParams GPU struct（对应 water_prepass.vert set=1 binding=0）
    // W-04: wave0-3 + waveSpeedSteepness01/23 字段已移除，改为 SSBO 管理
    struct alignas(16) WaterGBufferParamsGPU
    {
        glm::mat4 VPMatrix;       // 水面专用 VP 矩阵
        glm::mat4 ViewMatrix;     // 水面专用 View 矩阵，用于计算线性深度
        glm::vec4 cameraPosition; // 水面专用相机位置
        float minLodDist;      // LOD 0 环最小距离（m）
        int   lodLevels;       // 有效 LOD 数量
        int   meshDim;         // 网格每边顶点数 M
        float clipmapBaseScale; // LOD 0 Clipmap 世界覆盖边长（m），默认 128
        float maxWaveAmp;       // 最大波高（m）
        float detailBalance;    // LOD 缩放因子（CPU/GPU 同步，默认 2.0）
        float morphStartRatio;  // morph zone 起点比例 [0, 1)，默认 0.6
        float pad1;             // 填充对齐
        glm::vec4 waveTimeAndScale;      // x=time, y=垂直振幅缩放, z=水平偏移缩放, w=法线强度
        // W-04: wave0-3, waveSpeedSteepness01/23 移除，改用 GerstnerWaveGPU SSBO
        glm::vec4 waveParamsPad[8];      // 预留填充，保持 UBO 大小兼容
    };

    // ── WaterCompositeParams GPU struct（对应 water_composite.frag set=1 binding=2）
    struct alignas(16) WaterCompositeParamsGPU
    {
        glm::vec4 deepWaterColor;      // 深水颜色
        glm::vec4 shallowWaterColor;   // 浅水颜色
        float     fresnelPower;        // Fresnel 指数
        float     waterLevel;          // 水面 Y 高度
        float     specularIntensity;   // 高光强度
        float     foamIntensity;       // W-15: 泡沫强度
        glm::vec4 absorptionCoeff;     // W-16: 吸收系数 (RGB)
        glm::vec4 scatteringCoeff;     // W-16: 散射系数 (RGB)
        float     sssAnisotropy;       // W-16: 各向异性 g
        float     waterRoughness;      // 水面微面元粗糙度（0=镜面, 1=漫反射）
        float     waterIOR;            // Inspector optimization: IOR → GPU，动态计算 F0
        float     padComp1;            // 填充对齐
        glm::vec4 cameraPosition;       // offset  96: 相机世界位置
        glm::mat4 invViewProjMatrix;    // offset 112: 逆 VP 矩阵
        glm::vec4 mainLightDir;         // offset 176: 主光方向
        glm::mat4 viewMatrix;           // offset 192: 视图矩阵（SSR HZB 追踪）
        glm::mat4 projMatrix;           // offset 256: 投影矩阵（SSR HZB 追踪）
    };

    // ── WaterCausticsParams GPU struct（对应 water_caustics.comp set=0 binding=3, W-14）
    struct alignas(16) WaterCausticsParamsGPU
    {
        glm::vec4 sunDirection;       // offset  0: xyz=归一化太阳方向, w=unused
        glm::vec4 mainLightColor;     // offset 16: rgb=颜色, a=强度乘数
        glm::vec4 extinctionCoeff;    // offset 32: rgb=消光系数 (m⁻¹), a=unused
        float     causticsIntensity;  // offset 48: 焦散强度 [0, 2]
        float     causticsScale;      // offset 52: 焦散缩放
        float     waterLevel;         // offset 56: 水面世界 Y
        float     maxDepth;           // offset 60: 最大深度 (m)
    };

    class VansWaterSystem
    {
    public:
        // Compute 生成的周期位移贴图分辨率（W-01: 改为 256² per LOD layer）
        static constexpr uint32_t WAVE_TEXTURE_SIZE = 256;
        // N-01: Detail normal 贴图分辨率（1024²，单层世界空间平铺）
        static constexpr uint32_t DETAIL_TEXTURE_SIZE = 1024;

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
        void UpdateDetailNormalCompute(VansVKCommandBuffer& cmd);

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
        VansWaterLOD* GetLOD() const                       { return m_WaterLOD; }
        VansWaterWaveSystem* GetWaveSystem() const         { return m_WaveSystem; }

        // W-04: 运行时更新波分量 SSBO（editor 修改参数后调用）
        void UpdateWaveSSBO();

        // 纹理访问器（供 Editor 纹理预览，W-17）
        VansVKImage& GetDisplacementImage()      { return m_WaveDisplacementImage; }
        VansVKImage& GetReflectionImage()        { return m_WaterReflectionImage; }
        VansVKImage& GetRefractionImage()        { return m_WaterRefractionImage; }
        VansVKImage& GetCausticsImage()          { return m_WaterCausticsImage; }
        VansVKImage& GetThicknessImage()         { return m_WaterThicknessImage; }
        VansVKImage& GetDetailNormalImage()      { return m_DetailNormalImage; }
        VansVKImage& GetSSSScatterImage()         { return m_WaterSSSScatterImage; }  // W-16

    private:
        // ── 原始 Vulkan 缓冲分配 ────────────────────────────────
        bool AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags props,
                            VkBuffer& outBuffer, VkDeviceMemory& outMemory);

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
        VansWaterLOD*       m_WaterLOD   = nullptr;  // W-02: CDLOD 管理
        VansWaterWaveSystem* m_WaveSystem = nullptr;  // W-03: 波形系统（含 FFT）

        // ── 着色器 ───────────────────────────────────────────────
        VansGraphicsShader* m_WaterGBufferShader   = nullptr;  // water_prepass.vert/.frag
        VansGraphicsShader* m_WaterCompositeShader = nullptr;  // water_composite.vert/.frag
        VansComputeShader*  m_WaterEffectsShader   = nullptr;  // water_effects.comp (fallback)
        VansComputeShader*  m_WaterSSRShader       = nullptr;  // water_ssr.comp (HZB ray march)
        VansComputeShader*  m_WaveSimShader        = nullptr;  // water_wave_spectrum.comp (→ W-03)
        VansComputeShader*  m_WaterRefractionShader = nullptr;  // water_refraction.comp
        VansComputeShader*  m_WaterCausticsShader   = nullptr;  // water_caustics.comp (W-14)
        VansComputeShader*  m_DetailNormalShader    = nullptr;  // water_detail_normal.comp (N-01)
        VansComputeShader*  m_WaterThicknessShader  = nullptr;  // water_thickness.comp (W-16)
        VansComputeShader*  m_WaterSSSScatterShader = nullptr;  // water_sss_scatter.comp (W-16)

        // ── Descriptor Sets：Water GBuffer Pass（Set 1）──────────
        VkDescriptorSetLayout m_GBufPassLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_GBufPassSet    = VK_NULL_HANDLE;

        // ── Descriptor Sets：Water Wave Compute（Set 0）──────────
        VkDescriptorSetLayout m_WaveSimLayout  = VK_NULL_HANDLE;
        VkDescriptorSet       m_WaveSimSet     = VK_NULL_HANDLE;

        // ── Descriptor Sets：Water Effects Compute（Set 0）───────
        VkDescriptorSetLayout m_EffectsLayout  = VK_NULL_HANDLE;
        VkDescriptorSet       m_EffectsSet     = VK_NULL_HANDLE;

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
        VkDescriptorSetLayout m_DetailNormalLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_DetailNormalSet    = VK_NULL_HANDLE;

        // ── Descriptor Sets：Water Composite Pass（Set 1）────────
        VkDescriptorSetLayout m_CompPassLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_CompPassSet    = VK_NULL_HANDLE;

        // ── GPU Params UBO ───────────────────────────────────────
        VkBuffer       m_GBufParamsBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_GBufParamsMemory = VK_NULL_HANDLE;
        VkBuffer       m_CompParamsBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_CompParamsMemory = VK_NULL_HANDLE;
        VkBuffer       m_SSRParamsBuffer  = VK_NULL_HANDLE;
        VkDeviceMemory m_SSRParamsMemory  = VK_NULL_HANDLE;
        VkBuffer       m_CausticsParamsBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_CausticsParamsMemory = VK_NULL_HANDLE;
        VkBuffer       m_ThicknessParamsBuffer = VK_NULL_HANDLE;   // W-16: 厚度图参数 UBO
        VkDeviceMemory m_ThicknessParamsMemory = VK_NULL_HANDLE;
        VkBuffer       m_SSSParamsBuffer = VK_NULL_HANDLE;         // W-16: SSS 散射参数 UBO
        VkDeviceMemory m_SSSParamsMemory = VK_NULL_HANDLE;

        // ── 波形贴图：Compute 写入，WaterGBuffer 顶点采样 ─────────
        // W-01: 改为 Texture2DArray（256² × MAX_LOD_COUNT RGBA16F）
        VansVKImage m_WaveDisplacementImage;
        bool        m_WaveDisplacementReady = false;

        // ── 水体效果贴图：Pre-Water Compute 输出，Composite 采样 ───
        VansVKImage m_WaterReflectionImage;
        VansVKImage m_WaterRefractionImage;
        VansVKImage m_WaterCausticsImage;
        VansVKImage m_WaterThicknessImage;    // W-16: SSS 厚度图
        VansVKImage m_WaterSSSScatterImage;   // W-16: SSS 散射输出
        bool        m_WaterEffectsReady = false;

        // ── N-01: Detail Normal ──────────────────────────────────
        VansVKImage m_DetailNormalImage;               // Texture2DArray 256²×10 RGBA16F
        bool        m_DetailNormalReady   = false;

        // ── SSBO：Gerstner 波分量（W-04）───────────────────────────
        VkBuffer       m_WaveSSBO      = VK_NULL_HANDLE;
        VkDeviceMemory m_WaveSSBOMemory = VK_NULL_HANDLE;
        static constexpr uint32_t MAX_WAVE_COUNT = 128;  // 提升到 128 以覆盖粗 LOD（LOD4+ Nyquist 需要长波）

        // 全局 descriptor set（从 VansScene 传入，不拥有）
        VkDescriptorSetLayout m_GlobalLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_GlobalSet    = VK_NULL_HANDLE;
    };

} // namespace VansGraphics
