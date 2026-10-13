#pragma once

#include "../AssetCore/VansTriangleMeshData.h"
#include "../NavigationCore/VansNavigationSourceSnapshot.h"

#include <functional>
#include <string>
namespace VansEngine { struct VansPhysicsBoxShapeSnapshot; }

namespace Vans
{
struct VansSceneContentBuildPlan;
class VansAssetObjectRepository;

using VansNavigationMeshResolver = std::function<bool(
	const std::string& assetGuid,
	VansTriangleMeshData& data,
	std::string& error)>;

class VansSceneNavigationGeometry
{
public:
    static bool FreezeColliderBox(const VansEngine::VansPhysicsBoxShapeSnapshot& box,const std::string& id,
        VansNavigationObstacleGeometry& output,std::string& error);
	static bool CaptureSnapshot(
		const VansSceneContentBuildPlan& scene,
        const VansAssetObjectRepository& repository,
		const VansNavigationSettings& settings,
		const std::vector<std::string>& collisionLayers,
		const VansNavigationMeshResolver& resolveMesh,
		VansNavigationSourceSnapshot& snapshot,
		std::string& error);
};
}
