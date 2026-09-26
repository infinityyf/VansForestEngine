#pragma once

#include "../../PcgCore/VansPcgBatchPlan.h"
#include "../../SceneCore/VansSceneResourceLoadContext.h"
#include "../../SceneCore/VansSceneResourcePlan.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

namespace VansGraphics
{
class VansScene;

struct VansPcgPreviewProjection
{
	Vans::VansSceneResourceBuildPlan resources;
	std::unordered_set<std::string> requiredMaterials;
	Vans::VansSceneResourceLoadContext loadContext;
	std::shared_ptr<const Vans::VansPcgBatchUpdate> update;
	std::function<void(VansScene&)> refreshMaterialTextures;
};

// RenderCore owns the render-thread/GPU projection of an already planned PCG update.
// EngineAPILayer supplies memory-side dependencies and optional material refresh data only.
class VansPcgPreviewProjector
{
public:
	static bool Project(
		VansScene& scene,
		VansPcgPreviewProjection projection,
		std::string& error);
};
}
