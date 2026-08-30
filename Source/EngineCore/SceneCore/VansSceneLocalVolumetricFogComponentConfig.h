#pragma once

#include <array>

namespace Vans
{
// LocalVolumetricFog 是实体组件；位置、旋转与尺寸来自实体 Transform，
// 其中 Transform.scale 表示盒体的完整世界尺寸（米）。
struct VansSceneLocalVolumetricFogComponentConfig
{
	bool enabled = true;
	float visibilityDistanceMeters = 80.0f;
	std::array<float, 3> singleScatteringAlbedo{ 0.95f, 0.97f, 1.0f };
	float anisotropy = 0.2f;
	std::array<float, 3> emissivePerMeter{ 0.0f, 0.0f, 0.0f };
	float edgeFadeDistanceMeters = 2.0f;
	float distanceFadeStartMeters = 0.0f;
	float distanceFadeEndMeters = 2000.0f;
	float directLightingScale = 1.0f;
	float skyLightingScale = 1.0f;
	bool receiveCloudShadows = true;
};
}
