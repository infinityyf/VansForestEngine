#pragma once

#include <PxPhysicsAPI.h>

#include <cstdint>
#include <string>

namespace VansEngine
{
class VansCollisionFilter
{
public:
	enum Flag : std::uint32_t
	{
		None = 0u,
		Trigger = 1u << 0u,
		RagdollSelfCollision = 1u << 1u,
	};

	static bool Build(
		const std::string& layerName,
		std::uint32_t flags,
		std::uint32_t group,
		physx::PxFilterData& filterData);
};
}
