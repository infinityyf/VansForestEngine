#include "VansCollisionFilter.h"

#include "VansCollisionLayerManager.h"

namespace VansEngine
{
bool VansCollisionFilter::Build(
	const std::string& layerName,
	std::uint32_t flags,
	std::uint32_t group,
	physx::PxFilterData& filterData)
{
	auto& layers = VansCollisionLayerManager::Get();
	int layerIndex = -1;
	if (!layers.TryGetLayerIndex(layerName, layerIndex))
	{
		filterData = physx::PxFilterData{};
		return false;
	}

	filterData.word0 = static_cast<physx::PxU32>(layerIndex);
	filterData.word1 = layers.GetCollisionMask(layerIndex);
	filterData.word2 = static_cast<physx::PxU32>(flags);
	filterData.word3 = static_cast<physx::PxU32>(group);
	return true;
}
}
