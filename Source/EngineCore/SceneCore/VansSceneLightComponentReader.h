#pragma once

#include "VansSceneLightComponentConfig.h"

namespace Vans
{
struct VansSerializedValue;

class VansSceneLightComponentReader
{
public:
	static VansSceneLightComponentConfig ReadComponents(const VansSerializedValue& components);
	static VansSceneDirectionalLightComponentConfig ReadDirectionalLight(const VansSerializedValue& lightNode);
	static VansScenePointLightComponentConfig ReadPointLight(const VansSerializedValue& lightNode);
	static VansSceneSpotLightComponentConfig ReadSpotLight(const VansSerializedValue& lightNode);
	static VansSceneRectLightComponentConfig ReadRectLight(const VansSerializedValue& lightNode);
};
}
