#include "VansPhysicsNode.h"
#include "VansCollisionLayerManager.h"
#include "../RenderCore/VulkanCore/VansMesh.h"
#include "../ScriptCore/VansTransform.h"
#include "../Util/VansLog.h"
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
		m_Enabled = m_Actor != nullptr && m_Shape != nullptr;
		if (!m_Enabled)
		{
			VANS_LOG_ERROR("[PhysX] Physics node initialization failed: actor or collision shape is missing");
			Shutdown();
		}
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
            VANS_LOG_ERROR("Physics system not initialized!");
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
        // Trigger 物体如果配置为 Static，需要升级为 Kinematic 以支持 setKinematicTarget
        bool needsKinematicUpgrade = m_Properties.isTrigger &&
                                     m_Properties.bodyType == PhysicsBodyType::Static;

        if (m_Properties.bodyType == PhysicsBodyType::Static && !needsKinematicUpgrade)
        {
            m_Actor = physics->createRigidStatic(transform);
        }
        else
        {
            PxRigidDynamic* dynamicActor = physics->createRigidDynamic(transform);
            
            if (m_Properties.bodyType == PhysicsBodyType::Kinematic || needsKinematicUpgrade)
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
            VANS_LOG_ERROR("Failed to create physics actor!");
            return;
        }

        // Create and attach collision shape
        CreateCollisionShape();

        // 设置碰撞 Layer 的 FilterData
        ApplyFilterData();

        // 将 VansPhysicsNode* 存入 userData，供碰撞回调使用
        m_Actor->userData = this;

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

    void VansPhysicsNode::ApplyFilterData()
    {
        if (!m_Shape) return;

        auto& layerMgr = VansCollisionLayerManager::Get();
        int layerIdx = layerMgr.GetLayerIndex(m_Properties.layerName);

        PxFilterData filterData;
        filterData.word0 = static_cast<PxU32>(layerIdx);
        filterData.word1 = layerMgr.GetCollisionMask(layerIdx);
        filterData.word2 = m_Properties.isTrigger ? 1u : 0u;
        filterData.word3 = 0;

        VANS_LOG("[PhysX] ApplyFilterData: node='" << m_Name
                 << "' layer='" << m_Properties.layerName
                 << "' layerIdx=" << layerIdx
                 << " mask=0x" << std::hex << filterData.word1 << std::dec
                 << " isTrigger=" << m_Properties.isTrigger
                 << " userData=" << m_Actor->userData);

        m_Shape->setSimulationFilterData(filterData);
        m_Shape->setQueryFilterData(filterData);

        // Trigger 需要特殊 Shape 标志
        if (m_Properties.isTrigger)
        {
            m_Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
            m_Shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
            VANS_LOG("[PhysX] ApplyFilterData: set eTRIGGER_SHAPE for '" << m_Name << "'");
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
            VANS_LOG_ERROR("[PhysX Error] Triangle mesh colliders are NOT supported for dynamic rigid bodies!");
            VANS_LOG_ERROR("              Use 'convex' collider type instead for dynamic objects, or change bodyType to 'static'/'kinematic'.");
            VANS_LOG_ERROR("              Falling back to box collider...");
            return CreateBoxShape();
        }

        if (!m_Mesh || !m_Properties.useMeshCollider)
        {
            VANS_LOG_ERROR("Mesh collider requested but no mesh available, falling back to box");
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
            VANS_LOG_ERROR("Failed to cook triangle mesh, falling back to box");
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
            VANS_LOG_ERROR("Convex mesh collider requested but no mesh available, falling back to box");
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
            VANS_LOG_ERROR("Failed to cook convex mesh, falling back to box");
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

        // Kinematic 和 Trigger 都需要从 Transform 同步到 PhysX
        // Get transform from global storage
        const VansGraphics::VansTransform& transformData = VansGraphics::VansTransformStore::GlobalTransforms[m_TransformID];
        PxVec3 position = ToPxVec3(transformData.m_Position);
        PxQuat rotation = ToPxQuat(glm::quat(glm::radians(transformData.m_Rotation)));
        PxTransform transform(position, rotation);

		if (m_Properties.bodyType == PhysicsBodyType::Static && !m_Properties.isTrigger)
		{
			m_Actor->setGlobalPose(transform);
			return;
		}

		if (m_Properties.bodyType != PhysicsBodyType::Kinematic && !m_Properties.isTrigger)
			return;

		PxRigidDynamic* dynamicActor = m_Actor->is<PxRigidDynamic>();
		if (!dynamicActor)
			return;

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
