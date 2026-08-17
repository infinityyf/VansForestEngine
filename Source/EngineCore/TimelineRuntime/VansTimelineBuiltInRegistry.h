#pragma once

#include <string>

namespace Vans
{
class VansTimelineTrackExtensionRegistry;

bool VansRegisterTimelineRuntimeExtensions(
	VansTimelineTrackExtensionRegistry& registry,
	std::string& error);
}
