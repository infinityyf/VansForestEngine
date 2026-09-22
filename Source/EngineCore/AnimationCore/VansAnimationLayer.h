#pragma once

#include "VansBoneMask.h"
#include "VansPoseTypes.h"

#include <string>
#include <vector>

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
	enum class VansLayerActivationCurve { Linear, SmoothStep };
	enum class VansGraphSetBlendCurve { Linear, SmoothStep };
	enum class VansGraphSetPhasePolicy { Restart, MatchNormalizedTime, MatchMarker };
	enum class VansGraphSetEventPolicy { DominantSource, WeightedBoth };
	enum class VansGraphSetRootMotionPolicy { Blend, DominantSource, IncomingOnly };
	enum class VansGraphSetInterruptionPolicy { QueueLatest, Reject, Force };

	struct VansAnimationLayerDefinition
	{
		std::string id;
		std::string name;
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
		// 可选的动画曲线权重。曲线来自当前 Overlay Pose，用于复现 UE ALS
		// Layering_* 曲线对身体区域的动态开关；未提供时保持原有层权重语义。
		std::string weightCurve;
		float weightCurveDefault = 1.0f;
		float weightSmoothingTime = 0.0f;
		// Overlay activation is evaluated independently from GraphSet transitions.
		// It controls the local layer envelope while the Base graph remains live.
		float activationBlendInSeconds = 0.0f;
		float activationBlendOutSeconds = 0.0f;
		VansLayerActivationCurve activationCurve = VansLayerActivationCurve::SmoothStep;
		bool restartOnActivation = false;
		// Preserve the current Base pose motion on the overlay region.  This is
		// intentionally a layer feature; it never feeds back into Motion Matching.
		bool dynamicAdditive = false;
		float dynamicAdditiveWeight = 0.0f;
		float inertializationHalfLife = 0.0f;
		float inertializationMaxDuration = 0.0f;
		VansLayerRootMotionMode rootMotion = VansLayerRootMotionMode::Ignore;
		VansLayerCurveMode curves = VansLayerCurveMode::Blend;
		VansLayerEventMode events = VansLayerEventMode::ActiveOnly;
		VansLayerNodeTrackMode nodeTracks = VansLayerNodeTrackMode::Ignore;
		VansLayerSyncMode sync = VansLayerSyncMode::Independent;
		std::string syncLeaderLayerId;
		float eventWeightThreshold = 0.01f;
		bool updateWhenWeightIsZero = true;
	};

	// Layer Stack 只定义稳定的组合策略；具体 Pose Graph 由 Graph Set 绑定。
	struct VansAnimationGraphBindingDefinition
	{
		std::string layerId;
		std::string graphId;
		bool enabled = true;
	};

	struct VansAnimationGraphSetDefinition
	{
		std::string id;
		std::string name;
		std::vector<VansAnimationGraphBindingDefinition> bindings;
	};

	struct VansGraphSetTransitionPolicy
	{
		float duration = 0.2f;
		VansGraphSetBlendCurve curve = VansGraphSetBlendCurve::SmoothStep;
		VansGraphSetPhasePolicy phase = VansGraphSetPhasePolicy::MatchNormalizedTime;
		VansGraphSetEventPolicy events = VansGraphSetEventPolicy::DominantSource;
		VansGraphSetRootMotionPolicy rootMotion = VansGraphSetRootMotionPolicy::Blend;
		VansGraphSetInterruptionPolicy interruption = VansGraphSetInterruptionPolicy::QueueLatest;
		bool requireStateMatch = false;
	};

	struct VansGraphSetTransitionRule
	{
		std::string fromGraphSetId;
		std::string toGraphSetId;
		VansGraphSetTransitionPolicy policy;
	};

	struct VansAnimationLayerRuntimeState
	{
		float currentWeight = 1.0f;
		float targetWeight = 1.0f;
		bool activationRisePending = false;
		bool inertializationActive = false;
		float inertializationElapsed = 0.0f;
		std::vector<VansBoneTransform> previousPose;
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

		static VansPosePayload ApplyDynamicAdditive(
			const VansPosePayload& base,
			const VansPosePayload& layer,
			const VansAnimationFrameVector<VansBoneTransform>& baseReference,
			const Skeleton& skeleton,
			const VansCompiledBoneMask& mask,
			float weight,
			VansRotationBlendSpace rotationSpace = VansRotationBlendSpace::Local);

		static void BuildBindPose(const Skeleton& skeleton,
		                          VansAnimationFrameVector<VansBoneTransform>& outPose);
	};
}
