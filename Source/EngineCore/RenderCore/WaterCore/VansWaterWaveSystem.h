#pragma once
#include "vulkan/vulkan.h"
#include "glm/glm.hpp"
#include "../VulkanCore/VansVKImage.h"
#include "VansWaterConfig.h"
#include <vector>
#include <string>

// ============================================================
// VansWaterWaveSystem — 波形系统独立类（设计文档 W-03）
//
// 职责：
//   - 管理 Gerstner 波分量（SSBO 读写）
//   - 波形 simulation compute shader 调度
//   - 位移贴图 Texture2DArray 管理
//   - FFT 系统集成（通过 VansWaterFFT）
//
// 设计参考：波形 Clipmap 方案 §2.2-§2.5
// ============================================================

namespace VansGraphics
{
    class VansVKDevice;
    class VansVKCommandBuffer;
    class VansComputeShader;
    class VansWaterFFT;

    // GerstnerWaveGPU — SSBO 波分量（与 VansWaterSystem.h 中定义一致）
    struct alignas(16) GerstnerWaveGPU
    {
        float amplitude;
        float wavelength;
        float directionX;
        float directionY;
        float speed;
        float steepness;
        float pad0;
        float pad1;
    };

    // VansWaveMode 在 VansWaterConfig.h 中定义

    class VansWaterWaveSystem
    {
    public:
        static constexpr uint32_t WAVE_TEXTURE_SIZE = 256;
        static constexpr uint32_t MAX_LOD_COUNT     = 10;
        static constexpr uint32_t MAX_WAVE_COUNT    = 64;

        VansWaterWaveSystem()  = default;
        ~VansWaterWaveSystem() = default;

        // ── 生命周期 ─────────────────────────────────────────────
        bool Initialize(VansVKDevice* device,
                        const std::string& shaderRoot);
        void Shutdown(VkDevice logicDevice);

        // ── 波形管理 ─────────────────────────────────────────────
        void SetWaveMode(VansWaveMode mode)     { m_WaveMode = mode; }
        void AddWave(const GerstnerWaveGPU& w);
        void RemoveWave(uint32_t index);
        void UpdateSSBO(VkDevice logicDevice);
        void AutoGenerateWaves(int count, const glm::vec2& windDir,
                               float swellAmplitude, float windSpeed);
        const std::vector<GerstnerWaveGPU>& GetWaves() const { return m_Waves; }

        // ── 每帧调度 ─────────────────────────────────────────────
        void UpdateWaveSimulation(VansVKCommandBuffer& cmd, float deltaTime,
                                   const glm::vec3& cameraPos);

        // ── 访问 ──────────────────────────────────────────────────
        VansVKImage& GetDisplacementImage()      { return m_WaveDisplacementImage; }
        bool         IsDisplacementReady() const { return m_WaveDisplacementReady; }
        VkBuffer     GetWaveSSBO() const         { return m_WaveSSBO; }
        VkDescriptorSetLayout GetWaveSimLayout() const { return m_WaveSimLayout; }
        VkDescriptorSet       GetWaveSimSet()    const { return m_WaveSimSet; }

    private:
        bool AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags props,
                            VkBuffer& outBuffer, VkDeviceMemory& outMemory);

        VansVKDevice*      m_Device       = nullptr;
        VansComputeShader* m_WaveSimShader = nullptr;
        VansWaterFFT*      m_FFT          = nullptr;

        VansWaveMode m_WaveMode    = VansWaveMode::Gerstner;
        float        m_Time        = 0.0f;
        bool         m_Initialized = false;

        // Gerstner 波分量
        std::vector<GerstnerWaveGPU> m_Waves;
        VkBuffer       m_WaveSSBO        = VK_NULL_HANDLE;
        VkDeviceMemory m_WaveSSBOMemory  = VK_NULL_HANDLE;

        // 位移贴图 (Texture2DArray, 256² × 10 RGBA16F)
        VansVKImage m_WaveDisplacementImage;
        bool        m_WaveDisplacementReady = false;

        // Descriptor sets
        VkDescriptorSetLayout m_WaveSimLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_WaveSimSet    = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_GlobalLayout  = VK_NULL_HANDLE;
        VkDescriptorSet       m_GlobalSet     = VK_NULL_HANDLE;
    };

} // namespace VansGraphics
