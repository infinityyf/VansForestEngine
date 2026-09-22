#include "VansScene.h"
#include "../Configration/VansConfigration.h"

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
    {
        VANS_PROFILE_SCOPE("Shadow::RecordBatches", Vans::ProfileCategory::CommandRecord);
        VansDrawSubmission::Record(cmd, submission, 0, submission.batches.size());
    }
    DrawVegetationShadowNode(cmd, globalStateData);
}

bool VansGraphics::VansScene::BuildShadowDrawSubmission(
    GlobalStateData globalStateData,
    VansDrawSubmissionList& submission)
{
    VANS_PROFILE_SCOPE("Shadow::BuildSubmission", Vans::ProfileCategory::CommandRecord);
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
            const auto* caster = vkDevice->FindCurrentShadowCaster(proxy);
            if (caster != nullptr && caster->hasBounds &&
                !RenderBoundsIntersectsClipFrustum(caster->bounds, *cascadeWorldToClip))
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

    VANS_PROFILE_SCOPE("Shadow::FinalizeSubmission", Vans::ProfileCategory::CommandRecord);
    return FinalizeDrawSubmission(VansDrawSortPolicy::State, submission);
}

void VansGraphics::VansScene::DrawVegetationShadowNode(VansVKCommandBuffer& cmd, GlobalStateData globalStateData)
{
    if (IsRenderNodeEnabledForCurrentFrame(m_VegetationRenderNode))
        static_cast<VansVegetationRenderNode*>(m_VegetationRenderNode)->DrawShadow(cmd, globalStateData);
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
			job.casterHandles.find(casterHandle) != job.casterHandles.end();
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
    const bool sameQueue = !device->IsAsyncComputeEnabled();
    const auto lane=sameQueue ? Vans::VansGpuQueueLane::Graphics : Vans::VansGpuQueueLane::Compute;
    struct Work { VansVegetationSystem* system; bool cullReady=false; };
    std::vector<Work> work;
    work.reserve(m_VegetationCollection->BatchCount());
    m_VegetationCollection->ForEach([&](VansVegetationSystem& value)
    {
        work.push_back({&value,false});
    });
    // 按阶段记录固定数量的 GPU 时间戳，避免每区块采样耗尽 query pool。
    {
        VANS_PROFILE_SCOPE("Vegetation::GrassCull", Vans::ProfileCategory::CommandRecord);
        VANS_GPU_SCOPE_LANE(cmd.GetVKCommandBuffer(), "Vegetation Grass Cull", lane);
        for (auto& item:work)
            item.cullReady=item.system->DispatchCullPass(cmd,item.system->GetCullDistance(),sameQueue);
    }
    {
        VANS_PROFILE_SCOPE("Vegetation::TreeCull", Vans::ProfileCategory::CommandRecord);
        VANS_GPU_SCOPE_LANE(cmd.GetVKCommandBuffer(), "Vegetation Tree Cull", lane);
        for (auto& item:work) item.system->DispatchTreeCullPass(cmd,sameQueue);
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
            system->GetLodFadeDist(), item.cullReady && !system->CastsShadows(), sameQueue);
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
            vkDevice->GetLogicDevice(), globalStateData,
            node->m_UsesRoadDecalPass ? VansPass::ROAD_DECAL_MODIFIER : VansPass::DECAL_MODIFIER,
            0, 0, nodeIndex, 0.0f, packet))
        {
            submission.packets.push_back(std::move(packet));
        }
    }

    const auto& payloadBytes = vkDevice->GetCurrentRenderSceneSnapshot().materials.custom.bytes;
    auto priority = [&](const VansDrawPacket& packet) {
        // 道路使用 PBR payload，不能把同索引的普通贴花 custom payload 当成排序参数。
        if (m_DecalRenderNodes[packet.stableOrder]->m_UsesRoadDecalPass) return 0.0f;
        const size_t offset = static_cast<size_t>(packet.instanceData.materialIndex) * sizeof(VansCustomMaterialPayload);
        VansCustomMaterialPayload payload;
        if (offset <= payloadBytes.size() && sizeof(payload) <= payloadBytes.size() - offset)
            std::memcpy(&payload, payloadBytes.data() + offset, sizeof(payload));
        return payload.values[2].x;
    };
    std::sort(submission.packets.begin(), submission.packets.end(),
        [&](const VansDrawPacket& lhs, const VansDrawPacket& rhs) {
            const float leftPriority = priority(lhs), rightPriority = priority(rhs);
            return leftPriority != rightPriority ? leftPriority < rightPriority : lhs.stableOrder < rhs.stableOrder;
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
