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
	std::optional<float> rolloff;
	std::optional<std::string> attenuationMode;
	std::optional<float> reverbSend;
	std::optional<std::string> bus;
	std::optional<float> lowpassHighFrequencyGain;
	bool autoPlay = false;
};

using VansSceneAudioRuntimeOverrides = std::vector<VansSceneAudioRuntimeOverride>;
}
