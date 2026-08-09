#pragma once

#include "VansSceneTimelineComponentConfig.h"
#include "../AssetCore/Serialization/VansSerializedValue.h"

#include <optional>

namespace Vans
{
class VansSceneTimelineComponentReader
{
public:
	static std::optional<VansSceneTimelineComponentConfig> ReadFromAuthoringEntity(
		const VansSerializedValue& entity);
	static VansSceneTimelineComponentConfig ReadAuthoringComponent(
		const VansSerializedValue& component);
};
}
