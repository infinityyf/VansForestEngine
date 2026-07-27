#pragma once

#include <string>

namespace Vans
{
	struct VansSerializedValue;
	struct VansSceneContentBuildPlan;

	class VansSceneRuntimeProjection
	{
	public:
		static bool BuildRuntimeSceneContentPlan(
			const VansSerializedValue& sceneRoot,
			const std::string& projectRoot,
			VansSceneContentBuildPlan& outPlan,
			std::string& outError);
	};
}
