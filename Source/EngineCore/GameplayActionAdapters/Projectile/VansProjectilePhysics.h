#pragma once
#include "VansProjectileActionService.h"
#include "../../PhysicsCore/VansPhysicsNode.h"

namespace Vans
{
// Scene 实例化与无图形物理检查共用同一个属性构造入口。
inline VansEngine::PhysicsNodeProperties VansBuildProjectilePhysicsProperties(
    const VansProjectileSpawnRequest& request, const glm::vec3& boundsMin,
    const glm::vec3& boundsMax, const glm::vec3& worldScale)
{
    using namespace VansEngine;
    const auto combine = [](const std::string& name)
    {
        if (name == "Min") return PhysicsMaterialCombineMode::Min;
        if (name == "Multiply") return PhysicsMaterialCombineMode::Multiply;
        if (name == "Max") return PhysicsMaterialCombineMode::Max;
        return PhysicsMaterialCombineMode::Average;
    };
    PhysicsNodeProperties properties;
    properties.enabled = true;
    properties.bodyType = PhysicsBodyType::Dynamic;
    properties.colliderType = PhysicsColliderType::Box;
    properties.boxExtents = (boundsMax - boundsMin) * 0.5f;
    properties.shapeOffset = (boundsMax + boundsMin) * 0.5f * worldScale;
    properties.mass = request.mass;
    properties.layerName = request.collisionLayer;
    properties.material.restitution = request.restitution;
    properties.material.staticFriction = request.friction;
    properties.material.dynamicFriction = request.friction;
    properties.material.restitutionCombine = combine(request.restitutionCombine);
    properties.material.frictionCombine = combine(request.frictionCombine);
    return properties;
}

inline void VansLaunchProjectileBody(physx::PxRigidDynamic& actor, const VansProjectileSpawnRequest& request)
{
    physx::PxRigidBodyExt::setMassAndUpdateInertia(actor, request.mass);
    actor.setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_SPECULATIVE_CCD, true);
    actor.setLinearVelocity(physx::PxVec3(request.velocity.x, request.velocity.y, request.velocity.z));
    actor.setAngularVelocity(physx::PxVec3(request.angularVelocity.x, request.angularVelocity.y, request.angularVelocity.z));
}
}
