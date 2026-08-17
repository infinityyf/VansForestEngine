#pragma once

#include "VansTimelineDependencyBuilder.h"
#include "VansTimelineValidator.h"

#include <memory>
#include <unordered_map>

namespace Vans
{
inline constexpr std::uint32_t VansInvalidTimelineSlot = UINT32_MAX;

struct VansCompiledTimelineParameter
{
	VansTimelineParameterId id;
	VansTimelineValueType type = VansTimelineValueType::Null;
	VansTimelineValue defaultValue;
	std::uint32_t slot = VansInvalidTimelineSlot;
};

struct VansCompiledTimelineBinding
{
	VansTimelineBindingId id;
	VansTimelineBinding authoring;
	std::uint32_t slot = VansInvalidTimelineSlot;
};

struct VansCompiledTimelineSection
{
	VansTimelineId id;
	VansTimelineTick startTick = 0;
	VansTimelineTick durationTicks = 1;
	VansTimelineTick sourceInTick = 0;
	VansTimelineTick sourceOutTick = -1;
	double playRate = 1.0;
	bool reverse = false;
	VansTimelineLoopMode loopMode = VansTimelineLoopMode::None;
	std::int32_t loopCount = 1;
	VansTimelineTick preRollTicks = 0;
	VansTimelineTick postRollTicks = 0;
	VansTimelineTick easeInTicks = 0;
	VansTimelineTick easeOutTicks = 0;
	VansTimelineBlendCurve blendIn;
	VansTimelineBlendCurve blendOut;
	VansTimelineCompletionMode completionMode = VansTimelineCompletionMode::ProjectDefault;
	bool active = true;
	std::string assetGuid;
	std::string assetPath;
	std::vector<VansTimelineChannel> channels;
	std::vector<VansTimelineRange> ranges;
	VansTimelineCompiledDataView extensionData;
};

struct VansCompiledTimelineTrack
{
	VansTimelineId id;
	VansTimelineTrackTypeId typeId;
	VansTimelineOutputTypeId outputTypeId;
	bool outputApplierRequired = true;
	VansTimelineRegistrySlot extensionSlot = VansInvalidTimelineRegistrySlot;
	VansTimelineEvaluateExtensionFn evaluate = nullptr;
	VansTimelineClockRateFn clockRate = nullptr;
	VansTimelineTrackFlags flags = VansTimelineTrackFlags::None;
	VansTimelineEvaluationPhase phase = VansTimelineEvaluationPhase::PostScript;
	std::uint32_t bindingSlot = VansInvalidTimelineSlot;
	std::uint32_t conditionParameterSlot = VansInvalidTimelineSlot;
	VansTimelineValue conditionExpected;
	bool conditionNegate = false;
	VansTimelineBlendMode blendMode = VansTimelineBlendMode::Override;
	VansTimelineCompletionMode completionMode = VansTimelineCompletionMode::ProjectDefault;
	std::int32_t order = 0;
	std::int32_t priority = 0;
	VansTimelineCompiledDataView extensionData;
	std::vector<VansCompiledTimelineSection> sections;
};

class VansCompiledTimeline
{
public:
	const VansTimelineTimebase& Timebase() const { return m_Timebase; }
	VansTimelineTick DurationTicks() const { return m_DurationTicks; }
	const VansTimelineTickRange& PlaybackRange() const { return m_PlaybackRange; }
	VansTimelineCompletionMode DefaultCompletionMode() const { return m_DefaultCompletionMode; }
	const std::vector<VansCompiledTimelineTrack>& Tracks(VansTimelineEvaluationPhase phase) const
	{
		return phase == VansTimelineEvaluationPhase::Camera ? m_CameraTracks : m_PostScriptTracks;
	}
	const std::vector<VansTimelineTick>& DeterminismFences() const { return m_DeterminismFences; }
	const std::vector<VansTimelineMarker>& Markers() const { return m_Markers; }
	const std::vector<VansCompiledTimelineParameter>& Parameters() const { return m_Parameters; }
	const std::vector<VansCompiledTimelineBinding>& Bindings() const { return m_Bindings; }
	const VansTimelineDependencyClosure& Dependencies() const { return m_Dependencies; }
	const std::vector<std::byte>& CompiledBytes() const { return m_CompiledBytes; }
	const std::vector<VansTimelineValue>& CompiledValues() const { return m_CompiledValues; }
	std::uint64_t ContentHash() const { return m_ContentHash; }
	std::uint64_t RegistryManifestHash() const { return m_RegistryManifestHash; }
	const std::vector<std::shared_ptr<const VansCompiledTimeline>>& ChildTimelines() const { return m_ChildTimelines; }
	std::shared_ptr<const VansCompiledTimeline> ChildTimeline(const std::string& guid, const std::string& path) const;

	std::uint32_t ParameterSlot(VansTimelineParameterId id) const;
	std::uint32_t BindingSlot(VansTimelineBindingId id) const;

private:
	friend class VansTimelineCompiler;
	VansTimelineTimebase m_Timebase;
	VansTimelineTick m_DurationTicks = 0;
	VansTimelineTickRange m_PlaybackRange;
	VansTimelineCompletionMode m_DefaultCompletionMode = VansTimelineCompletionMode::RestoreState;
	std::vector<VansCompiledTimelineTrack> m_PostScriptTracks;
	std::vector<VansCompiledTimelineTrack> m_CameraTracks;
	std::vector<VansTimelineTick> m_DeterminismFences;
	std::vector<VansTimelineMarker> m_Markers;
	std::vector<VansCompiledTimelineParameter> m_Parameters;
	std::vector<VansCompiledTimelineBinding> m_Bindings;
	std::unordered_map<VansTimelineParameterId, std::uint32_t> m_ParameterSlots;
	std::unordered_map<VansTimelineBindingId, std::uint32_t> m_BindingSlots;
	VansTimelineDependencyClosure m_Dependencies;
	std::vector<std::byte> m_CompiledBytes;
	std::vector<VansTimelineValue> m_CompiledValues;
	std::vector<std::shared_ptr<const VansCompiledTimeline>> m_ChildTimelines;
	std::vector<std::pair<std::string, std::string>> m_ChildTimelineIdentities;
	std::uint64_t m_ContentHash = 0;
	std::uint64_t m_RegistryManifestHash = 0;
};

struct VansTimelineCompileOptions
{
	const VansTimelineTrackExtensionRegistry* extensions = nullptr;
	VansTimelineValidationContext validation;
	VansTimelineDependencyAssetLoader dependencyLoader;
	std::size_t maximumSubTimelineDepth = 16;
	std::vector<std::string> subTimelineAncestry;
};

struct VansTimelineCompileResult
{
	std::shared_ptr<const VansCompiledTimeline> timeline;
	VansTimelineDiagnostics diagnostics;
	explicit operator bool() const { return timeline != nullptr; }
};

class VansTimelineCompiler
{
public:
	static VansTimelineCompileResult Compile(
		const VansTimelineAsset& source,
		const VansTimelineCompileOptions& options);
};
}
