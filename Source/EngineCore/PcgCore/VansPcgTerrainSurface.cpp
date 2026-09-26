#include "VansPcgTerrainSurface.h"

#include "../TerrainCore/VansTerrainAsset.h"
#include "../TerrainCore/VansTerrainSurfaceQuery.h"

#include <utility>

namespace Vans
{
VansPcgSurfaceSampler CreatePcgTerrainSurface(
	std::shared_ptr<const VansTerrainAsset> terrain, std::string& error)
{
	error.clear();
	if (!terrain)
	{
		error = "The bound terrain heightfield is unavailable in memory.";
		return {};
	}
	if (!VansTerrainSurfaceQuery::Validate(*terrain, error)) return {};
	return [asset = std::move(terrain)](float x, float z, VansPcgSurfacePoint& point)
	{
		VansTerrainSurfaceSample sample;
		if (!VansTerrainSurfaceQuery::Sample(*asset, x, z, sample)) return false;
		point.height = sample.height;
		point.normal = sample.normal;
		return true;
	};
}

bool RaycastPcgTerrainSurface(std::shared_ptr<const VansTerrainAsset> terrain,
	const std::array<float, 3>& origin,
	const std::array<float, 3>& direction,
	float maximumDistance,
	VansPcgSurfaceHit& hit)
{
	if (!terrain) return false;
	VansTerrainSurfaceRayHit terrainHit;
	if (!VansTerrainSurfaceQuery::Raycast(
		*terrain, origin, direction, maximumDistance, terrainHit)) return false;
	hit.position = terrainHit.position;
	hit.normal = terrainHit.normal;
	return true;
}
}
