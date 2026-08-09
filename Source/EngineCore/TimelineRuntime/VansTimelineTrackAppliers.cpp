#include "VansTimelineTrackAppliers.h"

#include <algorithm>
#include <type_traits>

namespace Vans
{
namespace
{
std::string TargetIdentity(const VansResolvedTimelineTarget& target)
{
	if (target.component.IsValid())
		return "component:" + std::to_string(target.component.typeId) + ":" +
			std::to_string(target.component.index) + ":" + std::to_string(target.component.generation);
	if (target.entity.IsValid())
		return "entity:" + std::to_string(target.entity.index) + ":" + std::to_string(target.entity.generation);
	if (!target.assetGuid.empty()) return "asset:" + target.assetGuid;
	return "global";
}

bool ApplyOne(
	const VansTimelineEvaluationOutput& output,
	const VansTimelineRuntimeAdapters& adapters,
	VansTimelineRestoreCallback& restore,
	std::string& error)
{
	const VansTimelineApplyContext context{
		output.writerId,
		output.propertyKey,
		output.blendMode,
		output.hierarchicalBias,
		output.priority
	};
	return std::visit([&](const auto& value) -> bool
	{
		using T = std::decay_t<decltype(value)>;
		if constexpr (std::is_same_v<T, VansTimelineTransformOutput>)
			return adapters.transform ? adapters.transform(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelinePropertyOutput>)
			return adapters.property ? adapters.property(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelineActivationOutput>)
			return adapters.activation ? adapters.activation(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelineConstraintOutput>)
			return adapters.constraint ? adapters.constraint(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelineAnimationOutput>)
			return adapters.animation ? adapters.animation(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelineAnimatorParameterOutput>)
			return adapters.animatorParameter ? adapters.animatorParameter(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelineBoneOverrideOutput>)
			return adapters.boneOverride ? adapters.boneOverride(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelineAudioOutput>)
			return adapters.audio ? adapters.audio(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelineMediaOutput>)
			return adapters.media ? adapters.media(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelineParticleOutput>)
			return adapters.particle ? adapters.particle(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelineCameraCutOutput>)
			return adapters.cameraCut ? adapters.cameraCut(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelineCameraPropertyOutput>)
			return adapters.cameraProperty ? adapters.cameraProperty(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelineCameraShakeOutput>)
			return adapters.cameraShake ? adapters.cameraShake(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelineFadePostProcessOutput>)
			return adapters.fadePostProcess ? adapters.fadePostProcess(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelineLightOutput>)
			return adapters.light ? adapters.light(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelineMaterialParameterOutput>)
			return adapters.materialParameter ? adapters.materialParameter(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelineMaterialSwitchOutput>)
			return adapters.materialSwitch ? adapters.materialSwitch(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelineUIOutput>)
			return adapters.ui ? adapters.ui(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelineEventOutput>)
			return adapters.event ? adapters.event(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelineSubTimelineOutput>)
			return adapters.subTimeline ? adapters.subTimeline(context, output.target, value, restore, error) : false;
		else if constexpr (std::is_same_v<T, VansTimelineTimeScaleOutput>)
			return adapters.timeScale ? adapters.timeScale(context, output.target, value, restore, error) : false;
		else return false;
	}, output.value);
}

bool RuntimeApplyOrder(const VansTimelineEvaluationOutput& left, const VansTimelineEvaluationOutput& right)
{
	if (left.hierarchicalBias != right.hierarchicalBias) return left.hierarchicalBias < right.hierarchicalBias;
	if (left.priority != right.priority) return left.priority < right.priority;
	if (left.trackOrder != right.trackOrder) return left.trackOrder < right.trackOrder;
	if (left.sourceTrackId != right.sourceTrackId) return left.sourceTrackId < right.sourceTrackId;
	return left.writerId < right.writerId;
}
}

void VansTimelineTrackAppliers::Apply(
	std::vector<VansTimelineEvaluationOutput>& outputs,
	const VansTimelineRuntimeAdapters& adapters,
	VansTimelinePreAnimatedState& preAnimatedState,
	VansTimelineDiagnostics& diagnostics)
{
	std::stable_sort(outputs.begin(), outputs.end(), RuntimeApplyOrder);
	for (const auto& output : outputs)
	{
		VansTimelineRestoreCallback restore;
		std::string error;
		if (!ApplyOne(output, adapters, restore, error))
		{
			if (error.empty()) error = "Required Timeline runtime adapter is unavailable";
			diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error, output.sourceTrackId,
				output.propertyKey, std::move(error) });
			continue;
		}
		if (restore && output.completionMode == VansTimelineCompletionMode::RestoreState)
			preAnimatedState.Capture(output.writerId,
				TargetIdentity(output.target) + ":" + output.propertyKey, std::move(restore));
	}
}
}
