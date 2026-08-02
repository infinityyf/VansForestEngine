#pragma once

#include <array>
#include <optional>
#include <string>

#include "../AudioCore/VansAudioOcclusion.h"
#include "../AudioCore/VansAudioReverbPreset.h"

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
	bool occlusionEnabled = false;
	float occlusionGain = 0.45f;
	float occlusionHighFrequencyGain = 0.35f;
	std::string occlusionMaterial = "custom";
	float occlusionMaterialThickness = 1.0f;
	float occlusionAttack = 0.08f;
	float occlusionRelease = 0.18f;
	float occlusionQueryInterval = 0.12f;
	float occlusionMaxDistance = 100.0f;
	int occlusionMaxQueriesPerFrame = 4;
	float lowpassHighFrequencyGain = 1.0f;
	bool coneEnabled = false;
	float coneInnerAngle = 360.0f;
	float coneOuterAngle = 360.0f;
	float coneOuterGain = 1.0f;
	bool dopplerEnabled = false;
};

struct VansSceneAudioReverbZoneConfig
{
	std::string componentType = "AudioReverbZone";
	std::string shape = "sphere";
	std::string preset = "generic";
	std::string presetAssetGuid;
	float radius = 8.0f;
	std::array<float, 3> halfExtents{ 4.0f, 4.0f, 4.0f };
	float fadeDistance = 2.0f;
	float wetGain = 0.6f;
	int priority = 0;
	VansEngine::AudioReverbPresetParameters presetParameters;
	bool overridePresetParameters = false;
};

struct VansSceneVideoComponentConfig
{
	std::string sourceName;
};

struct VansSceneCameraMediaComponentConfig
{
	std::optional<VansSceneCameraComponentConfig> camera;
	std::optional<VansSceneAudioComponentConfig> audio;
	std::optional<VansSceneAudioReverbZoneConfig> audioReverbZone;
	std::optional<VansSceneVideoComponentConfig> video;
};
}
