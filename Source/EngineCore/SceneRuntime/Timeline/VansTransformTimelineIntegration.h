#pragma once

#include "../../TimelineRuntime/VansTimelineApplierRegistry.h"

namespace Vans
{
class VansRuntimeWorld;
class IVansTimelineTransformAccess;
class VansTimelineTrackExtensionRegistry;
bool VansRegisterSceneTimelineExtensions(
	VansTimelineTrackExtensionRegistry& registry,
	std::string& error);
bool VansRegisterTransformTimelineIntegration(
	VansRuntimeWorld& world,
	std::shared_ptr<IVansTimelineTransformAccess> access,
	VansTimelineApplierRegistry& registry,
	std::string& error);
}
