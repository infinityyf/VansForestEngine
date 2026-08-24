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
#include "../RuntimeCore/VansFramePhase.h"
#include <algorithm>
#include <cmath>

// ===========================================================================
// Draw commands — one per render pass type
// ===========================================================================

bool VansGraphics::VansScene::FinalizeDrawSubmission(
	VansDrawSortPolicy sortPolicy,
	VansDrawSubmissionList& submission)
{
	auto* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
	return vkDevice != nullptr &&
		VansDrawSubmission::Finalize(vkDevice->GetDrawInstanceArena(), sortPolicy, submission);
}

void VansGraphics::VansScene::DrawShadowNodes()
{
    VANS_ASSERT_FRAME_PHASE(VansFramePhase::GPURecord);

    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    DrawShadowNodes(vkDevice->GetCommandBuffer(), vkDevice->GetGlobalRenderStateData());
}

void VansGraphics::VansScene::DrawShadowNodes(VansVKCommandBuffer& cmd, GlobalStateData globalStateData)
{
    VansDrawSubmissionList submission;
    if (BuildShadowDrawSubmission(globalStateData, submission))
        VansDrawSubmission::Record(cmd, submission, 0, submission.batches.size());
    DrawVegetationShadowNode(cmd, globalStateData);
}

bool VansGraphics::VansScene::BuildShadowDrawSubmission(
    GlobalStateData globalStateData,
    VansDrawSubmissionList& submission)
{
    VANS_ASSERT_FRAME_PHASE(VansFramePhase::GPURecord);

    submission.Clear();
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    if (vkDevice == nullptr)
        return false;

    const VansRenderSceneFrameSnapshot& frameScene =
        vkDevice->GetCurrentRenderSceneSnapshot();
    const glm::mat4* cascadeWorldToClip = nullptr;
    const auto& directionLights = frameScene.light.directionalLights;
    if (!directionLights.empty() &&
        globalStateData.cascadeIndex >= 0 &&
        globalStateData.cascadeIndex < 4)
    {
        cascadeWorldToClip = &directionLights[0].m_ShadowMatrix[globalStateData.cascadeIndex];
    }

    std::uint64_t stableOrder = 0;
    const auto appendCaster = [&](VansRenderNode* node, bool hairNode, std::uint64_t order)
    {
        if (!IsRenderNodeEnabledForCurrentFrame(node)) return;
        auto* opaque = static_cast<VansCommonRenderNode*>(node);
        if (!opaque->m_SupportShadow) return;
        if (cascadeWorldToClip != nullptr)
        {
            const VansRenderProxyHandle proxy = FindMainRenderProxyHandle(node);
            const auto casterIt = std::find_if(
                frameScene.punctualShadow.casters.begin(),
                frameScene.punctualShadow.casters.end(),
                [proxy](const VansRenderPunctualShadowCasterInput& caster)
                {
                    return caster.proxy == proxy;
                });
            if (casterIt != frameScene.punctualShadow.casters.end() &&
                casterIt->hasBounds &&
                !RenderBoundsIntersectsClipFrustum(casterIt->bounds, *cascadeWorldToClip))
            {
                return;
            }
        }
        if (!node->m_Material)
		{
			VANS_LOG_ERROR("[VansScene] Skipping cascade shadow for node '" << node->m_NodeName
				<< "': material is not resolved.");
			return;
		}
        VansGraphicsShader* shadowShader = node->m_Material->GetPassShader(VansPass::SHADOW);
        if (shadowShader == nullptr && hairNode)
            shadowShader = node->m_Material->GetPassShader(VansPass::HAIR_SHADOW);
        if (shadowShader == nullptr) return;

        VansDrawPacket packet;
        if (node->BuildPassDrawPacket(
            vkDevice->GetLogicDevice(),
            globalStateData,
            VansPass::SHADOW,
            shadowShader,
            opaque->m_ShadowDescSets,
            opaque->m_ShadowDescSetLayouts,
            globalStateData.cascadeIndex,
            0,
            order,
            0.0f,
            packet))
        {
            submission.packets.push_back(std::move(packet));
        }
    };

    for (VansRenderNode* node : m_OpaqueRenderNodes)
        appendCaster(node, false, stableOrder++);
    for (VansRenderNode* node : m_HairRenderNodes)
        appendCaster(node, true, stableOrder++);

    return FinalizeDrawSubmission(VansDrawSortPolicy::State, submission);
}

void VansGraphics::VansScene::DrawVegetationShadowNode(VansVKCommandBuffer& cmd, GlobalStateData globalStateData)
{
    if (IsRenderNodeEnabledForCurrentFrame(m_VegetationRenderNode))
        static_cast<VansVegetationRenderNode*>(m_VegetationRenderNode)->DrawShadow(cmd, globalStateData);
}

void VansGraphics::VansScene::DrawSkyMotionVectorNode(
	VansVKCommandBuffer& cmd,
	GlobalStateData globalStateData)
{
	if (!IsRenderNodeEnabledForCurrentFrame(m_SkyBoxNode))
		return;
	static_cast<VansSkyBoxRenderNode*>(m_SkyBoxNode)->DrawSkyMotionVector(cmd, globalStateData);
}

void VansGraphics::VansScene::DrawPunctualShadowJob(const VansPunctualShadowRenderJob& job)
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
	if (vkDevice == nullptr)
		return;
    VansVKCommandBuffer& cmd = vkDevice->GetCommandBuffer();
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

	if (job.shadowViewIndex == VANS_INVALID_SHADOW_INDEX ||
		job.shadowViewIndex >= VANS_MAX_PUNCTUAL_SHADOW_VIEWS)
		return;
	const int shaderViewIndex = static_cast<int>(job.shadowViewIndex);

	const auto isSelectedCaster = [&](const VansRenderNode* node)
	{
		const VansRenderProxyHandle casterHandle =
			FindMainRenderProxyHandle(node);
		return casterHandle.IsValid() &&
			std::find(
				job.casterHandles.begin(),
				job.casterHandles.end(),
				casterHandle) != job.casterHandles.end();
	};

    VansDrawSubmissionList submission;
    std::uint64_t stableOrder = 0;
    const auto appendCaster = [&](VansRenderNode* node)
    {
        const std::uint64_t order = stableOrder++;
        if (!IsRenderNodeEnabledForCurrentFrame(node) || !isSelectedCaster(node) || node->m_Material == nullptr)
            return;
        auto* commonNode = static_cast<VansCommonRenderNode*>(node);
        if (!commonNode->m_SupportShadow)
            return;
        VansGraphicsShader* shader = node->m_Material->GetPassShader(VansPass::PUNCTUAL_SHADOW);
        if (shader == nullptr)
            return;
        VansDrawPacket packet;
        if (node->BuildPassDrawPacket(
            vkDevice->GetLogicDevice(), globalStateData, VansPass::PUNCTUAL_SHADOW, shader,
            commonNode->m_ShadowDescSets, commonNode->m_ShadowDescSetLayouts,
            shaderViewIndex, 0, order, 0.0f, packet))
        {
            submission.packets.push_back(std::move(packet));
        }
    };

    for (VansRenderNode* node : m_OpaqueRenderNodes)
        appendCaster(node);
    for (VansRenderNode* node : m_HairRenderNodes)
        appendCaster(node);

    if (FinalizeDrawSubmission(VansDrawSortPolicy::State, submission))
        VansDrawSubmission::Record(cmd, submission, 0, submission.batches.size());

    if (IsRenderNodeEnabledForCurrentFrame(m_VegetationRenderNode))
        static_cast<VansVegetationRenderNode*>(m_VegetationRenderNode)->DrawPunctualShadow(
			cmd, globalStateData, shaderViewIndex);
}

void VansGraphics::VansScene::DrawSkyBoxNode()
{
    if (m_SkyBoxNode == nullptr)
    {
        return;
    }
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer& cmd = vkDevice->GetCommandBuffer();
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
    VansDrawSubmissionList& submission = m_OpaqueDrawSubmissionScratch;
    if (BuildOpaqueDrawSubmission(globalStateData, submission))
        VansDrawSubmission::Record(cmd, submission, 0, submission.batches.size());
}

bool VansGraphics::VansScene::BuildOpaqueDrawSubmission(
    GlobalStateData globalStateData,
    VansDrawSubmissionList& submission)
{
    VANS_ASSERT_FRAME_PHASE(VansFramePhase::GPURecord);

    submission.Clear();
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    if (vkDevice == nullptr)
        return false;

    submission.packets.reserve(m_OpaqueRenderNodes.size());

    const glm::mat4 viewMatrix = vkDevice->GetCurrentRenderViewSnapshot().view;
    for (size_t nodeIndex = 0; nodeIndex < m_OpaqueRenderNodes.size(); ++nodeIndex)
    {
        auto& node = m_OpaqueRenderNodes[nodeIndex];
        if (!IsRenderNodeEnabledForCurrentFrame(node))
        {
            continue;
        }
        if (!ShouldDrawMainCameraNode(node))
        {
            continue;
        }
        const VansRenderTransformFrameData* transform =
            FindRenderNodeTransformForCurrentFrame(node);
        const float depth = transform != nullptr
            ? -(viewMatrix * glm::vec4(glm::vec3(transform->position), 1.0f)).z
            : 0.0f;
        VansDrawPacket packet;
        if (node->BuildPrimaryDrawPacket(
            vkDevice->GetLogicDevice(), globalStateData, VansPass::GBUFFER,
            0, 0, nodeIndex, depth, packet))
        {
            submission.packets.push_back(std::move(packet));
        }
    }

    return FinalizeDrawSubmission(VansDrawSortPolicy::StateThenFrontToBack, submission);
}

void VansGraphics::VansScene::DrawTerrainNode(bool shadowPass)
{
    if(m_TerrainRenderNode== nullptr)
    {
        return;
	}
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    DrawTerrainNode(vkDevice->GetCommandBuffer(), vkDevice->GetGlobalRenderStateData(), shadowPass);
}

void VansGraphics::VansScene::DrawTerrainNode(VansVKCommandBuffer& cmd, GlobalStateData globalStateData, bool shadowPass)
{
    if (m_TerrainRenderNode == nullptr)
    {
        return;
    }
    if (shadowPass)
    {
        static_cast<VansTerrainRenderNode*>(m_TerrainRenderNode)->DrawShadow(cmd, globalStateData);
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
    VansVKCommandBuffer& cmd = vkDevice->GetCommandBuffer();
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
    VansVKCommandBuffer& cmd = vkDevice->GetCommandBuffer();
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
    VansVKCommandBuffer& cmd = vkDevice->GetCommandBuffer();
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

	const VansVKDevice* vkDevice = dynamic_cast<const VansVKDevice*>(m_GraphicsDevice);
	const VansRenderFrameTimingSnapshot& frameTiming =
		vkDevice->GetCurrentRenderTimingSnapshot();
	const float deltaTime = static_cast<float>(frameTiming.deltaSeconds);
	const float time = static_cast<float>(frameTiming.elapsedSeconds);
	const bool sameQueueGraphicsConsumer = vkDevice == nullptr || !vkDevice->IsAsyncComputeEnabled();

	// 先生成本帧可见性，再让 Grass 模拟跳过不可见实例，避免 cull 前模拟全量草实例。
	const bool grassCullReady = m_VegetationSystem->DispatchCullPass(
		cmd, m_VegetationSystem->GetCullDistance(), sameQueueGraphicsConsumer);
	m_VegetationSystem->DispatchTreeCullPass(cmd, sameQueueGraphicsConsumer);

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
		grassCullReady,
		sameQueueGraphicsConsumer);

    }

void VansGraphics::VansScene::DrawTransParentNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer& cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    const glm::mat4 viewMatrix = vkDevice->GetCurrentRenderViewSnapshot().view;

    std::vector<VansRenderNode*> sortedNodes;
    sortedNodes.reserve(m_TransParentRenderNodes.size() + m_ParticleRenderNodes.size());
    for (auto* node : m_TransParentRenderNodes)
    {
        if (IsRenderNodeEnabledForCurrentFrame(node))
        {
            sortedNodes.push_back(node);
        }
    }
    for (auto* particleNode : m_ParticleRenderNodes)
    {
        if (IsRenderNodeEnabledForCurrentFrame(particleNode))
            sortedNodes.push_back(particleNode);
    }

    std::stable_sort(sortedNodes.begin(), sortedNodes.end(),
        [this, viewMatrix](const VansRenderNode* a, const VansRenderNode* b)
        {
            const VansRenderTransformFrameData* transformA =
                FindRenderNodeTransformForCurrentFrame(a);
            const VansRenderTransformFrameData* transformB =
                FindRenderNodeTransformForCurrentFrame(b);
            const glm::vec3 pa = transformA != nullptr
                ? glm::vec3(transformA->position)
                : (a->GetNodeType() == PARTICLE_NODE
                    ? static_cast<const VansParticleRenderNode*>(a)->GetSortCenterWS()
                    : glm::vec3(0.0f));
            const glm::vec3 pb = transformB != nullptr
                ? glm::vec3(transformB->position)
                : (b->GetNodeType() == PARTICLE_NODE
                    ? static_cast<const VansParticleRenderNode*>(b)->GetSortCenterWS()
                    : glm::vec3(0.0f));
            const float da = -(viewMatrix * glm::vec4(pa, 1.0f)).z;
            const float db = -(viewMatrix * glm::vec4(pb, 1.0f)).z;
            return da > db;
        });

    VansDrawSubmissionList submission;
    const auto flushPacketRun = [&]()
    {
        if (submission.packets.empty())
            return;
        if (FinalizeDrawSubmission(VansDrawSortPolicy::PreserveOrder, submission))
            VansDrawSubmission::Record(cmd, submission, 0, submission.batches.size());
        submission.Clear();
    };

    std::uint64_t stableOrder = 0;
    for (auto* node : sortedNodes)
    {
        if (!ShouldDrawMainCameraNode(node))
            continue;
        if (node->GetNodeType() == PARTICLE_NODE)
        {
            flushPacketRun();
            node->Draw(cmd, globalStateData);
            continue;
        }

        VansDrawPacket packet;
        if (node->BuildPrimaryDrawPacket(
            vkDevice->GetLogicDevice(), globalStateData, VansPass::FORWARD_TRANSPARENT,
            0, 0, stableOrder++, 0.0f, packet))
        {
            submission.packets.push_back(std::move(packet));
        }
    }
    flushPacketRun();
}

void VansGraphics::VansScene::DrawHairVisibilityNodes()
{
	VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
	VansVKCommandBuffer& cmd = vkDevice->GetCommandBuffer();
	GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
	VkDescriptorSetLayout oitLayout = vkDevice ? vkDevice->GetHairOITPassLayout() : VK_NULL_HANDLE;
	VkDescriptorSet oitSet = vkDevice ? vkDevice->GetHairOITPassDescriptorSet() : VK_NULL_HANDLE;
	VansDrawSubmissionList submission;
	std::uint64_t stableOrder = 0;
	for (auto& node : m_HairRenderNodes)
	{
		if (!IsRenderNodeEnabledForCurrentFrame(node))
			continue;
		if (!ShouldDrawMainCameraNode(node))
			continue;
		if (oitLayout != VK_NULL_HANDLE && oitSet != VK_NULL_HANDLE)
		{
			node->OverridePassDescriptorSet(1, oitLayout, oitSet);
		}
		VansDrawPacket packet;
		if (node->BuildPrimaryDrawPacket(
			vkDevice->GetLogicDevice(), globalStateData, VansPass::HAIR_VISIBILITY,
			0, 0, stableOrder++, 0.0f, packet))
		{
			submission.packets.push_back(std::move(packet));
		}
	}
	if (FinalizeDrawSubmission(VansDrawSortPolicy::State, submission))
		VansDrawSubmission::Record(cmd, submission, 0, submission.batches.size());
}

void VansGraphics::VansScene::DrawHairDeepOpacityNodes(VansGraphicsShader* shader)
{
    if (!shader)
        return;

    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer& cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    globalStateData.cascadeIndex = 0;
    VansDrawSubmissionList submission;
    std::uint64_t stableOrder = 0;
    for (auto& node : m_HairRenderNodes)
    {
        if (!IsRenderNodeEnabledForCurrentFrame(node))
            continue;
        auto* hairNode = static_cast<VansCommonRenderNode*>(node);
        VansDrawPacket packet;
        if (node->BuildPassDrawPacket(
            vkDevice->GetLogicDevice(), globalStateData, "hairDeepOpacity", shader,
            hairNode->m_ShadowDescSets, hairNode->m_ShadowDescSetLayouts,
            globalStateData.cascadeIndex, 0, stableOrder++, 0.0f, packet))
        {
            submission.packets.push_back(std::move(packet));
        }
    }
    if (FinalizeDrawSubmission(VansDrawSortPolicy::State, submission))
        VansDrawSubmission::Record(cmd, submission, 0, submission.batches.size());
}

void VansGraphics::VansScene::DrawForwardOpaqueAfterDeferredNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer& cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    VansDrawSubmissionList submission;
    const glm::mat4 viewMatrix = vkDevice->GetCurrentRenderViewSnapshot().view;
    std::uint64_t stableOrder = 0;
    for (auto& node : m_ForwardOpaqueAfterDeferredRenderNodes)
    {
        if (!IsRenderNodeEnabledForCurrentFrame(node))
        {
            continue;
        }
        if (!ShouldDrawMainCameraNode(node))
        {
            continue;
        }
        const VansRenderTransformFrameData* transform =
            FindRenderNodeTransformForCurrentFrame(node);
        const float depth = transform != nullptr
            ? -(viewMatrix * glm::vec4(glm::vec3(transform->position), 1.0f)).z
            : 0.0f;
        VansDrawPacket packet;
        if (node->BuildPrimaryDrawPacket(
            vkDevice->GetLogicDevice(), globalStateData, VansPass::FORWARD_OPAQUE_AFTER_DEFERRED,
            0, 0, stableOrder++, depth, packet))
        {
            submission.packets.push_back(std::move(packet));
        }
    }
    if (FinalizeDrawSubmission(VansDrawSortPolicy::StateThenFrontToBack, submission))
        VansDrawSubmission::Record(cmd, submission, 0, submission.batches.size());
}

void VansGraphics::VansScene::DrawPostProcessNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer& cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    for (auto& node  : m_PostProcessRenderNodes)
    {
        if (!IsRenderNodeEnabledForCurrentFrame(node)) continue;
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
    VansVKCommandBuffer& cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    for (auto& node : m_ScreenSpaceRenderNodes)
    {
        if (!IsRenderNodeEnabledForCurrentFrame(node)) continue;
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
    VansDrawSubmissionList submission;
    if (BuildDecalDrawSubmission(globalStateData, submission))
        VansDrawSubmission::Record(cmd, submission, 0, submission.batches.size());
}

bool VansGraphics::VansScene::BuildDecalDrawSubmission(
    GlobalStateData globalStateData,
    VansDrawSubmissionList& submission)
{
    submission.Clear();
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    if (vkDevice == nullptr)
        return false;

    for (size_t nodeIndex = 0; nodeIndex < m_DecalRenderNodes.size(); ++nodeIndex)
    {
        auto& node = m_DecalRenderNodes[nodeIndex];
        if (!IsRenderNodeEnabledForCurrentFrame(node)) continue;
        if (!ShouldDrawMainCameraNode(node)) continue;
        VansDrawPacket packet;
        if (node->BuildPrimaryDrawPacket(
            vkDevice->GetLogicDevice(), globalStateData, VansPass::DECAL_GBUFFER,
            0, 0, nodeIndex, 0.0f, packet))
        {
            submission.packets.push_back(std::move(packet));
        }
    }

    return FinalizeDrawSubmission(VansDrawSortPolicy::PreserveOrder, submission);
}

void VansGraphics::VansScene::DeferredShading()
{
    if (m_DeferredNode == nullptr)
    {
        return;
    }
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer& cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();

    m_DeferredNode->Draw(cmd, globalStateData);
}
