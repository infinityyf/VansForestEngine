#pragma once

#include <string>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "../VansNode.h"
#include "../AssetCore/VansTriangleMeshData.h"

namespace physx
{
    class PxRigidActor;
    class PxMaterial;
    class PxShape;
    class PxTriangleMesh;
    class PxConvexMesh;
}

// Forward declaration from VansGraphics namespace
namespace VansGraphics
{
    class VansScene;
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
    enum class PhysicsMaterialCombineMode { Average, Min, Multiply, Max };
    struct PhysicsMaterialProperties
    {
        float staticFriction = 0.5f;
        float dynamicFriction = 0.5f;
        float restitution = 0.3f;  // Bounciness
        PhysicsMaterialCombineMode frictionCombine = PhysicsMaterialCombineMode::Average;
        PhysicsMaterialCombineMode restitutionCombine = PhysicsMaterialCombineMode::Average;
    };

    // Physics node creation properties assembled by scene or gameplay adapters.
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
        glm::vec3 shapeOffset = glm::vec3(0.0f);
        glm::vec3 boxExtents = glm::vec3(1.0f, 1.0f, 1.0f);
        float sphereRadius = 1.0f;
        float capsuleRadius = 0.5f;
        float capsuleHalfHeight = 1.0f;

        // Initial dynamic-body state. Runtime gameplay requests populate these
        // before actor publication so no caller needs mutable PhysX access.
        bool enableSpeculativeCcd = false;
        glm::vec3 initialLinearVelocity = glm::vec3(0.0f);
        glm::vec3 initialAngularVelocity = glm::vec3(0.0f);

        // ── 碰撞 Layer ──────────────────────────────────────────────
        std::string layerName = "Default";  // 配置在 JSON 中的 layer 名称

        // ── Trigger 模式 ────────────────────────────────────────────
        bool isTrigger = false;             // 为 true 时作为触发器，不产生物理碰撞响应
        bool canCharacterStepUp = true;
        std::string hitRegion;              // 人体分区标识；空值表示普通碰撞体
    };

    class VansPhysicsNode;
    struct VansStaticMeshPhysicsInput
    {
        VansPhysicsNode* node = nullptr;
        PhysicsNodeProperties properties;
        uint32_t transformID = 0;
        std::shared_ptr<const Vans::VansTriangleMeshData> geometry;
    };

    // Physics Node - manages physics actor and integrates with scene
    class VansPhysicsNode : public VansGraphics::VansNode
    {
    public:
        VansPhysicsNode();
        ~VansPhysicsNode();

        // Lifecycle
        // Disabled nodes share immutable CPU data until first cooking; cooked actors retain no source pointer.
        void Initialize(const PhysicsNodeProperties& properties, uint32_t transformID,
            std::shared_ptr<const Vans::VansTriangleMeshData> collisionMesh = {});
        void Shutdown();
        // Publishes fresh static mesh actors under one simulation lease; failure releases the whole batch.
        // Existing resources are rejected and retained. A missing mesh never becomes a fallback collider.
        static bool InitializeStaticMeshBatch(const std::vector<VansStaticMeshPhysicsInput>& input, std::string& error);

        // Synchronize an authored transform through the node-owned lock.
        void SyncActorFromTransform();

        // Physics control
        // [迁移到 VansNode] SetEnabled/IsEnabled 由基类提供

    protected:
        void OnEnable()  override;
        void OnDisable() override;
        void OnDestroy() override;

    public:
        // Dynamic body control
        bool ApplyImpulseAtPosition(const glm::vec3& impulse, const glm::vec3& worldPoint, float maxAngularDelta);
        bool ResetMotion(const glm::vec3& position, const glm::vec3& rotationDegrees,
            const glm::vec3& linearVelocity, const glm::vec3& angularVelocity);
        glm::vec3 GetLinearVelocity() const;
        glm::vec3 GetAngularVelocity() const;

        // Properties access
        const PhysicsNodeProperties& GetProperties() const { return m_Properties; }
        bool HasActor() const;
        bool IsInScene() const;
        const void* GetActorIdentity() const { return m_Actor; }
        uint32_t GetTransformID() const { return m_TransformID; }

        // Name
        void SetName(const std::string& name) { m_Name = name; }
        const std::string& GetName() const { return m_Name; }

    private:
        friend class VansGraphics::VansScene;
        friend class VansPhysicsCollisionGeometry;

        // Scene batches already hold VansPhysicsSystem::GetSimulationMutex().
        // These methods make that precondition explicit and avoid recursive locking.
        void InitializeLocked(const PhysicsNodeProperties& properties, uint32_t transformID,
            std::shared_ptr<const Vans::VansTriangleMeshData> collisionMesh,
            physx::PxTriangleMesh* cookedPrototype = nullptr);
        void ShutdownLocked();
        void SyncActorFromTransformLocked();
        bool SyncTransformFromActorLocked();

        // Helper methods
        void CreatePhysicsActor(const Vans::VansTriangleMeshData* collisionMesh);
        void CreateCollisionShape(const Vans::VansTriangleMeshData* collisionMesh);
        bool ApplyFilterData();
        void UpdateShapeGeometryFromTransformScale();
        physx::PxShape* CreateBoxShape();
        physx::PxShape* CreateSphereShape();
        physx::PxShape* CreateCapsuleShape();
        physx::PxShape* CreateMeshShape(const Vans::VansTriangleMeshData* collisionMesh);
        physx::PxShape* CreateConvexMeshShape(const Vans::VansTriangleMeshData* collisionMesh);
        physx::PxMaterial* CreatePhysicsMaterial();

        // Data
        std::string m_Name;
        PhysicsNodeProperties m_Properties;
        uint32_t m_TransformID = 0;
        std::shared_ptr<const Vans::VansTriangleMeshData> m_UncookedGeometry;
        // PhysX objects
        physx::PxRigidActor* m_Actor = nullptr;
        physx::PxMaterial* m_Material = nullptr;
        physx::PxShape* m_Shape = nullptr;
        glm::vec3 m_AppliedShapeScale = glm::vec3(1.0f);
        
        // Cooked mesh data (owned by this node)
        physx::PxTriangleMesh* m_TriangleMesh = nullptr;
        physx::PxConvexMesh* m_ConvexMesh = nullptr;
    };
}
