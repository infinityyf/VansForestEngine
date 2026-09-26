#pragma once

#include "../AssetCore/VansTriangleMeshData.h"
#include "VansNavigationTypes.h"

#include <functional>
#include <string>
#include <vector>

namespace Vans
{
struct VansSceneObjectBuildPlan;

using VansNavigationMeshResolver = std::function<bool(
	const std::string& assetGuid,
	VansTriangleMeshData& data,
	std::string& error)>;

class VansSceneNavigationGeometry
{
public:
	static bool BuildEnvironmentGeometry(
		const VansSceneObjectBuildPlan& sceneObjects,
		const VansNavigationAreaSettings& areaSettings,
		const VansNavigationMeshResolver& resolveMesh,
		VansNavigationGeometry& geometry,
		std::string& error);
	static bool CollectEnvironmentMeshAssets(
		const VansSceneObjectBuildPlan& sceneObjects,
		std::vector<std::string>& assetGuids,
		std::string& error);
};
}
