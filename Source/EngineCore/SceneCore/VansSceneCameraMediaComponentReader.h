#pragma once

#include "VansSceneCameraMediaComponentConfig.h"

#include <functional>
#include <optional>
#include <string>

namespace Vans
{
struct VansSerializedValue;

using VansSceneAssetGuidResolver = std::function<std::string(const VansSerializedValue& source)>;

class VansSceneCameraMediaComponentReader
{
public:
	static VansSceneCameraComponentConfig ReadCamera(const VansSerializedValue& cameraNode);
	static std::optional<VansSceneAudioComponentConfig> ReadAudio(
		const VansSerializedValue& audioNode,
		const VansSceneAssetGuidResolver& assetGuidResolver);
	static std::optional<VansSceneVideoComponentConfig> ReadVideo(
		const VansSerializedValue& videoNode,
		const VansSceneAssetGuidResolver& assetGuidResolver);
};
}
