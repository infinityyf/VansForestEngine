#pragma once

#include "VansTimelineCompiledDataWriter.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
struct VansCompiledTimelineTrack;
struct VansTimelineExtensionEvaluationContext;
struct VansTimelineDependency;
struct VansTimelineEvaluationOutput;
struct VansTimelineValidationContext;

struct VansTimelineExtensionCompileContext
{
	const std::unordered_map<VansTimelineParameterId, std::uint32_t>& parameterSlots;
	const std::unordered_map<VansTimelineBindingId, std::uint32_t>& bindingSlots;
	std::uint32_t ParameterSlot(VansTimelineParameterId id) const
	{
		const auto found = parameterSlots.find(id);
		return found == parameterSlots.end() ? UINT32_MAX : found->second;
	}
	std::uint32_t BindingSlot(VansTimelineBindingId id) const
	{
		const auto found = bindingSlots.find(id);
		return found == bindingSlots.end() ? UINT32_MAX : found->second;
	}
};

using VansTimelineRegistrySlot = std::uint32_t;
inline constexpr VansTimelineRegistrySlot VansInvalidTimelineRegistrySlot = UINT32_MAX;

enum class VansTimelineBindingRequirement : std::uint8_t
{
	None,
	Optional,
	Required
};

using VansTimelineValidateExtensionFn = void(*)(
	const VansTimelineTrack&,
	const VansTimelineSourceSchema&,
	const VansTimelineValidationContext&,
	VansTimelineDiagnostics&);
using VansTimelineCollectDependenciesFn = void(*)(
	const VansTimelineTrack&,
	std::vector<VansTimelineDependency>&);
using VansTimelineCompileExtensionFn = bool(*)(
	const VansTimelineExtensionCompileContext&,
	const VansTimelineTrack&,
	const VansTimelineSourceSchema&,
	VansTimelineCompiledDataWriter&,
	VansTimelineCompiledDataView&,
	std::vector<VansTimelineCompiledDataView>&,
	VansTimelineDiagnostics&);
using VansTimelineEvaluateExtensionFn = void(*)(VansTimelineExtensionEvaluationContext&);
using VansTimelineClockRateFn = double(*)(
	const class VansCompiledTimeline&,
	const VansCompiledTimelineTrack&,
	VansTimelineTick);

struct VansTimelineOutputDeclaration
{
	VansTimelineOutputTypeId typeId;
	std::string stableName;
	std::uint32_t payloadSize = 0;
	std::uint32_t payloadAlignment = 1;
	bool applierRequired = true;
};

struct VansTimelineTrackExtensionDescriptor
{
	VansTimelineTrackTypeId typeId;
	std::string stableName;
	VansTimelineTrackFlags flags = VansTimelineTrackFlags::None;
	VansTimelineEvaluationPhase phase = VansTimelineEvaluationPhase::PostScript;
	VansTimelineBindingRequirement binding = VansTimelineBindingRequirement::None;
	VansTimelineSourceSchema sourceSchema;
	VansTimelineValidateExtensionFn validate = nullptr;
	VansTimelineCollectDependenciesFn collectDependencies = nullptr;
	VansTimelineCompileExtensionFn compile = nullptr;
	VansTimelineEvaluateExtensionFn evaluate = nullptr;
	VansTimelineClockRateFn clockRate = nullptr;
	std::vector<VansTimelineOutputDeclaration> outputs;

	std::string displayName;
	std::string category;
	std::string sectionAssetKind;
	bool editorVisible = true;
};

void VansValidateTimelineExtensionSchema(
	const VansTimelineTrack& track,
	const VansTimelineSourceSchema& schema,
	VansTimelineDiagnostics& diagnostics);
bool VansCompileTimelineExtensionSchema(
	const VansTimelineExtensionCompileContext& context,
	const VansTimelineTrack& track,
	const VansTimelineSourceSchema& schema,
	VansTimelineCompiledDataWriter& writer,
	VansTimelineCompiledDataView& trackData,
	std::vector<VansTimelineCompiledDataView>& sectionData,
	VansTimelineDiagnostics& diagnostics);
}
