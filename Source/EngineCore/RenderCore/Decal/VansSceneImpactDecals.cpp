#include "../VansScene.h"
#include "VansImpactDecalSystem.h"
#include "../../GameplayActionAdapters/Decal/VansDecalActionService.h"
#include "../../PhysicsCore/VansTerrainPhysicsNode.h"
#include "../../Util/VansLog.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include "../VulkanCore/VansRenderPass.h"

namespace VansGraphics
{
void VansScene::UpdateDecalPassDescriptorSet()
{
    if (!m_DecalPassDescriptorSet) return;
    auto* passes = VansRenderPassManager::GetInstance();
    const auto image = [](VansVKImage& value) {
        return VkDescriptorImageInfo{value.GetSampler(), value.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    };
    const std::array<VkDescriptorImageInfo, 3> images{image(passes->GetGbuffer2()),
        image(passes->GetGbuffer1()), image(passes->GetNormal())};
    bool changed = m_DecalPassDescriptorsDirty;
    for (size_t i=0; i<images.size(); ++i)
        changed |= images[i].sampler != m_DecalPassImages[i].sampler
            || images[i].imageView != m_DecalPassImages[i].imageView;
    if (!changed) return;
    // 调用点沿用渲染帧退役后的 descriptor 更新阶段，所有贴花只写这一套绑定。
    auto* descriptors = VansVKDescriptorManager::GetInstance();
    descriptors->BeginDescriptorUpdate();
    const uint32_t bindings[] = {DECAL_PASS_BINDING_GBUFFER2, DECAL_PASS_BINDING_GBUFFER1, DECAL_PASS_BINDING_NORMAL};
    for (size_t i=0; i<images.size(); ++i)
        descriptors->WriteImageDescriptor(m_DecalPassDescriptorSet, bindings[i],
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {images[i]});
    descriptors->CommitDescriptorUpdates();
    m_DecalPassImages = images;
    m_DecalPassDescriptorsDirty = false;
}

Vans::VansDecalSceneBackend VansScene::MakeDecalSceneBackend()
{
    return {[this](const std::string& source, const Vans::VansSurfaceImpact& impact, std::string& error) {
        if (!m_ImpactDecals || m_LoadMode != VansSceneLoadMode::Runtime) return false;
        if (impact.kind == Vans::VansSurfaceImpactKind::Terrain &&
            (!m_TerrainPhysicsNode || !m_TerrainPhysicsNode->IsEnabled())) return false;
        return m_ImpactDecals->Spawn(source, impact, error);
    }};
}

bool VansScene::PrepareImpactDecalPools()
{
    if (m_LoadMode != VansSceneLoadMode::Runtime || !m_RuntimeWorld) return true;
    const auto templates = m_DecalRenderNodes;
    size_t total = 0;
    for (auto* node : templates)
    {
        const auto* source = dynamic_cast<VansDecalRenderNode*>(node);
        if (!source || !source->m_ImpactPoolConfig) continue;
        total += source->m_ImpactPoolConfig->capacity;
        if (!source->m_ImpactPoolConfig->IsValid() || !source->m_Mesh || !source->m_Material ||
            total > VansImpactDecalSystem::MaximumInstances)
        { VANS_LOG_ERROR("[Decal] Invalid impact pool: " << source->m_NodeName); return false; }
    }
    if (!total) return true;
    m_ImpactDecals = std::make_unique<VansImpactDecalSystem>(*m_RuntimeWorld, m_TerrainRenderNode);
    for (auto* node : templates)
    {
        auto* source = dynamic_cast<VansDecalRenderNode*>(node);
        if (!source || !source->m_ImpactPoolConfig) continue;
        const auto config = *source->m_ImpactPoolConfig;
        source->SetEnabled(false);
        std::vector<VansDecalRenderNode*> nodes;
        for (uint32_t i=0; i<config.capacity; ++i)
        {
            // 场景加载阶段创建，模型/材质共享；开枪只改实例状态，不分配 GPU 描述符。
            auto instance = std::make_unique<VansDecalRenderNode>(source->m_Device);
            instance->m_Mesh = source->m_Mesh;
            instance->m_SourceMesh = source->m_SourceMesh;
            instance->m_SubmeshIndex = source->m_SubmeshIndex;
            instance->m_Material = source->m_Material;
            instance->m_RayTracingEnabled = false;
            instance->SetName(source->m_NodeName + "_Runtime_" + std::to_string(i));
            instance->SetTransformData(glm::vec3(0), glm::vec3(0), glm::vec3(config.diameter/2,config.depth/2,config.diameter/2));
            instance->SetEnabled(false);
            nodes.push_back(instance.get());
            RegistRenderNode(instance.release(), DECAL_NODE);
        }
        std::string error;
        if (!m_ImpactDecals->AddPool(source->m_EntityGuid, config, nodes, error))
        { VANS_LOG_ERROR("[Decal] " << error); return false; }
        VANS_LOG("[Decal] Prepared impact pool template=" << source->m_EntityGuid << " capacity=" << config.capacity);
    }
    return true;
}
}
