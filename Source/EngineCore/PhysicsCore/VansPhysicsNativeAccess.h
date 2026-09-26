#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "VansPhysics.h"

#include <PxPhysicsAPI.h>
#include <characterkinematic/PxControllerManager.h>

namespace VansEngine
{
	// Bridge for PhysicsCore and explicit physics integration adapters. Other
	// engine modules use VansPhysicsSystem, VansPhysicsQuery, or a typed node.
	class VansPhysicsNativeAccess final
	{
	public:
		static physx::PxScene* Scene(VansPhysicsSystem& system);
		static const physx::PxScene* Scene(const VansPhysicsSystem& system);
		static physx::PxPhysics* Physics(VansPhysicsSystem& system);
		static physx::PxControllerManager* ControllerManager(VansPhysicsSystem& system);
		static physx::PxMaterial* DefaultMaterial(VansPhysicsSystem& system);
		static const physx::PxCookingParams* CookingParams(
			const VansPhysicsSystem& system);

		static physx::PxConvexMesh* CookConvexMesh(
			VansPhysicsSystem& system,
			const physx::PxConvexMeshDesc& desc);
		static physx::PxTriangleMesh* CookTriangleMesh(
			VansPhysicsSystem& system,
			const physx::PxTriangleMeshDesc& desc);
		static physx::PxHeightField* CookHeightField(
			VansPhysicsSystem& system,
			const physx::PxHeightFieldDesc& desc);
	};

	physx::PxFilterFlags VansCollisionFilterShader(
		physx::PxFilterObjectAttributes attributes0,
		physx::PxFilterData filterData0,
		physx::PxFilterObjectAttributes attributes1,
		physx::PxFilterData filterData1,
		physx::PxPairFlags& pairFlags,
		const void* constantBlock,
		physx::PxU32 constantBlockSize);
}
