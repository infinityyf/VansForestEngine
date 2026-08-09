#include "VansTimelinePlayer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Vans
{
bool VansTimelinePlayer::Load(
	std::shared_ptr<const VansCompiledTimeline> timeline,
	const VansRuntimeTimelineComponent& component,
	VansRuntimeWorld* world,
	VansEntityHandle owner,
	std::string writerId,
	std::string& error)
{
	error.clear();
	if (!timeline || !world || !owner.IsValid())
	{
		error = "Timeline Player requires a compiled asset, RuntimeWorld and living owner";
		m_State = VansTimelinePlayerState::Error;
		return false;
	}
	m_Timeline = std::move(timeline);
	m_Component = component;
	m_BindingResolver.BindWorld(world, owner);
	m_BindingResolver.SetOverrides(component.instance.bindingOverrides);
	m_CurrentTick = m_Timeline->Source().playbackRange.startTick;
	m_PlayRate = std::abs(component.instance.playbackSpeed);
	m_Direction = component.instance.playbackSpeed < 0.0 ? -1 : 1;
	m_LoopMode = component.instance.loopMode;
	m_LoopCount = component.instance.loopCount;
	m_CompletedLoops = 0;
	m_SubTickRemainder = 0.0;
	m_WriterId = std::move(writerId);
	m_Diagnostics.clear();
	m_Segments.clear();
	m_RestoreRequested = false;
	m_InitialEvaluationPending = false;
	m_SeekEvaluationPending = false;
	m_FiredOnceEvents.clear();
	m_State = VansTimelinePlayerState::Stopped;
	return true;
}

bool VansTimelinePlayer::Reload(
	std::shared_ptr<const VansCompiledTimeline> timeline,
	std::string& error)
{
	error.clear();
	if (!timeline || !m_Timeline)
	{
		error = "Timeline Player reload requires both current and replacement compiled assets";
		return false;
	}
	const auto oldRange = m_Timeline->Source().playbackRange;
	const auto newRange = timeline->Source().playbackRange;
	const double normalizedTime = oldRange.endTick > oldRange.startTick
		? std::clamp(static_cast<double>(m_CurrentTick - oldRange.startTick) /
			static_cast<double>(oldRange.endTick - oldRange.startTick), 0.0, 1.0)
		: 0.0;
	m_Timeline = std::move(timeline);
	m_CurrentTick = newRange.startTick + static_cast<VansTimelineTick>(std::llround(
		normalizedTime * static_cast<double>(newRange.endTick - newRange.startTick)));
	m_CurrentTick = std::clamp(m_CurrentTick, newRange.startTick, newRange.endTick);
	m_BindingResolver.Invalidate();
	m_Diagnostics.clear();
	m_Segments.clear();
	m_SubTickRemainder = 0.0;
	m_InitialEvaluationPending = false;
	m_RestoreRequested = false;
	if (m_State == VansTimelinePlayerState::Playing || m_State == VansTimelinePlayerState::Paused)
	{
		AddSegmentWithFences(m_CurrentTick, m_CurrentTick, VansTimelineEvaluationReason::Jump,
			VansTimelineSeekPolicy::ContinuousOnly, m_CompletedLoops);
		m_SeekEvaluationPending = true;
	}
	else m_SeekEvaluationPending = false;
	return true;
}

void VansTimelinePlayer::Play()
{
	if (!m_Timeline || m_State == VansTimelinePlayerState::Error || m_State == VansTimelinePlayerState::Unloaded)
		return;
	const bool starting = m_State == VansTimelinePlayerState::Stopped || m_State == VansTimelinePlayerState::Completed;
	if (starting) m_FiredOnceEvents.clear();
	if (m_State == VansTimelinePlayerState::Completed)
	{
		m_CurrentTick = m_Direction >= 0 ? m_Timeline->Source().playbackRange.startTick
			: m_Timeline->Source().playbackRange.endTick;
		m_CompletedLoops = 0;
	}
	m_State = VansTimelinePlayerState::Playing;
	if (starting) m_InitialEvaluationPending = true;
}

void VansTimelinePlayer::Pause()
{
	if (m_State == VansTimelinePlayerState::Playing) m_State = VansTimelinePlayerState::Paused;
}

void VansTimelinePlayer::Resume()
{
	if (m_State == VansTimelinePlayerState::Paused) m_State = VansTimelinePlayerState::Playing;
}

void VansTimelinePlayer::Stop()
{
	if (m_State == VansTimelinePlayerState::Unloaded || m_State == VansTimelinePlayerState::Stopped) return;
	m_State = VansTimelinePlayerState::Stopped;
	m_Segments.clear();
	RequestRestore();
}

void VansTimelinePlayer::Rewind()
{
	if (!m_Timeline) return;
	const auto range = m_Timeline->Source().playbackRange;
	m_CurrentTick = m_Direction >= 0 ? range.startTick : range.endTick;
	m_CompletedLoops = 0;
	m_SubTickRemainder = 0.0;
	m_Segments.clear();
	m_InitialEvaluationPending = false;
	m_SeekEvaluationPending = false;
}

void VansTimelinePlayer::SeekTicks(
	VansTimelineTick tick,
	VansTimelineSeekPolicy policy,
	VansTimelineEvaluationReason reason)
{
	if (!m_Timeline) return;
	const auto range = m_Timeline->Source().playbackRange;
	const VansTimelineTick target = std::clamp(tick, range.startTick, range.endTick);
	m_Segments.clear();
	m_SubTickRemainder = 0.0;
	AddSegmentWithFences(m_CurrentTick, target, reason, policy, m_CompletedLoops);
	m_CurrentTick = target;
	m_InitialEvaluationPending = false;
	m_SeekEvaluationPending = true;
}

void VansTimelinePlayer::SetPlayRate(double rate)
{
	if (!std::isfinite(rate)) return;
	m_PlayRate = std::abs(rate);
	if (rate < 0.0) m_Direction = -1;
}

void VansTimelinePlayer::SetDirection(int direction)
{
	m_Direction = direction < 0 ? -1 : 1;
}

void VansTimelinePlayer::SetLoop(VansTimelineLoopMode mode, std::int32_t loopCount)
{
	m_LoopMode = mode;
	m_LoopCount = loopCount;
}

void VansTimelinePlayer::SetParameter(std::string name, VansTimelineKeyValue value)
{
	if (!name.empty()) m_Component.instance.parameters[std::move(name)] = std::move(value);
}

void VansTimelinePlayer::SetEditorPreviewPolicy(
	bool enabled,
	bool safeEvents,
	bool includeSubTimelines)
{
	m_EditorPreview = enabled;
	m_PreviewSafeEvents = safeEvents;
	m_IncludeSubTimelines = includeSubTimelines;
}

void VansTimelinePlayer::AddSegmentWithFences(
	VansTimelineTick previousTick,
	VansTimelineTick currentTick,
	VansTimelineEvaluationReason reason,
	VansTimelineSeekPolicy seekPolicy,
	std::int32_t loopIteration)
{
	if (!m_Timeline) return;
	VansTimelineTick cursor = previousTick;
	if (currentTick >= previousTick)
	{
		for (VansTimelineTick fence : m_Timeline->DeterminismFences())
		{
			if (cursor < fence && fence < currentTick)
			{
				m_Segments.push_back({ cursor, fence, reason, seekPolicy, m_Direction, loopIteration });
				cursor = fence;
			}
		}
	}
	else
	{
		for (auto iterator = m_Timeline->DeterminismFences().rbegin(); iterator != m_Timeline->DeterminismFences().rend(); ++iterator)
		{
			if (currentTick < *iterator && *iterator < cursor)
			{
				m_Segments.push_back({ cursor, *iterator, reason, seekPolicy, m_Direction, loopIteration });
				cursor = *iterator;
			}
		}
	}
	m_Segments.push_back({ cursor, currentTick, reason, seekPolicy, m_Direction, loopIteration });
}

void VansTimelinePlayer::Advance(double deltaSeconds)
{
	if (!m_Timeline) return;
	if (m_SeekEvaluationPending)
	{
		m_SeekEvaluationPending = false;
		return;
	}
	m_Segments.clear();
	if (m_State != VansTimelinePlayerState::Playing) return;
	if (m_InitialEvaluationPending)
	{
		const VansTimelineTick previous = m_CurrentTick == std::numeric_limits<VansTimelineTick>::min()
			? m_CurrentTick : m_CurrentTick - 1;
		AddSegmentWithFences(previous, m_CurrentTick, VansTimelineEvaluationReason::Playback,
			VansTimelineSeekPolicy::AllEdges, m_CompletedLoops);
		m_InitialEvaluationPending = false;
	}
	if (m_Component.instance.updateMode == VansTimelineUpdateMode::Manual || deltaSeconds <= 0.0 || m_PlayRate <= 0.0)
		return;
	const auto& source = m_Timeline->Source();
	const double localTimeScale = VansTimelineEvaluator::EvaluateLocalTimeScale(*m_Timeline, m_CurrentTick);
	if (localTimeScale <= 0.0)
		return;
	const double exactTicks = deltaSeconds * m_PlayRate * localTimeScale *
		static_cast<double>(source.timebase.ticksPerSecond) + m_SubTickRemainder;
	VansTimelineTick remaining = static_cast<VansTimelineTick>(std::floor(exactTicks));
	m_SubTickRemainder = exactTicks - static_cast<double>(remaining);
	if (remaining <= 0) return;
	const auto range = source.playbackRange;
	while (remaining > 0 && m_State == VansTimelinePlayerState::Playing)
	{
		const VansTimelineTick boundary = m_Direction > 0 ? range.endTick : range.startTick;
		const VansTimelineTick distance = std::llabs(boundary - m_CurrentTick);
		const VansTimelineTick step = std::min(remaining, distance);
		const VansTimelineTick target = m_CurrentTick + static_cast<VansTimelineTick>(m_Direction) * step;
		AddSegmentWithFences(m_CurrentTick, target, VansTimelineEvaluationReason::Playback,
			VansTimelineSeekPolicy::AllEdges, m_CompletedLoops);
		m_CurrentTick = target;
		remaining -= step;
		if (m_CurrentTick != boundary) break;

		++m_CompletedLoops;
		const bool canLoop = m_LoopMode != VansTimelineLoopMode::None &&
			(m_LoopCount <= 0 || m_CompletedLoops < m_LoopCount);
		if (!canLoop)
		{
			m_State = VansTimelinePlayerState::Completed;
			if (source.defaultCompletionMode == VansTimelineCompletionMode::RestoreState)
				RequestRestore();
			break;
		}
		if (m_LoopMode == VansTimelineLoopMode::PingPong)
			m_Direction = -m_Direction;
		else
		{
			const VansTimelineTick wrap = m_Direction > 0 ? range.startTick : range.endTick;
			AddSegmentWithFences(boundary, wrap, VansTimelineEvaluationReason::LoopWrap,
				VansTimelineSeekPolicy::AllEdges, m_CompletedLoops);
			m_CurrentTick = wrap;
		}
		if (distance == 0 && remaining > 0 && range.endTick <= range.startTick)
			break;
	}
}

bool VansTimelinePlayer::UpdatePostScript(
	double deltaSeconds,
	std::vector<VansTimelineEvaluationOutput>& outputs)
{
	Advance(deltaSeconds);
	if (!m_Timeline || m_Segments.empty()) { outputs.clear(); return false; }
	VansTimelineEvaluator::Evaluate(*m_Timeline, VansTimelineEvaluationPhase::PostScript,
		m_Segments, m_Component.instance.parameters, m_BindingResolver, m_WriterId, outputs, m_Diagnostics);
	FilterPlaybackEvents(outputs);
	return true;
}

void VansTimelinePlayer::FilterPlaybackEvents(std::vector<VansTimelineEvaluationOutput>& outputs)
{
	outputs.erase(std::remove_if(outputs.begin(), outputs.end(), [&](const VansTimelineEvaluationOutput& output)
	{
		if (!m_IncludeSubTimelines && std::holds_alternative<VansTimelineSubTimelineOutput>(output.value))
			return true;
		const auto* event = std::get_if<VansTimelineEventOutput>(&output.value);
		if (!event)
			return false;
		if (m_EditorPreview && (!m_PreviewSafeEvents || !event->config.editorSafe))
			return true;
		if (!event->config.oncePerPlayback)
			return false;
		return !m_FiredOnceEvents.insert(output.propertyKey).second;
	}), outputs.end());
}

bool VansTimelinePlayer::UpdateCamera(std::vector<VansTimelineEvaluationOutput>& outputs)
{
	if (!m_Timeline || m_Segments.empty()) { outputs.clear(); return false; }
	VansTimelineEvaluator::Evaluate(*m_Timeline, VansTimelineEvaluationPhase::Camera,
		m_Segments, m_Component.instance.parameters, m_BindingResolver, m_WriterId, outputs, m_Diagnostics);
	m_Segments.clear();
	return true;
}

void VansTimelinePlayer::RequestRestore()
{
	if (m_Component.instance.restoreStateOnStop) m_RestoreRequested = true;
}

bool VansTimelinePlayer::ConsumeRestoreRequest()
{
	const bool requested = m_RestoreRequested;
	m_RestoreRequested = false;
	return requested;
}
}
