#pragma once

#include <string>

namespace Vans
{
	struct VansSkinProfile;
	struct VansSerializedValue;
	struct VansSceneContentBuildPlan;

	class VansSceneRuntimeProjection
	{
	public:
		static VansSerializedValue BuildSkinProfileMaterialParameters(const VansSkinProfile& profile);

		static bool BuildRuntimeSceneContentPlan(
			const VansSerializedValue& sceneRoot,
			const std::string& projectRoot,
			VansSceneContentBuildPlan& outPlan,
			std::string& outError);
	};
}
