#pragma once

#include "../../TimelineRuntime/VansTimelineApplierRegistry.h"

namespace VansGraphics
{
class VansPostProcessProfile;
}
namespace Vans { class VansAssetObjectRepository; }
namespace VansGraphics
{
bool VansRegisterPostProcessTimelineIntegration(
	VansPostProcessProfile& profile,
	const Vans::VansAssetObjectRepository& repository,
	Vans::VansTimelineApplierRegistry& registry,
	std::string& error);
}
