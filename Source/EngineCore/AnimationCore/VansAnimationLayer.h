#pragma once

#include "VansBoneMask.h"
#include "VansPoseTypes.h"

#include <string>

namespace VansGraphics
{
	enum class VansAnimationLayerKind { Base, Overlay };
	enum class VansLayerBlendMode { Override, Additive };
	enum class VansRotationBlendSpace { Local, Mesh };
	enum class VansAdditiveReferenceMode { BindPose, FirstFrame, ClipTime, ReferenceClip };
	enum class VansLayerRootMotionMode { Ignore, Base, BlendByRootWeight, Override };
	enum class VansLayerCurveMode { BaseOnly, Override, Blend, Normalize, Min, Max };
	enum class VansLayerEventMode { Ignore, ActiveOnly, Always };
	enum class VansLayerNodeTrackMode { Ignore, Override };
	enum class VansLayerSyncMode { Independent, NormalizedTime, MarkerSync, SyncedGraph };

	struct VansAnimationLayerDefinition
	{
		std::string id;
		std::string name;
		std::string graphId;
		VansAnimationLayerKind kind = VansAnimationLayerKind::Overlay;
		std::string maskGuid;
		std::string maskPathHint;
		VansLayerBlendMode blendMode = VansLayerBlendMode::Override;
		VansRotationBlendSpace rotationSpace = VansRotationBlendSpace::Local;
		VansAdditiveReferenceMode additiveReference = VansAdditiveReferenceMode::BindPose;
		std::string referenceClipName;
		float referenceTime = 0.0f;
		std::string weightParameter;
		float fixedWeight = 1.0f;
		bool useWeightParameter = false;
		float weightSmoothingTime = 0.0f;
		VansLayerRootMotionMode rootMotion = VansLayerRootMotionMode::Ignore;
		VansLayerCurveMode curves = VansLayerCurveMode::Blend;
		VansLayerEventMode events = VansLayerEventMode::ActiveOnly;
		VansLayerNodeTrackMode nodeTracks = VansLayerNodeTrackMode::Ignore;
		VansLayerSyncMode sync = VansLayerSyncMode::Independent;
		std::string syncLeaderLayerId;
		float eventWeightThreshold = 0.01f;
		bool enabled = true;
		bool updateWhenWeightIsZero = true;
	};

	struct VansAnimationLayerRuntimeState
	{
		float currentWeight = 1.0f;
		bool initialized = false;
	};

	class VansAnimationLayerMixer
	{
	public:
		static VansPosePayload ApplyLayer(const VansPosePayload& base,
		                                  const VansPosePayload& layer,
		                                  const VansAnimationLayerDefinition& definition,
		                                  const VansCompiledBoneMask& mask,
		                                  const Skeleton& skeleton,
		                                  const VansAnimationFrameVector<VansBoneTransform>& referencePose,
		                                  float layerWeight);

		static void BuildBindPose(const Skeleton& skeleton,
		                          VansAnimationFrameVector<VansBoneTransform>& outPose);
	};
}
