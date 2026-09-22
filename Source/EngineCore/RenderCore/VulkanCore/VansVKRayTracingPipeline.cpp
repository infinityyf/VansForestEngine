#include "VansVKDevice.h"
#include "../VansScene.h"
#include "../VansCamera.h"
#include "../VansMaterial.h"
#include "VansVKCommandBuffer.h"
#include "../../Util/VansProfiler.h"
#include "../../Util/VansLog.h"
#include <stdexcept>

namespace VansGraphics
{
	bool VansVKDevice::ApplyGISettingsAtSafePoint(const VansGISettings& settings, bool forceRebuild)
	{
		if (!m_Scene) return false;
		try
		{
			const bool rebuild = forceRebuild ||
				!GISettingsResourceLayoutEquals(rayTracingContext.GetAppliedSettings(), settings);
			if (rebuild)
			{
				if (!rayTracingContext.CreateRayTracingResource(this, &m_VansVKCommandBuffer, m_Scene, settings))
					return false;
				if (auto* materials = m_Scene->GetMaterialManager())
				{
					materials->ClearAmbientSkyCacheRenderData(m_VansVKLogicDevice);
				}
				PrepareAmbientSkyCacheRenderData();
				// 新资源发布后才废弃消费者绑定；失败时旧光照和预览仍然有效。
				m_Scene->GetReflectionProbeSystem()->ReleaseGILightingBindings();
				if (auto* materials = m_Scene->GetMaterialManager()) materials->m_SSGITemporalFrame = 0;
				ResetFeatureDescriptorSets();
				m_Scene->MarkRenderNodeDescriptorSetsDirty();
			}
			else rayTracingContext.UpdateGISettings(settings);
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

		if (rebuildProbeResources) WaitForDevice();
		// 帧内仍只消费本帧快照。Inspector 的显式应用在渲染事务中报告失败；
		// 非交互场景准备失败必须显式终止，不能让新配置配上旧资源继续渲染。
		if (!ApplyGISettingsAtSafePoint(giFrame.settings, rebuildProbeResources))
			throw std::runtime_error("Failed to apply the GI render snapshot");
	}

	void VansVKDevice::UpdateRayTracing(VansVKCommandBuffer& computeCmd)
	{
        rayTracingContext.SetWorldViewCenter(m_CurrentRenderView.position);
		VansMaterialManager* materialManager = m_Scene->GetMaterialManager();
		rayTracingContext.PrepareGIProbeUpdate(
			m_CurrentRenderSceneSnapshot.light, materialManager,
            m_AsyncComputeEnabled ? m_CurrentFrameContext.rayTracingFence : m_CurrentFrameContext.graphicsFence);
		const VkCommandBuffer commandBuffer = computeCmd.GetVKCommandBuffer();
		const Vans::VansGpuQueueLane queueLane = m_AsyncComputeEnabled
			? Vans::VansGpuQueueLane::Compute
			: Vans::VansGpuQueueLane::Graphics;
		{
			VANS_GPU_SCOPE_LANE(commandBuffer, "DDGI.RayTrace", queueLane);
			rayTracingContext.DispatchRayTracing(this, &computeCmd, m_Scene);
		}
		{
			VANS_GPU_SCOPE_LANE(commandBuffer, "DDGI.Update", queueLane);
			rayTracingContext.UpdateGIProbe(this, &computeCmd, materialManager);
		}
	}
}
