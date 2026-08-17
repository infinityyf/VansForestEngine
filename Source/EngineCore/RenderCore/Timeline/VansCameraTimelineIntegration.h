#pragma once

#include "../../TimelineRuntime/VansTimelineApplierRegistry.h"

namespace Vans
{
class VansRuntimeWorld;
class VansTimelineTrackExtensionRegistry;
}
namespace VansGraphics
{
class VansCamera;
class VansCameraControlArbiter;
class VansVirtualCameraParameterStore;
bool VansRegisterRenderTimelineExtensions(
	Vans::VansTimelineTrackExtensionRegistry& registry,
	std::string& error);
bool VansRegisterCameraTimelineIntegration(
	Vans::VansRuntimeWorld& world,
	VansCamera& mainCamera,
	VansCameraControlArbiter& arbiter,
	VansVirtualCameraParameterStore& virtualCameraParameters,
	Vans::VansTimelineApplierRegistry& registry,
	std::string& error);
}
