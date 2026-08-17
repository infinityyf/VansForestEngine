#pragma once

#include "../../TimelineRuntime/VansTimelineApplierRegistry.h"

namespace VansGraphics
{
class VansPostProcessProfile;
}
namespace Vans { class VansAssetResolver; }
namespace VansGraphics
{
bool VansRegisterPostProcessTimelineIntegration(
	VansPostProcessProfile& profile,
	std::shared_ptr<Vans::VansAssetResolver> resolver,
	Vans::VansTimelineApplierRegistry& registry,
	std::string& error);
}
