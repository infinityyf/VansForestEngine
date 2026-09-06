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
	struct VansSceneLocalVolumetricFogComponentConfig;

	class VansSceneRuntimeProjection
	{
	public:
		static VansSerializedValue BuildSkinProfileMaterialParameters(const VansSkinProfile& profile);

		// 使用与完整场景投影相同的当前 schema 读取器，为编辑器实时预览生成运行时配置。
		static bool ProjectLocalVolumetricFogComponent(
			const VansSerializedValue& component,
			VansSceneLocalVolumetricFogComponentConfig& outConfig);

		// Projects an in-memory array of current-schema entities without requiring or
		// synthesizing a complete Scene document. Used by editor/runtime entity creation.
		static bool BuildRuntimeSceneEntityPlan(
			const VansSerializedValue& entities,
			const std::string& projectRoot,
			VansSceneContentBuildPlan& outPlan,
			std::string& outError);

		static bool BuildRuntimeSceneContentPlan(
			const VansSerializedValue& sceneRoot,
			const std::string& projectRoot,
			VansSceneContentBuildPlan& outPlan,
			std::string& outError);
	};
}
