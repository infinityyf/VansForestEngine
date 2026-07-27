#pragma once

#include "VansProjectSettings.h"

namespace Vans
{
	struct VansProjectRenderSettingsData
	{
		VansProjectFSRSettings fsrSettings;
	};

	struct VansProjectPhysicsSettingsData
	{
		float fixedTimeStep = 1.0f / 60.0f;
	};
}
