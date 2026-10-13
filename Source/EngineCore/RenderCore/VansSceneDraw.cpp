#include "VansScene.h"

#include "VansMainCameraVisibility.h"
#include "VulkanCore/VansMesh.h"
#include "VulkanCore/VansVKDevice.h"
#include "TerrainCore/VansTerrain.h"
#include "WaterCore/VansWaterSystem.h"
#include "VegetationCore/VansVegetationSystem.h"
#include "VegetationCore/VansVegetationCollection.h"
#include "Particles/VansParticleRenderSystem.h"
#include "../Util/VansLog.h"
#include "../Util/VansProfiler.h"
#include "VulkanCore/VansRenderPass.h"
#include "../RuntimeCore/VansFramePhase.h"
#include <algorithm>
#include <cmath>
#include <cstring>

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

VansGraphics::VansCascadeShadowDrawPlan
VansGraphics::VansScene::BuildCascadeShadowDrawPlan() const
{
    VANS_PROFILE_SCOPE("Shadow::SelectCascadeCandidates", Vans::ProfileCategory::RenderPrepare);
    VansCascadeShadowDrawPlan plan;
    const auto* vkDevice = dynamic_cast<const VansVKDevice*>(m_GraphicsDevice);
    if (vkDevice == nullptr)
        return plan;

    const auto& directionalLights =
        vkDevice->GetCurrentRenderSceneSnapshot().light.directionalLights;
    const auto appendNode = [&](VansRenderNode* node, std::uint64_t order)
    {
        const VansShadowDrawCandidate candidate{ node, order };
        plan.withoutCascade.push_back(candidate);
        const VansRenderProxyHandle proxy = FindMainRenderProxyHandle(node);
        const auto* caster = vkDevice->FindCurrentShadowCaster(proxy);
        for (std::size_t cascade = 0; cascade < plan.cascades.size(); ++cascade)
        {
            if (directionalLights.empty() || caster == nullptr || !caster->hasBounds ||
                RenderBoundsIntersectsClipFrustum(
                    caster->bounds, directionalLights[0].m_ShadowMatrix[cascade]))
            {
                plan.cascades[cascade].push_back(candidate);
            }
        }
    };
    std::uint64_t stableOrder = 0;
    for (VansRenderNode* node : m_OpaqueRenderNodes)
        appendNode(node, stableOrder++);
    for (VansRenderNode* node : m_HairRenderNodes)
        appendNode(node, stableOrder++);
    return plan;
}

void VansGraphics::VansScene::DrawShadowNodes(const VansCascadeShadowDrawPlan& plan)
{
    VANS_ASSERT_FRAME_PHASE(VansFramePhase::GPURecord);

    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    DrawShadowNodes(vkDevice->GetCommandBuffer(), vkDevice->GetGlobalRenderStateData(), plan);
}

void VansGraphics::VansScene::DrawShadowNodes(
    VansVKCommandBuffer& cmd, GlobalStateData globalStateData,
    const VansCascadeShadowDrawPlan& plan)
{
    VansDrawSubmissionList& submission = m_ShadowDrawSubmissionScratch;
    if (BuildShadowDrawSubmission(globalStateData, submission, plan))
    {
        VANS_PROFILE_SCOPE("Shadow::RecordBatches", Vans::ProfileCategory::CommandRecord);
        VansDrawSubmission::Record(cmd, submission, 0, submission.batches.size());
    }
    DrawVegetationShadowNode(cmd, globalStateData);
}

bool VansGraphics::VansScene::BuildShadowDrawSubmission(
    GlobalStateData globalStateData,
    VansDrawSubmissionList& submission,
    const VansCascadeShadowDrawPlan& plan)
{
    VANS_PROFILE_SCOPE("Shadow::BuildSubmission", Vans::ProfileCategory::CommandRecord);
    VANS_ASSERT_FRAME_PHASE(VansFramePhase::GPURecord);

    submission.Clear();
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    if (vkDevice == nullptr)
        return false;

    const auto& candidates = globalStateData.cascadeIndex >= 0 &&
        globalStateData.cascadeIndex < static_cast<int>(plan.cascades.size())
        ? plan.cascades[globalStateData.cascadeIndex]
        : plan.withoutCascade;
    const auto appendCaster = [&](VansRenderNode* node, std::uint64_t order)
    {
        if (!IsRenderNodeEnabledForCurrentFrame(node)) return;
        auto* opaque = static_cast<VansCommonRenderNode*>(node);
        if (!opaque->m_SupportShadow) return;
        if (!node->m_Material)
		{
			VANS_LOG_ERROR("[VansScene] Skipping cascade shadow for node '" << node->m_NodeName
				<< "': material is not resolved.");
			return;
		}
        VansGraphicsShader* shadowShader = node->m_Material->GetPassShader(VansPass::SHADOW);
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

    for (const VansShadowDrawCandidate& candidate : candidates)
        appendCaster(candidate.node, candidate.stableOrder);

    VANS_PROFILE_SCOPE("Shadow::FinalizeSubmission", Vans::ProfileCategory::CommandRecord);
    return FinalizeDrawSubmission(VansDrawSortPolicy::State, submission);
}

void VansGraphics::VansScene::DrawVegetationShadowNode(VansVKCommandBuffer& cmd, GlobalStateData globalStateData)
{
    if (IsRenderNodeEnabledForCurrentFrame(m_VegetationRenderNode))
        static_cast<VansVegetationRenderNode*>(m_VegetationRenderNode)->DrawShadow(cmd, globalStateData);
}

VansGraphics::VansScene::PunctualShadowNodeLookup
VansGraphics::VansScene::BuildPunctualShadowNodeLookup() const
{
    VANS_PROFILE_SCOPE("Shadow::BuildNodeLookup", Vans::ProfileCategory::RenderPrepare);
    PunctualShadowNodeLookup lookup;
    lookup.reserve(m_OpaqueRenderNodes.size() + m_HairRenderNodes.size());
    std::uint64_t stableOrder = 0;
    const auto appendNode = [&](VansRenderNode* node)
    {
        const std::uint64_t order = stableOrder++;
        const VansRenderProxyHandle handle = FindMainRenderProxyHandle(node);
        if (handle.IsValid())
            lookup.emplace(handle, VansShadowDrawCandidate{ node, order });
    };
    for (VansRenderNode* node : m_OpaqueRenderNodes)
        appendNode(node);
    for (VansRenderNode* node : m_HairRenderNodes)
        appendNode(node);
    return lookup;
}

VansGraphics::VansPunctualShadowDrawPlan
VansGraphics::VansScene::BuildPunctualShadowDrawPlan(
    const std::vector<VansPunctualShadowRenderJob>& jobs) const
{
    VANS_PROFILE_SCOPE("Shadow::ResolveCandidates", Vans::ProfileCategory::RenderPrepare);
    VansPunctualShadowDrawPlan plan(jobs.size());
    if (jobs.empty())
        return plan;

    const PunctualShadowNodeLookup nodeLookup = BuildPunctualShadowNodeLookup();
    for (std::size_t jobIndex = 0; jobIndex < jobs.size(); ++jobIndex)
    {
        const VansPunctualShadowRenderJob& job = jobs[jobIndex];
        if (job.shadowViewIndex == VANS_INVALID_SHADOW_INDEX ||
            job.shadowViewIndex >= VANS_MAX_PUNCTUAL_SHADOW_VIEWS)
            continue;

        auto& selectedNodes = plan[jobIndex];
        selectedNodes.reserve(job.casterHandles.size());
        for (const VansRenderProxyHandle& handle : job.casterHandles)
        {
            const auto range = nodeLookup.equal_range(handle);
            for (auto it = range.first; it != range.second; ++it)
                selectedNodes.push_back(it->second);
        }
        std::sort(selectedNodes.begin(), selectedNodes.end(),
            [](const VansShadowDrawCandidate& a,
                const VansShadowDrawCandidate& b)
            {
                return a.stableOrder < b.stableOrder;
            });
    }
    return plan;
}

void VansGraphics::VansScene::DrawPunctualShadowJob(
    const VansPunctualShadowRenderJob& job,
    const std::vector<VansShadowDrawCandidate>& selectedNodes)
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

    VansDrawSubmissionList& submission = m_ShadowDrawSubmissionScratch;
    submission.Clear();
    submission.packets.reserve(selectedNodes.size());
    const auto appendCaster = [&](VansRenderNode* node, std::uint64_t order)
    {
        if (!IsRenderNodeEnabledForCurrentFrame(node) || node->m_Material == nullptr)
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

    {
        VANS_PROFILE_SCOPE("Shadow::BuildPackets", Vans::ProfileCategory::CommandRecord);
        for (const VansShadowDrawCandidate& selected : selectedNodes)
            appendCaster(selected.node, selected.stableOrder);
    }

    bool finalized = false;
    {
        VANS_PROFILE_SCOPE("Shadow::FinalizeSubmission", Vans::ProfileCategory::CommandRecord);
        finalized = FinalizeDrawSubmission(VansDrawSortPolicy::State, submission);
    }
    if (finalized)
    {
        VANS_PROFILE_SCOPE("Shadow::RecordBatches", Vans::ProfileCategory::CommandRecord);
        VansDrawSubmission::Record(cmd, submission, 0, submission.batches.size());
    }

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
            static_cast<int>(node->m_GroundWeatherEffects), 0, nodeIndex, depth, packet))
        {
            submission.packets.push_back(std::move(packet));
        }
    }

    return FinalizeDrawSubmission(VansDrawSortPolicy::StateThenFrontToBack, submission);
}

void VansGraphics::VansScene::DrawTerrainNode(bool shadowPass)
{
    if(m_TerrainRenderNode== nullptr) return;
    auto* vkDevice=static_cast<VansVKDevice*>(m_GraphicsDevice);
    DrawTerrainNode(vkDevice->GetCommandBuffer(),vkDevice->GetGlobalRenderStateData(),shadowPass);
}

std::vector<VansGraphics::VansSubmitResourceAccess> VansGraphics::VansScene::TerrainInteractionReadResources() const
{
    if (!IsRenderNodeEnabledForCurrentFrame(m_TerrainRenderNode)) return {};
    const auto* terrain=static_cast<const VansTerrainRenderNode*>(m_TerrainRenderNode)->GetTerrain();
    return terrain?terrain->InteractionReadResources():std::vector<VansSubmitResourceAccess>{};
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
// 在原始不透明光照 pass 内调用，输出不包含任何介质合成。
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
    if (!m_VegetationCollection) return;
    const auto* device = dynamic_cast<const VansVKDevice*>(m_GraphicsDevice);
    if (!device) return;
    const auto& timing = device->GetCurrentRenderTimingSnapshot();
	const uint32_t frameSlot = device->GetFrameResourceSlot();
    const bool sameQueue = !device->IsAsyncComputeEnabled();
    const auto lane=sameQueue ? Vans::VansGpuQueueLane::Graphics : Vans::VansGpuQueueLane::Compute;
	struct Work { VansVegetationSystem* system; bool grassCullReady=false; bool treeCullReady=false; };
    std::vector<Work> work;
    work.reserve(m_VegetationCollection->BatchCount());
    m_VegetationCollection->ForEachViewRelevant([&](VansVegetationSystem& value)
    {
		work.push_back({&value,false,false});
    });
    bool stateTransferred=false;
    for (auto& item:work)
        if (item.system->HasGrassStateTransfer()) {
            item.system->RecordGrassStateTransfer(cmd);
            stateTransferred=true;
        }
    if (stateTransferred) {
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,{barrier});
    }
    // 按阶段记录固定数量的 GPU 时间戳，避免每区块采样耗尽 query pool。
    {
        VANS_PROFILE_SCOPE("Vegetation::GrassCull", Vans::ProfileCategory::CommandRecord);
        VANS_GPU_SCOPE_LANE(cmd.GetVKCommandBuffer(), "Vegetation Grass Cull", lane);
		bool hasGrassCull = false;
        for (auto& item:work)
		{
			item.grassCullReady = item.system->PrepareGrassCull(cmd);
			hasGrassCull |= item.grassCullReady;
		}
		if (hasGrassCull)
		{
			VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, { barrier });
			for (auto& item : work)
				if (item.grassCullReady)
					item.system->DispatchGrassCull(cmd, item.system->GetCullDistance(), frameSlot);
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
			cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
					(sameQueue ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT : 0u), { barrier });
			for (auto& item : work)
				if (item.grassCullReady)
					item.system->CopyGrassCullCounts(cmd);
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
			cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, { barrier });
		}
    }
    {
        VANS_PROFILE_SCOPE("Vegetation::TreeCull", Vans::ProfileCategory::CommandRecord);
        VANS_GPU_SCOPE_LANE(cmd.GetVKCommandBuffer(), "Vegetation Tree Cull", lane);
		bool hasTreeCull = false;
		for (auto& item : work)
		{
			item.treeCullReady = item.system->PrepareTreeCull(cmd, frameSlot);
			hasTreeCull |= item.treeCullReady;
		}
		if (hasTreeCull)
		{
			const bool shadowViewValid =
				!device->GetCurrentRenderSceneSnapshot().light.directionalLights.empty();
			VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, { barrier });
			for (auto& item : work)
				if (item.treeCullReady)
					item.system->DispatchTreeCull(cmd, shadowViewValid, frameSlot);
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT |
				(sameQueue ? VK_ACCESS_SHADER_READ_BIT : 0u);
			cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT |
					(sameQueue ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT : 0u), { barrier });
			for (auto& item : work)
				if (item.treeCullReady)
					item.system->CopyTreeCullCounts(cmd, frameSlot);
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
			cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, { barrier });
		}
    }
    {
        VANS_PROFILE_SCOPE("Vegetation::GrassSimulation", Vans::ProfileCategory::CommandRecord);
        VANS_GPU_SCOPE_LANE(cmd.GetVKCommandBuffer(), "Vegetation Grass Simulation", lane);
        for (const auto& item:work) {
        auto* system=item.system;
        system->Update(cmd, static_cast<float>(timing.deltaSeconds), static_cast<float>(timing.elapsedSeconds),
            system->GetWindDirection(), system->GetWindStrength(), system->GetWindFrequency(),
            system->GetWindSpeed(), system->GetWindBendMult(), system->GetStiffness(),
            system->GetDamping(), system->GetSoftness(), system->GetLodFullDist(),
            // 投影草必须更新主相机视野外的骨骼，阴影不能读取过期或未初始化的形变。
			 system->GetLodFadeDist(), item.grassCullReady && !system->CastsShadows(), sameQueue, frameSlot);
        }
    }
}

void VansGraphics::VansScene::DrawTransParentNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer& cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    const glm::mat4 viewMatrix = vkDevice->GetCurrentRenderViewSnapshot().view;

    struct TransparentItem
    {
        VansRenderNode* node = nullptr;
        const VansParticleDrawItem* particle = nullptr;
        float depth = 0;
    };
    std::vector<TransparentItem> sorted;
    sorted.reserve(m_TransParentRenderNodes.size()+m_ParticleRenderSystem.DrawItems().size());
    for (auto* node : m_TransParentRenderNodes)
    {
        if (!IsRenderNodeEnabledForCurrentFrame(node) || !ShouldDrawMainCameraNode(node)) continue;
        const auto* transform = FindRenderNodeTransformForCurrentFrame(node);
        const glm::vec3 center = transform ? glm::vec3(transform->position) : glm::vec3(0);
        sorted.push_back({node,nullptr,-(viewMatrix*glm::vec4(center,1)).z});
    }
    for (const auto& particle : m_ParticleRenderSystem.DrawItems())
        sorted.push_back({nullptr,&particle,-(viewMatrix*glm::vec4(particle.center,1)).z});
    std::stable_sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b) { return a.depth > b.depth; });

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
    for (const auto& item : sorted)
    {
        if (item.particle)
        {
            flushPacketRun();
            item.particle->material->Draw(cmd,globalStateData,m_ParticleRenderSystem.UploadBuffer(),*item.particle);
            continue;
        }
        auto* node = item.node;
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

void VansGraphics::VansScene::DrawHairNodes(const char* passName)
{
	VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
	VansVKCommandBuffer& cmd = vkDevice->GetCommandBuffer();
	GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
	VkDescriptorSetLayout passLayout = vkDevice ? vkDevice->GetHairPassLayout() : VK_NULL_HANDLE;
	VkDescriptorSet passSet = vkDevice ? vkDevice->GetHairPassDescriptorSet() : VK_NULL_HANDLE;
	VansDrawSubmissionList submission;
	std::uint64_t stableOrder = 0;
	for (auto& node : m_HairRenderNodes)
	{
		if (!IsRenderNodeEnabledForCurrentFrame(node))
			continue;
		if (!ShouldDrawMainCameraNode(node))
			continue;
		if (passLayout != VK_NULL_HANDLE && passSet != VK_NULL_HANDLE)
		{
			node->OverridePassDescriptorSet(1, passLayout, passSet);
		}
		VansDrawPacket packet;
		if (node->BuildPrimaryDrawPacket(
			vkDevice->GetLogicDevice(), globalStateData, passName,
			0, 0, stableOrder++, 0.0f, packet))
		{
			submission.packets.push_back(std::move(packet));
		}
	}
	if (FinalizeDrawSubmission(VansDrawSortPolicy::State, submission))
		VansDrawSubmission::Record(cmd, submission, 0, submission.batches.size());
}

void VansGraphics::VansScene::DrawForwardOpaquePreAtmosphereNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer& cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    VansDrawSubmissionList submission;
    const glm::mat4 viewMatrix = vkDevice->GetCurrentRenderViewSnapshot().view;
    std::uint64_t stableOrder = 0;
    for (auto& node : m_ForwardOpaquePreAtmosphereRenderNodes)
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
            vkDevice->GetLogicDevice(), globalStateData, VansPass::FORWARD_OPAQUE_PRE_ATMOSPHERE,
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
            vkDevice->GetLogicDevice(), globalStateData,
            node->m_UsesRoadDecalPass ? VansPass::ROAD_DECAL_MODIFIER :
                node->m_UsesSplineDecalPass ? VansPass::SPLINE_DECAL_MODIFIER : VansPass::DECAL_MODIFIER,
            0, 0, nodeIndex, 0.0f, packet))
        {
            submission.packets.push_back(std::move(packet));
        }
    }

    const auto& payloadBytes = vkDevice->GetCurrentRenderSceneSnapshot().materials.custom.bytes;
    auto priority = [&](const VansDrawPacket& packet) -> int {
        const auto* node = m_DecalRenderNodes[packet.stableOrder];
        if (node->m_DecalSortPriority) return *node->m_DecalSortPriority;
        // 道路使用 PBR payload，不能把同索引的普通贴花 custom payload 当成排序参数。
        if (node->m_UsesRoadDecalPass) return 0;
        const size_t offset = static_cast<size_t>(packet.instanceData.materialIndex) * sizeof(VansCustomMaterialPayload);
        VansCustomMaterialPayload payload;
        if (offset <= payloadBytes.size() && sizeof(payload) <= payloadBytes.size() - offset)
        {
            std::memcpy(&payload, payloadBytes.data() + offset, sizeof(payload));
            if (std::isfinite(payload.values[2].x))
                return static_cast<int>(std::round(std::clamp(payload.values[2].x, -32768.0f, 32767.0f)));
        }
        return 0;
    };
    // 不同层级不能合成同一个实例批次，否则混合顺序失去逐层保证。
    for (auto& packet : submission.packets)
        packet.orderGroup = static_cast<std::uint64_t>(priority(packet) + 32768);
    std::sort(submission.packets.begin(), submission.packets.end(),
        [](const VansDrawPacket& lhs, const VansDrawPacket& rhs) {
            return lhs.orderGroup != rhs.orderGroup ? lhs.orderGroup < rhs.orderGroup : lhs.stableOrder < rhs.stableOrder;
        });
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
