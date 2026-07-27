#pragma once

#include "VansSceneAnimationComponentConfig.h"

#include <optional>

namespace Vans
{
struct VansSerializedValue;

class VansSceneAnimationComponentReader
{
public:
	static std::optional<VansSceneAnimationComponentConfig> ReadFromComponents(
		const VansSerializedValue& components);
	static std::optional<VansSceneAnimationComponentConfig> ReadFromAuthoringEntity(
		const VansSerializedValue& entity);
	static VansSceneAnimationComponentConfig ReadAnimation(
		const VansSerializedValue& animationNode);
	static VansSceneAnimationComponentConfig ReadAuthoringAnimationComponent(
		const VansSerializedValue& animationComponent);
	static VansSceneRagdollComponentConfig ReadRagdoll(
		const VansSerializedValue& ragdollNode);
};
}
