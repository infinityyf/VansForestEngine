#pragma once

#include <optional>
#include <string>
#include <vector>

namespace Vans
{
struct VansSceneAudioRuntimeOverride
{
	std::string name;
	std::optional<float> volume;
	std::optional<float> pitch;
	std::optional<bool> loop;
	std::optional<bool> spatial;
	std::optional<float> referenceDistance;
	std::optional<float> maxDistance;
	bool autoPlay = false;
};

using VansSceneAudioRuntimeOverrides = std::vector<VansSceneAudioRuntimeOverride>;
}
