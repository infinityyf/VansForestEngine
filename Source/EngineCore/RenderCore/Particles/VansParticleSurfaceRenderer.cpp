#include "VansParticleSurfaceRenderer.h"
#include "VansParticleRenderSystem.h"
#include "../VansScene.h"
#include "../VulkanCore/VansTexture.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../VulkanCore/VansRenderPass.h"
#include "../../ParticleCore/VansParticleInstanceData.h"
#include "../../Util/VansLog.h"

namespace VansGraphics
{
VansParticleSurfaceRenderer::~VansParticleSurfaceRenderer()
{
    auto* manager = VansVKDescriptorManager::GetInstance();
    if (!m_OwnedSets.empty()) manager->DestroyDescriptorSet(m_OwnedSets);
    if (m_OwnedLayout) manager->ReleaseDescriptorSetLayout(m_OwnedLayout);
}
bool VansParticleSurfaceRenderer::Initialize(VansScene& scene, const VansParticleRendererConfig& config,
    const VansParticleTextureBindings& textures)
{
    m_Config = config;
    const bool ribbon = config.m_Type == VansParticleRendererType::Ribbon;
    const bool softRibbon = ribbon && config.m_Ribbon.softIntersection > 0.0f;
    const bool sixWay = config.m_LightingMode == VansParticleLightingMode::SixWayLit;
    m_Shader = static_cast<VansGraphicsShader*>(scene.FindShaderAsset(ribbon
        ? (softRibbon ? "ParticleRibbonSoft" : "ParticleRibbon") : sixWay ? "ParticleSixWay" : "Particle"));
    VansTexture* color = sixWay ? textures.positiveAxes : textures.color;
    if (!m_Shader || !color || (sixWay && !textures.negativeAxes))
    { VANS_LOG_ERROR("[Particle] Surface material has unresolved shader or texture dependencies"); return false; }
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}
    };
    if (sixWay)
    {
        bindings.push_back({1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr});
        bindings.push_back({2,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr});
        bindings.push_back({3,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VANS_PUNCTUAL_SHADOW_ATLAS_COUNT,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr});
    }
    if (softRibbon) bindings.push_back({1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr});
    if (!VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom(bindings,m_OwnedLayout,m_OwnedSets)) return false;
    auto* manager = VansVKDescriptorManager::GetInstance();
    auto* passes = VansRenderPassManager::GetInstance();
    manager->BeginDescriptorUpdate();
    const auto write = [&](uint32_t binding, VansVKImage& image, VkImageLayout layout) {
        manager->WriteImageDescriptor(m_OwnedSets[0],binding,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{image.GetSampler(),image.GetImageView(),layout}});
    };
    write(0,color->GetImage(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (sixWay)
    {
        write(1,textures.negativeAxes->GetImage(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        manager->WriteImageDescriptor(m_OwnedSets[0],2,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{passes->GetCascadeShadowSampler(),passes->GetCascadeShadowArrayView(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
        manager->WriteImageDescriptor(m_OwnedSets[0],3,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            passes->GetPunctualShadowDescriptorInfos());
    }
    if (softRibbon) write(1,passes->GetDepth(),VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    manager->CommitDescriptorUpdates();
    m_Layouts = {scene.GetGlobalDescriptorSetLayout(),m_OwnedLayout};
    m_Sets = {scene.GetGlobalDescriptorSet(),m_OwnedSets[0]};
    return true;
}
void VansParticleSurfaceRenderer::Draw(VansVKCommandBuffer& cmd, GlobalStateData& state,
    VkBuffer buffer, const VansParticleDrawItem& item) const
{
    if (!m_Shader || !buffer || !item.indexCount || !item.instanceCount) return;
    static std::vector<VkVertexInputBindingDescription> billboardBindings = {
        {0,16,VK_VERTEX_INPUT_RATE_VERTEX},{1,sizeof(VansParticleInstanceData),VK_VERTEX_INPUT_RATE_INSTANCE}
    };
    static std::vector<VkVertexInputAttributeDescription> billboardAttributes = {
        {0,0,VK_FORMAT_R32G32_SFLOAT,0},{1,0,VK_FORMAT_R32G32_SFLOAT,8},
        {2,1,VK_FORMAT_R32G32B32_SFLOAT,offsetof(VansParticleInstanceData,m_WorldPosition)},
        {3,1,VK_FORMAT_R32_SFLOAT,offsetof(VansParticleInstanceData,m_Size)},
        {4,1,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(VansParticleInstanceData,m_Color)},
        {5,1,VK_FORMAT_R32_SFLOAT,offsetof(VansParticleInstanceData,m_Rotation)},
        {6,1,VK_FORMAT_R32_SFLOAT,offsetof(VansParticleInstanceData,m_FrameIndex)},
        {7,1,VK_FORMAT_R32G32_SFLOAT,offsetof(VansParticleInstanceData,m_Padding)}
    };
    static std::vector<VkVertexInputBindingDescription> ribbonBindings = {{0,sizeof(VansPolylineVertex),VK_VERTEX_INPUT_RATE_VERTEX}};
    static std::vector<VkVertexInputAttributeDescription> ribbonAttributes = {
        {0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(VansPolylineVertex,position)},
        {1,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(VansPolylineVertex,color)},
        {2,0,VK_FORMAT_R32G32_SFLOAT,offsetof(VansPolylineVertex,uv)}
    };
    auto* savedBindings = state.vertexInputBindingDescriptions;
    auto* savedAttributes = state.vertexInputAttributeDescriptions;
    state.vertexInputBindingDescriptions = item.ribbon ? &ribbonBindings : &billboardBindings;
    state.vertexInputAttributeDescriptions = item.ribbon ? &ribbonAttributes : &billboardAttributes;
    auto* pipeline = cmd.EnsureGraphicsShader(*m_Shader,state,m_Layouts);
    state.vertexInputBindingDescriptions = savedBindings;
    state.vertexInputAttributeDescriptions = savedAttributes;
    if (!pipeline) return;
    cmd.BindGraphicsPipeline(*pipeline);
    cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,*pipeline,0,m_Sets,{});
    const auto& sixWay = m_Config.m_SixWayLighting;
    if (m_Config.m_LightingMode == VansParticleLightingMode::SixWayLit)
    {
        const glm::vec4 constants[5] = {
            {m_Config.m_SpriteSheetEnabled ? m_Config.m_SpriteColumns : 1,m_Config.m_SpriteSheetEnabled ? m_Config.m_SpriteRows : 1,0,0},
            {sixWay.m_LightIntensity,sixWay.m_AmbientIntensity,sixWay.m_EmissiveIntensity,sixWay.m_AbsorptionStrength},
            {sixWay.m_LightmapRemapMin,sixWay.m_LightmapRemapMax,0.004f,0}, {0,-1,0,0},{1,1,1,1}
        };
        cmd.UpdatePushConstants(*pipeline,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(constants),constants);
    }
    else
    {
        const glm::vec4 constants = item.ribbon ? glm::vec4(m_Config.m_Ribbon.softIntersection,0,0,0)
            : glm::vec4(m_Config.m_SpriteSheetEnabled ? m_Config.m_SpriteColumns : 1,m_Config.m_SpriteSheetEnabled ? m_Config.m_SpriteRows : 1,0,0);
        cmd.UpdatePushConstants(*pipeline,item.ribbon ? VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(constants),&constants);
    }
    const VkBuffer buffers[] = {buffer,buffer};
    const VkDeviceSize offsets[] = {item.vertexOffset,item.instanceOffset};
    cmd.BindVertexBuffers(0,item.ribbon ? 1 : 2,buffers,offsets);
    cmd.BindIndexBuffer(buffer,item.indexOffset,VK_INDEX_TYPE_UINT32);
    cmd.DrawIndexed(item.indexCount,item.instanceCount,0,0,0);
}
}
