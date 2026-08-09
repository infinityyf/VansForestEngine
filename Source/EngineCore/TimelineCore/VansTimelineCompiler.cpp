#include "VansTimelineCompiler.h"

#include "VansTimelineSerialization.h"

#include <algorithm>

namespace Vans
{
namespace
{
VansTimelineEvaluationPhase TrackPhase(VansTimelineTrackType type)
{
	return type == VansTimelineTrackType::CameraCut || type == VansTimelineTrackType::CameraProperty
		|| type == VansTimelineTrackType::CameraShake
		? VansTimelineEvaluationPhase::Camera
		: VansTimelineEvaluationPhase::PostScript;
}

bool EvaluatesContinuous(VansTimelineTrackType type)
{
	switch (type)
	{
	case VansTimelineTrackType::Transform:
	case VansTimelineTrackType::Property:
	case VansTimelineTrackType::Constraint:
	case VansTimelineTrackType::AnimationClip:
	case VansTimelineTrackType::AnimatorParameter:
	case VansTimelineTrackType::BoneOverride:
	case VansTimelineTrackType::Audio:
	case VansTimelineTrackType::Media:
	case VansTimelineTrackType::Particle:
	case VansTimelineTrackType::CameraProperty:
	case VansTimelineTrackType::CameraShake:
	case VansTimelineTrackType::FadePostProcess:
	case VansTimelineTrackType::Light:
	case VansTimelineTrackType::MaterialParameter:
	case VansTimelineTrackType::TimeScale:
	case VansTimelineTrackType::Custom:
		return true;
	default:
		return false;
	}
}

bool EvaluatesEdges(VansTimelineTrackType type)
{
	switch (type)
	{
	case VansTimelineTrackType::Activation:
	case VansTimelineTrackType::AnimatorParameter:
	case VansTimelineTrackType::Audio:
	case VansTimelineTrackType::Media:
	case VansTimelineTrackType::Particle:
	case VansTimelineTrackType::CameraCut:
	case VansTimelineTrackType::MaterialSwitch:
	case VansTimelineTrackType::UIState:
	case VansTimelineTrackType::EventSignal:
	case VansTimelineTrackType::SubTimeline:
	case VansTimelineTrackType::Spawnable:
	case VansTimelineTrackType::SceneState:
	case VansTimelineTrackType::Custom:
		return true;
	default:
		return false;
	}
}

bool RuntimeOrder(const VansCompiledTimelineTrack& left, const VansCompiledTimelineTrack& right)
{
	if (left.source.priority != right.source.priority) return left.source.priority > right.source.priority;
	if (left.source.order != right.source.order) return left.source.order < right.source.order;
	return left.source.id < right.source.id;
}
}

VansTimelineCompileResult VansTimelineCompiler::Compile(
	const VansTimelineAsset& source,
	const VansTimelineCompileOptions& options)
{
	VansTimelineCompileResult result;
	VansTimelineAsset normalized = source;
	VansTimelineSerialization::Normalize(normalized);
	result.diagnostics = VansTimelineValidator::Validate(normalized, options.validation);
	if (VansTimelineValidator::HasErrors(result.diagnostics))
		return result;

	auto compiled = std::make_shared<VansCompiledTimeline>();
	compiled->m_Source = std::move(normalized);
	VansTimelineDiagnostics dependencyDiagnostics;
	if (!VansTimelineDependencyBuilder::BuildClosure(
		compiled->m_Source,
		options.dependencyLoader,
		compiled->m_Dependencies,
		dependencyDiagnostics))
	{
		result.diagnostics.insert(result.diagnostics.end(), dependencyDiagnostics.begin(), dependencyDiagnostics.end());
		return result;
	}
	result.diagnostics.insert(result.diagnostics.end(), dependencyDiagnostics.begin(), dependencyDiagnostics.end());

	for (const auto& track : compiled->m_Source.tracks)
	{
		if (!track.enabled || track.runtimeMuted) continue;
		VansCompiledTimelineTrack runtimeTrack;
		runtimeTrack.source = track;
		runtimeTrack.phase = TrackPhase(track.type);
		runtimeTrack.evaluatesContinuousValues = EvaluatesContinuous(track.type);
		runtimeTrack.evaluatesEdges = EvaluatesEdges(track.type);
		auto& destination = runtimeTrack.phase == VansTimelineEvaluationPhase::Camera
			? compiled->m_CameraTracks : compiled->m_PostScriptTracks;
		destination.push_back(std::move(runtimeTrack));
	}
	std::stable_sort(compiled->m_PostScriptTracks.begin(), compiled->m_PostScriptTracks.end(), RuntimeOrder);
	std::stable_sort(compiled->m_CameraTracks.begin(), compiled->m_CameraTracks.end(), RuntimeOrder);
	for (const auto& marker : compiled->m_Source.markers)
		if (marker.determinismFence) compiled->m_DeterminismFences.push_back(marker.tick);
	std::sort(compiled->m_DeterminismFences.begin(), compiled->m_DeterminismFences.end());
	compiled->m_DeterminismFences.erase(
		std::unique(compiled->m_DeterminismFences.begin(), compiled->m_DeterminismFences.end()),
		compiled->m_DeterminismFences.end());
	result.timeline = std::move(compiled);
	return result;
}
}
