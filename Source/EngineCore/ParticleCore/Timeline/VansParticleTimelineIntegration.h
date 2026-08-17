#pragma once

#include "../../TimelineRuntime/VansTimelineApplierRegistry.h"

namespace Vans
{
class VansRuntimeWorld;
class VansTimelineTrackExtensionRegistry;
bool VansRegisterParticleTimelineExtensions(VansTimelineTrackExtensionRegistry&, std::string&);
bool VansRegisterParticleTimelineIntegration(VansRuntimeWorld&, VansTimelineApplierRegistry&, std::string&);
}
