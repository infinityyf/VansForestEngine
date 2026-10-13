#include "VansPhysicsNode.h"
#include "VansPhysics.h"
#include "VansPhysicsNativeAccess.h"
#include "VansCollisionFilter.h"
#include "VansPhysicsShapeDimensions.h"
#include "../SceneRuntime/Transform/VansTransformStore.h"
#include "../Util/VansLog.h"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>
#include <unordered_set>
#include <unordered_map>
#include <new>

namespace VansEngine
{
	using namespace physx;

    bool VansPhysicsNode::ResetMotion(const glm::vec3& position, const glm::vec3& rotationDegrees,
        const glm::vec3& linearVelocity, const glm::vec3& angularVelocity)
    {
        const PxVec3 p(position.x,position.y,position.z), linear(linearVelocity.x,linearVelocity.y,linearVelocity.z),
            angular(angularVelocity.x,angularVelocity.y,angularVelocity.z);
        const auto q = glm::quat(glm::radians(rotationDegrees));
        const PxQuat rotation(q.x,q.y,q.z,q.w);
        if (!p.isFinite() || !rotation.isFinite() || !linear.isFinite() || !angular.isFinite()) return false;
        std::lock_guard<std::mutex> lock(VansPhysicsSystem::GetInstance().GetSimulationMutex());
        auto* body = m_Actor ? m_Actor->is<PxRigidDynamic>() : nullptr;
        if (!body || m_Properties.bodyType != PhysicsBodyType::Dynamic) return false;
        // 允许对暂时离开场景的池对象复位，重新启用前清掉旧力和旧速度。
        body->setGlobalPose(PxTransform(p,rotation),false);
        // 离场对象没有场景模拟器；removeActor 已移除待积累的力，不能访问其力累加器。
        if (body->getScene())
        {
            body->clearForce(PxForceMode::eFORCE); body->clearForce(PxForceMode::eIMPULSE);
            body->clearTorque(PxForceMode::eFORCE); body->clearTorque(PxForceMode::eIMPULSE);
        }
        body->setLinearVelocity(linear,false); body->setAngularVelocity(angular,false);
        if (body->getScene()) body->wakeUp();
        Vans::VansTransform transform = Vans::VansTransformStore::Read(m_TransformID);
        transform.m_Position = position; transform.m_Rotation = rotationDegrees;
        Vans::VansTransformStore::Write(m_TransformID, transform);
        return true;
    }

    bool VansPhysicsNode::ApplyImpulseAtPosition(const glm::vec3& impulse, const glm::vec3& worldPoint, float maxAngularDelta)
    {
        const PxVec3 force(impulse.x, impulse.y, impulse.z), point(worldPoint.x, worldPoint.y, worldPoint.z);
        if (!force.isFinite() || !point.isFinite() || !std::isfinite(maxAngularDelta) || maxAngularDelta < 0) return false;
        std::lock_guard<std::mutex> lock(VansPhysicsSystem::GetInstance().GetSimulationMutex());
        auto* body = m_Actor ? m_Actor->is<PxRigidDynamic>() : nullptr;
        if (!IsEnabled() || !body || !body->getScene() || body->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC)) return false;
        PxVec3 linearDelta, angularDelta;
        PxRigidBodyExt::computeVelocityDeltaFromImpulse(*body, body->getGlobalPose(), point, force,
            1.0f, 1.0f, linearDelta, angularDelta);
        const float angularSpeed = angularDelta.magnitude();
        if (!linearDelta.isFinite() || !angularDelta.isFinite() || !std::isfinite(angularSpeed)) return false;
        if (angularSpeed > maxAngularDelta) angularDelta *= maxAngularDelta / angularSpeed;
        const auto linear = body->getLinearVelocity() + linearDelta, angular = body->getAngularVelocity() + angularDelta;
        if (!linear.isFinite() || !angular.isFinite()) return false;
        body->setLinearVelocity(linear, true);
        body->setAngularVelocity(angular, true);
        return true;
    }

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

    void VansPhysicsNode::Initialize(const PhysicsNodeProperties& properties, uint32_t transformID,
        std::shared_ptr<const Vans::VansTriangleMeshData> collisionMesh)
    {
        auto& physics = VansPhysicsSystem::GetInstance();
        std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
        InitializeLocked(properties, transformID, std::move(collisionMesh));
    }

    bool VansPhysicsNode::InitializeStaticMeshBatch(const std::vector<VansStaticMeshPhysicsInput>& input, std::string& error)
    try
    {
        std::unordered_set<VansPhysicsNode*> identities;
        identities.reserve(input.size());
        std::unordered_map<const Vans::VansTriangleMeshData*, PxTriangleMesh*> prototypes;
        auto& physics = VansPhysicsSystem::GetInstance();
        std::lock_guard<std::mutex> lease(physics.GetSimulationMutex());
        for (const auto& item : input)
        {
            if (!item.node || !identities.insert(item.node).second || item.node->m_Actor || item.node->m_Material ||
                item.node->m_Shape || item.node->m_TriangleMesh || item.node->m_ConvexMesh || item.node->m_UncookedGeometry)
            { error = "Static mesh batch requires distinct fresh physics nodes"; return false; }
            if (!item.properties.enabled || item.properties.bodyType != PhysicsBodyType::Static ||
                item.properties.colliderType != PhysicsColliderType::Mesh || !item.properties.useMeshCollider || !item.geometry)
            { error = "Static mesh batch requires enabled static Mesh inputs"; return false; }
            if (prototypes.emplace(item.geometry.get(), nullptr).second &&
                !item.geometry->Validate((std::numeric_limits<std::uint64_t>::max)(), error)) return false;
        }
        try
        {
            for (const auto& item : input)
            {
                auto& prototype = prototypes.at(item.geometry.get());
                item.node->InitializeLocked(item.properties, item.transformID, item.geometry, prototype);
                if (!item.node->m_Actor || !item.node->m_Shape || !item.node->m_TriangleMesh)
                {
                    error = "Static mesh batch failed triangle-mesh cooking";
                    for (const auto& rollback : input) rollback.node->ShutdownLocked();
                    return false;
                }
                prototype = item.node->m_TriangleMesh;
            }
        }
        catch (...) { for (const auto& rollback : input) rollback.node->ShutdownLocked(); throw; }
        error.clear(); return true;
    }
    catch (const std::bad_alloc&) { error = "Static mesh batch allocation failed"; return false; }

    void VansPhysicsNode::InitializeLocked(
        const PhysicsNodeProperties& properties,
        uint32_t transformID,
        std::shared_ptr<const Vans::VansTriangleMeshData> collisionMesh,
        PxTriangleMesh* cookedPrototype)
    {
        ShutdownLocked();
        m_Properties = properties;
        m_TransformID = transformID;
        m_UncookedGeometry = std::move(collisionMesh);
        if (cookedPrototype)
        {
            cookedPrototype->acquireReference();
            m_TriangleMesh = cookedPrototype;
        }

        if (!m_Properties.enabled)
        {
            m_Enabled = false;
            return;
        }

        CreatePhysicsActor(m_UncookedGeometry.get());
		m_UncookedGeometry.reset();
		m_Enabled = m_Actor != nullptr && m_Shape != nullptr;
		if (!m_Enabled)
		{
			VANS_LOG_ERROR("[PhysX] Physics node initialization failed: actor or collision shape is missing");
			ShutdownLocked();
		}
    }

    void VansPhysicsNode::Shutdown()
    {
        if (!m_Actor && !m_Material && !m_TriangleMesh && !m_ConvexMesh)
        {
            m_Shape = nullptr;
            m_Enabled = false;
            m_UncookedGeometry.reset();
            return;
        }
        auto& physics = VansPhysicsSystem::GetInstance();
        std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
        ShutdownLocked();
    }

    void VansPhysicsNode::ShutdownLocked()
    {
        if (m_Actor)
        {
            // Remove from scene first
            PxScene* scene = m_Actor->getScene();
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

        // createMaterial 的初始引用由 Node 持有；Shape 只释放自己的引用。
        if (m_Material) m_Material->release();
        m_Material = nullptr;
        m_Shape = nullptr;
        m_Enabled = false;
        m_UncookedGeometry.reset();
    }

    void VansPhysicsNode::CreatePhysicsActor(const Vans::VansTriangleMeshData* collisionMesh)
    {
        VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
        PxPhysics* physics = VansPhysicsNativeAccess::Physics(physicsSystem);
        PxScene* scene = VansPhysicsNativeAccess::Scene(physicsSystem);

        if (!physics || !scene)
        {
            VANS_LOG_ERROR("Physics system not initialized!");
            return;
        }

        // Get transform from global storage
        const Vans::VansTransform& transformData = Vans::VansTransformStore::Read(m_TransformID);
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
                if (!m_Properties.hitRegion.empty())
                    dynamicActor->setRigidBodyFlag(PxRigidBodyFlag::eUSE_KINEMATIC_TARGET_FOR_SCENE_QUERIES, true);
            }
            
            m_Actor = dynamicActor;
        }

        if (!m_Actor)
        {
            VANS_LOG_ERROR("Failed to create physics actor!");
            return;
        }

        // Create and attach collision shape
        CreateCollisionShape(collisionMesh);

        // 惯量必须根据已挂载的形状计算；空 actor 上计算会丢失配置质量和惯量。
        if (m_Shape && m_Properties.bodyType == PhysicsBodyType::Dynamic && !needsKinematicUpgrade)
        {
            PxRigidDynamic* dynamicActor = m_Actor->is<PxRigidDynamic>();
            PxRigidBodyExt::setMassAndUpdateInertia(*dynamicActor, m_Properties.mass);
            dynamicActor->setRigidBodyFlag(
                PxRigidBodyFlag::eENABLE_SPECULATIVE_CCD,
                m_Properties.enableSpeculativeCcd);
            dynamicActor->setLinearVelocity(ToPxVec3(m_Properties.initialLinearVelocity));
            dynamicActor->setAngularVelocity(ToPxVec3(m_Properties.initialAngularVelocity));
        }

        // 设置碰撞 Layer 的 FilterData
        if (!ApplyFilterData())
        {
            VANS_LOG_ERROR("[PhysX] Unknown collision layer '" << m_Properties.layerName
                << "' for physics node '" << m_Name << "'");
            m_Actor->release();
            m_Actor = nullptr;
            m_Shape = nullptr;
            return;
        }

        // 将 VansPhysicsNode* 存入 userData，供碰撞回调使用
        m_Actor->userData = this;

        // Set actor name for debugging
        m_Actor->setName(m_Name.c_str());

        // Add to scene
        scene->addActor(*m_Actor);
    }

    void VansPhysicsNode::CreateCollisionShape(const Vans::VansTriangleMeshData* collisionMesh)
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
            shape = CreateMeshShape(collisionMesh);
            break;
        case PhysicsColliderType::ConvexMesh:
            shape = CreateConvexMeshShape(collisionMesh);
            break;
        default:
            shape = CreateBoxShape(); // Default to box
            break;
        }

        if (shape)
        {
            shape->setLocalPose(PxTransform(PxVec3(
                m_Properties.shapeOffset.x,
                m_Properties.shapeOffset.y,
                m_Properties.shapeOffset.z)));
            m_Actor->attachShape(*shape);
            m_Shape = shape;
            const Vans::VansTransform& transformData = Vans::VansTransformStore::Read(m_TransformID);
            m_AppliedShapeScale = transformData.m_Scale;
            shape->release(); // Actor holds a reference
        }
    }

    bool VansPhysicsNode::ApplyFilterData()
    {
        if (!m_Shape) return false;

        PxFilterData filterData;
        if (!VansCollisionFilter::Build(
            m_Properties.layerName,
            m_Properties.isTrigger ? VansCollisionFilter::Trigger : VansCollisionFilter::None,
            0u,
            filterData))
            return false;

        m_Shape->setSimulationFilterData(filterData);
        m_Shape->setQueryFilterData(filterData);

        // Trigger 需要特殊 Shape 标志
        if (m_Properties.isTrigger)
        {
            m_Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
            m_Shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
        }
        return true;
    }

    PxShape* VansPhysicsNode::CreateBoxShape()
    {
        VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
        PxPhysics* physics = VansPhysicsNativeAccess::Physics(physicsSystem);

        // Apply scale from transform
        const Vans::VansTransform& transformData = Vans::VansTransformStore::Read(m_TransformID);
        const auto scaledExtents=ResolvePhysicsShapeDimensions(m_Properties,transformData.m_Scale,VansPhysicsShapeScalePhase::Creation).boxExtents;
        
        PxBoxGeometry boxGeom(scaledExtents.x, scaledExtents.y, scaledExtents.z);
        return physics->createShape(boxGeom, *m_Material);
    }

    PxShape* VansPhysicsNode::CreateSphereShape()
    {
        VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
        PxPhysics* physics = VansPhysicsNativeAccess::Physics(physicsSystem);

        // Use uniform scale (average of x,y,z)
        const Vans::VansTransform& transformData = Vans::VansTransformStore::Read(m_TransformID);
        const auto scaledRadius=ResolvePhysicsShapeDimensions(m_Properties,transformData.m_Scale,VansPhysicsShapeScalePhase::Creation).sphereRadius;

        PxSphereGeometry sphereGeom(scaledRadius);
        return physics->createShape(sphereGeom, *m_Material);
    }

    PxShape* VansPhysicsNode::CreateCapsuleShape()
    {
        VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
        PxPhysics* physics = VansPhysicsNativeAccess::Physics(physicsSystem);

        // Apply scale
        const Vans::VansTransform& transformData = Vans::VansTransformStore::Read(m_TransformID);
        const auto dimensions=ResolvePhysicsShapeDimensions(m_Properties,transformData.m_Scale,VansPhysicsShapeScalePhase::Creation);
        const auto scaledRadius=dimensions.capsuleRadius;
        const auto scaledHalfHeight=dimensions.capsuleHalfHeight;

        PxCapsuleGeometry capsuleGeom(scaledRadius, scaledHalfHeight);
        return physics->createShape(capsuleGeom, *m_Material);
    }

    PxShape* VansPhysicsNode::CreateMeshShape(const Vans::VansTriangleMeshData* collisionMesh)
    {
        //Triangle mesh validation: NOT supported for dynamic bodies
        if (m_Properties.bodyType == PhysicsBodyType::Dynamic)
        {
            VANS_LOG_ERROR("[PhysX Error] Triangle mesh colliders are NOT supported for dynamic rigid bodies!");
            VANS_LOG_ERROR("              Use 'convex' collider type instead for dynamic objects, or change bodyType to 'static'/'kinematic'.");
            VANS_LOG_ERROR("              Falling back to box collider...");
            return CreateBoxShape();
        }

        if (!collisionMesh || !m_Properties.useMeshCollider)
        {
            VANS_LOG_ERROR("Mesh collider requested but no mesh available, falling back to box");
            return CreateBoxShape();
        }

        VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
        PxPhysics* physics = VansPhysicsNativeAccess::Physics(physicsSystem);

        if (!m_TriangleMesh)
        {
            std::string error;
            if (!collisionMesh->Validate((std::numeric_limits<std::uint64_t>::max)(), error))
            {
                VANS_LOG_ERROR("[PhysX] Mesh collider CPU data is invalid: " << error << "; falling back to box");
                return CreateBoxShape();
            }
            // Batch instances share an owned cooked prototype; single nodes cook directly from packed CPU input.
            PxTriangleMeshDesc meshDesc;
            meshDesc.points.count = static_cast<PxU32>(collisionMesh->VertexCount());
            meshDesc.points.stride = 3 * sizeof(float);
            meshDesc.points.data = collisionMesh->positions.data();
            meshDesc.triangles.count = static_cast<PxU32>(collisionMesh->TriangleCount());
            meshDesc.triangles.stride = 3 * sizeof(PxU32);
            meshDesc.triangles.data = collisionMesh->indices.data();
            m_TriangleMesh = VansPhysicsNativeAccess::CookTriangleMesh(physicsSystem, meshDesc);
        }
        
        if (!m_TriangleMesh)
        {
            VANS_LOG_ERROR("Failed to cook triangle mesh, falling back to box");
            return CreateBoxShape();
        }

        // Apply scale from transform
        const Vans::VansTransform& transformData = Vans::VansTransformStore::Read(m_TransformID);
        PxMeshScale meshScale(ToPxVec3(transformData.m_Scale));
        
        PxTriangleMeshGeometry triGeom(m_TriangleMesh, meshScale);
        return physics->createShape(triGeom, *m_Material);
    }

    PxShape* VansPhysicsNode::CreateConvexMeshShape(const Vans::VansTriangleMeshData* collisionMesh)
    {
        if (!collisionMesh || !m_Properties.useMeshCollider)
        {
            VANS_LOG_ERROR("Convex mesh collider requested but no mesh available, falling back to box");
            return CreateBoxShape();
        }

        VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
        PxPhysics* physics = VansPhysicsNativeAccess::Physics(physicsSystem);

        std::string error;
        if (!collisionMesh->Validate((std::numeric_limits<std::uint64_t>::max)(), error))
        {
            VANS_LOG_ERROR("[PhysX] Convex mesh collider CPU data is invalid: " << error << "; falling back to box");
            return CreateBoxShape();
        }

        // Cooking copies the input into the native mesh before Initialize returns.
        PxConvexMeshDesc convexDesc;
        convexDesc.points.count = static_cast<PxU32>(collisionMesh->VertexCount());
        convexDesc.points.stride = 3 * sizeof(float);
        convexDesc.points.data = collisionMesh->positions.data();
        convexDesc.flags = PxConvexFlag::eCOMPUTE_CONVEX;
        convexDesc.vertexLimit = 255;

        // Cook the convex mesh
        m_ConvexMesh = VansPhysicsNativeAccess::CookConvexMesh(physicsSystem, convexDesc);
        
        if (!m_ConvexMesh)
        {
            VANS_LOG_ERROR("Failed to cook convex mesh, falling back to box");
            return CreateBoxShape();
        }

        // Apply scale from transform
        const Vans::VansTransform& transformData = Vans::VansTransformStore::Read(m_TransformID);
        PxMeshScale meshScale(ToPxVec3(transformData.m_Scale));
        
        PxConvexMeshGeometry convexGeom(m_ConvexMesh, meshScale);
        return physics->createShape(convexGeom, *m_Material);
    }

    void VansPhysicsNode::UpdateShapeGeometryFromTransformScale()
    {
        if (!m_Shape || !Vans::VansTransformStore::IsAllocated(m_TransformID))
            return;

        const Vans::VansTransform& transformData = Vans::VansTransformStore::Read(m_TransformID);
        const glm::vec3 scale = transformData.m_Scale;
        const glm::vec3 delta = glm::abs(scale - m_AppliedShapeScale);
        if (delta.x <= 1e-5f && delta.y <= 1e-5f && delta.z <= 1e-5f)
            return;

        bool updated = true;
        const auto dimensions=ResolvePhysicsShapeDimensions(m_Properties,scale,VansPhysicsShapeScalePhase::Update);
        switch (m_Properties.colliderType)
        {
        case PhysicsColliderType::Box:
        {
            const auto scaledExtents=dimensions.boxExtents;
            m_Shape->setGeometry(PxBoxGeometry(scaledExtents.x, scaledExtents.y, scaledExtents.z));
            break;
        }
        case PhysicsColliderType::Sphere:
        {
            m_Shape->setGeometry(PxSphereGeometry(dimensions.sphereRadius));
            break;
        }
        case PhysicsColliderType::Capsule:
        {
            m_Shape->setGeometry(PxCapsuleGeometry(
                dimensions.capsuleRadius,dimensions.capsuleHalfHeight));
            break;
        }
        case PhysicsColliderType::Mesh:
            if (m_TriangleMesh)
                m_Shape->setGeometry(PxTriangleMeshGeometry(m_TriangleMesh, PxMeshScale(ToPxVec3(scale))));
            else
                updated = false;
            break;
        case PhysicsColliderType::ConvexMesh:
            if (m_ConvexMesh)
                m_Shape->setGeometry(PxConvexMeshGeometry(m_ConvexMesh, PxMeshScale(ToPxVec3(scale))));
            else
                updated = false;
            break;
        default:
            updated = false;
            break;
        }

        if (updated)
            m_AppliedShapeScale = scale;
        else
            VANS_LOG_WARN("[PhysX] Failed to update scaled geometry for node '" << m_Name << "'");
    }

    PxMaterial* VansPhysicsNode::CreatePhysicsMaterial()
    {
        VansPhysicsSystem& physicsSystem = VansPhysicsSystem::GetInstance();
        PxPhysics* physics = VansPhysicsNativeAccess::Physics(physicsSystem);

        auto* material = physics->createMaterial(
            m_Properties.material.staticFriction,
            m_Properties.material.dynamicFriction,
            m_Properties.material.restitution
        );
        const auto combine = [](PhysicsMaterialCombineMode mode)
        {
            switch (mode)
            {
            case PhysicsMaterialCombineMode::Min: return PxCombineMode::eMIN;
            case PhysicsMaterialCombineMode::Multiply: return PxCombineMode::eMULTIPLY;
            case PhysicsMaterialCombineMode::Max: return PxCombineMode::eMAX;
            default: return PxCombineMode::eAVERAGE;
            }
        };
        if (material)
        {
            material->setFrictionCombineMode(combine(m_Properties.material.frictionCombine));
            material->setRestitutionCombineMode(combine(m_Properties.material.restitutionCombine));
        }
        return material;
    }

    bool VansPhysicsNode::SyncTransformFromActorLocked()
    {
        if (!m_Actor || !m_Enabled)
            return false;

        // Only update for dynamic actors (static ones don't move by physics)
        if (m_Properties.bodyType != PhysicsBodyType::Dynamic)
            return false;

        PxTransform pxTransform = m_Actor->getGlobalPose();
        
        // Update global transform storage
        Vans::VansTransform transformData = Vans::VansTransformStore::Read(m_TransformID);
        
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
            Vans::VansTransformStore::Write(m_TransformID, transformData);
        }

        return changed;
    }

    void VansPhysicsNode::SyncActorFromTransform()
    {
        auto& physics = VansPhysicsSystem::GetInstance();
        std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
        SyncActorFromTransformLocked();
    }

    void VansPhysicsNode::SyncActorFromTransformLocked()
    {
        if (!m_Actor || !m_Enabled)
            return;

        // Kinematic 和 Trigger 都需要从 Transform 同步到 PhysX
        // Get transform from global storage
        const Vans::VansTransform& transformData = Vans::VansTransformStore::Read(m_TransformID);
        UpdateShapeGeometryFromTransformScale();
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

    void VansPhysicsNode::OnEnable()
    {
        auto& physSys = VansEngine::VansPhysicsSystem::GetInstance();
        std::lock_guard<std::mutex> lock(physSys.GetSimulationMutex());
        if (!m_Actor)
        {
            // Actor 尚未创建（例如 Initialize 时 enabled=false），按需创建
            CreatePhysicsActor(m_UncookedGeometry.get());
            m_UncookedGeometry.reset();
            if (!m_Actor || !m_Shape)
            {
                VANS_LOG_ERROR("[PhysX] Deferred physics activation failed: " << m_Name);
                ShutdownLocked();
            }
        }
        else
        {
            // 将已有 actor 重新加入 PhysX Scene
            PxScene* scene = VansPhysicsNativeAccess::Scene(physSys);
            if (scene)
                scene->addActor(*m_Actor);
        }
    }

    void VansPhysicsNode::OnDisable()
    {
        if (!m_Actor) return;
        auto& physSys = VansEngine::VansPhysicsSystem::GetInstance();
        std::lock_guard<std::mutex> lock(physSys.GetSimulationMutex());
        PxScene* scene = VansPhysicsNativeAccess::Scene(physSys);
        if (scene)
            scene->removeActor(*m_Actor);
    }

    void VansPhysicsNode::OnDestroy()
    {
        Shutdown();
    }

    glm::vec3 VansPhysicsNode::GetLinearVelocity() const
    {
        std::lock_guard<std::mutex> lock(VansPhysicsSystem::GetInstance().GetSimulationMutex());
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
        std::lock_guard<std::mutex> lock(VansPhysicsSystem::GetInstance().GetSimulationMutex());
        if (!m_Actor || m_Properties.bodyType != PhysicsBodyType::Dynamic)
            return glm::vec3(0.0f);

        PxRigidDynamic* dynamicActor = m_Actor->is<PxRigidDynamic>();
        if (dynamicActor)
        {
            return ToGlmVec3(dynamicActor->getAngularVelocity());
        }
        
        return glm::vec3(0.0f);
    }

    bool VansPhysicsNode::HasActor() const
    {
        std::lock_guard<std::mutex> lock(VansPhysicsSystem::GetInstance().GetSimulationMutex());
        return m_Actor != nullptr;
    }

    bool VansPhysicsNode::IsInScene() const
    {
        std::lock_guard<std::mutex> lock(VansPhysicsSystem::GetInstance().GetSimulationMutex());
        return m_Actor && m_Actor->getScene();
    }
}
