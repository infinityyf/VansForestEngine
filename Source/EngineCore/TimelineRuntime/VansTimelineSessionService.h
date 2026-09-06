#pragma once

#include "../RuntimeCore/VansGenerationPool.h"
#include "VansTimelineApplierRegistry.h"
#include "VansTimelineBindingResolver.h"
#include "VansTimelineClockRegistry.h"
#include "VansTimelineEvaluator.h"
#include "VansTimelineParameterBlock.h"
#include "VansTimelinePreAnimatedState.h"
#include "VansTimelineWriterRegistry.h"
#include "../EventCore/VansPayloadSchemaRegistry.h"
#include "Events/VansTimelineRuntimeEvents.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Vans
{
struct VansTimelineSessionDesc
{
	VansTimelineSessionKind kind = VansTimelineSessionKind::External;
	std::shared_ptr<const VansCompiledTimeline> timeline;
	VansRuntimeWorld* world = nullptr;
	VansEntityHandle owner;
	VansTimelineSessionScope scope;
	VansTimelineSessionHandle parent;
	VansTimelineSessionHandle root;
	std::int32_t hierarchicalBias = 0;
	std::string clockType = std::string(TimelineClockNames::GameTime);
	std::shared_ptr<IVansTimelineClockSource> externalClock;
	VansTimelineClockHandle externalClockHandle;
	std::vector<VansTimelineBindingOverride> bindingOverrides;
	std::vector<VansTimelineRuntimeBinding> runtimeBindings;
	std::vector<VansTimelineParameterOverride> parameterOverrides;
	std::vector<VansTimelineBindingOverride> inheritedBindingOverrides;
	std::vector<VansTimelineRuntimeBinding> inheritedRuntimeBindings;
	std::vector<VansTimelineParameterOverride> inheritedParameterOverrides;
	VansTimelineLoopMode loopMode = VansTimelineLoopMode::None;
	std::int32_t loopCount = 1;
	double playRate = 1.0;
	bool restoreStateOnStop = true;
	bool previewSafeEvents = false;
	bool includeSubTimelines = true;
	std::string debugLabel;
};

struct VansTimelineSessionView
{
	VansTimelineSessionHandle handle;
	VansTimelineSessionKind kind = VansTimelineSessionKind::External;
	VansTimelineSessionHandle parent;
	VansTimelineSessionHandle root;
	VansTimelinePlayerState state = VansTimelinePlayerState::Unloaded;
	VansTimelineTick tick = 0;
	std::int32_t loopIteration = 0;
	std::uint64_t clockSerial = 0;
};

struct VansTimelineSessionResult
{
	bool success = false;
	VansTimelineSessionHandle handle;
	std::string error;
	explicit operator bool() const { return success; }
};

class VansTimelineSessionService
{
public:
	VansTimelineSessionService(
		VansTimelineClockRegistry& clocks,
		VansTimelineApplierRegistry& appliers,
		VansPayloadSchemaRegistry* payloads = nullptr);
	void BindPayloadSchemas(VansPayloadSchemaRegistry* payloads) { m_Payloads = payloads; }
	VansTimelineSessionResult Create(const VansTimelineSessionDesc& desc);
	bool Play(VansTimelineSessionHandle handle, bool restart = false);
	bool Pause(VansTimelineSessionHandle handle);
	bool Resume(VansTimelineSessionHandle handle);
	bool Stop(VansTimelineSessionHandle handle, VansTimelineEndReason reason = VansTimelineEndReason::Stopped);
	bool Seek(VansTimelineSessionHandle handle, VansTimelineTick tick, VansTimelineSeekPolicy policy);
	bool ConfigurePlayback(VansTimelineSessionHandle handle, double playRate, int direction,
		VansTimelineLoopMode loopMode, std::int32_t loopCount = 1);
	bool SetParameter(VansTimelineSessionHandle handle, VansTimelineParameterId id, const VansTimelineValue& value);
	std::optional<VansTimelineSessionView> Query(VansTimelineSessionHandle handle) const;
	bool Release(VansTimelineSessionHandle handle);
	void Advance(VansTimelineSessionHandle handle, double deltaSeconds);
	void Evaluate(VansTimelineSessionHandle handle, VansTimelineEvaluationPhase phase);
	void AdvanceAndEvaluateAll(VansTimelineSessionKind kind, VansTimelineEvaluationPhase phase, double deltaSeconds);
	void StopAll(VansTimelineEndReason reason = VansTimelineEndReason::WorldShutdown);
	std::size_t SessionCount() const { return m_Sessions.ActiveCount(); }
	std::size_t WriterCount() const { return m_Writers.ActiveCount(); }
	std::size_t RestoreTokenCount() const { return m_PreAnimated.TokenCount(); }
	const VansTimelineDiagnostics& Diagnostics() const { return m_Diagnostics; }

private:
	struct WriterHandleHash
	{
		std::size_t operator()(VansTimelineWriterHandle handle) const noexcept
		{
			return (static_cast<std::size_t>(handle.index) << 1) ^ handle.generation;
		}
	};

	struct Session
	{
		VansTimelineSessionHandle handle;
		VansTimelineSessionKind kind = VansTimelineSessionKind::External;
		VansTimelineSessionHandle parent;
		VansTimelineSessionHandle root;
		std::shared_ptr<const VansCompiledTimeline> timeline;
		VansTimelineBindingResolver bindings;
		VansTimelineParameterBlock parameters;
		std::shared_ptr<IVansTimelineClockSource> clock;
		std::shared_ptr<VansTimelineOwnedClockSource> ownedClock;
		bool drivesClock = false;
		VansTimelineClockHandle clockHandle;
		VansTimelinePlayerState state = VansTimelinePlayerState::Stopped;
		VansTimelineTick currentTick = 0;
		double subTickRemainder = 0.0;
		double playRate = 1.0;
		int direction = 1;
		VansTimelineLoopMode loopMode = VansTimelineLoopMode::None;
		std::int32_t loopCount = 1;
		std::int32_t loopIteration = 0;
		std::int32_t hierarchicalBias = 0;
		bool restoreStateOnStop = true;
		bool previewSafeEvents = false;
		bool includeSubTimelines = true;
		std::string debugLabel;
		std::vector<VansTimelineTraversalSegment> traversal;
		VansTimelineOutputArena arena;
		std::vector<VansTimelineEvaluationOutput> outputs;
		std::unordered_set<VansTimelineWriterHandle, WriterHandleHash> postScriptWriters;
		std::unordered_set<VansTimelineWriterHandle, WriterHandleHash> cameraWriters;
		std::unordered_map<VansTimelineId, VansTimelineSessionHandle> childSessions;
		std::unordered_set<VansTimelineId> failedChildSections;
		std::unordered_set<VansTimelineId> firedOnce;
		std::unordered_set<std::string> firedPerLoop;
		VansRuntimeWorld* world = nullptr;
		VansEntityHandle owner;
		VansTimelineSessionScope scope;
		std::vector<VansTimelineBindingOverride> bindingOverrides;
		std::vector<VansTimelineRuntimeBinding> runtimeBindings;
		std::uint64_t correlation = 0;
		std::uint64_t eventSequence = 0;
		bool includeCurrentTickOnNextTraversal = false;
	};

	Session* Resolve(VansTimelineSessionHandle handle);
	const Session* Resolve(VansTimelineSessionHandle handle) const;
	void AddDiagnostic(VansTimelineSessionHandle handle, std::string code, std::string message);
	void BuildTraversal(Session& session, VansTimelineTick requestedTick,
		VansTimelineEvaluationReason reason, VansTimelineSeekPolicy policy, std::uint64_t clockSerial,
		bool discontinuity);
	void ReconcileWriters(Session& session, VansTimelineEvaluationPhase phase);
	void DeactivateWritersBeforeApply(Session& session, VansTimelineEvaluationPhase phase);
	void ReleaseSessionWriters(Session& session);
	void PublishLifecycle(Session& session, VansTimelineLifecycleKind kind,
		VansTimelineEndReason reason = VansTimelineEndReason::Stopped);
	void PublishCrossedMarkers(Session& session);
	void PublishSignalOutputs(Session& session);
	void ReconcileChildSessions(Session& session);
	bool HasNewSessionError(VansTimelineSessionHandle handle, std::size_t firstDiagnostic) const;
	void FailSession(Session& session, std::string code, std::string message);

	VansTimelineClockRegistry& m_Clocks;
	VansTimelineApplierRegistry& m_Appliers;
	VansPayloadSchemaRegistry* m_Payloads = nullptr;
	std::uint64_t m_NextCorrelation = 1;
	VansGenerationPool<Session> m_Sessions;
	VansTimelineWriterRegistry m_Writers;
	VansTimelinePreAnimatedState m_PreAnimated;
	VansTimelineDiagnostics m_Diagnostics;
};
}
