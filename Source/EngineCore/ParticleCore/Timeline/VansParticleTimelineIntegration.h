#pragma once

#include "../../TimelineRuntime/VansTimelineApplierRegistry.h"

namespace VansGraphics { class VansParticleManager; }
namespace Vans
{
class VansRuntimeWorld;
class VansTimelineTrackExtensionRegistry;
bool VansRegisterParticleTimelineExtensions(VansTimelineTrackExtensionRegistry&, std::string&);
bool VansRegisterParticleTimelineIntegration(VansRuntimeWorld&, VansGraphics::VansParticleManager&, VansTimelineApplierRegistry&, std::string&);
}
