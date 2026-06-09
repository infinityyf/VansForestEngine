#pragma once
#include "vulkan/vulkan.h"
#include "glm/glm.hpp"
#include "../VulkanCore/VansVKImage.h"
#include <string>

// ============================================================
// VansWaterFFT — FFT 海洋频谱计算器（设计文档 W-09/W-10）
//
// 职责：
//   - Phillips 频谱初始化 (water_init_spectrum.comp)
//   - Stockham FFT 蝶形迭代 (water_fft_iter.comp)
//   - 时间演化 (water_time_evolve.comp)
//   - 逐 LOD 位移提取到 Clipmap Texture2DArray (water_displacement_extract.comp)
//
// 设计参考：波形 Clipmap 方案 §3.2-§3.6
// ============================================================

namespace VansGraphics
{
    class VansVKDevice;
    class VansVKCommandBuffer;
    class VansComputeShader;

    class VansWaterFFT
    {
    public:
        static constexpr uint32_t FFT_RESOLUTION = 256;
        static constexpr uint32_t MAX_LOD_COUNT  = 10;

        VansWaterFFT()  = default;
        ~VansWaterFFT() = default;

        // ── 生命周期 ─────────────────────────────────────────────
        bool Initialize(VansVKDevice* device, const std::string& shaderRoot,
                        VansVKImage* displacementImage);
        void Shutdown(VkDevice logicDevice);

        // ── 参数设置 ─────────────────────────────────────────────
        void SetWindParams(glm::vec2 direction, float speed, float amplitude);
        void SetOceanBaseScale(float scale) { m_BaseScale = scale; }

        // ── 每帧调度 ─────────────────────────────────────────────
        // 完整 FFT 管线：Init (if dirty) → Evolve → IFFT (16 iter) → Extract
        void UpdateFFT(VansVKCommandBuffer& cmd, float time);

        bool NeedsReinit() const { return m_NeedsReinit; }
        void MarkReinit()        { m_NeedsReinit = true; }

    private:
        bool AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags props,
                            VkBuffer& outBuffer, VkDeviceMemory& outMemory);

        VansVKDevice* m_Device       = nullptr;
        VansVKImage*  m_DisplacementImage = nullptr;  // 指向 VansWaterWaveSystem 的 Texture2DArray

        // FFT 着色器
        VansComputeShader* m_InitSpectrumShader      = nullptr;
        VansComputeShader* m_FFTIterShader           = nullptr;
        VansComputeShader* m_TimeEvolveShader        = nullptr;
        VansComputeShader* m_DisplacementExtractShader = nullptr;

        // Ping-Pong 双缓冲 (256² × 2 RGBA32F each)
        VansVKImage m_H0Spectrum;      // H0(k): layer 0=real, 1=imag
        VansVKImage m_HtSpectrum;      // H(t): evolved
        VansVKImage m_PingPong[2];     // IFFT ping-pong [0]=src, [1]=dst

        // UBO 参数缓冲
        VkBuffer       m_ParamsBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_ParamsMemory = VK_NULL_HANDLE;

        // Descriptor sets
        VkDescriptorSetLayout m_InitLayout       = VK_NULL_HANDLE;
        VkDescriptorSet       m_InitSet          = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_FFTIterLayout    = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_EvolveLayout     = VK_NULL_HANDLE;
        VkDescriptorSet       m_EvolveSet        = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_ExtractLayout    = VK_NULL_HANDLE;
        VkDescriptorSet       m_ExtractSet       = VK_NULL_HANDLE;

        // 参数
        glm::vec2 m_WindDirection = {0.7071f, 0.7071f};
        float     m_WindSpeed     = 12.0f;
        float     m_Amplitude     = 1.5f;
        float     m_BaseScale     = 256.0f;
        float     m_Time          = 0.0f;
        bool      m_NeedsReinit   = true;
        bool      m_Initialized   = false;
    };

} // namespace VansGraphics
