#pragma once

#include "../../TimelineRuntime/VansTimelineApplierRegistry.h"

namespace Vans
{
class VansRuntimeWorld;
class VansTimelineTrackExtensionRegistry;
bool VansRegisterActivationTimelineExtension(
	VansTimelineTrackExtensionRegistry& registry,
	std::string& error);
bool VansRegisterActivationTimelineIntegration(
	VansRuntimeWorld& world,
	VansTimelineApplierRegistry& registry,
	std::string& error);
}
