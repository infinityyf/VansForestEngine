#pragma once

#include "VansScenePhysicsComponentConfig.h"

namespace Vans
{
struct VansSerializedValue;

class VansScenePhysicsComponentReader
{
public:
	static VansScenePhysicsComponentsConfig ReadComponents(const VansSerializedValue& components);
	static VansScenePhysicsComponentsConfig ReadAuthoringComponents(const VansSerializedValue& entity);

	static VansScenePhysicsNodeConfig ReadPhysicsNode(const VansSerializedValue& physicsNode);
	static VansScenePhysicsNodeConfig ReadAuthoringPhysicsComponent(const VansSerializedValue& component);
	static VansSceneClothNodeConfig ReadClothNode(const VansSerializedValue& clothNode);
	static VansSceneCharacterControllerConfig ReadCharacterController(
		const VansSerializedValue& characterController);
};
}
