#pragma once

#include "../CameraCore/VansCameraCore.h"
#include "../GameplayActionCore/VansGameplayRuntime.h"
#include "../GameplayActionSchema/VansGAFProjectConfiguration.h"
#include "Projectile/VansProjectileActionService.h"
#include "Decal/VansDecalActionService.h"
#include "Combat/VansCombatSceneBackend.h"
#include "VFX/VansVFXActionService.h"

#include <functional>

namespace VansEngine { class VansAudioManager; }

namespace Vans
{
class VansFakeActionService;
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
	VansEngine::VansAudioManager* audio = nullptr;
	VansDecalSceneBackend decalBackend;
	VansCombatSceneBackend combatBackend;
    VansVFXSceneBackend vfxBackend;
};

// Scene code supplies stable engine facilities only. Domain modules own their
// contributor descriptors, capability factories, and dependency declarations.
bool VansDiscoverSceneGameplayContributors(
	const VansGAFProjectConfiguration& configuration,
	const VansSceneGameplayContributorContext& context,
	VansGameplayRuntimeDependencies& dependencies,
	std::string& error);

bool VansDiscoverSimulationGameplayContributors(
	const VansGAFProjectConfiguration& configuration,
	VansTimelineRuntimeSystem& timeline,
	VansGameplayRuntimeDependencies& dependencies,
	std::vector<std::shared_ptr<VansFakeActionService>>& services,
	std::string& error);
}
