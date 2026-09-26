#pragma once

#include "../../TimelineRuntime/VansTimelineApplierRegistry.h"

namespace VansGraphics
{
class VansPostProcessProfile;
}
namespace Vans
{
class VansAssetObjectRepository;
class VansTimelineTrackExtensionRegistry;
}
namespace VansGraphics
{
bool VansRegisterPostProcessTimelineExtensions(
	Vans::VansTimelineTrackExtensionRegistry& registry,
	std::string& error);
bool VansRegisterPostProcessTimelineIntegration(
	VansPostProcessProfile& profile,
	const Vans::VansAssetObjectRepository& repository,
	Vans::VansTimelineApplierRegistry& registry,
	std::string& error);
}
