#pragma once

#include "VansTimelineEvaluation.h"
#include "VansTimelinePreAnimatedState.h"

#include <functional>

namespace Vans
{
struct VansTimelineApplyContext
{
	std::string writerId;
	std::string propertyKey;
	VansTimelineBlendMode blendMode = VansTimelineBlendMode::Override;
	std::int32_t hierarchicalBias = 0;
	std::int32_t priority = 0;
};

template <typename Output>
using VansTimelineApplyFunction = std::function<bool(
	const VansTimelineApplyContext& context,
	const VansResolvedTimelineTarget& target,
	const Output& output,
	VansTimelineRestoreCallback& restore,
	std::string& error)>;

struct VansTimelineRuntimeAdapters
{
	VansTimelineApplyFunction<VansTimelineTransformOutput> transform;
	VansTimelineApplyFunction<VansTimelinePropertyOutput> property;
	VansTimelineApplyFunction<VansTimelineActivationOutput> activation;
	VansTimelineApplyFunction<VansTimelineConstraintOutput> constraint;
	VansTimelineApplyFunction<VansTimelineAnimationOutput> animation;
	VansTimelineApplyFunction<VansTimelineAnimatorParameterOutput> animatorParameter;
	VansTimelineApplyFunction<VansTimelineBoneOverrideOutput> boneOverride;
	VansTimelineApplyFunction<VansTimelineAudioOutput> audio;
	VansTimelineApplyFunction<VansTimelineMediaOutput> media;
	VansTimelineApplyFunction<VansTimelineParticleOutput> particle;
	VansTimelineApplyFunction<VansTimelineCameraCutOutput> cameraCut;
	VansTimelineApplyFunction<VansTimelineCameraPropertyOutput> cameraProperty;
	VansTimelineApplyFunction<VansTimelineCameraShakeOutput> cameraShake;
	VansTimelineApplyFunction<VansTimelineFadePostProcessOutput> fadePostProcess;
	VansTimelineApplyFunction<VansTimelineLightOutput> light;
	VansTimelineApplyFunction<VansTimelineMaterialParameterOutput> materialParameter;
	VansTimelineApplyFunction<VansTimelineMaterialSwitchOutput> materialSwitch;
	VansTimelineApplyFunction<VansTimelineUIOutput> ui;
	VansTimelineApplyFunction<VansTimelineEventOutput> event;
	VansTimelineApplyFunction<VansTimelineSubTimelineOutput> subTimeline;
	VansTimelineApplyFunction<VansTimelineTimeScaleOutput> timeScale;
};

class VansTimelineTrackAppliers
{
public:
	static void Apply(
		std::vector<VansTimelineEvaluationOutput>& outputs,
		const VansTimelineRuntimeAdapters& adapters,
		VansTimelinePreAnimatedState& preAnimatedState,
		VansTimelineDiagnostics& diagnostics);
};
}
