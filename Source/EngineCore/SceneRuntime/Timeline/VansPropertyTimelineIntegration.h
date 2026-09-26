#pragma once

#include "../../TimelineRuntime/VansTimelineApplierRegistry.h"

namespace Vans
{
class VansRuntimeWorld;
class IVansTimelineTransformAccess;
class VansTimelineTrackExtensionRegistry;
class VansTimelinePropertyAccessRegistry;
bool VansRegisterSceneTimelinePropertyAccessors(
	VansTimelinePropertyAccessRegistry& registry,
	std::string& error);
bool VansRegisterPropertyTimelineExtension(
	VansTimelineTrackExtensionRegistry& registry,
	const VansTimelinePropertyAccessRegistry& accessors,
	std::string& error);
bool VansRegisterPropertyTimelineIntegration(
	VansRuntimeWorld& world,
	const VansTimelinePropertyAccessRegistry& accessors,
	std::shared_ptr<IVansTimelineTransformAccess> transformAccess,
	VansTimelineApplierRegistry& registry,
	std::string& error);
}
