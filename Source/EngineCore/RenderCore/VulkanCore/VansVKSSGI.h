#pragma once

#include "VansVKBuffer.h"
#include "../GICore/VansSSGIHistory.h"
#include "../GICore/VansSSGIParameters.h"
#include "../VansRenderRuntimeConfig.h"
#include <array>
#include <vector>
#include <memory>

namespace VansGraphics
{
    class VansComputeShader;
    class VansGIMemoryPublication;
    class VansVKDevice;
    class VansRuntimeRenderTextureManager;

    // SSGI 的分辨率相关资源和历史；纹理分配仍归既有运行时注册表所有。
    struct VansVKSSGI
    {
        static constexpr const char* TextureResult = "Runtime.SSGI.Result";
        static constexpr const char* TextureProbeCacheRadiance = "Runtime.SSGI.ProbeCacheRadiance";
        static constexpr const char* TextureProbeCacheSurface = "Runtime.SSGI.ProbeCacheSurface";
        static constexpr const char* TextureFilterResult = "Runtime.SSGI.FilterResult";
        static constexpr const char* TextureTemporalA = "Runtime.SSGI.TemporalA";
        static constexpr const char* TextureTemporalB = "Runtime.SSGI.TemporalB";
        static constexpr const char* TextureMomentsA = "Runtime.SSGI.MomentsA";
        static constexpr const char* TextureMomentsB = "Runtime.SSGI.MomentsB";
        static constexpr const char* TextureSurfaceHistoryA = "Runtime.SSGI.SurfaceHistoryA";
        static constexpr const char* TextureSurfaceHistoryB = "Runtime.SSGI.SurfaceHistoryB";
        static constexpr const char* TextureAtrousA = "Runtime.SSGI.AtrousA";

        void Initialize(VansVKDevice& device, VansVKCommandBuffer& command,
            VansRuntimeRenderTextureManager& textures, const VansGISettings& gi, uint32_t width, uint32_t height);
        void Destroy(VkDevice device, VansRuntimeRenderTextureManager& textures);
        void RefreshMemory(const VansRuntimeRenderTextureManager& textures) const;

        VansSSGIHistory history;
        VansVKBuffer params;
        std::array<VansVKBuffer, VansCommandRecordingConfig::MaximumFramesInFlight> temporalParams;
        VansComputeShader* shader = nullptr;
        VansComputeShader* probeCacheShader = nullptr;
        VansComputeShader* temporalShader = nullptr;
        VansComputeShader* atrousShader = nullptr;
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        VkDescriptorSetLayout probeCacheLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout temporalLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout atrousLayout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> sets;
        // Cache/Temporal/Atrous 都按 frame slot * 2 + 写入目标索引。
        std::vector<VkDescriptorSet> probeCacheSets;
        std::vector<VkDescriptorSet> temporalSets;
        std::vector<VkDescriptorSet> atrousSets;
    private:
        std::shared_ptr<VansGIMemoryPublication> m_MemoryPublication;
    };
}
