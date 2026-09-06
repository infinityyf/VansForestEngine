#include "VansTimelineSessionService.h"

#include "../Util/VansLog.h"

#include "Events/VansTimelineRuntimeEvents.h"
#include "../EventCore/VansEventBus.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cmath>

namespace Vans
{
namespace
{
VansEventLane EventLane(const std::string& name)
{
	if (name == "Script") return VansEventLane::Script;
	if (name == "MainThread") return VansEventLane::MainThread;
	if (name == "Editor") return VansEventLane::Editor;
	if (name == "Diagnostics") return VansEventLane::Diagnostics;
	if (name == "RenderPrep") return VansEventLane::RenderPrep;
	return VansEventLane::GameLogic;
}

bool PolicyAllows(
	const std::string& policy,
	const VansTimelineTraversalSegment& segment,
	const VansTimelineId& id,
	std::unordered_set<VansTimelineId>& once,
	std::unordered_set<std::string>& perLoop)
{
	if (policy == "ForwardOnly" && segment.playbackDirection < 0) return false;
	if (policy == "BackwardOnly" && segment.playbackDirection > 0) return false;
	if (policy == "ExactSeek" && segment.seekPolicy != VansTimelineSeekPolicy::ExactTick) return false;
	if (policy == "OncePerPlayback") return once.insert(id).second;
	if (policy == "OncePerLoop")
		return perLoop.insert(id + "#" + std::to_string(segment.loopIteration)).second;
	return true;
}
}

VansTimelineSessionService::VansTimelineSessionService(
	VansTimelineClockRegistry& clocks,
	VansTimelineApplierRegistry& appliers,
	VansPayloadSchemaRegistry* payloads)
	: m_Clocks(clocks), m_Appliers(appliers), m_Payloads(payloads)
{
	m_PreAnimated.BindAppliers(&m_Appliers);
}

void VansTimelineSessionService::PublishLifecycle(
	Session& session,
	VansTimelineLifecycleKind kind,
	VansTimelineEndReason reason)
{
	VansTimelineSessionLifecycleEvent event;
	event.kind = kind;
	event.context = { session.handle, session.root, session.parent, session.currentTick,
		session.timeline ? session.timeline->ContentHash() : 0, session.correlation,
		session.eventSequence++, reason };
	VansEventBus::Get().Enqueue(std::move(event), VansEventLane::GameLogic);
}

void VansTimelineSessionService::PublishCrossedMarkers(Session& session)
{
	for (const VansTimelineTraversalSegment& segment : session.traversal)
		for (const VansTimelineMarker& marker : session.timeline->Markers())
		{
			if (!marker.runtimeObservable ||
				(session.kind == VansTimelineSessionKind::Preview &&
					(!session.previewSafeEvents || !marker.editorSafe)) ||
				!VansTimelineEvaluator::Crossed(segment, marker.tick) ||
				!PolicyAllows(marker.firePolicy, segment, marker.id, session.firedOnce, session.firedPerLoop))
				continue;
			if (!m_Payloads || !marker.payloadType)
			{
				AddDiagnostic(session.handle, "Timeline.PayloadSchemaMissing",
					"Observable Timeline marker has no registered payload schema");
				continue;
			}
			std::string error;
			if (!m_Payloads->Validate(marker.payloadType, marker.payload, error))
			{
				AddDiagnostic(session.handle, "Timeline.MarkerPayloadInvalid", error);
				continue;
			}
			VansTimelineMarkerReachedEvent event;
			event.context = { session.handle, session.root, session.parent, marker.tick,
				session.timeline->ContentHash(), session.correlation, session.eventSequence++,
				VansTimelineEndReason::Stopped };
			event.markerId = marker.id;
			event.category = marker.category;
			event.payloadType = marker.payloadType;
			event.payload = marker.payload;
			VansEventBus::Get().Enqueue(std::move(event), VansEventLane::GameLogic);
		}
}

void VansTimelineSessionService::PublishSignalOutputs(Session& session)
{
	const VansTimelineTrackTypeId signalType = VansMakeStableId<VansTimelineTrackTypeTag>(TimelineNames::EventSignal);
	const VansTimelineCompiledDataReader reader(session.timeline->CompiledBytes(), session.timeline->CompiledValues());
	for (const VansTimelineEvaluationOutput& output : session.outputs)
	{
		if (output.phase != VansTimelineEvaluationPhase::PostScript) continue;
		const auto& tracks = session.timeline->Tracks(output.phase);
		if (output.trackIndex >= tracks.size()) continue;
		const VansCompiledTimelineTrack& track = tracks[output.trackIndex];
		if (track.typeId != signalType || output.sectionIndex >= track.sections.size()) continue;
		const VansTimelineSampleOutput* sample = output.payload.As<VansTimelineSampleOutput>();
		if (!sample || !sample->entered) continue;
		const auto stringAt = [&](std::size_t slot) -> std::string
		{
			const VansTimelineValue* value = reader.ValueAt(track.sections[output.sectionIndex].extensionData, slot);
			const auto* text = value ? std::get_if<std::string>(value) : nullptr;
			return text ? *text : std::string{};
		};
		const VansTimelineValue* payloadValue = reader.ValueAt(track.sections[output.sectionIndex].extensionData, 2);
		const auto* payload = payloadValue ? std::get_if<VansTimelineStructValue>(payloadValue) : nullptr;
		const VansTimelineValue* editorSafeValue = reader.ValueAt(track.sections[output.sectionIndex].extensionData, 6);
		const auto* editorSafe = editorSafeValue ? std::get_if<bool>(editorSafeValue) : nullptr;
		if (session.kind == VansTimelineSessionKind::Preview &&
			(!session.previewSafeEvents || !editorSafe || !*editorSafe)) continue;
		const std::string signalId = stringAt(0);
		const std::string policy = stringAt(5);
		const VansTimelineTraversalSegment* crossing = nullptr;
		for (const auto& segment : session.traversal)
			if (VansTimelineEvaluator::Crossed(segment, track.sections[output.sectionIndex].startTick))
			{ crossing = &segment; break; }
		if (!crossing || !PolicyAllows(policy, *crossing, signalId,
			session.firedOnce, session.firedPerLoop)) continue;
		const VansTimelinePayloadTypeId payloadType =
			VansMakeStableId<VansTimelinePayloadTypeTag>(stringAt(1));
		if (!m_Payloads || !payloadType || !payload)
		{
			AddDiagnostic(session.handle, "Timeline.PayloadSchemaMissing",
				"Timeline signal has no registered payload schema");
			continue;
		}
		std::string error;
		if (!m_Payloads->Validate(payloadType, payload->value, error))
		{
			AddDiagnostic(session.handle, "Timeline.SignalPayloadInvalid", error);
			continue;
		}
		VansTimelineSignalFiredEvent event;
		event.context = { session.handle, session.root, session.parent, sample->timelineTick,
			session.timeline->ContentHash(), session.correlation, session.eventSequence++,
			VansTimelineEndReason::Stopped };
		event.signalId = signalId;
		event.payloadType = payloadType;
		event.payload = payload->value;
		const VansEventLane lane = EventLane(stringAt(3));
		if (stringAt(4) == "NextFrame") VansEventBus::Get().EnqueueNextFrame(std::move(event), lane);
		else VansEventBus::Get().Enqueue(std::move(event), lane);
	}
}

void VansTimelineSessionService::ReconcileChildSessions(Session& session)
{
	const VansTimelineTrackTypeId childType = VansMakeStableId<VansTimelineTrackTypeTag>(TimelineNames::SubTimeline);
	const VansTimelineCompiledDataReader reader(session.timeline->CompiledBytes(), session.timeline->CompiledValues());
	std::unordered_set<VansTimelineId> active;
	if (session.includeSubTimelines)
		for (const VansTimelineEvaluationOutput& output : session.outputs)
		{
			const auto& tracks = session.timeline->Tracks(output.phase);
			if (output.trackIndex >= tracks.size()) continue;
			const VansCompiledTimelineTrack& track = tracks[output.trackIndex];
			if (track.typeId != childType || output.sectionIndex >= track.sections.size()) continue;
			const auto* sample = output.payload.As<VansTimelineSampleOutput>();
			if (!sample || !sample->active) continue;
			const VansCompiledTimelineSection& section = track.sections[output.sectionIndex];
			const VansTimelineValue* policyValue = reader.ValueAt(section.extensionData, 0);
			const auto* configuredPolicy = policyValue ? std::get_if<std::string>(policyValue) : nullptr;
			const std::string policy = configuredPolicy ? *configuredPolicy : "FailParent";
			auto handleFailure = [&](std::string code, std::string message)
			{
				if (policy != "Ignore") AddDiagnostic(session.handle, std::move(code), message);
				if (policy == "FailParent")
					FailSession(session, "Timeline.SubTimelineFailed", std::move(message));
				else session.failedChildSections.insert(section.id);
			};
			active.insert(section.id);
			if (session.failedChildSections.find(section.id) != session.failedChildSections.end()) continue;
			auto childIt = session.childSessions.find(section.id);
			if (childIt == session.childSessions.end())
			{
				auto childAsset = session.timeline->ChildTimeline(section.assetGuid, section.assetPath);
				if (!childAsset)
				{
					handleFailure("Timeline.SubTimelineMissing",
						"Compiled SubTimeline dependency is unavailable");
					if (session.state == VansTimelinePlayerState::Error) return;
					continue;
				}
				VansTimelineSessionDesc desc;
				desc.kind = VansTimelineSessionKind::SubTimeline;
				desc.timeline = std::move(childAsset);
				desc.world = session.world;
				desc.owner = session.owner;
				desc.scope = session.scope;
				desc.parent = session.handle;
				desc.root = session.root;
				desc.hierarchicalBias = session.hierarchicalBias + track.priority;
				desc.clockType = std::string(TimelineClockNames::Manual);
				desc.inheritedBindingOverrides = session.bindingOverrides;
				desc.inheritedRuntimeBindings = session.runtimeBindings;
				for (const VansCompiledTimelineParameter& parameter : session.timeline->Parameters())
					if (const VansTimelineValue* value = session.parameters.Get(parameter.slot))
						if (desc.timeline->ParameterSlot(parameter.id) != VansInvalidTimelineSlot)
							desc.inheritedParameterOverrides.push_back({ parameter.id, *value });
				desc.restoreStateOnStop = session.restoreStateOnStop;
				desc.previewSafeEvents = session.previewSafeEvents;
				desc.includeSubTimelines = true;
				desc.debugLabel = section.id;
				const VansTimelineSessionResult created = Create(desc);
				if (!created)
				{
					handleFailure("Timeline.SubTimelineCreateFailed", created.error);
					if (session.state == VansTimelinePlayerState::Error) return;
					continue;
				}
				childIt = session.childSessions.emplace(section.id, created.handle).first;
			}
			if (Session* child = Resolve(childIt->second))
			{
				const std::size_t firstDiagnostic = m_Diagnostics.size();
				const VansTimelineTick tick = std::clamp(sample->localTick,
					child->timeline->PlaybackRange().startTick, child->timeline->PlaybackRange().endTick);
				child->clock->SetAbsolute(child->clockHandle, tick, true);
				BuildTraversal(*child, tick, VansTimelineEvaluationReason::Playback,
					sample->rebuild ? VansTimelineSeekPolicy::RebuildActive : VansTimelineSeekPolicy::AllEdges,
					child->clock->Sample(child->clockHandle).discontinuitySerial, false);
				Evaluate(childIt->second, VansTimelineEvaluationPhase::PostScript);
				if (HasNewSessionError(childIt->second, firstDiagnostic))
				{
					if (policy == "FailParent")
					{
						FailSession(session, "Timeline.SubTimelineFailed",
							"Child Timeline evaluation failed");
						return;
					}
					const VansTimelineSessionHandle failedChild = childIt->second;
					if (policy == "StopChild") Stop(failedChild, VansTimelineEndReason::Failed);
					Release(failedChild);
					session.childSessions.erase(childIt);
					session.failedChildSections.insert(section.id);
				}
			}
		}
	for (auto iterator = session.childSessions.begin(); iterator != session.childSessions.end();)
	{
		if (active.find(iterator->first) != active.end()) { ++iterator; continue; }
		Release(iterator->second);
		iterator = session.childSessions.erase(iterator);
	}
	for (auto iterator = session.failedChildSections.begin(); iterator != session.failedChildSections.end();)
		if (active.find(*iterator) == active.end()) iterator = session.failedChildSections.erase(iterator);
		else ++iterator;
}

bool VansTimelineSessionService::HasNewSessionError(
	VansTimelineSessionHandle handle,
	std::size_t firstDiagnostic) const
{
	for (std::size_t index = firstDiagnostic; index < m_Diagnostics.size(); ++index)
		if (m_Diagnostics[index].severity == VansTimelineDiagnosticSeverity::Error &&
			m_Diagnostics[index].session == handle) return true;
	return false;
}

void VansTimelineSessionService::FailSession(
	Session& session,
	std::string code,
	std::string message)
{
	if (session.state == VansTimelinePlayerState::Error) return;
	AddDiagnostic(session.handle, std::move(code), std::move(message));
	std::vector<VansTimelineSessionHandle> children;
	for (const auto& [id, child] : session.childSessions) { (void)id; children.push_back(child); }
	for (VansTimelineSessionHandle child : children) Stop(child, VansTimelineEndReason::Failed);
	ReleaseSessionWriters(session);
	session.traversal.clear();
	session.clock->SetPaused(session.clockHandle, true);
	session.state = VansTimelinePlayerState::Error;
	PublishLifecycle(session, VansTimelineLifecycleKind::Failed, VansTimelineEndReason::Failed);
}

VansTimelineSessionService::Session* VansTimelineSessionService::Resolve(VansTimelineSessionHandle handle)
{
	return m_Sessions.Resolve(handle);
}

const VansTimelineSessionService::Session* VansTimelineSessionService::Resolve(VansTimelineSessionHandle handle) const
{
	return m_Sessions.Resolve(handle);
}

void VansTimelineSessionService::AddDiagnostic(
	VansTimelineSessionHandle handle,
	std::string code,
	std::string message)
{
	m_Diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error, std::move(code), {}, {}, {},
		std::move(message), handle });
}

VansTimelineSessionResult VansTimelineSessionService::Create(const VansTimelineSessionDesc& desc)
{
	if (!desc.timeline) return { false, {}, "Timeline.SessionAssetMissing" };
	if (desc.parent.IsValid() && !Resolve(desc.parent)) return { false, {}, "Timeline.InvalidParentSession" };
	Session session;
	session.kind = desc.kind;
	session.parent = desc.parent;
	session.root = desc.root;
	session.timeline = desc.timeline;
	session.playRate = std::abs(desc.playRate);
	session.direction = desc.playRate < 0.0 ? -1 : 1;
	session.loopMode = desc.loopMode;
	session.loopCount = std::max(1, desc.loopCount);
	session.hierarchicalBias = desc.hierarchicalBias;
	session.restoreStateOnStop = desc.restoreStateOnStop;
	session.previewSafeEvents = desc.previewSafeEvents;
	session.includeSubTimelines = desc.includeSubTimelines;
	session.debugLabel = desc.debugLabel;
	session.world = desc.world;
	session.owner = desc.owner;
	session.scope = desc.scope;
	session.bindingOverrides = desc.bindingOverrides;
	session.bindingOverrides.insert(session.bindingOverrides.begin(),
		desc.inheritedBindingOverrides.begin(), desc.inheritedBindingOverrides.end());
	session.runtimeBindings = desc.runtimeBindings;
	session.runtimeBindings.insert(session.runtimeBindings.begin(),
		desc.inheritedRuntimeBindings.begin(), desc.inheritedRuntimeBindings.end());
	session.correlation = m_NextCorrelation++;
	session.currentTick = desc.timeline->PlaybackRange().startTick;
	session.bindings.BindWorld(desc.world, desc.owner);
	session.bindings.SetOverrides(session.bindingOverrides);
	session.bindings.SetRuntimeBindings(session.runtimeBindings);
	std::vector<VansTimelineParameterOverride> parameters = desc.inheritedParameterOverrides;
	parameters.insert(parameters.end(), desc.parameterOverrides.begin(), desc.parameterOverrides.end());
	if (!session.parameters.Initialize(*desc.timeline, parameters, m_Diagnostics))
		return { false, {}, "Timeline.ParameterInitializationFailed" };
	if (desc.externalClock)
	{
		session.clock = desc.externalClock;
		session.clockHandle = desc.externalClockHandle;
		session.drivesClock = false;
	}
	else
	{
		const VansTimelineClockTypeId clockType = VansMakeStableId<VansTimelineClockTag>(desc.clockType);
		session.clock = m_Clocks.Resolve(clockType);
		if (!session.clock) return { false, {}, "Timeline.ClockMissing" };
		session.ownedClock = std::dynamic_pointer_cast<VansTimelineOwnedClockSource>(session.clock);
		if (!session.ownedClock) return { false, {}, "Timeline.ClockRequiresExternalHandle" };
		session.clockHandle = session.ownedClock->Create(session.currentTick);
		session.drivesClock = true;
	}
	if (!session.clockHandle.IsValid()) return { false, {}, "Timeline.ClockHandleInvalid" };
	const VansTimelineSessionHandle handle = m_Sessions.Emplace(std::move(session));
	Session* stored = Resolve(handle);
	stored->handle = handle;
	if (!stored->root.IsValid()) stored->root = stored->parent.IsValid() ? Resolve(stored->parent)->root : handle;
	PublishLifecycle(*stored, VansTimelineLifecycleKind::SessionCreated);
	return { true, handle, {} };
}

bool VansTimelineSessionService::Play(VansTimelineSessionHandle handle, bool restart)
{
	Session* session = Resolve(handle);
	if (!session) { AddDiagnostic(handle, "Timeline.InvalidSession", "Play used a stale Timeline session handle"); return false; }
	const bool beginsPlayback = restart || session->state == VansTimelinePlayerState::Stopped ||
		session->state == VansTimelinePlayerState::Completed;
	if (restart)
	{
		session->currentTick = session->direction > 0 ? session->timeline->PlaybackRange().startTick : session->timeline->PlaybackRange().endTick;
		session->clock->SetAbsolute(session->clockHandle, session->currentTick, true);
		session->loopIteration = 0;
	}
	if (beginsPlayback)
	{
		session->includeCurrentTickOnNextTraversal = true;
		session->firedOnce.clear();
		session->firedPerLoop.clear();
	}
	session->clock->SetPaused(session->clockHandle, false);
	session->state = VansTimelinePlayerState::Playing;
	PublishLifecycle(*session, VansTimelineLifecycleKind::Started);
	return true;
}

bool VansTimelineSessionService::Pause(VansTimelineSessionHandle handle)
{
	Session* session = Resolve(handle);
	if (!session || session->state != VansTimelinePlayerState::Playing) return false;
	session->clock->SetPaused(session->clockHandle, true);
	session->state = VansTimelinePlayerState::Paused;
	for (const auto& [id, child] : session->childSessions) { (void)id; Pause(child); }
	PublishLifecycle(*session, VansTimelineLifecycleKind::Paused);
	return true;
}

bool VansTimelineSessionService::Resume(VansTimelineSessionHandle handle)
{
	Session* session = Resolve(handle);
	if (!session || session->state != VansTimelinePlayerState::Paused) return false;
	session->clock->SetPaused(session->clockHandle, false);
	session->state = VansTimelinePlayerState::Playing;
	for (const auto& [id, child] : session->childSessions) { (void)id; Resume(child); }
	PublishLifecycle(*session, VansTimelineLifecycleKind::Resumed);
	return true;
}

void VansTimelineSessionService::ReleaseSessionWriters(Session& session)
{
	std::vector<VansTimelineWriterHandle> writers;
	writers.insert(writers.end(), session.postScriptWriters.begin(), session.postScriptWriters.end());
	writers.insert(writers.end(), session.cameraWriters.begin(), session.cameraWriters.end());
	for (VansTimelineWriterHandle writer : writers)
	{
		const VansTimelineWriterDesc* desc = m_Writers.Resolve(writer);
		const bool restore = session.restoreStateOnStop && desc &&
			desc->completion != VansTimelineCompletionMode::KeepState;
		const bool releasedNow = m_PreAnimated.ReleaseWriter(writer, restore);
		if (releasedNow) m_Appliers.ReleaseWriter(writer);
		m_Writers.Release(writer);
	}
	session.postScriptWriters.clear();
	session.cameraWriters.clear();
}

bool VansTimelineSessionService::Stop(VansTimelineSessionHandle handle, VansTimelineEndReason reason)
{
	Session* session = Resolve(handle);
	if (!session) return false;
	std::vector<VansTimelineSessionHandle> children;
	for (const auto& [id, child] : session->childSessions) { (void)id; children.push_back(child); }
	for (VansTimelineSessionHandle child : children) Stop(child, reason);
	ReleaseSessionWriters(*session);
	session->traversal.clear();
	session->clock->SetPaused(session->clockHandle, true);
	session->state = VansTimelinePlayerState::Stopped;
	PublishLifecycle(*session, VansTimelineLifecycleKind::Stopped, reason);
	return true;
}

bool VansTimelineSessionService::Seek(
	VansTimelineSessionHandle handle,
	VansTimelineTick tick,
	VansTimelineSeekPolicy policy)
{
	Session* session = Resolve(handle);
	if (!session) return false;
	tick = std::clamp(tick, session->timeline->PlaybackRange().startTick, session->timeline->PlaybackRange().endTick);
	session->clock->SetAbsolute(session->clockHandle, tick, true);
	BuildTraversal(*session, tick, VansTimelineEvaluationReason::Jump, policy,
		session->clock->Sample(session->clockHandle).discontinuitySerial, true);
	PublishLifecycle(*session, VansTimelineLifecycleKind::Seeked);
	return true;
}

bool VansTimelineSessionService::ConfigurePlayback(
	VansTimelineSessionHandle handle,
	double playRate,
	int direction,
	VansTimelineLoopMode loopMode,
	std::int32_t loopCount)
{
	Session* session = Resolve(handle);
	if (!session || !std::isfinite(playRate) || playRate <= 0.0 || direction == 0 || loopCount < 1)
		return false;
	session->playRate = playRate;
	session->direction = direction < 0 ? -1 : 1;
	session->loopMode = loopMode;
	session->loopCount = loopCount;
	return true;
}

bool VansTimelineSessionService::SetParameter(
	VansTimelineSessionHandle handle,
	VansTimelineParameterId id,
	const VansTimelineValue& value)
{
	Session* session = Resolve(handle);
	return session && session->parameters.Set(*session->timeline, id, value);
}

std::optional<VansTimelineSessionView> VansTimelineSessionService::Query(
	VansTimelineSessionHandle handle) const
{
	const Session* session = Resolve(handle);
	if (!session) return std::nullopt;
	return VansTimelineSessionView{ handle, session->kind, session->parent, session->root,
		session->state, session->currentTick, session->loopIteration,
		session->clock->Sample(session->clockHandle).discontinuitySerial };
}

bool VansTimelineSessionService::Release(VansTimelineSessionHandle handle)
{
	Session* session = Resolve(handle);
	if (!session) return false;
	std::vector<VansTimelineSessionHandle> children;
	m_Sessions.ForEach([&](VansTimelineSessionHandle childHandle, const Session& child)
	{
		if (child.parent == handle) children.push_back(childHandle);
	});
	for (VansTimelineSessionHandle child : children) Release(child);
	ReleaseSessionWriters(*session);
	session->clock->SetPaused(session->clockHandle, true);
	session->state = VansTimelinePlayerState::Stopped;
	if (session->ownedClock) session->ownedClock->Release(session->clockHandle);
	PublishLifecycle(*session, VansTimelineLifecycleKind::Released, VansTimelineEndReason::Released);
	return m_Sessions.Release(handle);
}

void VansTimelineSessionService::BuildTraversal(
	Session& session,
	VansTimelineTick requestedTick,
	VansTimelineEvaluationReason reason,
	VansTimelineSeekPolicy policy,
	std::uint64_t clockSerial,
	bool discontinuity)
{
	const VansTimelineTick previous = session.currentTick;
	const VansTimelineTick start = session.timeline->PlaybackRange().startTick;
	const VansTimelineTick end = session.timeline->PlaybackRange().endTick;
	session.traversal.clear();
	bool includePreviousEndpoint = session.includeCurrentTickOnNextTraversal;
	session.includeCurrentTickOnNextTraversal = false;
	auto add = [&](VansTimelineTick from, VansTimelineTick to,
		VansTimelineEvaluationReason segmentReason, bool includeFrom = false)
	{
		includeFrom = includeFrom || includePreviousEndpoint;
		includePreviousEndpoint = false;
		if (from == to && !includeFrom && policy != VansTimelineSeekPolicy::ExactTick &&
			policy != VansTimelineSeekPolicy::RebuildActive) return;
		for (VansTimelineTick fence : session.timeline->DeterminismFences())
		{
			if ((to >= from && from < fence && fence < to) || (to < from && to < fence && fence < from))
			{
				session.traversal.push_back({ from, fence, segmentReason, policy, to >= from ? 1 : -1,
					session.loopIteration, clockSerial, discontinuity, includeFrom });
				from = fence;
				includeFrom = false;
			}
		}
		session.traversal.push_back({ from, to, segmentReason, policy, to >= from ? 1 : -1,
			session.loopIteration, clockSerial, discontinuity, includeFrom });
	};

	VansTimelineTick cursor = previous;
	VansTimelineTick next = requestedTick;
	VansTimelineEvaluationReason segmentReason = reason;
	while ((session.direction > 0 && next > end) || (session.direction < 0 && next < start))
	{
		const bool forward = session.direction > 0;
		const VansTimelineTick boundary = forward ? end : start;
		const VansTimelineTick overshoot = forward ? next - end : start - next;
		add(cursor, boundary, segmentReason);
		if (session.loopMode == VansTimelineLoopMode::None || ++session.loopIteration >= session.loopCount)
		{
			session.state = VansTimelinePlayerState::Completed;
			next = boundary;
			cursor = boundary;
			PublishLifecycle(session, VansTimelineLifecycleKind::Completed,
				VansTimelineEndReason::Completed);
			break;
		}
		PublishLifecycle(session, VansTimelineLifecycleKind::Looped);
		segmentReason = VansTimelineEvaluationReason::LoopWrap;
		if (session.loopMode == VansTimelineLoopMode::PingPong)
		{
			session.direction = -session.direction;
			cursor = boundary;
			next = forward ? end - overshoot : start + overshoot;
		}
		else
		{
			cursor = forward ? start : end;
			next = forward ? start + overshoot : end - overshoot;
			includePreviousEndpoint = true;
		}
	}
	if (cursor != next || includePreviousEndpoint || policy == VansTimelineSeekPolicy::ExactTick ||
		policy == VansTimelineSeekPolicy::RebuildActive)
		add(cursor, next, segmentReason);
	session.currentTick = std::clamp(next, start, end);
}

void VansTimelineSessionService::Advance(VansTimelineSessionHandle handle, double deltaSeconds)
{
	Session* session = Resolve(handle);
	if (!session || session->state != VansTimelinePlayerState::Playing) return;
	double localClockRate = 1.0;
	for (const VansCompiledTimelineTrack& track :
		session->timeline->Tracks(VansTimelineEvaluationPhase::PostScript))
		if (track.clockRate) localClockRate *= track.clockRate(
			*session->timeline, track, session->currentTick);
	if (!std::isfinite(localClockRate) || localClockRate < 0.0)
	{
		FailSession(*session, "Timeline.ClockRateInvalid",
			"A Timeline clock-rate extension returned an invalid rate");
		return;
	}
	const long double scaled = static_cast<long double>(deltaSeconds) *
		session->timeline->Timebase().ticksPerSecond * session->playRate * localClockRate +
		session->subTickRemainder;
	const VansTimelineTick delta = static_cast<VansTimelineTick>(std::trunc(scaled));
	session->subTickRemainder = static_cast<double>(scaled - delta);
	if (session->drivesClock)
		session->clock->Advance(session->clockHandle, delta * session->direction);
	const VansTimelineClockSample sample = session->clock->Sample(session->clockHandle);
	BuildTraversal(*session, sample.absoluteTick,
		sample.discontinuity ? VansTimelineEvaluationReason::ClockCorrection : VansTimelineEvaluationReason::Playback,
		sample.discontinuity ? VansTimelineSeekPolicy::RebuildActive : VansTimelineSeekPolicy::AllEdges,
		sample.discontinuitySerial, sample.discontinuity);
	if (session->drivesClock && sample.absoluteTick != session->currentTick)
		session->clock->SetAbsolute(session->clockHandle, session->currentTick, false);
}

void VansTimelineSessionService::ReconcileWriters(
	Session& session,
	VansTimelineEvaluationPhase phase)
{
	auto& activeWriters = phase == VansTimelineEvaluationPhase::Camera
		? session.cameraWriters : session.postScriptWriters;
	std::unordered_set<VansTimelineWriterHandle, WriterHandleHash> current;
	for (const VansTimelineEvaluationOutput& output : session.outputs)
		if (output.writer.IsValid() && output.retainsPreAnimatedState) current.insert(output.writer);
	for (VansTimelineWriterHandle writer : activeWriters)
	{
		if (current.find(writer) != current.end()) continue;
		const VansTimelineWriterDesc* desc = m_Writers.Resolve(writer);
		const bool releasedNow = m_PreAnimated.ReleaseWriter(writer, desc &&
			desc->completion != VansTimelineCompletionMode::KeepState);
		if (releasedNow) m_Appliers.ReleaseWriter(writer);
		m_Writers.Release(writer);
	}
	activeWriters = std::move(current);
}

void VansTimelineSessionService::DeactivateWritersBeforeApply(
	Session& session,
	VansTimelineEvaluationPhase phase)
{
	auto& activeWriters = phase == VansTimelineEvaluationPhase::Camera
		? session.cameraWriters : session.postScriptWriters;
	std::unordered_set<VansTimelineWriterHandle, WriterHandleHash> retained;
	for (const VansTimelineEvaluationOutput& output : session.outputs)
	{
		if (!output.retainsPreAnimatedState) continue;
		const VansTimelineWriterHandle writer = m_Writers.Find(
			output.session, output.trackIndex, output.sectionIndex, output.typeId);
		if (writer.IsValid()) retained.insert(writer);
	}
	for (VansTimelineWriterHandle writer : activeWriters)
	{
		if (retained.find(writer) != retained.end()) continue;
		const VansTimelineWriterDesc* desc = m_Writers.Resolve(writer);
		const bool releasedNow = m_PreAnimated.ReleaseWriter(writer, desc &&
			desc->completion != VansTimelineCompletionMode::KeepState);
		if (releasedNow) m_Appliers.ReleaseWriter(writer);
		m_Writers.Release(writer);
	}
	activeWriters = std::move(retained);
}

void VansTimelineSessionService::Evaluate(
	VansTimelineSessionHandle handle,
	VansTimelineEvaluationPhase phase)
{
	Session* session = Resolve(handle);
	if (!session || session->state == VansTimelinePlayerState::Error) return;
	if (session->traversal.empty())
	{
		if (session->state != VansTimelinePlayerState::Playing &&
			session->state != VansTimelinePlayerState::Paused) return;
		const VansTimelineClockSample sample = session->clock->Sample(session->clockHandle);
		session->traversal.push_back({ session->currentTick, session->currentTick,
			VansTimelineEvaluationReason::Playback, VansTimelineSeekPolicy::ContinuousOnly,
			session->direction, session->loopIteration, sample.discontinuitySerial, false, false });
	}
	const std::size_t firstEvaluationDiagnostic = m_Diagnostics.size();
	VansTimelineEvaluator::Evaluate(*session->timeline, phase, session->traversal,
		session->parameters, session->bindings, session->handle, session->root,
		session->hierarchicalBias, session->arena, session->outputs, m_Diagnostics);
	for (std::size_t index = firstEvaluationDiagnostic; index < m_Diagnostics.size(); ++index)
		if (!m_Diagnostics[index].session.IsValid()) m_Diagnostics[index].session = session->handle;
	for (VansTimelineEvaluationOutput& output : session->outputs)
	{
		output.sessionKind = session->kind;
		const VansCompiledTimelineTrack* track = output.trackIndex < session->timeline->Tracks(output.phase).size()
			? &session->timeline->Tracks(output.phase)[output.trackIndex] : nullptr;
		output.applierSlot = m_Appliers.SlotOf(output.typeId);
		if (track && output.applierSlot == VansInvalidTimelineApplierSlot && !track->outputApplierRequired)
			output.retainsPreAnimatedState = false;
	}
	DeactivateWritersBeforeApply(*session, phase);
	m_Appliers.Apply(*session->timeline, session->outputs, session->bindings, session->scope,
		m_Writers, m_PreAnimated, m_Diagnostics);
	// 单阶段应用以 Session 为事务边界。先接管本次求值创建的全部 writer，
	// 再检查错误，确保部分应用失败时 FailSession 能完整恢复现场。
	ReconcileWriters(*session, phase);
	if (HasNewSessionError(session->handle, firstEvaluationDiagnostic))
	{
		VANS_LOG_ERROR("[Timeline] Session evaluation failed: " << session->debugLabel <<
			" phase=" << (phase == VansTimelineEvaluationPhase::Camera ? "Camera" : "PostScript"));
		FailSession(*session, "Timeline.SessionEvaluationFailed",
			"Timeline output evaluation or application failed");
		return;
	}
	if (phase == VansTimelineEvaluationPhase::PostScript)
	{
		PublishCrossedMarkers(*session);
		PublishSignalOutputs(*session);
		ReconcileChildSessions(*session);
		if (session->state == VansTimelinePlayerState::Error) return;
	}
	if (phase == VansTimelineEvaluationPhase::Camera)
	{
		std::vector<VansTimelineSessionHandle> children;
		for (const auto& [id, child] : session->childSessions) { (void)id; children.push_back(child); }
		for (VansTimelineSessionHandle child : children) Evaluate(child, phase);
		session->traversal.clear();
	}
}

void VansTimelineSessionService::AdvanceAndEvaluateAll(
	VansTimelineSessionKind kind,
	VansTimelineEvaluationPhase phase,
	double deltaSeconds)
{
	std::vector<VansTimelineSessionHandle> handles;
	m_Sessions.ForEach([&](VansTimelineSessionHandle handle, const Session& session)
	{
		if (session.kind == kind) handles.push_back(handle);
	});
	for (VansTimelineSessionHandle handle : handles)
	{
		if (phase == VansTimelineEvaluationPhase::PostScript) Advance(handle, deltaSeconds);
		Evaluate(handle, phase);
	}
}

void VansTimelineSessionService::StopAll(VansTimelineEndReason reason)
{
	std::vector<VansTimelineSessionHandle> handles;
	m_Sessions.ForEach([&](VansTimelineSessionHandle handle, const Session& session)
	{
		if (!session.parent.IsValid()) handles.push_back(handle);
	});
	for (VansTimelineSessionHandle handle : handles) { Stop(handle, reason); Release(handle); }
	m_PreAnimated.RestoreAll();
	m_Appliers.ReleaseAll();
}
}
