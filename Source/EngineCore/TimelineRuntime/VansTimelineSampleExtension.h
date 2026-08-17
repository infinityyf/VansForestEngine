#pragma once

#include "../TimelineCore/VansTimelineTrackExtension.h"

namespace Vans
{
VansTimelineSourceField VansMakeTimelineSourceField(
	std::string name,
	VansTimelineValueType type,
	VansTimelineValue defaultValue,
	bool required = false,
	std::vector<std::string> enumValues = {});

VansTimelineChannelSchema VansMakeTimelineChannelSchema(
	std::string name,
	VansTimelineValueType type,
	bool required = false,
	std::string typeField = {},
	std::vector<std::pair<std::string, VansTimelineValueType>> typeCases = {});

VansTimelineTrackFlags VansTimelineContinuousTrackFlags(bool supportsChannels = true);

void VansEvaluateTimelineSampleExtension(VansTimelineExtensionEvaluationContext& context);

VansTimelineTrackExtensionDescriptor VansMakeTimelineSampleExtension(
	std::string_view stableName,
	std::string displayName,
	std::string category,
	VansTimelineEvaluationPhase phase,
	VansTimelineBindingRequirement binding,
	VansTimelineTrackFlags flags,
	VansTimelineSourceSchema schema,
	VansTimelineCollectDependenciesFn collectDependencies = nullptr);

VansTimelineTrackExtensionDescriptor VansMakeTimelinePointExtension(
	std::string_view stableName,
	std::string displayName,
	std::string category,
	VansTimelineEvaluationPhase phase,
	VansTimelineBindingRequirement binding,
	VansTimelineSourceSchema schema,
	VansTimelineCollectDependenciesFn collectDependencies = nullptr);
}
