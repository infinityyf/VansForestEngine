#pragma once
#include "vulkan/vulkan.h"
#include "glm/glm.hpp"
#include "../VulkanCore/VansVKImage.h"
#include <string>
#include <cstdint>

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
        static constexpr uint32_t FIELD_COUNT    = 6;

        struct Params
        {
            uint32_t resolution = FFT_RESOLUTION;
            uint32_t lodCount = MAX_LOD_COUNT;
            float baseScale = 64.0f;
            float detailBalance = 2.0f;

            glm::vec2 windDirection = {0.7071f, 0.7071f};
            float windSpeed = 12.0f;
            float spectrumAmplitude = 1.0f;

            float choppiness = 1.0f;
            float smallWaveDamping = 0.001f;
            float windDependency = 0.07f;
            float depth = 10000.0f;
            float repeatPeriod = 0.0f;

            float maxWaveAmp = 5.0f;
            float normalScale = 1.0f;
            float foamSlopeScale = 0.25f;
            float foamFoldScale = 1.0f;
            float foamFoldThreshold = 0.0f;

            uint32_t randomSeed = 1337;
        };

        VansWaterFFT() = default;
        ~VansWaterFFT() = default;

        bool Initialize(VansVKDevice* device, const std::string& shaderRoot,
                        VansVKImage* displacementImage,
                        VansVKImage* derivativeImage);
        void Shutdown(VkDevice logicDevice);

        void SetParams(const Params& params);
        const Params& GetParams() const { return m_Params; }

        void UpdateFFT(VansVKCommandBuffer& cmd, float time);

        bool IsReady() const { return m_Initialized && m_DescriptorsReady; }
        bool NeedsReinit() const { return m_NeedsReinit; }
        void MarkReinit() { m_NeedsReinit = true; }

    private:
        bool AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags props,
                            VkBuffer& outBuffer, VkDeviceMemory& outMemory);
        bool CreateDescriptors();
        void UpdateParamsBuffer(float time);
        void BarrierImage(VansVKCommandBuffer& cmd, VansVKImage& image,
                          VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                          VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                          uint32_t baseLayer, uint32_t layerCount);

        VansVKDevice* m_Device = nullptr;
        VansVKImage*  m_DisplacementImage = nullptr;
        VansVKImage*  m_DerivativeImage = nullptr;

        VansComputeShader* m_InitSpectrumShader = nullptr;
        VansComputeShader* m_TimeEvolveShader = nullptr;
        VansComputeShader* m_FFTIterShader = nullptr;
        VansComputeShader* m_DisplacementExtractShader = nullptr;

        VansVKImage m_H0Spectrum;
        VansVKImage m_PingPong[2];

        VkBuffer       m_ParamsBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_ParamsMemory = VK_NULL_HANDLE;

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
