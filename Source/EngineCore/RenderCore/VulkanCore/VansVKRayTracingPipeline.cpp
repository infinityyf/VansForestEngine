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
		if (m_Scene == nullptr || !m_Scene->IsSceneReady())
			return;

		const bool rebuildProbeResources = m_Scene->AreGIProbeResourcesDirty();
		const bool updateParams = m_Scene->AreGIParametersDirty();
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
			m_Scene->ClearGIProbeResourcesDirty();
		}
		else
		{
			rayTracingContext.UpdateGISettings(m_Scene->GetGISettings());
		}

		UploadSSGIParamsFromGISettings();
		m_Scene->ClearGIParametersDirty();
	}

	void VansVKDevice::UpdateRayTracing(VansVKCommandBuffer& computeCmd)
	{
		VansLightManager* lightManager = m_Scene->GetLightManager();
		VansMaterialManager* materialManager = m_Scene->GetMaterialManager();
		rayTracingContext.PrepareGIProbeUpdate(lightManager, materialManager);
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
			rayTracingContext.UpdateGIProbe(this, &computeCmd, lightManager, materialManager);
		}
	}
}
