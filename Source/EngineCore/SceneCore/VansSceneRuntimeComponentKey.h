#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace Vans
{
inline std::string CanonicalRuntimeComponentKeyForName(std::string componentName)
{
	std::transform(componentName.begin(), componentName.end(), componentName.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	if (componentName == "render") return "render";
	if (componentName == "physics") return "physics";
	if (componentName == "cloth") return "cloth";
	if (componentName == "charactercontroller") return "charController";
	if (componentName == "directionallight") return "directional_light";
	if (componentName == "pointlight") return "point_light";
	if (componentName == "spotlight") return "spot_light";
	if (componentName == "rectlight") return "rect_light";
	if (componentName == "camera") return "camera";
	if (componentName == "audio") return "audio";
	if (componentName == "audioreverbzone") return "audio_reverb_zone";
	if (componentName == "audiovolume") return "audio_volume";
	if (componentName == "video") return "video";
	if (componentName == "particle") return "particle";
	if (componentName == "animation") return "animation";
	if (componentName == "vehicle") return "vehicle";
	return componentName;
}
}
