#include "VansScene.h"
#include "../Configration/VansConfigration.h"

#include "VansMainCameraVisibility.h"
#include "VulkanCore/VansMesh.h"
#include "VulkanCore/VansVKDevice.h"
#include "TerrainCore/VansTerrain.h"
#include "WaterCore/VansWaterSystem.h"
#include "VegetationCore/VansVegetationSystem.h"
#include "VansParticleRenderNode.h"
#include "../Util/VansLog.h"
#include "VulkanCore/VansRenderPass.h"
#include "../VansTimer.h"
#include "../RuntimeCore/VansFramePhase.h"
#include <algorithm>
#include <cmath>

// ===========================================================================
// Draw commands — one per render pass type
// ===========================================================================

void VansGraphics::VansScene::DrawShadowNodes()
{
    VANS_ASSERT_FRAME_PHASE(VansFramePhase::GPURecord);

    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    DrawShadowNodes(vkDevice->GetCommandBuffer(), vkDevice->GetGlobalRenderStateData());
}

void VansGraphics::VansScene::DrawShadowNodes(VansVKCommandBuffer& cmd, GlobalStateData globalStateData)
{
    DrawShadowNodeRange(cmd, globalStateData, 0, m_OpaqueRenderNodes.size());
    DrawHairShadowNodes(cmd, globalStateData);
    DrawVegetationShadowNode(cmd, globalStateData);
}

void VansGraphics::VansScene::DrawShadowNodeRange(VansVKCommandBuffer& cmd, GlobalStateData globalStateData, size_t begin, size_t end)
{
    VANS_ASSERT_FRAME_PHASE(VansFramePhase::GPURecord);

    const glm::mat4* cascadeWorldToClip = nullptr;
    const auto& directionLights = m_LightManager.GetDirectionLights();
    if (!directionLights.empty() &&
        globalStateData.cascadeIndex >= 0 &&
        globalStateData.cascadeIndex < 4)
    {
        cascadeWorldToClip = &directionLights[0].m_ShadowMatrix[globalStateData.cascadeIndex];
    }

    // Iterate opaque nodes instead of dedicated shadow node list
    const size_t clampedEnd = (std::min)(end, m_OpaqueRenderNodes.size());
    for (size_t nodeIndex = begin; nodeIndex < clampedEnd; ++nodeIndex)
    {
        auto& node = m_OpaqueRenderNodes[nodeIndex];
        if (node == nullptr || !node->IsEnabled()) continue;

        // Check support_shadow flag on the node
        auto* opaque = static_cast<VansCommonRenderNode*>(node);
        if (!opaque->m_SupportShadow) continue;
		if (cascadeWorldToClip != nullptr && !IsNodeVisibleInFrustum(node, *cascadeWorldToClip)) continue;
		if (!node->m_Material)
		{
			VANS_LOG_ERROR("[VansScene] Skipping cascade shadow for node '" << node->m_NodeName
				<< "': material is not resolved.");
			continue;
		}

        // Check if the material has a shadow pass shader
        VansGraphicsShader* shadowShader = node->m_Material->GetPassShader(VansPass::SHADOW);
        if (!shadowShader) continue;

        // Draw with shadow shader using cascade shadow push constants
        node->DrawCascadeShadowWithPassShader(cmd, globalStateData, shadowShader,
                                               opaque->m_ShadowDescSets, opaque->m_ShadowDescSetLayouts);
    }
}

void VansGraphics::VansScene::DrawHairShadowNodes(VansVKCommandBuffer& cmd, GlobalStateData globalStateData)
{
    const glm::mat4* cascadeWorldToClip = nullptr;
    const auto& directionLights = m_LightManager.GetDirectionLights();
    if (!directionLights.empty() &&
        globalStateData.cascadeIndex >= 0 &&
        globalStateData.cascadeIndex < 4)
    {
        cascadeWorldToClip = &directionLights[0].m_ShadowMatrix[globalStateData.cascadeIndex];
    }

	for (auto& node : m_HairRenderNodes)
	{
		if (node == nullptr || !node->IsEnabled()) continue;
		auto* hairNode = static_cast<VansCommonRenderNode*>(node);
		if (!hairNode->m_SupportShadow) continue;
		if (cascadeWorldToClip != nullptr && !IsNodeVisibleInFrustum(node, *cascadeWorldToClip)) continue;
		if (!node->m_Material)
		{
			VANS_LOG_ERROR("[VansScene] Skipping hair shadow for node '" << node->m_NodeName
				<< "': material is not resolved.");
			continue;
		}
		VansGraphicsShader* hairShadowShader = node->m_Material->GetPassShader(VansPass::SHADOW);
		if (!hairShadowShader)
			hairShadowShader = node->m_Material->GetPassShader(VansPass::HAIR_SHADOW);
		if (!hairShadowShader) continue;
		node->DrawCascadeShadowWithPassShader(cmd, globalStateData, hairShadowShader,
			hairNode->m_ShadowDescSets, hairNode->m_ShadowDescSetLayouts);
	}
}

void VansGraphics::VansScene::DrawVegetationShadowNode(VansVKCommandBuffer& cmd, GlobalStateData globalStateData)
{
    if (m_VegetationRenderNode && m_VegetationRenderNode->IsEnabled())
        static_cast<VansVegetationRenderNode*>(m_VegetationRenderNode)->DrawShadow(cmd, globalStateData);
}

void VansGraphics::VansScene::DrawMotionVectorNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    DrawMotionVectorNodes(vkDevice->GetCommandBuffer(), vkDevice->GetGlobalRenderStateData());
}

void VansGraphics::VansScene::DrawMotionVectorNodes(VansVKCommandBuffer& cmd, GlobalStateData globalStateData)
{
    DrawMotionVectorNodeRange(cmd, globalStateData, 0, m_OpaqueRenderNodes.size());
}

void VansGraphics::VansScene::DrawMotionVectorNodeRange(VansVKCommandBuffer& cmd, GlobalStateData globalStateData, size_t begin, size_t end)
{
    const glm::mat4 viewProjection = m_Camera
        ? m_Camera->GetProjectiveMatrix() * m_Camera->GetViewMatrix()
        : glm::mat4(1.0f);

    const size_t clampedEnd = (std::min)(end, m_OpaqueRenderNodes.size());
    for (size_t nodeIndex = begin; nodeIndex < clampedEnd; ++nodeIndex)
    {
        auto& node = m_OpaqueRenderNodes[nodeIndex];
        if (node == nullptr || !node->IsEnabled()) continue;
		if (m_Camera != nullptr && !IsNodeVisibleInFrustum(node, viewProjection)) continue;
		if (!IsMainCameraNodeVisible(node)) continue;

        auto* opaque = static_cast<VansCommonRenderNode*>(node);

        // Use the velocity pass shader registered for this material
        VansGraphicsShader* mvShader = node->m_Material->GetPassShader(VansPass::VELOCITY);
        if (!mvShader) continue;

        // Reuse shadow descriptor sets (Global / EmptyPass / Object — same 3 sets)
        node->DrawCascadeShadowWithPassShader(cmd, globalStateData, mvShader,
                                               opaque->m_ShadowDescSets, opaque->m_ShadowDescSetLayouts);
    }
}

void VansGraphics::VansScene::DrawPunctualShadowJob(const VansPunctualShadowRenderJob& job)
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
	if (vkDevice == nullptr)
		return;
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();

    VkViewport viewPort = {};
	viewPort.x = static_cast<float>(job.atlasRect.x);
	viewPort.y = static_cast<float>(job.atlasRect.y);
	viewPort.width = static_cast<float>(job.atlasRect.width);
	viewPort.height = static_cast<float>(job.atlasRect.height);
    viewPort.minDepth = 0.0f;
    viewPort.maxDepth = 1.0f;

    VkRect2D scissor = {};
	scissor.offset = { static_cast<int32_t>(job.atlasRect.x), static_cast<int32_t>(job.atlasRect.y) };
	scissor.extent = { job.atlasRect.width, job.atlasRect.height };

    cmd.SetViewport(0, { viewPort });
    cmd.SetScissor(0, { scissor });

	const int pointCount = static_cast<int>((std::min)(m_LightManager.GetPointLights().size(), static_cast<size_t>(m_LightManager.GetMaxPointLightCount())));
	const int spotCount = static_cast<int>((std::min)(m_LightManager.GetSpotLight().size(), static_cast<size_t>(m_LightManager.GetMaxSpotLightCount())));
	int shaderLightIndex = static_cast<int>(job.gpuLightIndex);
	if (job.lightType == VansPunctualShadowLightType::Spot)
		shaderLightIndex += pointCount;
	else if (job.lightType == VansPunctualShadowLightType::Rect)
		shaderLightIndex += pointCount + spotCount;

	const auto isSelectedCaster = [&](const VansRenderNode* node)
	{
		const uint64_t casterId = reinterpret_cast<uint64_t>(node);
		return std::find(job.casterIds.begin(), job.casterIds.end(), casterId) != job.casterIds.end();
	};

    for (auto& node : m_OpaqueRenderNodes)
    {
        if (node == nullptr || !node->IsEnabled()) continue;
		if (!isSelectedCaster(node)) continue;

        auto* opaque = static_cast<VansCommonRenderNode*>(node);
        if (!opaque->m_SupportShadow) continue;

        VansGraphicsShader* shader = node->m_Material->GetPassShader(VansPass::PUNCTUAL_SHADOW);
        if (!shader) continue;

        node->DrawPunctualShadowWithPassShader(cmd, globalStateData, shader,
                                                opaque->m_ShadowDescSets, opaque->m_ShadowDescSetLayouts,
											shaderLightIndex, static_cast<int>(job.faceIndex));
    }

	for (auto& node : m_HairRenderNodes)
	{
		if (node == nullptr || !node->IsEnabled()) continue;
		if (!isSelectedCaster(node)) continue;
		auto* hairNode = static_cast<VansCommonRenderNode*>(node);
		if (!hairNode->m_SupportShadow) continue;
		VansGraphicsShader* shader = node->m_Material->GetPassShader(VansPass::PUNCTUAL_SHADOW);
		if (!shader) continue;
		node->DrawPunctualShadowWithPassShader(cmd, globalStateData, shader,
			hairNode->m_ShadowDescSets, hairNode->m_ShadowDescSetLayouts,
			shaderLightIndex, static_cast<int>(job.faceIndex));
	}

    if (m_VegetationRenderNode && m_VegetationRenderNode->IsEnabled())
        static_cast<VansVegetationRenderNode*>(m_VegetationRenderNode)->DrawPunctualShadow(
			cmd, globalStateData, shaderLightIndex, static_cast<int>(job.faceIndex));
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
    VANS_ASSERT_FRAME_PHASE(VansFramePhase::GPURecord);

    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    DrawOpaqueNodes(vkDevice->GetCommandBuffer(), vkDevice->GetGlobalRenderStateData());
}

void VansGraphics::VansScene::DrawOpaqueNodes(VansVKCommandBuffer& cmd, GlobalStateData globalStateData)
{
    DrawOpaqueNodeRange(cmd, globalStateData, 0, m_OpaqueRenderNodes.size());
}

void VansGraphics::VansScene::DrawOpaqueNodeRange(VansVKCommandBuffer& cmd, GlobalStateData globalStateData, size_t begin, size_t end)
{
    VANS_ASSERT_FRAME_PHASE(VansFramePhase::GPURecord);

    const glm::mat4 viewProjection = m_Camera
        ? m_Camera->GetProjectiveMatrix() * m_Camera->GetViewMatrix()
        : glm::mat4(1.0f);
    const size_t clampedEnd = (std::min)(end, m_OpaqueRenderNodes.size());
    for (size_t nodeIndex = begin; nodeIndex < clampedEnd; ++nodeIndex)
    {
        auto& node = m_OpaqueRenderNodes[nodeIndex];
        if (node == nullptr || !node->IsEnabled())
        {
            continue;
        }
        if (m_Camera != nullptr && !IsNodeVisibleInFrustum(node, viewProjection))
        {
            continue;
        }
        if (!IsMainCameraNodeVisible(node))
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
    DrawTerrainNode(vkDevice->GetCommandBuffer(), vkDevice->GetGlobalRenderStateData(), shadowPass, motionVectorPass);
}

void VansGraphics::VansScene::DrawTerrainNode(VansVKCommandBuffer& cmd, GlobalStateData globalStateData, bool shadowPass, bool motionVectorPass)
{
    if (m_TerrainRenderNode == nullptr)
    {
        return;
    }
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
    DrawVegetationNode(vkDevice->GetCommandBuffer(), vkDevice->GetGlobalRenderStateData());
}

void VansGraphics::VansScene::DrawVegetationNode(VansVKCommandBuffer& cmd, GlobalStateData globalStateData)
{
    if (m_VegetationRenderNode == nullptr)
    {
        return;
    }
    m_VegetationRenderNode->Draw(cmd, globalStateData);
}

void VansGraphics::VansScene::DrawWaterNode()
{
    if (m_WaterRenderNode == nullptr)
    {
        return;
    }
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    m_WaterRenderNode->Draw(cmd, globalStateData);
}

// ============================================================
// DrawWaterGBufferNode — 设计文档 Pass 7
// 在 m_VansWaterGBufferPass 内调用，委托给 VansWaterSystem::RenderWaterGBuffer。
// Phase 1 为 Stub；Phase 2 实现 CDLOD 网格 + water_prepass Shader。
// ============================================================
void VansGraphics::VansScene::DrawWaterGBufferNode()
{
    if (!HasWaterNodes())
    {
        return;
    }
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    m_WaterSystem->RenderWaterGBuffer(cmd, globalStateData);
}

// ============================================================
// DrawWaterCompositeNode — 设计文档 Pass 9
// 在 DeferredSkybox Pass 内调用（Deferred + SkyBox 之后，EndRenderPass 之前）。
// 读 WaterGBuf → 全屏 Fresnel 合成 → 写 SceneColor。
// ============================================================
void VansGraphics::VansScene::DrawWaterCompositeNode()
{
    if (!HasWaterNodes())
        return;
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    m_WaterSystem->RenderWaterComposite(cmd, globalStateData);
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

    // 先生成本帧可见性，再让 Grass 模拟跳过不可见实例，避免 cull 前模拟全量草实例。
    const bool grassCullReady = m_VegetationSystem->DispatchCullPass(cmd, m_VegetationSystem->GetCullDistance());
    m_VegetationSystem->DispatchTreeCullPass(cmd);

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
        m_VegetationSystem->GetLodFadeDist(),
        grassCullReady);

    }

void VansGraphics::VansScene::DrawTransParentNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    glm::mat4 viewMatrix(1.0f);
    if (m_Camera)
    {
        viewMatrix = m_Camera->GetViewMatrix();
    }

    std::vector<VansRenderNode*> sortedNodes;
    sortedNodes.reserve(m_TransParentRenderNodes.size() + m_ParticleRenderNodes.size());
    for (auto* node : m_TransParentRenderNodes)
    {
        if (node != nullptr && node->IsEnabled())
        {
            sortedNodes.push_back(node);
        }
    }
    for (auto* particleNode : m_ParticleRenderNodes)
    {
        if (particleNode != nullptr && particleNode->IsEnabled())
            sortedNodes.push_back(particleNode);
    }

    std::stable_sort(sortedNodes.begin(), sortedNodes.end(),
        [viewMatrix](const VansRenderNode* a, const VansRenderNode* b)
        {
            const glm::vec3 pa = a->GetNodeType() == PARTICLE_NODE
                ? static_cast<const VansParticleRenderNode*>(a)->GetSortCenterWS()
                : glm::vec3(a->m_ModelData.Postion);
            const glm::vec3 pb = b->GetNodeType() == PARTICLE_NODE
                ? static_cast<const VansParticleRenderNode*>(b)->GetSortCenterWS()
                : glm::vec3(b->m_ModelData.Postion);
            const float da = -(viewMatrix * glm::vec4(pa, 1.0f)).z;
            const float db = -(viewMatrix * glm::vec4(pb, 1.0f)).z;
            return da > db;
        });

    for (auto* node : sortedNodes)
    {
        if (!IsMainCameraNodeVisible(node))
            continue;
        node->Draw(cmd, globalStateData);
    }
}

void VansGraphics::VansScene::DrawHairVisibilityNodes()
{
	VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
	VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
	GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
	VkDescriptorSetLayout oitLayout = vkDevice ? vkDevice->GetHairOITPassLayout() : VK_NULL_HANDLE;
	VkDescriptorSet oitSet = vkDevice ? vkDevice->GetHairOITPassDescriptorSet() : VK_NULL_HANDLE;
	for (auto& node : m_HairRenderNodes)
	{
		if (node == nullptr || !node->IsEnabled())
			continue;
		if (!IsMainCameraNodeVisible(node))
			continue;
		if (oitLayout != VK_NULL_HANDLE && oitSet != VK_NULL_HANDLE)
		{
			node->OverridePassDescriptorSet(1, oitLayout, oitSet);
		}
		node->Draw(cmd, globalStateData);
	}
}

void VansGraphics::VansScene::DrawHairDeepOpacityNodes(VansGraphicsShader* shader)
{
    if (!shader)
        return;

    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    globalStateData.cascadeIndex = 0;
    for (auto& node : m_HairRenderNodes)
    {
        if (node == nullptr || !node->IsEnabled())
            continue;
        auto* hairNode = static_cast<VansCommonRenderNode*>(node);
        node->DrawCascadeShadowWithPassShader(cmd, globalStateData, shader,
            hairNode->m_ShadowDescSets, hairNode->m_ShadowDescSetLayouts);
    }
}

void VansGraphics::VansScene::DrawForwardOpaqueAfterDeferredNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    for (auto& node : m_ForwardOpaqueAfterDeferredRenderNodes)
    {
        if (node == nullptr || !node->IsEnabled())
        {
            continue;
        }
        if (!IsMainCameraNodeVisible(node))
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
        if (!node->IsEnabled()) continue;
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
        if (!node->IsEnabled()) continue;
        //apply mesh
        node->Draw(cmd, globalStateData);
    }
}

void VansGraphics::VansScene::DrawDecalNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    DrawDecalNodes(vkDevice->GetCommandBuffer(), vkDevice->GetGlobalRenderStateData());
}

void VansGraphics::VansScene::DrawDecalNodes(VansVKCommandBuffer& cmd, GlobalStateData globalStateData)
{
    DrawDecalNodeRange(cmd, globalStateData, 0, m_DecalRenderNodes.size());
}

void VansGraphics::VansScene::DrawDecalNodeRange(VansVKCommandBuffer& cmd, GlobalStateData globalStateData, size_t begin, size_t end)
{
    const size_t clampedEnd = (std::min)(end, m_DecalRenderNodes.size());
    for (size_t nodeIndex = begin; nodeIndex < clampedEnd; ++nodeIndex)
    {
        auto& node = m_DecalRenderNodes[nodeIndex];
        if (node == nullptr || !node->IsEnabled()) continue;
        if (!IsMainCameraNodeVisible(node)) continue;
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
