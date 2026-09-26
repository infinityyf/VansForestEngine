#pragma once

#include <functional>
#include <memory>
#include <string>

class VansScriptAudioReverbZoneComponent;
class VansScriptObject;

namespace Vans
{
	class VansAssetObjectRepository;
	struct VansAudioReverbPresetAsset;
	struct VansSceneAudioReverbZoneConfig;
}

namespace VansGraphics
{
	struct VansSceneAudioReverbZoneDependencies
	{
		bool success = true;
		std::string error;
		std::shared_ptr<const Vans::VansAudioReverbPresetAsset> presetAsset;
	};

	struct VansSceneAudioReverbZoneBuildResult
	{
		bool success = false;
		std::string error;
		VansScriptAudioReverbZoneComponent* component = nullptr;
	};

	class VansSceneAudioReverbZoneComponentBuilder
	{
	public:
		static VansSceneAudioReverbZoneDependencies ResolveDependencies(
			const Vans::VansSceneAudioReverbZoneConfig& config,
			const Vans::VansAssetObjectRepository& repository);

		static VansSceneAudioReverbZoneBuildResult Build(
			VansScriptObject& object,
			const Vans::VansSceneAudioReverbZoneConfig& config,
			const VansSceneAudioReverbZoneDependencies& dependencies,
			const std::function<void()>& ensureObjectTransform);
	};
}
