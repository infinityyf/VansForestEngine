#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"

#include <vector>

namespace Vans
{
struct VansSceneMaterialConfig
{
	VansSerializedValue root;
};

using VansSceneMaterialConfigs = std::vector<VansSceneMaterialConfig>;
}
