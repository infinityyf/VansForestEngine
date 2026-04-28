#include "../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansScene.h"
#include "../Configration/VansConfigration.h"

#include "VulkanCore/VansMesh.h"
#include "VulkanCore/VansVKDevice.h"
#include "TerrainCore/VansTerrain.h"
#include "../Util/VansLog.h"
#include "VulkanCore/VansRenderPass.h"
#include "../VansTimer.h"

// ===========================================================================
// Draw commands — one per render pass type
// ===========================================================================

void VansGraphics::VansScene::DrawShadowNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();

    // Iterate opaque nodes instead of dedicated shadow node list
    for (auto& node : m_OpaqueRenderNodes)
    {
        if (node == nullptr) continue;

        // Check support_shadow flag on the node
        auto* opaque = static_cast<VansCommonRenderNode*>(node);
        if (!opaque->m_SupportShadow) continue;

        // Check if the material has a shadow pass shader
        VansGraphicsShader* shadowShader = node->m_Material->GetPassShader(VansPass::SHADOW);
        if (!shadowShader) continue;

        // Draw with shadow shader using cascade shadow push constants
        node->DrawCascadeShadowWithPassShader(cmd, globalStateData, shadowShader,
                                               opaque->m_ShadowDescSets, opaque->m_ShadowDescSetLayouts);
    }
}

void VansGraphics::VansScene::DrawMotionVectorNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();

    for (auto& node : m_OpaqueRenderNodes)
    {
        if (node == nullptr) continue;

        auto* opaque = static_cast<VansCommonRenderNode*>(node);

        // Use the velocity pass shader registered for this material
        VansGraphicsShader* mvShader = node->m_Material->GetPassShader(VansPass::VELOCITY);
        if (!mvShader) continue;

        // Reuse shadow descriptor sets (Global / EmptyPass / Object — same 3 sets)
        node->DrawCascadeShadowWithPassShader(cmd, globalStateData, mvShader,
                                               opaque->m_ShadowDescSets, opaque->m_ShadowDescSetLayouts);
    }
}

void VansGraphics::VansScene::DrawPointShadow(int lightIndex)
{
    auto vansConfigration = VansConfigration::GetInstance();
    float punctualShadowSize = vansConfigration->GetPunctualShadowMapWidth();
    float patchShadowSize = punctualShadowSize / 8;

    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    for (int shadowDirection = 0; shadowDirection < 6; shadowDirection++)
    {
        float regionOffsetX = (lightIndex * 6 + shadowDirection) % 8 * patchShadowSize;
        float regionOffsetY = (lightIndex * 6 + shadowDirection) / 8 * patchShadowSize;

        VkViewport viewPort = {};
        viewPort.x = regionOffsetX;
        viewPort.y = regionOffsetY;
        viewPort.width = patchShadowSize;
        viewPort.height = patchShadowSize;
        viewPort.minDepth = 0.0f;
        viewPort.maxDepth = 1.0f;

        VkRect2D scissor = {};
        scissor.offset = { (int)(regionOffsetX), (int)(regionOffsetY) };
        scissor.extent = { (uint32_t)(patchShadowSize), (uint32_t)(patchShadowSize) };

        cmd.SetViewport(0, { viewPort });
        cmd.SetScissor(0, { scissor });

        for (auto& node : m_OpaqueRenderNodes)
        {
            if (node == nullptr) continue;

            auto* opaque = static_cast<VansCommonRenderNode*>(node);
            if (!opaque->m_SupportShadow) continue;

            VansGraphicsShader* shader = node->m_Material->GetPassShader(VansPass::PUNCTUAL_SHADOW);
            if (!shader) continue;

            node->DrawPunctualShadowWithPassShader(cmd, globalStateData, shader,
                                                    opaque->m_ShadowDescSets, opaque->m_ShadowDescSetLayouts,
                                                    lightIndex, shadowDirection);
        }
    }
}

void VansGraphics::VansScene::DrawSpotShadow(int pointCount, int lightIndex)
{
    auto vansConfigration = VansConfigration::GetInstance();
    float punctualShadowSize = vansConfigration->GetPunctualShadowMapWidth();
    float patchShadowSize = punctualShadowSize / 8;

    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();

    float regionOffsetX = (pointCount * 6 + lightIndex) % 8 * patchShadowSize;
    float regionOffsetY = (pointCount * 6 + lightIndex) / 8 * patchShadowSize;

    VkViewport viewPort = {};
    viewPort.x = regionOffsetX;
    viewPort.y = regionOffsetY;
    viewPort.width = patchShadowSize;
    viewPort.height = patchShadowSize;
    viewPort.minDepth = 0.0f;
    viewPort.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = { (int)(regionOffsetX), (int)(regionOffsetY) };
    scissor.extent = { (uint32_t)(patchShadowSize), (uint32_t)(patchShadowSize) };

    cmd.SetViewport(0, { viewPort });
    cmd.SetScissor(0, { scissor });

    for (auto& node : m_OpaqueRenderNodes)
    {
        if (node == nullptr) continue;

        auto* opaque = static_cast<VansCommonRenderNode*>(node);
        if (!opaque->m_SupportShadow) continue;

        VansGraphicsShader* shader = node->m_Material->GetPassShader(VansPass::PUNCTUAL_SHADOW);
        if (!shader) continue;

        node->DrawPunctualShadowWithPassShader(cmd, globalStateData, shader,
                                                opaque->m_ShadowDescSets, opaque->m_ShadowDescSetLayouts,
                                                pointCount + lightIndex, 0);
    }
}

void VansGraphics::VansScene::DrawRectShadow(int pointCount, int spotCount, int lightIndex)
{
    auto vansConfigration = VansConfigration::GetInstance();
    float punctualShadowSize = vansConfigration->GetPunctualShadowMapWidth();
    float patchShadowSize = punctualShadowSize / 8;

    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();

    // Atlas slot index = pointCount*6 (point uses 6 faces) + spotCount + lightIndex
    int slotIndex = pointCount * 6 + spotCount + lightIndex;
    float regionOffsetX = (slotIndex % 8) * patchShadowSize;
    float regionOffsetY = (slotIndex / 8) * patchShadowSize;

    VkViewport viewPort = {};
    viewPort.x = regionOffsetX;
    viewPort.y = regionOffsetY;
    viewPort.width = patchShadowSize;
    viewPort.height = patchShadowSize;
    viewPort.minDepth = 0.0f;
    viewPort.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = { (int)(regionOffsetX), (int)(regionOffsetY) };
    scissor.extent = { (uint32_t)(patchShadowSize), (uint32_t)(patchShadowSize) };

    cmd.SetViewport(0, { viewPort });
    cmd.SetScissor(0, { scissor });

    // Encode logical lightIndex passed to shader as (pointCount + spotCount + lightIndex)
    // so PunctualShadow.vert can pick the rect branch via uPointLightCount + uSpotLightCount.
    int shaderLightIndex = pointCount + spotCount + lightIndex;
    for (auto& node : m_OpaqueRenderNodes)
    {
        if (node == nullptr) continue;

        auto* opaque = static_cast<VansCommonRenderNode*>(node);
        if (!opaque->m_SupportShadow) continue;

        VansGraphicsShader* shader = node->m_Material->GetPassShader(VansPass::PUNCTUAL_SHADOW);
        if (!shader) continue;

        node->DrawPunctualShadowWithPassShader(cmd, globalStateData, shader,
                                                opaque->m_ShadowDescSets, opaque->m_ShadowDescSetLayouts,
                                                shaderLightIndex, 0);
    }
}

void VansGraphics::VansScene::DrawSkyBoxNode()
{
    if (m_SkyBoxNode == nullptr)
    {
        return;
    }
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    m_SkyBoxNode->Draw(cmd, globalStateData);
}

void VansGraphics::VansScene::DrawOpaqueNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    for (auto& node : m_OpaqueRenderNodes)
    {
        if (node == nullptr)
        {
            continue;
        }
        node->Draw(cmd, globalStateData);
    }
}

void VansGraphics::VansScene::DrawTerrainNode(bool shadowPass, bool motionVectorPass)
{
    if(m_TerrainRenderNode== nullptr)
    {
        return;
	}
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    if (shadowPass)
    {
        static_cast<VansTerrainRenderNode*>(m_TerrainRenderNode)->DrawShadow(cmd, globalStateData);
    }
    else if (motionVectorPass)
    {
        static_cast<VansTerrainRenderNode*>(m_TerrainRenderNode)->DrawMotionVector(cmd, globalStateData);
    }
    else
    {
        static_cast<VansTerrainRenderNode*>(m_TerrainRenderNode)->Draw(cmd, globalStateData);
    }
    
}

void VansGraphics::VansScene::DrawVegetationNode()
{
    if (m_VegetationRenderNode == nullptr)
    {
        return;
    }
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    m_VegetationRenderNode->Draw(cmd, globalStateData);
}

// ===========================================================================
// RecordVegetationCompute — dispatches bone simulation + skinning compute
// passes for the vegetation system. Called once per frame BEFORE the deferred
// render pass so that skinned vertex data is ready for the GBuffer draw.
// ===========================================================================
void VansGraphics::VansScene::RecordVegetationCompute(VansVKCommandBuffer& cmd)
{
    if (m_VegetationSystem == nullptr)
    {
        return;
    }

    float deltaTime = static_cast<float>(VansTimer::GetLastFrameDelta());
    float time      = static_cast<float>(VansTimer::GetFrameTime());

    // Camera position is read directly in the shader via the global CameraData UBO (set=0)
    // All simulation params are stored on the system (loaded from scene JSON via SetSimParams).
    m_VegetationSystem->Update(cmd, deltaTime, time,
        m_VegetationSystem->GetWindDirection(),
        m_VegetationSystem->GetWindStrength(),
        m_VegetationSystem->GetWindFrequency(),
        m_VegetationSystem->GetWindSpeed(),
        m_VegetationSystem->GetWindBendMult(),
        m_VegetationSystem->GetStiffness(),
        m_VegetationSystem->GetDamping(),
        m_VegetationSystem->GetSoftness(),
        m_VegetationSystem->GetLodFullDist(),
        m_VegetationSystem->GetLodFadeDist());

    // P0: GPU 视锥 + 距离剔除 — 写入每实例的可见性标志，绘制时 VS 早退出不可见实例
    m_VegetationSystem->DispatchCullPass(cmd, m_VegetationSystem->GetCullDistance());
    }

void VansGraphics::VansScene::DrawTransParentNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    for (auto& node : m_TransParentRenderNodes)
    {
        if (node == nullptr)
        {
            continue;
        }
        node->Draw(cmd, globalStateData);
    }
}

void VansGraphics::VansScene::DrawPostProcessNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    for (auto& node  : m_PostProcessRenderNodes)
    {
        //apply mesh
        node->Draw(cmd, globalStateData);
    }
}

//ssao
//ssr
//contact shadow
void VansGraphics::VansScene::DrawScreenSpaceFeatureNode()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    for (auto& node : m_ScreenSpaceRenderNodes)
    {
        //apply mesh
        node->Draw(cmd, globalStateData);
    }
}

void VansGraphics::VansScene::DeferredShading()
{
    if (m_DeferredNode == nullptr)
    {
        return;
    }
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();

    m_DeferredNode->Draw(cmd, globalStateData);
}
