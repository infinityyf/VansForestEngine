#pragma once

#include "VansSceneVehicleComponentConfig.h"

namespace Vans
{
struct VansSerializedValue;

class VansSceneVehicleComponentReader
{
public:
	static VansSceneVehicleObjectConfigs ReadObjects(const VansSerializedValue& objectsArray);
	static VansSceneVehicleObjectConfig ReadObject(const VansSerializedValue& objectNode);
	static VansSceneVehicleObjectConfig ReadComponents(const VansSerializedValue& components);
	static VansSceneVehicleObjectConfig ReadAuthoringComponents(const VansSerializedValue& entity);
	static VansSceneVehicleComponentConfig ReadVehicle(const VansSerializedValue& vehicleNode);
};
}
