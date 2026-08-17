#pragma once
#include "vulkan/vulkan.h"
#include "glm/glm.hpp"
#include "VansWaterConfig.h"
#include "../VulkanCore/VansVKImage.h"
#include "../VulkanCore/VansVKBuffer.h"
#include <string>
#include <cstdint>
#include <array>

namespace VansGraphics
{
    class VansVKDevice;
    class VansVKCommandBuffer;
    class VansComputeShader;

    class VansWaterFFT
    {
    public:
        static constexpr uint32_t FFT_RESOLUTION = 256;
        static constexpr uint32_t MAX_CASCADE_COUNT = VansWaterConfig::MAX_SPECTRUM_CASCADES;
        // height、horizontal X/Z，以及解析 height slope X/Z。
        static constexpr uint32_t FIELD_COUNT    = 5;

        enum class OutputMode : uint32_t
        {
            Displacement = 0,
            SpectralSlope = 1,
        };

        struct alignas(16) IterPushConstants
        {
            int stage;
            int direction;
            int inverse;
            int resolution;
            int normalize;
            int fieldCount;
            int cascadeCount;
            int pad0;
        };

        struct Params
        {
            uint32_t resolution = FFT_RESOLUTION;
            uint32_t cascadeCount = MAX_CASCADE_COUNT;
            glm::vec2 windDirection = {0.7071f, 0.7071f};
            float windSpeed = 12.0f;
            float spectrumAmplitude = 0.001f;

            float choppiness = 1.0f;
            float smallWaveDamping = 0.001f;
            float windDependency = 0.07f;
            float depth = 10000.0f;
            float repeatPeriod = 0.0f;

            uint32_t randomSeed = 1337;
            float capillaryCoefficient = 0.000074f; // surface tension / water density (m^3/s^2)
            std::array<float, MAX_CASCADE_COUNT> domainCoverage = { 64.0f, 256.0f, 1024.0f, 4096.0f };
            std::array<float, MAX_CASCADE_COUNT> minWavelength = { 1.0f, 64.0f, 256.0f, 1024.0f };
            std::array<float, MAX_CASCADE_COUNT> maxWavelength = { 64.0f, 256.0f, 1024.0f, 4096.0f };
        };

        VansWaterFFT() = default;
        ~VansWaterFFT() = default;

        bool Initialize(VansVKDevice* device, const std::string& shaderRoot,
                        VansVKImage* displacementImage,
                        VansVKImage* derivativeImage,
                        OutputMode outputMode = OutputMode::Displacement);
        void Shutdown(VkDevice logicDevice);

        void SetParams(const Params& params);
        const Params& GetParams() const { return m_Params; }

        void UpdateFFT(VansVKCommandBuffer& cmd, float time);

        bool IsReady() const { return m_Initialized && m_DescriptorsReady; }
        bool NeedsReinit() const { return m_NeedsReinit; }
        void MarkReinit() { m_NeedsReinit = true; }

        VansVKImage& GetH0SpectrumImage() { return m_H0Spectrum; }
        VansVKImage& GetPingPongImage(uint32_t index) { return m_PingPong[index & 1u]; }

    private:
        bool CreateDescriptors();
        void UpdateParamsBuffer(float time);
        void BarrierImage(VansVKCommandBuffer& cmd, VansVKImage& image,
                          VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                          VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                          uint32_t baseLayer, uint32_t layerCount);

        VansVKDevice* m_Device = nullptr;
        VansVKImage*  m_DisplacementImage = nullptr;
        VansVKImage*  m_DerivativeImage = nullptr;
        OutputMode m_OutputMode = OutputMode::Displacement;

        VansComputeShader* m_InitSpectrumShader = nullptr;
        VansComputeShader* m_TimeEvolveShader = nullptr;
        VansComputeShader* m_FFTIterShader = nullptr;
        VansComputeShader* m_DisplacementExtractShader = nullptr;

        VansVKImage m_H0Spectrum;
        VansVKImage m_PingPong[2];

        VansVKBuffer m_ParamsBuffer;
        bool m_ParamsBufferCreated = false;

        VkDescriptorSetLayout m_InitLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_InitSet = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_EvolveLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_EvolveSet = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_IterLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_IterSet[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        VkDescriptorSetLayout m_ExtractLayout = VK_NULL_HANDLE;
        VkDescriptorSet       m_ExtractSet[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };

        Params m_Params;
        bool m_NeedsReinit = true;
        bool m_Initialized = false;
        bool m_DescriptorsReady = false;
    };
}
