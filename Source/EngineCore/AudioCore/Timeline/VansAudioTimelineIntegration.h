#pragma once

#include "../../TimelineRuntime/VansTimelineApplierRegistry.h"

namespace Vans
{
class VansRuntimeWorld;
class VansTimelineTrackExtensionRegistry;
}
namespace VansEngine { class VansAudioManager; }
namespace Vans
{
bool VansRegisterAudioTimelineExtensions(
	VansTimelineTrackExtensionRegistry& registry,
	std::string& error);
bool VansRegisterAudioTimelineIntegration(
	VansRuntimeWorld& world,
	VansEngine::VansAudioManager& audioManager,
	VansTimelineApplierRegistry& registry,
	std::string& error);
}
