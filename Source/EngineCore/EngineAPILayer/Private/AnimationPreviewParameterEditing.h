#pragma once

#include "../Public/EngineDTOs.h"

namespace VansGraphics
{
class VansAnimationController;
class VansAnimationNode;
}

namespace Vans::EditorAPI
{
// 参数编辑独立于图/Slot 播放路由，场景组件持有供源动画与目标 IK 共用的参数。
class AnimationPreviewParameterEditing final
{
public:
	static bool Apply(VansGraphics::VansAnimationNode& sceneTarget,
		const AnimationPreviewParameterValue& value);
	static bool Apply(VansGraphics::VansAnimationController& isolatedTarget,
		const AnimationPreviewParameterValue& value);
};
}
