#pragma once

#include "../TimelineRuntime/VansTimelineApplierRegistry.h"

namespace Vans
{
class VansGameplayRuntime;
class VansTimelineTrackExtensionRegistry;

bool VansRegisterGameplayActionTimelineExtensions(
	VansTimelineTrackExtensionRegistry& registry,
	std::string& error);
bool VansRegisterGameplayActionTimelineIntegration(
	VansGameplayRuntime& gameplay,
	VansTimelineApplierRegistry& registry,
	std::string& error);
}
