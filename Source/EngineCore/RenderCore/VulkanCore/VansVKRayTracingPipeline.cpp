#include "VansVKDevice.h"
#include "../VansScene.h"
#include "../VansCamera.h"
#include "../VansMaterial.h"
#include "VansVKCommandBuffer.h"
#include "../../Util/VansProfiler.h"

namespace VansGraphics
{
	void VansVKDevice::ProcessPendingGISettings()
	{
		if (m_Scene == nullptr || !m_CurrentRenderSceneSnapshot.sceneReady ||
			!m_CurrentRenderSceneSnapshot.gi.prepared)
			return;

		const VansRenderGIFrameData& giFrame = m_CurrentRenderSceneSnapshot.gi;
		const bool rebuildProbeResources = giFrame.rebuildProbeResources;
		const bool updateParams = giFrame.updateParameters;
		if (!rebuildProbeResources && !updateParams)
			return;

		if (rebuildProbeResources)
		{
			WaitForDevice();

			auto* materialManager = m_Scene->GetMaterialManager();
			if (materialManager != nullptr)
				materialManager->m_SSGITemporalFrame = 0;

			rayTracingContext.CleanupSceneResources(m_VansVKLogicDevice, materialManager);
			rayTracingContext.CreateRayTracingResource(this, &m_VansVKCommandBuffer, m_Scene);
			ResetFeatureDescriptorSets();
			m_Scene->MarkRenderNodeDescriptorSetsDirty();
		}
		else
		{
			rayTracingContext.UpdateGISettings(giFrame.settings);
		}

		// 帧内设置更新只能消费当前渲染快照，不能回读可变 Scene，也不能隐式
		// 复用上一帧快照。调用方显式传入同一份 GI 数据，保证 DDGI 与 SSGI
		// 在一个渲染事务中观察到一致的配置。
		UploadSSGIParams(giFrame.settings);
	}

	void VansVKDevice::UpdateRayTracing(VansVKCommandBuffer& computeCmd)
	{
		VansMaterialManager* materialManager = m_Scene->GetMaterialManager();
		rayTracingContext.PrepareGIProbeUpdate(
			m_CurrentRenderSceneSnapshot.light, materialManager);
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
