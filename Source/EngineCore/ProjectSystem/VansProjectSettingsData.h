#pragma once

#include "VansProjectSettings.h"

namespace Vans
{
	struct VansProjectRenderSettingsData
	{
		VansProjectFSRSettings fsrSettings;
		VansProjectCommandRecordingSettings commandRecordingSettings;
		VansProjectMainCameraHiZCullSettings mainCameraHiZCullSettings;
	};

	struct VansProjectPhysicsSettingsData
	{
		float fixedTimeStep = 1.0f / 60.0f;
	};
}
