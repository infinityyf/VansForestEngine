#pragma once

#include "VansSceneCameraMediaComponentConfig.h"

#include <functional>
#include <optional>
#include <string>

namespace Vans
{
struct VansSerializedValue;

using VansSceneSourceNameResolver = std::function<std::string(const VansSerializedValue& source)>;

class VansSceneCameraMediaComponentReader
{
public:
	static VansSceneCameraMediaComponentConfig ReadComponents(const VansSerializedValue& components);
	static VansSceneCameraMediaComponentConfig ReadComponents(
		const VansSerializedValue& components,
		const VansSceneSourceNameResolver& sourceResolver);

	static VansSceneCameraComponentConfig ReadCamera(const VansSerializedValue& cameraNode);
	static std::optional<VansSceneAudioComponentConfig> ReadAudio(
		const VansSerializedValue& audioNode,
		const VansSceneSourceNameResolver& sourceResolver);
	static std::optional<VansSceneVideoComponentConfig> ReadVideo(
		const VansSerializedValue& videoNode,
		const VansSceneSourceNameResolver& sourceResolver);
};
}
