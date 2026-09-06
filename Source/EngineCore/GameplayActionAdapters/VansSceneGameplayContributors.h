#pragma once

#include "../CameraCore/VansCameraCore.h"
#include "../GameplayActionCore/VansGameplayRuntime.h"
#include "../GameplayActionSchema/VansGAFProjectConfiguration.h"
#include "Projectile/VansProjectileActionService.h"

#include <functional>

namespace Vans
{
class VansRuntimeWorld;
class VansTimelineRuntimeSystem;

struct VansSceneGameplayContributorContext
{
	VansRuntimeWorld& world;
	VansGameplayRuntime& gameplay;
	VansCameraRuntime& camera;
	VansTimelineRuntimeSystem& timeline;
	std::function<bool(VansEntityHandle, glm::vec3&)> resolveEntityPosition;
	VansProjectileSceneBackend projectileBackend;
};

// Scene code supplies stable engine facilities only. Domain modules own their
// contributor descriptors, capability factories, and dependency declarations.
bool VansDiscoverSceneGameplayContributors(
	const VansGAFProjectConfiguration& configuration,
	const VansSceneGameplayContributorContext& context,
	VansGameplayRuntimeDependencies& dependencies,
	std::string& error);
}
