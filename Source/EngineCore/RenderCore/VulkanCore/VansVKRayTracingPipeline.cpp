#include "VansVKDevice.h"
#include "../VansScene.h"
#include "../VansCamera.h"
#include "../VansMaterial.h"
#include "VansVKCommandBuffer.h"

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
			{
				materialManager->UnregisterRuntimeRenderTexture(VansMaterialManager::RT_SH_R_RESULT);
				materialManager->UnregisterRuntimeRenderTexture(VansMaterialManager::RT_SH_G_RESULT);
				materialManager->UnregisterRuntimeRenderTexture(VansMaterialManager::RT_SH_B_RESULT);
				materialManager->UnregisterRuntimeRenderTexture(VansMaterialManager::RT_GI_VISIBILITY_ATLAS);
				materialManager->m_SSGITemporalFrame = 0;
			}

			rayTracingContext.CleanupSceneResources(m_VansVKLogicDevice, materialManager);
			rayTracingContext.CreateRayTracingResource(this, &m_VansVKCommandBuffer, m_Scene);
			ResetFeatureDescriptorSets();
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
		rayTracingContext.DispatchRayTracing(this, &computeCmd, m_Scene);

		VansLightManager* lightManager = m_Scene->GetLightManager();
		VansMaterialManager* materialManager = m_Scene->GetMaterialManager();
		rayTracingContext.UpdateGIProbe(this, &computeCmd, lightManager, materialManager);
	}
}
