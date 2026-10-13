#pragma once
#include "VansParticleRenderAsset.h"
#include "../VansRenderRuntimeConfig.h"
#include "../VulkanCore/VansVKBuffer.h"
#include "../VulkanCore/VansShader.h"

#include <array>

namespace VansGraphics
{
class VansScene;
class VansVKCommandBuffer;
struct VansParticleDrawItem;
// 仅有 GPU 材质资源，无场景 Transform、脚本组件或模拟实例的所有权。
class VansParticleSurfaceRenderer final
{
public:
    VansParticleSurfaceRenderer() = default;
    ~VansParticleSurfaceRenderer();
    VansParticleSurfaceRenderer(const VansParticleSurfaceRenderer&) = delete;
    VansParticleSurfaceRenderer& operator=(const VansParticleSurfaceRenderer&) = delete;
    bool Initialize(VansScene& scene, const VansParticleRendererConfig& config,
        const VansParticleTextureBindings& textures);
	bool SelectFrameDescriptorSets(VkDescriptorSet globalSet,
		std::uint32_t frameSlot, std::uint32_t gbufferTargetSlot);
    void Draw(VansVKCommandBuffer& cmd, GlobalStateData& state,
        VkBuffer buffer, const VansParticleDrawItem& item) const;
private:
    VansParticleRendererConfig m_Config;
    VansGraphicsShader* m_Shader = nullptr;
    std::vector<VkDescriptorSet> m_Sets;
    std::vector<VkDescriptorSetLayout> m_Layouts;
    VkDescriptorSetLayout m_OwnedLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_OwnedSets;
	std::array<std::uint32_t, VansCommandRecordingConfig::MaximumFramesInFlight> m_DepthTargetSlots{};
};
}
