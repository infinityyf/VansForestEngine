#pragma once

#include "VansTimelineEvaluator.h"

#include <memory>
#include <unordered_set>

namespace Vans
{
class VansTimelinePlayer
{
public:
	bool Load(
		std::shared_ptr<const VansCompiledTimeline> timeline,
		const VansRuntimeTimelineComponent& component,
		VansRuntimeWorld* world,
		VansEntityHandle owner,
		std::string writerId,
		std::string& error);
	bool Reload(std::shared_ptr<const VansCompiledTimeline> timeline, std::string& error);

	void Play();
	void Pause();
	void Resume();
	void Stop();
	void Rewind();
	void SeekTicks(VansTimelineTick tick, VansTimelineSeekPolicy policy, VansTimelineEvaluationReason reason);
	void SetPlayRate(double rate);
	void SetDirection(int direction);
	void SetLoop(VansTimelineLoopMode mode, std::int32_t loopCount);
	void SetParameter(std::string name, VansTimelineKeyValue value);
	void SetEditorPreviewPolicy(bool enabled, bool safeEvents, bool includeSubTimelines);

	bool UpdatePostScript(double deltaSeconds, std::vector<VansTimelineEvaluationOutput>& outputs);
	bool UpdateCamera(std::vector<VansTimelineEvaluationOutput>& outputs);

	VansTimelinePlayerState State() const { return m_State; }
	VansTimelineTick CurrentTick() const { return m_CurrentTick; }
	const std::string& WriterId() const { return m_WriterId; }
	const VansTimelineDiagnostics& Diagnostics() const { return m_Diagnostics; }
	bool ConsumeRestoreRequest();

private:
	void Advance(double deltaSeconds);
	void AddSegmentWithFences(
		VansTimelineTick previousTick,
		VansTimelineTick currentTick,
		VansTimelineEvaluationReason reason,
		VansTimelineSeekPolicy seekPolicy,
		std::int32_t loopIteration);
	void FilterPlaybackEvents(std::vector<VansTimelineEvaluationOutput>& outputs);
	void RequestRestore();

	std::shared_ptr<const VansCompiledTimeline> m_Timeline;
	VansRuntimeTimelineComponent m_Component;
	VansTimelineBindingResolver m_BindingResolver;
	VansTimelinePlayerState m_State = VansTimelinePlayerState::Unloaded;
	VansTimelineTick m_CurrentTick = 0;
	double m_PlayRate = 1.0;
	int m_Direction = 1;
	VansTimelineLoopMode m_LoopMode = VansTimelineLoopMode::None;
	std::int32_t m_LoopCount = 1;
	std::int32_t m_CompletedLoops = 0;
	double m_SubTickRemainder = 0.0;
	std::string m_WriterId;
	std::vector<VansTimelineEvaluationSegment> m_Segments;
	VansTimelineDiagnostics m_Diagnostics;
	bool m_RestoreRequested = false;
	bool m_InitialEvaluationPending = false;
	bool m_SeekEvaluationPending = false;
	std::unordered_set<std::string> m_FiredOnceEvents;
	bool m_EditorPreview = false;
	bool m_PreviewSafeEvents = false;
	bool m_IncludeSubTimelines = true;
};
}
