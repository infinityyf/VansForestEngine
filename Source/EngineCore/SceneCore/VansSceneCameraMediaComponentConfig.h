#pragma once

#include <optional>
#include <string>

namespace Vans
{
struct VansSceneCameraComponentConfig
{
	std::optional<float> fov;
	std::optional<float> nearClip;
	std::optional<float> farClip;
};

struct VansSceneAudioComponentConfig
{
	std::string sourceName;
};

struct VansSceneVideoComponentConfig
{
	std::string sourceName;
};

struct VansSceneCameraMediaComponentConfig
{
	std::optional<VansSceneCameraComponentConfig> camera;
	std::optional<VansSceneAudioComponentConfig> audio;
	std::optional<VansSceneVideoComponentConfig> video;
};
}
