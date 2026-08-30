#pragma once

#include "VansSceneRenderSettingsConfig.h"

#include <string>

namespace Vans
{
struct VansSerializedValue;

class VansSceneRenderSettingsConfigReader
{
public:
	static bool Read(
		const VansSerializedValue& sceneSettings,
		VansSceneRenderSettingsConfig& config,
		std::string& error);
};
}
