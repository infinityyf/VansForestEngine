#pragma once

#include <array>
#include <string>

namespace Vans
{
	// Converts scene XYZW quaternions to the renderer's Euler representation.
	// Yaw-only projection is opt-in per configured AI agent; the default path
	// preserves model-axis corrections on X/Z.
	std::array<float, 3> ProjectSceneQuaternionToEulerDegrees(
		const std::array<float, 4>& rotationXYZW,
		bool yawOnly);

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
