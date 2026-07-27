#pragma once

#include "VansSceneRenderSettingsConfig.h"

namespace Vans
{
struct VansSerializedValue;

class VansSceneRenderSettingsConfigReader
{
public:
	static VansSceneRenderSettingsConfig Read(const VansSerializedValue& sceneSettings);
};
}
