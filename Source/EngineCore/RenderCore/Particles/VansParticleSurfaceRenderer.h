#pragma once
#include "VansParticleRenderAsset.h"
#include "../VulkanCore/VansVKBuffer.h"
#include "../VulkanCore/VansShader.h"

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
    void Draw(VansVKCommandBuffer& cmd, GlobalStateData& state,
        VkBuffer buffer, const VansParticleDrawItem& item) const;
private:
    VansParticleRendererConfig m_Config;
    VansGraphicsShader* m_Shader = nullptr;
    std::vector<VkDescriptorSet> m_Sets;
    std::vector<VkDescriptorSetLayout> m_Layouts;
    VkDescriptorSetLayout m_OwnedLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_OwnedSets;
};
}
