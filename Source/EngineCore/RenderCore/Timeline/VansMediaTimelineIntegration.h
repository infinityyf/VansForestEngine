#pragma once

#include "../../TimelineRuntime/VansTimelineApplierRegistry.h"

namespace Vans
{
class VansRuntimeWorld;
class VansTimelineTrackExtensionRegistry;
}
namespace VansGraphics
{
class VansVideoManager;
bool VansRegisterMediaTimelineExtensions(Vans::VansTimelineTrackExtensionRegistry&, std::string&);
bool VansRegisterMediaTimelineIntegration(
	Vans::VansRuntimeWorld&, VansVideoManager&, Vans::VansTimelineApplierRegistry&, std::string&);
}
