#include "VansAnimationWorldQueryBatch.h"

#include "../../PhysicsCore/VansPhysicsQuery.h"
#include "../../SceneRuntime/Transform/VansTransformStore.h"

#include <limits>

namespace VansGraphics
{
	void VansAnimationWorldQueryBatch::Execute(
		const std::vector<VansWorldQueryRequest>& requests,
		std::vector<VansWorldQueryResult>& results)
	{
		results.assign(requests.size(), VansWorldQueryResult{});
		if (requests.empty())
			return;

		std::vector<VansEngine::VansPhysicsShapeCastRequest> physicsRequests;
		physicsRequests.reserve(requests.size());
		for (const VansWorldQueryRequest& request : requests)
		{
			VansEngine::VansPhysicsShapeCastRequest physicsRequest;
			physicsRequest.origin = request.originWorld;
			physicsRequest.direction = request.directionWorld;
			physicsRequest.distance = request.distance;
			physicsRequest.sphereRadius = request.sweepRadius;
			physicsRequest.filter.layerMask = request.collisionMask;
			physicsRequest.filter.includeTriggers = false;
			if (request.ignoredOwnerId != 0 &&
				request.ignoredOwnerId - 1u <=
					static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)()))
			{
				physicsRequest.filter.ignoredTransformId =
					static_cast<std::uint32_t>(request.ignoredOwnerId - 1u);
			}
			physicsRequests.push_back(std::move(physicsRequest));
		}

		std::vector<VansEngine::VansPhysicsShapeCastResult> physicsResults;
		VansEngine::VansPhysicsQuery::CastClosestBatch(physicsRequests, physicsResults);
		for (std::size_t index = 0; index < requests.size(); ++index)
		{
			VansWorldQueryResult& result = results[index];
			result.requestId = requests[index].requestId;
			if (index >= physicsResults.size() || !physicsResults[index].hit)
				continue;

			const VansEngine::VansPhysicsQueryHit& hit = physicsResults[index].value;
			result.hit = true;
			result.positionWorld = hit.position;
			result.normalWorld = hit.normal;
			result.distance = hit.distance;
			result.layerIndex = hit.layerIndex;
			result.supportMovable = hit.supportMovable;
			result.supportPositionWorld = hit.supportPosition;
			result.supportRotationWorld = hit.supportRotation;
			result.hasSupportTransform = hit.hasSupportTransform;
			if (hit.transformId != (std::numeric_limits<std::uint32_t>::max)())
			{
				result.support.id = static_cast<std::uint64_t>(hit.transformId) + 1u;
				result.support.generation =
					Vans::VansTransformStore::GetGeneration(hit.transformId);
			}
		}
	}
}
