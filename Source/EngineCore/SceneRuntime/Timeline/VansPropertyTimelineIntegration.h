#pragma once

#include "../../TimelineRuntime/VansTimelineApplierRegistry.h"

namespace Vans
{
class VansRuntimeWorld;
class VansTimelineTrackExtensionRegistry;
class VansTimelinePropertyAccessRegistry;
bool VansRegisterPropertyTimelineExtension(
	VansTimelineTrackExtensionRegistry& registry,
	std::string& error);
bool VansRegisterPropertyTimelineIntegration(
	VansRuntimeWorld& world,
	const VansTimelinePropertyAccessRegistry& accessors,
	VansTimelineApplierRegistry& registry,
	std::string& error);
}
