#pragma once

namespace VansGraphics
{
	struct VansAnimGraphNodeLayout
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	enum class VansAnimGraphNodeType
	{
		Entry,
		Output,
		Clip,
		Blend,
		Blend1D,
		BlendSpace2D,
		IfCondition,
		Switch,
		AdditiveBlend,
		SpeedScale,
		StateMachine,
		MotionMatching,
		Slot,
		TargetPoseInput,
		Goal,
		AimConstraint,
		Grounding,
		LimbIK,
		ChainIK,
		PoseCheckpoint,
		RotationDistribution,
		SaveCachedPose,
		UseCachedPose,
		LayeredBlendPerBone
	};
}
