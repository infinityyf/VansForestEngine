#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <PxPhysicsAPI.h>
#include <string>
#include <memory>
#include "../ScriptCore/VansTransform.h"
#include "VansPhysics.h"

using namespace physx;

// Forward declaration from VansGraphics namespace
namespace VansGraphics
{
    class VansMesh;
}

namespace VansEngine
{
    // Physics collider types
    enum class PhysicsColliderType
    {
        None = 0,
        Box,
        Sphere,
        Capsule,
        Mesh,           // Triangle mesh for static objects
        ConvexMesh      // Convex mesh for dynamic objects
    };

    // Physics body type
    enum class PhysicsBodyType
    {
        Static = 0,     // Immovable objects
        Dynamic,        // Physics-driven movement
        Kinematic       // Script-driven movement with collision
    };

    // Physics material properties
    struct PhysicsMaterialProperties
    {
        float staticFriction = 0.5f;
        float dynamicFriction = 0.5f;
        float restitution = 0.3f;  // Bounciness
    };

    // Physics node properties loaded from JSON
    struct PhysicsNodeProperties
    {
        bool enabled = false;
        PhysicsBodyType bodyType = PhysicsBodyType::Static;
        PhysicsColliderType colliderType = PhysicsColliderType::Box;
        float mass = 1.0f;
        bool useMeshCollider = false;
        bool useConvexDecomposition = false;
        PhysicsMaterialProperties material;
        
        // Collision shape parameters
        glm::vec3 boxExtents = glm::vec3(1.0f, 1.0f, 1.0f);
        float sphereRadius = 1.0f;
        float capsuleRadius = 0.5f;
        float capsuleHalfHeight = 1.0f;

        // ── 碰撞 Layer ──────────────────────────────────────────────
        std::string layerName = "Default";  // 配置在 JSON 中的 layer 名称
        int layerIndex = 0;                 // 运行时解析的 layer 索引

        // ── Trigger 模式 ────────────────────────────────────────────
        bool isTrigger = false;             // 为 true 时作为触发器，不产生物理碰撞响应
    };

    // Physics Node - manages physics actor and integrates with scene
    class VansPhysicsNode
    {
    public:
        VansPhysicsNode();
        ~VansPhysicsNode();

        // Lifecycle
        void Initialize(const PhysicsNodeProperties& properties, uint32_t transformID, VansGraphics::VansMesh* mesh = nullptr);
        void Shutdown();

        // Update transform from physics simulation
        bool UpdateTransformFromPhysics();
        
        // Update physics from transform (for kinematic objects)
        void UpdatePhysicsFromTransform();

        // Physics control
        void SetEnabled(bool enabled);
        bool IsEnabled() const { return m_Enabled; }

        // Dynamic body control
        void AddForce(const glm::vec3& force, PxForceMode::Enum mode = PxForceMode::eFORCE);
        void AddTorque(const glm::vec3& torque, PxForceMode::Enum mode = PxForceMode::eFORCE);
        void SetLinearVelocity(const glm::vec3& velocity);
        void SetAngularVelocity(const glm::vec3& velocity);
        glm::vec3 GetLinearVelocity() const;
        glm::vec3 GetAngularVelocity() const;

        // Collision queries
        void SetCollisionEnabled(bool enabled);
        bool IsCollisionEnabled() const;

        // Properties access
        const PhysicsNodeProperties& GetProperties() const { return m_Properties; }
        PxRigidActor* GetActor() const { return m_Actor; }
        uint32_t GetTransformID() const { return m_TransformID; }

        // Name
        void SetName(const std::string& name) { m_Name = name; }
        const std::string& GetName() const { return m_Name; }

    private:
        // Helper methods
        void CreatePhysicsActor();
        void CreateCollisionShape();
        void ApplyFilterData();
        PxShape* CreateBoxShape();
        PxShape* CreateSphereShape();
        PxShape* CreateCapsuleShape();
        PxShape* CreateMeshShape();
        PxShape* CreateConvexMeshShape();
        PxMaterial* CreatePhysicsMaterial();

        // Data
        std::string m_Name;
        PhysicsNodeProperties m_Properties;
        uint32_t m_TransformID = 0;
        VansGraphics::VansMesh* m_Mesh = nullptr;
        bool m_Enabled = false;

        // PhysX objects
        PxRigidActor* m_Actor = nullptr;
        PxMaterial* m_Material = nullptr;
        PxShape* m_Shape = nullptr;
        
        // Cooked mesh data (owned by this node)
        PxTriangleMesh* m_TriangleMesh = nullptr;
        PxConvexMesh* m_ConvexMesh = nullptr;
    };
}
