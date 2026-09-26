#include "VansSceneRenderPreparationExecutor.h"

#include "../VulkanCore/VansVKDevice.h"
#include "../WaterCore/VansWaterSystem.h"
#include "../../ScriptCore/VansScriptContext.h"
#include "../../Util/VansLog.h"

namespace VansGraphics
{
bool VansSceneRenderPreparationExecutor::PrepareAfterSceneContentLoaded(VansScene& scene, VansVKDevice& device)
{
	if (!scene.PrepareImpactDecalPools()) return false;
	if (!scene.PrepareCombatSurfaces(device)) return false;
	device.PreparePBRMaterialData();
	BindVideoComponentsToPreparedMaterials(scene);

	device.PrepareInstanceTransformData();
	scene.CreateGlobalDescriptorSet(device.GetLogicDevice());
	if (!scene.InitializeEnvironmentRendering(device))
	{
		VANS_LOG_ERROR("[SceneRenderPreparation] Atmosphere system initialization failed");
		return false;
	}

	if (!scene.PrepareReflectionProbeRuntime(device))
		return false;
	scene.BindWaterSystemGlobalDescriptors();
	scene.BindMaterialVideoDescriptorSet();
	scene.UpdateGlobalTileLightDescriptors();

	device.PrepareIESProfileData();

	scene.CreateSceneNodeDescriptorSets();
	device.PrepareRayTracingData();
	return true;
}

void VansSceneRenderPreparationExecutor::BindVideoComponentsToPreparedMaterials(VansScene& scene)
{
	const int kSlotsPerMat = 5;

	for (auto* obj : scene.GetSceneObjects())
	{
		auto* videoComp = obj->GetComponent<VansScriptVideoComponent>();
		auto* renderComp = obj->GetComponent<VansScriptRenderComponent>();
		if (!videoComp || !renderComp || !renderComp->m_RenderNode || !renderComp->m_RenderNode->m_Material)
			continue;

		auto* emissiveMat = dynamic_cast<VansEmissiveMaterial*>(renderComp->m_RenderNode->m_Material);
		if (!emissiveMat || emissiveMat->m_VideoName.empty() || emissiveMat->m_MaterialIndex < 0)
			continue;

		videoComp->m_BindlessFirstSlot = emissiveMat->m_MaterialIndex * kSlotsPerMat;
		videoComp->m_MaterialManagerRef = scene.GetMaterialManager();
		VANS_LOG("[SceneRenderPreparation] VideoComponent '" << obj->m_ObjectName
			<< "' Bindless slot " << videoComp->m_BindlessFirstSlot
			<< "~" << videoComp->m_BindlessFirstSlot + kSlotsPerMat - 1 << " bound");
	}
}
}
