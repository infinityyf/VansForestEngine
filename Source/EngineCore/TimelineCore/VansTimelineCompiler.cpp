#include "VansTimelineCompiler.h"

#include "VansTimelineSerialization.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace Vans
{
std::uint32_t VansCompiledTimeline::ParameterSlot(VansTimelineParameterId id) const
{
	const auto found = m_ParameterSlots.find(id);
	return found == m_ParameterSlots.end() ? VansInvalidTimelineSlot : found->second;
}

std::uint32_t VansCompiledTimeline::BindingSlot(VansTimelineBindingId id) const
{
	const auto found = m_BindingSlots.find(id);
	return found == m_BindingSlots.end() ? VansInvalidTimelineSlot : found->second;
}

std::shared_ptr<const VansCompiledTimeline> VansCompiledTimeline::ChildTimeline(
	const std::string& guid, const std::string& path) const
{
	for (std::size_t index = 0; index < m_ChildTimelineIdentities.size(); ++index)
		if ((!guid.empty() && m_ChildTimelineIdentities[index].first == guid) ||
			(!path.empty() && m_ChildTimelineIdentities[index].second == path))
			return m_ChildTimelines[index];
	return {};
}

namespace
{
bool RuntimeOrder(const VansCompiledTimelineTrack& left, const VansCompiledTimelineTrack& right)
{
	if (left.priority != right.priority) return left.priority < right.priority;
	if (left.order != right.order) return left.order < right.order;
	return left.id < right.id;
}

VansCompiledTimelineSection CompileSection(
	const VansTimelineSection& source,
	const VansTimelineCompiledDataView& extensionData)
{
	VansCompiledTimelineSection section;
	section.id = source.id;
	section.startTick = source.startTick;
	section.durationTicks = source.durationTicks;
	section.sourceInTick = source.sourceInTick;
	section.sourceOutTick = source.sourceOutTick;
	section.playRate = source.playRate;
	section.reverse = source.reverse;
	section.loopMode = source.loopMode;
	section.loopCount = source.loopCount;
	section.preRollTicks = source.preRollTicks;
	section.postRollTicks = source.postRollTicks;
	section.easeInTicks = source.easeInTicks;
	section.easeOutTicks = source.easeOutTicks;
	section.blendIn = source.blendIn;
	section.blendOut = source.blendOut;
	section.completionMode = source.completionMode;
	section.active = source.active;
	section.assetGuid = source.assetGuid;
	section.assetPath = source.assetPath;
	section.channels = source.channels;
	section.ranges = source.ranges;
	section.extensionData = extensionData;
	return section;
}
}

VansTimelineCompileResult VansTimelineCompiler::Compile(
	const VansTimelineAsset& source,
	const VansTimelineCompileOptions& options)
{
	VansTimelineCompileResult result;
	if (!options.extensions || !options.extensions->IsSealed())
	{
		result.diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
			"Timeline.TrackRegistryUnavailable", {}, {}, {},
			"Timeline compilation requires a sealed track extension registry" });
		return result;
	}
	VansTimelineAsset normalized = source;
	VansTimelineSerialization::Normalize(normalized);
	VansTimelineValidationContext validation = options.validation;
	validation.extensions = options.extensions;
	result.diagnostics = VansTimelineValidator::Validate(normalized, validation);
	if (VansTimelineValidator::HasErrors(result.diagnostics)) return result;

	auto compiled = std::make_shared<VansCompiledTimeline>();
	compiled->m_Timebase = normalized.timebase;
	compiled->m_DurationTicks = normalized.durationTicks;
	compiled->m_PlaybackRange = normalized.playbackRange;
	compiled->m_DefaultCompletionMode = normalized.defaultCompletionMode;
	compiled->m_Markers = normalized.markers;
	compiled->m_ContentHash = VansStableHash64(VansTimelineSerialization::Encode(normalized).dump());
	compiled->m_RegistryManifestHash = options.extensions->ManifestHash();

	for (const VansTimelineParameterDescriptor& parameter : normalized.parameters)
	{
		const std::uint32_t slot = static_cast<std::uint32_t>(compiled->m_Parameters.size());
		compiled->m_ParameterSlots.emplace(parameter.id, slot);
		compiled->m_Parameters.push_back({ parameter.id, parameter.type, parameter.defaultValue, slot });
	}
	for (const VansTimelineBinding& binding : normalized.bindings)
	{
		const std::uint32_t slot = static_cast<std::uint32_t>(compiled->m_Bindings.size());
		compiled->m_BindingSlots.emplace(binding.stableId, slot);
		compiled->m_Bindings.push_back({ binding.stableId, binding, slot });
	}

	VansTimelineCompiledDataWriter writer;
	const VansTimelineExtensionCompileContext extensionCompileContext{
		compiled->m_ParameterSlots, compiled->m_BindingSlots };
	for (const VansTimelineTrack& track : normalized.tracks)
	{
		if (!track.enabled) continue;
		const VansTimelineTrackExtensionDescriptor* descriptor = options.extensions->Resolve(track.type.typeId);
		if (!descriptor) continue;
		VansCompiledTimelineTrack runtime;
		runtime.id = track.id;
		runtime.typeId = track.type.typeId;
		if (!descriptor->outputs.empty())
		{
			runtime.outputTypeId = descriptor->outputs.front().typeId;
			runtime.outputApplierRequired = descriptor->outputs.front().applierRequired;
		}
		runtime.extensionSlot = options.extensions->SlotOf(track.type.typeId);
		runtime.evaluate = descriptor->evaluate;
		runtime.clockRate = descriptor->clockRate;
		runtime.flags = descriptor->flags;
		runtime.phase = descriptor->phase;
		runtime.bindingSlot = track.bindingId.empty() ? VansInvalidTimelineSlot :
			compiled->BindingSlot(VansMakeStableId<VansTimelineBindingTag>(track.bindingId));
		runtime.conditionParameterSlot = track.condition.parameterId ?
			compiled->ParameterSlot(track.condition.parameterId) : VansInvalidTimelineSlot;
		runtime.conditionExpected = track.condition.expectedValue;
		runtime.conditionNegate = track.condition.negate;
		runtime.blendMode = track.blendMode;
		runtime.completionMode = track.completionMode;
		runtime.order = track.order;
		runtime.priority = track.priority;
		std::vector<VansTimelineCompiledDataView> sections;
		if (!descriptor->compile(extensionCompileContext, track, descriptor->sourceSchema, writer,
			runtime.extensionData, sections, result.diagnostics)) continue;
		if (sections.size() != track.sections.size())
		{
			result.diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
				"Timeline.CompileFailed", {}, track.id, "sections",
				"Timeline extension compiler returned the wrong section-data count" });
			continue;
		}
		for (std::size_t index = 0; index < track.sections.size(); ++index)
			runtime.sections.push_back(CompileSection(track.sections[index], sections[index]));
		auto& destination = runtime.phase == VansTimelineEvaluationPhase::Camera ?
			compiled->m_CameraTracks : compiled->m_PostScriptTracks;
		destination.push_back(std::move(runtime));
	}
	if (VansTimelineValidator::HasErrors(result.diagnostics)) return result;

	if (!VansTimelineDependencyBuilder::BuildClosure(normalized, *options.extensions,
		options.dependencyLoader, compiled->m_Dependencies, result.diagnostics)) return result;
	for (const VansTimelineDependency& dependency : compiled->m_Dependencies.direct)
	{
		if (dependency.kind != VansTimelineDependencyKind::Asset || dependency.stableType != "Timeline") continue;
		if (!options.dependencyLoader)
		{
			result.diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
				"Timeline.DependencyLoaderMissing", {}, dependency.sourceObjectId, "dependency",
				"SubTimeline compilation requires a dependency loader" });
			return result;
		}
		VansTimelineAsset childSource; std::string identity; std::string error;
		if (!options.dependencyLoader(dependency, childSource, identity, error))
		{
			result.diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
				"Timeline.DependencyLoadFailed", {}, dependency.sourceObjectId, "dependency", error });
			return result;
		}
		const std::string childIdentity = !identity.empty() ? identity :
			(!dependency.guid.empty() ? dependency.guid : dependency.path);
		if (childIdentity.empty())
		{
			result.diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
				"Timeline.SubTimelineIdentityMissing", {}, dependency.sourceObjectId, "dependency",
				"SubTimeline dependency has no stable identity" });
			return result;
		}
		if (std::find(options.subTimelineAncestry.begin(), options.subTimelineAncestry.end(),
			childIdentity) != options.subTimelineAncestry.end())
		{
			result.diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
				"Timeline.SubTimelineCycle", {}, dependency.sourceObjectId, "dependency",
				"SubTimeline dependency graph contains a cycle" });
			return result;
		}
		VansTimelineCompileOptions childOptions = options;
		if (childOptions.maximumSubTimelineDepth == 0)
		{
			result.diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
				"Timeline.SubTimelineDepth", {}, dependency.sourceObjectId, "dependency",
				"SubTimeline nesting exceeds the configured depth" });
			return result;
		}
		--childOptions.maximumSubTimelineDepth;
		childOptions.subTimelineAncestry.push_back(childIdentity);
		VansTimelineCompileResult child = Compile(childSource, childOptions);
		if (!child)
		{
			result.diagnostics.insert(result.diagnostics.end(), child.diagnostics.begin(), child.diagnostics.end());
			return result;
		}
		compiled->m_ChildTimelineIdentities.emplace_back(dependency.guid, dependency.path);
		compiled->m_ChildTimelines.push_back(std::move(child.timeline));
	}
	compiled->m_CompiledBytes = writer.Bytes();
	compiled->m_CompiledValues = writer.Values();
	std::stable_sort(compiled->m_PostScriptTracks.begin(), compiled->m_PostScriptTracks.end(), RuntimeOrder);
	std::stable_sort(compiled->m_CameraTracks.begin(), compiled->m_CameraTracks.end(), RuntimeOrder);
	for (const auto& marker : normalized.markers) if (marker.determinismFence) compiled->m_DeterminismFences.push_back(marker.tick);
	std::sort(compiled->m_DeterminismFences.begin(), compiled->m_DeterminismFences.end());
	compiled->m_DeterminismFences.erase(std::unique(compiled->m_DeterminismFences.begin(), compiled->m_DeterminismFences.end()), compiled->m_DeterminismFences.end());
	result.timeline = std::move(compiled);
	return result;
}
}
