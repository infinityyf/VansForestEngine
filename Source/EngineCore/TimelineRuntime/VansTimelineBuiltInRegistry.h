#pragma once

#include "../EventCore/VansEventLane.h"

#include <string>
#include <string_view>
#include <vector>

namespace Vans
{
class VansTimelineTrackExtensionRegistry;

bool VansRegisterTimelineRuntimeExtensions(
	VansTimelineTrackExtensionRegistry& registry,
	std::string& error);

const std::vector<std::string>& VansTimelineSignalLaneNames();
bool VansResolveTimelineSignalLane(std::string_view stableName, VansEventLane& lane);
}
