#pragma once

#include "../../TimelineRuntime/VansTimelineApplierRegistry.h"

namespace Vans
{
class VansRuntimeWorld;
class VansTimelinePropertyAccessRegistry;
class VansTimelineTrackExtensionRegistry;
}

namespace VansGraphics
{
class VansScene;

bool VansRegisterRenderPropertyTimelineExtensions(
	Vans::VansTimelineTrackExtensionRegistry& registry,
	std::string& error);

bool VansRegisterRenderTimelinePropertyAccessors(
	Vans::VansTimelinePropertyAccessRegistry& registry,
	std::string& error);

bool VansRegisterRenderPropertyTimelineIntegration(
	VansScene& scene,
	Vans::VansRuntimeWorld& world,
	Vans::VansTimelineApplierRegistry& registry,
	std::string& error);
}
