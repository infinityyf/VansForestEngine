#pragma once

#include "VansSceneTimelineComponentConfig.h"
#include "../AssetCore/Serialization/VansSerializedValue.h"

#include <optional>
#include <string>

namespace Vans
{
class VansSceneTimelineComponentReader
{
public:
	static bool ReadFromAuthoringEntity(
		const VansSerializedValue& entity,
		std::optional<VansSceneTimelineComponentConfig>& outConfig,
		std::string& error);
	static bool ReadAuthoringComponent(
		const VansSerializedValue& component,
		VansSceneTimelineComponentConfig& outConfig,
		std::string& error);
};
}
