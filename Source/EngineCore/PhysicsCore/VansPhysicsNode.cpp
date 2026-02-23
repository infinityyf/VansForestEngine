#include "VansPhysicsNode.h"
#include "../RenderCore/VulkanCore/VansMesh.h"
#include "../ScriptCore/VansTransform.h"
#include <iostream>

namespace VansEngine
{
    // Helper: Convert glm to PxVec3
    inline PxVec3 ToPxVec3(const glm::vec3& v) { return PxVec3(v.x, v.y, v.z); }
    
    // Helper: Convert PxVec3 to glm
    inline glm::vec3 ToGlmVec3(const PxVec3& v) { return glm::vec3(v.x, v.y, v.z); }
    
    // Helper: Convert glm quat to PxQuat
    inline PxQuat ToPxQuat(const glm::quat& q) { return PxQuat(q.x, q.y, q.z, q.w); }
    
    // Helper: Convert PxQuat to glm
    inline glm::quat ToGlmQuat(const PxQuat& q) { return glm::quat(q.w, q.x, q.y, q.z); }

    VansPhysicsNode::VansPhysicsNode()
        : m_TransformID(0)
        , m_Mesh(nullptr)
        , m_Enabled(false)
        , m_Actor(nullptr)
        , m_Material(nullptr)
        , m_Shape(nullptr)
        , m_TriangleMesh(nullptr)
        , m_ConvexMesh(nullptr)
    {
    }

    VansPhysicsNode::~VansPhysicsNode()
    {
        Shutdown();
    }

    void VansPhysicsNode::Initialize(const PhysicsNodeProperties& properties, uint32_t transformID, VansGraphics::VansMesh* mesh)
    {
        m_Properties = properties;
        m_TransformID = transformID;
        m_Mesh = mesh;

        if (!m_Properties.enabled)
        {
            return;
        }

        CreatePhysicsActor();
        m_Enabled = true;
    }

    void VansPhysicsNode::Shutdown()
    {
        if (m_Actor)
        {
            // Remove from scene first
            PxScene* scene = VansPhysicsSystem::GetInstance().GetScene();
            if (scene)
            {
                scene->removeActor(*m_Actor);
            }
            
            m_Actor->release();
            m_Actor = nullptr;
        }

        // Release cooked meshes
        if (m_TriangleMesh)
        {
            m_TriangleMesh->release();
            m_TriangleMesh = nullptr;
        }

        if (m_ConvexMesh)
        {
            m_ConvexMesh->release();
            m_ConvexMesh = nullptr;
        }

        // Material is released by PhysX automatically
        m_Material = nullptr;
        m_Shape = nullptr;
        m_Enabled = false;
    }

    void VansPhysicsNode::CreatePhysicsActor()
    {
        VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
        PxPhysics* physics = physicsSystem.GetPhysics();
        PxScene* scene = physicsSystem.GetScene();

        if (!physics || !scene)
        {
            std::cerr << "Physics system not initialized!" << std::endl;
            return;
        }

        // Get transform from global storage
        const VansGraphics::VansTransform& transformData = VansGraphics::VansTransformStore::GlobalTransforms[m_TransformID];
        PxVec3 position = ToPxVec3(transformData.m_Position);
        PxQuat rotation = ToPxQuat(glm::quat(glm::radians(transformData.m_Rotation)));
        PxTransform transform(position, rotation);

        // Create material
        m_Material = CreatePhysicsMaterial();

        // Create actor based on body type
        if (m_Properties.bodyType == PhysicsBodyType::Static)
        {
            m_Actor = physics->createRigidStatic(transform);
        }
        else
        {
            PxRigidDynamic* dynamicActor = physics->createRigidDynamic(transform);
            
            if (m_Properties.bodyType == PhysicsBodyType::Kinematic)
            {
                dynamicActor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
            }
            else // Dynamic
            {
                // Set mass and calculate inertia
                PxRigidBodyExt::setMassAndUpdateInertia(*dynamicActor, m_Properties.mass);
            }
            
            m_Actor = dynamicActor;
        }

        if (!m_Actor)
        {
            std::cerr << "Failed to create physics actor!" << std::endl;
            return;
        }

        // Create and attach collision shape
        CreateCollisionShape();

        // Set actor name for debugging
        m_Actor->setName(m_Name.c_str());

        // Add to scene
        scene->addActor(*m_Actor);
    }

    void VansPhysicsNode::CreateCollisionShape()
    {
        PxShape* shape = nullptr;

        switch (m_Properties.colliderType)
        {
        case PhysicsColliderType::Box:
            shape = CreateBoxShape();
            break;
        case PhysicsColliderType::Sphere:
            shape = CreateSphereShape();
            break;
        case PhysicsColliderType::Capsule:
            shape = CreateCapsuleShape();
            break;
        case PhysicsColliderType::Mesh:
            shape = CreateMeshShape();
            break;
        case PhysicsColliderType::ConvexMesh:
            shape = CreateConvexMeshShape();
            break;
        default:
            shape = CreateBoxShape(); // Default to box
            break;
        }

        if (shape)
        {
            m_Actor->attachShape(*shape);
            m_Shape = shape;
            shape->release(); // Actor holds a reference
        }
    }

    PxShape* VansPhysicsNode::CreateBoxShape()
    {
        VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
        PxPhysics* physics = physicsSystem.GetPhysics();

        // Apply scale from transform
        const VansGraphics::VansTransform& transformData = VansGraphics::VansTransformStore::GlobalTransforms[m_TransformID];
        glm::vec3 scaledExtents = m_Properties.boxExtents * transformData.m_Scale;
        
        PxBoxGeometry boxGeom(scaledExtents.x, scaledExtents.y, scaledExtents.z);
        return physics->createShape(boxGeom, *m_Material);
    }

    PxShape* VansPhysicsNode::CreateSphereShape()
    {
        VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
        PxPhysics* physics = physicsSystem.GetPhysics();

        // Use uniform scale (average of x,y,z)
        const VansGraphics::VansTransform& transformData = VansGraphics::VansTransformStore::GlobalTransforms[m_TransformID];
        float avgScale = (transformData.m_Scale.x + transformData.m_Scale.y + transformData.m_Scale.z) / 3.0f;
        float scaledRadius = m_Properties.sphereRadius * avgScale;

        PxSphereGeometry sphereGeom(scaledRadius);
        return physics->createShape(sphereGeom, *m_Material);
    }

    PxShape* VansPhysicsNode::CreateCapsuleShape()
    {
        VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
        PxPhysics* physics = physicsSystem.GetPhysics();

        // Apply scale
        const VansGraphics::VansTransform& transformData = VansGraphics::VansTransformStore::GlobalTransforms[m_TransformID];
        float avgRadiusScale = (transformData.m_Scale.x + transformData.m_Scale.z) / 2.0f;
        float scaledRadius = m_Properties.capsuleRadius * avgRadiusScale;
        float scaledHalfHeight = m_Properties.capsuleHalfHeight * transformData.m_Scale.y;

        PxCapsuleGeometry capsuleGeom(scaledRadius, scaledHalfHeight);
        return physics->createShape(capsuleGeom, *m_Material);
    }

    PxShape* VansPhysicsNode::CreateMeshShape()
    {
        //Triangle mesh validation: NOT supported for dynamic bodies
        if (m_Properties.bodyType == PhysicsBodyType::Dynamic)
        {
            std::cerr << "[PhysX Error] Triangle mesh colliders are NOT supported for dynamic rigid bodies!" << std::endl;
            std::cerr << "              Use 'convex' collider type instead for dynamic objects, or change bodyType to 'static'/'kinematic'." << std::endl;
            std::cerr << "              Falling back to box collider..." << std::endl;
            return CreateBoxShape();
        }

        if (!m_Mesh || !m_Properties.useMeshCollider)
        {
            std::cerr << "Mesh collider requested but no mesh available, falling back to box" << std::endl;
            return CreateBoxShape();
        }

        VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
        PxPhysics* physics = physicsSystem.GetPhysics();

        // Build triangle mesh from VansMesh
        PxTriangleMeshDesc meshDesc;
        meshDesc.points.count = m_Mesh->GetMeshVertexCount();
        meshDesc.points.stride = sizeof(float) * 8;
        meshDesc.points.data = m_Mesh->GetMeshRawPositionData().data();

        meshDesc.triangles.count = static_cast<PxU32>(m_Mesh->GetMeshTriangleIndex().size() / 3);
        meshDesc.triangles.stride = 3 * sizeof(int);
        meshDesc.triangles.data = m_Mesh->GetMeshTriangleIndex().data();

        // Cook the triangle mesh
        m_TriangleMesh = physicsSystem.CookTriangleMesh(meshDesc);
        
        if (!m_TriangleMesh)
        {
            std::cerr << "Failed to cook triangle mesh, falling back to box" << std::endl;
            return CreateBoxShape();
        }

        // Apply scale from transform
        const VansGraphics::VansTransform& transformData = VansGraphics::VansTransformStore::GlobalTransforms[m_TransformID];
        PxMeshScale meshScale(ToPxVec3(transformData.m_Scale));
        
        PxTriangleMeshGeometry triGeom(m_TriangleMesh, meshScale);
        return physics->createShape(triGeom, *m_Material);
    }

    PxShape* VansPhysicsNode::CreateConvexMeshShape()
    {
        if (!m_Mesh || !m_Properties.useMeshCollider)
        {
            std::cerr << "Convex mesh collider requested but no mesh available, falling back to box" << std::endl;
            return CreateBoxShape();
        }

        VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
        PxPhysics* physics = physicsSystem.GetPhysics();

        // Build convex mesh from VansMesh
        PxConvexMeshDesc convexDesc;
        convexDesc.points.count = m_Mesh->GetMeshVertexCount();
        convexDesc.points.stride = sizeof(float) * 8;
        convexDesc.points.data = m_Mesh->GetMeshRawPositionData().data();
        convexDesc.flags = PxConvexFlag::eCOMPUTE_CONVEX;
        convexDesc.vertexLimit = 255;
        
        // Cook the convex mesh
        m_ConvexMesh = physicsSystem.CookConvexMesh(convexDesc);
        
        if (!m_ConvexMesh)
        {
            std::cerr << "Failed to cook convex mesh, falling back to box" << std::endl;
            return CreateBoxShape();
        }

        // Apply scale from transform
        const VansGraphics::VansTransform& transformData = VansGraphics::VansTransformStore::GlobalTransforms[m_TransformID];
        PxMeshScale meshScale(ToPxVec3(transformData.m_Scale));
        
        PxConvexMeshGeometry convexGeom(m_ConvexMesh, meshScale);
        return physics->createShape(convexGeom, *m_Material);
    }

    PxMaterial* VansPhysicsNode::CreatePhysicsMaterial()
    {
        VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
        PxPhysics* physics = physicsSystem.GetPhysics();

        return physics->createMaterial(
            m_Properties.material.staticFriction,
            m_Properties.material.dynamicFriction,
            m_Properties.material.restitution
        );
    }

    bool VansPhysicsNode::UpdateTransformFromPhysics()
    {
        if (!m_Actor || !m_Enabled)
            return false;

        // Only update for dynamic actors (static ones don't move by physics)
        if (m_Properties.bodyType != PhysicsBodyType::Dynamic)
            return false;

        PxTransform pxTransform = m_Actor->getGlobalPose();
        
        // Update global transform storage
        VansGraphics::VansTransform& transformData = VansGraphics::VansTransformStore::GlobalTransforms[m_TransformID];
        
        glm::vec3 newPos = ToGlmVec3(pxTransform.p);
        glm::quat newRotQuat = ToGlmQuat(pxTransform.q);
        glm::vec3 newRotEuler = glm::degrees(glm::eulerAngles(newRotQuat));

        // Check if actually changed to avoid unnecessary GPU updates
        // Epsilon comparison could be used here for more robustness
        bool changed = (transformData.m_Position != newPos) || (transformData.m_Rotation != newRotEuler);

        if (changed)
        {
            transformData.m_Position = newPos;
            transformData.m_Rotation = newRotEuler;
        }

        return changed;
    }

    void VansPhysicsNode::UpdatePhysicsFromTransform()
    {
        if (!m_Actor || !m_Enabled)
            return;

        // Only update for kinematic actors
        if (m_Properties.bodyType != PhysicsBodyType::Kinematic)
            return;

        PxRigidDynamic* dynamicActor = m_Actor->is<PxRigidDynamic>();
        if (!dynamicActor)
            return;

        // Get transform from global storage
        const VansGraphics::VansTransform& transformData = VansGraphics::VansTransformStore::GlobalTransforms[m_TransformID];
        PxVec3 position = ToPxVec3(transformData.m_Position);
        PxQuat rotation = ToPxQuat(glm::quat(glm::radians(transformData.m_Rotation)));
        PxTransform transform(position, rotation);

        // Set kinematic target
        dynamicActor->setKinematicTarget(transform);
    }

    void VansPhysicsNode::SetEnabled(bool enabled)
    {
        if (m_Enabled == enabled)
            return;

        if (enabled && !m_Actor)
        {
            CreatePhysicsActor();
        }
        else if (!enabled && m_Actor)
        {
            PxScene* scene = VansPhysicsSystem::GetInstance().GetScene();
            if (scene)
            {
                scene->removeActor(*m_Actor);
            }
        }

        m_Enabled = enabled;
    }

    void VansPhysicsNode::AddForce(const glm::vec3& force, PxForceMode::Enum mode)
    {
        if (!m_Actor || m_Properties.bodyType != PhysicsBodyType::Dynamic)
            return;

        PxRigidDynamic* dynamicActor = m_Actor->is<PxRigidDynamic>();
        if (dynamicActor)
        {
            dynamicActor->addForce(ToPxVec3(force), mode);
        }
    }

    void VansPhysicsNode::AddTorque(const glm::vec3& torque, PxForceMode::Enum mode)
    {
        if (!m_Actor || m_Properties.bodyType != PhysicsBodyType::Dynamic)
            return;

        PxRigidDynamic* dynamicActor = m_Actor->is<PxRigidDynamic>();
        if (dynamicActor)
        {
            dynamicActor->addTorque(ToPxVec3(torque), mode);
        }
    }

    void VansPhysicsNode::SetLinearVelocity(const glm::vec3& velocity)
    {
        if (!m_Actor || m_Properties.bodyType != PhysicsBodyType::Dynamic)
            return;

        PxRigidDynamic* dynamicActor = m_Actor->is<PxRigidDynamic>();
        if (dynamicActor)
        {
            dynamicActor->setLinearVelocity(ToPxVec3(velocity));
        }
    }

    void VansPhysicsNode::SetAngularVelocity(const glm::vec3& velocity)
    {
        if (!m_Actor || m_Properties.bodyType != PhysicsBodyType::Dynamic)
            return;

        PxRigidDynamic* dynamicActor = m_Actor->is<PxRigidDynamic>();
        if (dynamicActor)
        {
            dynamicActor->setAngularVelocity(ToPxVec3(velocity));
        }
    }

    glm::vec3 VansPhysicsNode::GetLinearVelocity() const
    {
        if (!m_Actor || m_Properties.bodyType != PhysicsBodyType::Dynamic)
            return glm::vec3(0.0f);

        PxRigidDynamic* dynamicActor = m_Actor->is<PxRigidDynamic>();
        if (dynamicActor)
        {
            return ToGlmVec3(dynamicActor->getLinearVelocity());
        }
        
        return glm::vec3(0.0f);
    }

    glm::vec3 VansPhysicsNode::GetAngularVelocity() const
    {
        if (!m_Actor || m_Properties.bodyType != PhysicsBodyType::Dynamic)
            return glm::vec3(0.0f);

        PxRigidDynamic* dynamicActor = m_Actor->is<PxRigidDynamic>();
        if (dynamicActor)
        {
            return ToGlmVec3(dynamicActor->getAngularVelocity());
        }
        
        return glm::vec3(0.0f);
    }

    void VansPhysicsNode::SetCollisionEnabled(bool enabled)
    {
        if (!m_Shape)
            return;

        if (enabled)
        {
            m_Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
        }
        else
        {
            m_Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
        }
    }

    bool VansPhysicsNode::IsCollisionEnabled() const
    {
        if (!m_Shape)
            return false;

        return m_Shape->getFlags() & PxShapeFlag::eSIMULATION_SHAPE;
    }
}
