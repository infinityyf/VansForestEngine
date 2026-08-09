#pragma once

#include "../TimelineCore/VansTimelineTypes.h"

#include <string>

namespace Vans
{
struct VansRuntimeTimelineComponent
{
	std::string assetGuid;
	std::string assetPath;
	VansTimelineInstanceConfig instance;
};
}
