#include "AnimationPreviewParameterEditing.h"
#include "../../AnimationCore/VansAnimationController.h"
#include "../../AnimationCore/VansAnimationNode.h"

namespace Vans::EditorAPI
{
bool AnimationPreviewParameterEditing::Apply(VansGraphics::VansAnimationNode& sceneTarget,
	const AnimationPreviewParameterValue& value)
{
	// Retarget 每帧从组件控制器向源控制器同步参数，直接写源控制器会被旧值覆盖。
	auto* controller = sceneTarget.GetController();
	return controller && Apply(*controller, value);
}

bool AnimationPreviewParameterEditing::Apply(VansGraphics::VansAnimationController& controller,
	const AnimationPreviewParameterValue& value)
{
	if (value.name.empty() || !controller.HasParameter(value.name)) return false;
	switch (value.type)
	{
	case AnimationPreviewParameterType::Float: controller.SetFloat(value.name, value.floatValue); break;
	case AnimationPreviewParameterType::Bool: controller.SetBool(value.name, value.boolValue); break;
	case AnimationPreviewParameterType::Int: controller.SetInt(value.name, value.intValue); break;
	case AnimationPreviewParameterType::Trigger: controller.SetTrigger(value.name); break;
	case AnimationPreviewParameterType::Vector3:
		controller.SetVector3(value.name, glm::vec3(value.vectorValue.x, value.vectorValue.y, value.vectorValue.z)); break;
	case AnimationPreviewParameterType::Quaternion:
		controller.SetQuaternion(value.name, glm::quat(value.quaternionValue.w, value.quaternionValue.x,
			value.quaternionValue.y, value.quaternionValue.z)); break;
	default: return false;
	}
	return true;
}
}
