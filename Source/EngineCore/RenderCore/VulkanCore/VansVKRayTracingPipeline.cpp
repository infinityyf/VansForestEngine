#include "VansVKDevice.h"
#include "../VansScene.h"

namespace VansGraphics
{
	void VansVKDevice::UpdateRayTracing()
	{
		auto camera = m_Scene->GetCamera();
		rayTracingContext.m_RayTracingConstant.cameraPos = camera->GetPosition();
		rayTracingContext.m_RayTracingConstant.cameraDir = camera->GetForward();
		rayTracingContext.m_RayTracingConstant.cameraRight = camera->GetRight();
		rayTracingContext.m_RayTracingConstant.cameraUp = camera->GetUp();
		rayTracingContext.DispatchRayTracing(this, &m_VansVKCommandBuffer, m_Scene);

		VansLightManager* lightManager = m_Scene->GetLightManager();
		VansMaterialManager* materialManager = m_Scene->GetMaterialManager();
		rayTracingContext.UpdateGIProbe(this, &m_VansVKCommandBuffer, lightManager, materialManager);
	}
}
