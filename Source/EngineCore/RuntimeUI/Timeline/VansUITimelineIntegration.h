#pragma once

#include "../../TimelineRuntime/VansTimelineApplierRegistry.h"

namespace Vans
{
class VansTimelineTrackExtensionRegistry;
bool VansRegisterUITimelineExtensions(VansTimelineTrackExtensionRegistry&, std::string&);
bool VansRegisterUITimelineIntegration(VansTimelineApplierRegistry&, std::string&);
}
