#pragma once

#include "../../TimelineRuntime/VansTimelineApplierRegistry.h"

namespace Vans
{
class VansAssetObjectRepository;
class VansRuntimeWorld;
class VansTimelineTrackExtensionRegistry;
bool VansRegisterAnimationTimelineExtensions(VansTimelineTrackExtensionRegistry&, std::string&);
bool VansRegisterAnimationTimelineIntegration(
	VansRuntimeWorld&, const VansAssetObjectRepository&,
	VansTimelineApplierRegistry&, std::string&);
}
