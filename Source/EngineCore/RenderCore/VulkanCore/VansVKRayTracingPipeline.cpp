#include "VansVKDevice.h"
#include "../VansScene.h"
#include "../VansCamera.h"
#include "../VansMaterial.h"
#include "VansVKCommandBuffer.h"
#include "VansRenderPass.h"
#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "../../Util/VansProfiler.h"
#include "../GICore/VansGIFrameCost.h"
#include "../../Util/VansLog.h"
#include <stdexcept>

namespace VansGraphics
{
	bool VansVKDevice::ApplyGISettingsAtSafePoint(const VansGISettings& settings,
        const std::shared_ptr<const VansGIVoxelMaterialInputs>& materials, bool forceRebuild)
	{
		if (!m_Scene) return false;
		try
		{
			const bool rebuild = forceRebuild ||
				!GISettingsResourceLayoutEquals(rayTracingContext.GetAppliedSettings(), settings);
			if (rebuild)
			{
				if (!rayTracingContext.CreateRayTracingResource(this, &m_MaintenanceGraphicsCommandBuffer, m_Scene, settings, materials))
					return false;
				if (auto* materials = m_Scene->GetMaterialManager())
				{
					materials->m_AmbientSkyCache.Destroy(m_VansVKLogicDevice, materials->m_RuntimeRenderTextureManager);
				}
				PrepareAmbientSkyCacheRenderData();
				SetupHairLightingDescriptors(VansRenderPassManager::GetInstance());
				// 新资源发布后才废弃消费者绑定；失败时旧光照和预览仍然有效。
				m_Scene->GetReflectionProbeSystem()->ReleaseGILightingBindings();
				if (auto* materials = m_Scene->GetMaterialManager()) materials->m_SSGI.history.Reset();
				ResetFeatureDescriptorSets();
				m_Scene->MarkRenderNodeDescriptorSetsDirty();
			}
			else rayTracingContext.UpdateGISettings(this, &m_MaintenanceGraphicsCommandBuffer, settings);
			UploadSSGIParams(settings);
			return true;
		}
		catch (const std::exception& error)
		{
			VANS_LOG_ERROR("[GI] Settings preparation failed: " << error.what());
			return false;
		}
	}

	void VansVKDevice::ProcessPendingGISettings()
	{
		if (m_Scene == nullptr || !m_CurrentRenderSceneSnapshot.sceneReady ||
			!m_CurrentRenderSceneSnapshot.gi.prepared)
			return;

		const VansRenderGIFrameData& giFrame = m_CurrentRenderSceneSnapshot.gi;
		const bool rebuildProbeResources = giFrame.rebuildProbeResources ||
            (giFrame.settings.world.enabled && rayTracingContext.NeedsWorldSourceRebuild());
		const bool updateParams = giFrame.updateParameters;
		if (!rebuildProbeResources && !updateParams)
			return;

		// Both rebuilds and parameter-only edits replace the shared GI UBO that
		// already submitted frames may still read. Drain at this settings boundary.
		if (!WaitForDevice())
			throw std::runtime_error("Failed to retire GPU work before GI settings update");
		// 帧内仍只消费本帧快照。Inspector 的显式应用在渲染事务中报告失败；
		// 非交互场景准备失败必须显式终止，不能让新配置配上旧资源继续渲染。
		if (!ApplyGISettingsAtSafePoint(giFrame.settings, m_CurrentRenderSceneSnapshot.materials.voxelMaterials, rebuildProbeResources))
			throw std::runtime_error("Failed to apply the GI render snapshot");
	}

	void VansVKDevice::UpdateRayTracing(VansVKCommandBuffer& computeCmd)
	{
        // Feedback 只依赖上帧 RayTracing 节点完成；不要求上帧最终 Graphics fence
        // 已退役。这样调度器仍按原顺序消费反馈，未来可让 Graphics 尾段在途。
        if (const VkFence feedbackFence = rayTracingContext.GetPendingGIProbeFeedbackFence();
            feedbackFence != VK_NULL_HANDLE)
        {
            VANS_PROFILE_WAIT(GIProfileScope(GICostStage::FeedbackWait));
            if (VansGraphics::vkWaitForFences(m_VansVKLogicDevice, 1, &feedbackFence,
                    VK_TRUE, UINT64_MAX) != VK_SUCCESS)
                throw std::runtime_error("Failed to wait for previous GI probe feedback");
            rayTracingContext.CompleteGIProbeUpdate(feedbackFence);
        }
        rayTracingContext.SetWorldViewCenter(m_CurrentRenderView.position);
		VansMaterialManager* materialManager = m_Scene->GetMaterialManager();
		rayTracingContext.PrepareGIProbeUpdate(
			m_CurrentRenderSceneSnapshot.light, materialManager->m_SSGI.history,
            m_AsyncComputeEnabled ? CurrentFrameContext().rayTracingFence : CurrentFrameContext().graphicsFence,
            m_GILightingContentChanges);
        m_GILightingContentChanges = {};
		const VkCommandBuffer commandBuffer = computeCmd.GetVKCommandBuffer();
		const Vans::VansGpuQueueLane queueLane = m_AsyncComputeEnabled
			? Vans::VansGpuQueueLane::Compute
			: Vans::VansGpuQueueLane::Graphics;
		{
			VANS_GPU_SCOPE_LANE(commandBuffer, GIProfileScope(GICostStage::ProbeTrace), queueLane);
			rayTracingContext.DispatchRayTracing(this, &computeCmd, m_Scene);
		}
		{
			VANS_GPU_SCOPE_LANE(commandBuffer, GIProfileScope(GICostStage::ProbeUpdate), queueLane);
			rayTracingContext.UpdateGIProbe(this, &computeCmd, materialManager);
		}
	}
}
