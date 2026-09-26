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
		VansCameraLensLimits cameraLensLimits;
	};

	struct VansProjectPhysicsSettingsData
	{
		VansEngine::VansPhysicsTiming timing;
		std::unordered_map<std::string, std::vector<std::string>> queryProfiles;
	};
}
