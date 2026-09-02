#pragma once

#include "VansNavigationTypes.h"

namespace Vans
{
struct VansSceneObjectBuildPlan;

class VansSceneNavigationGeometry
{
public:
	static VansNavigationGeometry BuildEnvironmentGeometry(
		const VansSceneObjectBuildPlan& sceneObjects);
};
}
