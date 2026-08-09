#pragma once

#include "../TimelineCore/VansTimelineTypes.h"

#include <string>

namespace Vans
{
struct VansSceneTimelineComponentConfig
{
	bool valid = false;
	bool enabled = true;
	std::string timelineAssetGuid;
	std::string timelineAssetPath;
	VansTimelineInstanceConfig instance;
};
}
