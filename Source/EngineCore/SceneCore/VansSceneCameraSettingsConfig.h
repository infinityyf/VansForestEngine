#pragma once

#include <array>
#include <optional>

namespace Vans
{
struct VansSceneCameraSettingsConfig
{
	std::optional<std::array<float, 3>> position;
	std::optional<std::array<float, 3>> rotation;
	std::optional<float> fov;
	std::optional<float> nearClip;
	std::optional<float> farClip;
};
}
