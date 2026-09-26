#include "VansSceneAudioReverbZoneComponentBuilder.h"

#include "../../AssetCore/VansAssetObjectRepository.h"
#include "../../AudioCore/VansAudioReverbPresetAsset.h"
#include "../../SceneCore/VansSceneCameraMediaComponentConfig.h"
#include "../../ScriptCore/VansScriptContext.h"

namespace VansGraphics
{
VansSceneAudioReverbZoneDependencies
VansSceneAudioReverbZoneComponentBuilder::ResolveDependencies(
	const Vans::VansSceneAudioReverbZoneConfig& config,
	const Vans::VansAssetObjectRepository& repository)
{
	VansSceneAudioReverbZoneDependencies dependencies;
	if (config.presetAssetGuid.empty())
		return dependencies;

	Vans::VansAssetGuid guid;
	if (!Vans::VansAssetGuid::TryParse(config.presetAssetGuid, guid))
	{
		dependencies.success = false;
		dependencies.error = "Audio reverb preset GUID is invalid: '" +
			config.presetAssetGuid + "'";
		return dependencies;
	}

	dependencies.presetAsset =
		repository.ResolveLatest<Vans::VansAudioReverbPresetAsset>(guid);
	if (!dependencies.presetAsset)
	{
		dependencies.success = false;
		dependencies.error = "Audio reverb preset is unavailable: '" +
			config.presetAssetGuid + "'";
	}
	return dependencies;
}

VansSceneAudioReverbZoneBuildResult
VansSceneAudioReverbZoneComponentBuilder::Build(
	VansScriptObject& object,
	const Vans::VansSceneAudioReverbZoneConfig& config,
	const VansSceneAudioReverbZoneDependencies& dependencies,
	const std::function<void()>& ensureObjectTransform)
{
	VansSceneAudioReverbZoneBuildResult result;
	if (!dependencies.success)
	{
		result.error = dependencies.error;
		return result;
	}
	if (!config.presetAssetGuid.empty() && !dependencies.presetAsset)
	{
		result.error = "Audio reverb preset dependency was not resolved";
		return result;
	}

	ensureObjectTransform();
	auto* component = new VansScriptAudioReverbZoneComponent();
	component->m_ComponentName = config.componentType;
	component->m_Shape = config.shape;
	component->m_Preset = config.preset;
	component->m_PresetAssetGuid = config.presetAssetGuid;
	component->m_PresetParameters = dependencies.presetAsset
		? dependencies.presetAsset->parameters
		: config.presetParameters;
	component->m_OverridePresetParameters =
		dependencies.presetAsset != nullptr || config.overridePresetParameters;
	component->m_Radius = config.radius;
	component->m_HalfExtentX = config.halfExtents[0];
	component->m_HalfExtentY = config.halfExtents[1];
	component->m_HalfExtentZ = config.halfExtents[2];
	component->m_FadeDistance = config.fadeDistance;
	component->m_WetGain = config.wetGain;
	component->m_Priority = config.priority;
	object.AddComponent(component);

	result.success = true;
	result.component = component;
	return result;
}
}
