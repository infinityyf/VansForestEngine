#pragma once

#include "VansProjectSettings.h"

#include <unordered_map>
#include <vector>

namespace Vans
{
	struct VansProjectRenderSettingsData
	{
		VansProjectUpscalerSettings upscalerSettings;
		VansProjectCommandRecordingSettings commandRecordingSettings;
		VansProjectRenderOutputSettings renderOutputSettings;
		VansProjectAtmosphereQualitySettings atmosphereQualitySettings;
		VansProjectNearMediaQualitySettings nearMediaQualitySettings;
		VansProjectCloudShadowQualitySettings cloudShadowQualitySettings;
		VansProjectMainCameraHiZCullSettings mainCameraHiZCullSettings;
	};

	struct VansProjectPhysicsSettingsData
	{
		float fixedTimeStep = 1.0f / 60.0f;
		std::unordered_map<std::string, std::vector<std::string>> queryProfiles;
	};
}
