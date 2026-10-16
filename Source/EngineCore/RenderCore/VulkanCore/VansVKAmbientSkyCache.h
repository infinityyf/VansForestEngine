#pragma once

#include "VansVKBuffer.h"
#include "../VansRenderRuntimeConfig.h"
#include "../SkyLightingCore/VansAmbientSkyCacheParameters.h"
#include <array>
#include <vector>
#include <memory>

namespace VansGraphics
{
    class VansComputeShader;
    class VansGIMemoryPublication;
    class VansVKDevice;
    class VansRuntimeRenderTextureManager;
    struct GIAmbientSkyCacheSettings;

    // 缓存生命周期只在既有设备维护安全点执行；纹理分配仍归运行时注册表所有。
    struct VansVKAmbientSkyCache
    {
        static constexpr const char* TextureX = "Runtime.AmbientSkyCache.X";
        static constexpr const char* TextureY = "Runtime.AmbientSkyCache.Y";
        static constexpr const char* TextureZ = "Runtime.AmbientSkyCache.Z";

        void Initialize(VansVKDevice& device, VansVKCommandBuffer& command,
            VansRuntimeRenderTextureManager& textures, const GIAmbientSkyCacheSettings& settings,
            const glm::vec3& cameraPosition, bool hasWorld);
        void Destroy(VkDevice device, VansRuntimeRenderTextureManager& textures);
        void RefreshMemory(const VansRuntimeRenderTextureManager& textures) const;

        VansComputeShader* shader = nullptr;
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> sets;
        std::array<VansVKBuffer, VansCommandRecordingConfig::MaximumFramesInFlight> params;
        std::array<VansVKBuffer, VansCommandRecordingConfig::MaximumFramesInFlight> info;
        glm::vec3 origin{0.0f};
        glm::ivec3 ringOffset{0};
        bool initialized = false;
        uint32_t frameOffset = 0u;
    private:
        std::shared_ptr<VansGIMemoryPublication> m_MemoryPublication;
    };
}
