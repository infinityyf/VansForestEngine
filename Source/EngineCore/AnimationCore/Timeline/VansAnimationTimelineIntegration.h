#pragma once

#include "../../TimelineRuntime/VansTimelineApplierRegistry.h"

namespace Vans
{
class VansAssetResolver;
class VansRuntimeWorld;
class VansTimelineTrackExtensionRegistry;
bool VansRegisterAnimationTimelineExtensions(VansTimelineTrackExtensionRegistry&, std::string&);
bool VansRegisterAnimationTimelineIntegration(
	VansRuntimeWorld&, std::shared_ptr<VansAssetResolver>, VansTimelineApplierRegistry&, std::string&);
}
