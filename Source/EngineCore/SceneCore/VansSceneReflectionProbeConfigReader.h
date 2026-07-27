#pragma once

#include "VansSceneReflectionProbeConfig.h"

namespace Vans
{
struct VansSerializedValue;

class VansSceneReflectionProbeConfigReader
{
public:
	static VansSceneReflectionProbeConfig Read(const VansSerializedValue& sceneSettings);
};
}
