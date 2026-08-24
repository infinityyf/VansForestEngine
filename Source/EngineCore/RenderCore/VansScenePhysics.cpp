#include "VansScene.h"

#include "SceneBuild/VansScenePhysicsComponentBuilder.h"

#include "../PhysicsCore/VansPhysics.h"
#include "../PhysicsCore/VansPhysicsNode.h"
#include "../PhysicsCore/VansPhysicsVehicle.h"
#include "../PhysicsCore/VansClothNode.h"
#include "../PhysicsCore/VansClothSystem.h"
#include "../AssetCore/VansClothProfile.h"
#include "../AssetCore/Storage/VansClothProfileStorage.h"
#include "../PhysicsCore/VansCharacterControllerNode.h"
#include "../PhysicsCore/VansCollisionLayerManager.h"
#include "../Configration/VansConfigration.h"
#include "../ScriptCore/VansScriptContext.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../AnimationCore/MotionMatching/VansMotionMatching.h"
#include "../RuntimeCore/VansFramePhase.h"

#include "VulkanCore/VansMesh.h"
#include "VulkanCore/VansVKDevice.h"
#include "../Util/VansLog.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace
{
glm::vec3 ToVec3(const std::array<float, 3>& value)
{
    return glm::vec3(value[0], value[1], value[2]);
}

void ApplyBodyType(const std::string& bodyType, VansEngine::PhysicsNodeProperties& properties)
{
    if (bodyType == "static")
        properties.bodyType = VansEngine::PhysicsBodyType::Static;
    else if (bodyType == "dynamic")
        properties.bodyType = VansEngine::PhysicsBodyType::Dynamic;
    else if (bodyType == "kinematic")
        properties.bodyType = VansEngine::PhysicsBodyType::Kinematic;
}

void ApplyColliderType(const std::string& colliderType, VansEngine::PhysicsNodeProperties& properties)
{
    if (colliderType == "box")
        properties.colliderType = VansEngine::PhysicsColliderType::Box;
    else if (colliderType == "sphere")
        properties.colliderType = VansEngine::PhysicsColliderType::Sphere;
    else if (colliderType == "capsule")
        properties.colliderType = VansEngine::PhysicsColliderType::Capsule;
    else if (colliderType == "mesh")
        properties.colliderType = VansEngine::PhysicsColliderType::Mesh;
    else if (colliderType == "convex")
        properties.colliderType = VansEngine::PhysicsColliderType::ConvexMesh;
}
}

// ===========================================================================
// Vehicle initialization
// ===========================================================================

void VansGraphics::VansScene::InitVehicle(VansEngine::VansPhysicsSystem* physicsSystem, const glm::vec3& position,
    const std::string& bodyRenderNodeName, const std::vector<std::string>& tireRenderNodeNames,
    uint32_t bodyTransformID, const std::vector<uint32_t>& tireTransformIDs,
    const VansEngine::VansVehicleTuning& tuning,
    const std::vector<std::vector<VansEngine::VansVehicleVisualBinding>>& wheelVisualBindings)
{
    if (m_Vehicle) return; // Already initialized

    m_Vehicle = new VansEngine::VansPhysicsVehicle();
    m_Vehicle->SetTuning(tuning);
    m_Vehicle->SetBodyRenderNodeName(bodyRenderNodeName);
    m_Vehicle->SetTireRenderNodeNames(tireRenderNodeNames);
    m_Vehicle->SetBodyTransformID(bodyTransformID);
    m_Vehicle->SetTireTransformIDs(tireTransformIDs);
    m_Vehicle->SetWheelVisualBindings(wheelVisualBindings);
    auto vehicleAxisToVec3 = [](PxVehicleAxes::Enum axis) -> PxVec3
    {
        switch (axis)
        {
        case PxVehicleAxes::ePosX: return PxVec3(1.0f, 0.0f, 0.0f);
        case PxVehicleAxes::eNegX: return PxVec3(-1.0f, 0.0f, 0.0f);
        case PxVehicleAxes::ePosY: return PxVec3(0.0f, 1.0f, 0.0f);
        case PxVehicleAxes::eNegY: return PxVec3(0.0f, -1.0f, 0.0f);
        case PxVehicleAxes::ePosZ: return PxVec3(0.0f, 0.0f, 1.0f);
        case PxVehicleAxes::eNegZ: return PxVec3(0.0f, 0.0f, -1.0f);
        default: return PxVec3(0.0f, 1.0f, 0.0f);
        }
    };

    const PxVec3 upAxis = vehicleAxisToVec3(tuning.verticalAxis);
    const PxVec3 startPosition(position.x, position.y, position.z);
    PxTransform startPose(startPosition + upAxis * tuning.startHeightOffset, PxQuat(PxIdentity));

    // Initialize with default parameters (empty path triggers built-in defaults)
    m_Vehicle->Initialize(physicsSystem, "", startPose);

    VANS_LOG("[VansScene] Vehicle initialized at " << position.x << ", " << position.y << ", " << position.z
              << ", startHeightOffset=" << tuning.startHeightOffset
              << ", bodyNode='" << bodyRenderNodeName << "', bodyTransformID=" << bodyTransformID
              << ", tires=" << tireRenderNodeNames.size()
              << ", tireTransformIDs=" << tireTransformIDs.size()
              << ", wheelVisualGroups=" << wheelVisualBindings.size());
}

void VansGraphics::VansScene::RegisterPhysicsNode(VansEngine::VansPhysicsNode* physicsNode)
{
    if (physicsNode)
        m_PhysicsNodes.push_back(physicsNode);
}

void VansGraphics::VansScene::RegisterClothNode(VansEngine::VansClothNode* clothNode, VansRenderNode* renderNodeForStaging)
{
    if (!clothNode)
        return;

    m_ClothNodes.push_back(clothNode);

    VkDeviceSize stagingSize =
        static_cast<VkDeviceSize>(renderNodeForStaging && renderNodeForStaging->m_Mesh
            ? renderNodeForStaging->m_Mesh->GetMeshVertexCount() : 0)
        * static_cast<VkDeviceSize>(renderNodeForStaging && renderNodeForStaging->m_Mesh
            ? renderNodeForStaging->m_Mesh->GetMeshVertexStride() : 8 * sizeof(uint16_t));
    VansVKDevice* vkDev = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VkDevice nativeDev = vkDev ? vkDev->GetLogicDevice() : VK_NULL_HANDLE;
    m_ClothStagingBuffers.emplace_back();
    if (stagingSize > 0 && nativeDev != VK_NULL_HANDLE)
    {
        m_ClothStagingBuffers.back().CreatVulkanBuffer(
            nativeDev,
            stagingSize,
            VK_FORMAT_UNDEFINED,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_ClothStagingBuffers.back().PersistentMap();
    }
}

void VansGraphics::VansScene::RegisterCharacterControllerNode(VansEngine::VansCharacterControllerNode* controllerNode)
{
    if (controllerNode)
        m_CharControllerNodes.push_back(controllerNode);
}

// ===========================================================================
// Physics node loading from typed scene component config
// ===========================================================================

// ===========================================================================
// Single cloth node loading
// ===========================================================================

VansEngine::VansClothNode* VansGraphics::VansScenePhysicsComponentBuilder::LoadClothNode(
    VansScene& scene,
    const Vans::VansSceneClothNodeConfig& config,
    VansRenderNode* associatedRenderNode,
    std::string* outProfilePath)
{
    using namespace VansEngine;

    VansRenderNode* renderNode = associatedRenderNode;

    if (!renderNode)
    {
        VANS_LOG_WARN("[VansScenePhysicsComponentBuilder] LoadClothNode: no valid render node, skipping.");
        return nullptr;
    }

    ClothNodeProperties clothProps;
    if (!config.profilePath)
    {
        VANS_LOG_ERROR("[VansScenePhysicsComponentBuilder] LoadClothNode: Cloth component requires profilePath.");
        return nullptr;
    }

    // ── 新格式：通过 profilePath 从 .clothprofile 文件加载配置 ──────────────
    if (config.profilePath)
    {
        std::string profilePath = *config.profilePath;

        VansClothProfile profile;
        std::string profileError;
        if (!VansEngine::VansClothProfileStorage::Load(profilePath, profile, profileError))
        {
            VANS_LOG_ERROR("[VansScenePhysicsComponentBuilder] LoadClothNode: 加载 Profile 失败: " << profilePath
                           << " (" << profileError << ")，回退为默认参数。");
        }
        else
        {
            // 通过 profile 局部坐标近邻匹配填充 props
            VansMesh* mesh = renderNode->m_Mesh;
            if (mesh)
            {
                // 骨骼蒙皮数据将在 Pass5（所有 AnimationNode 加载完毕后）通过
                // LateBindBonesFromProfile() 延迟解析，此处传入 nullptr 即可。
                clothProps = ClothNodeProperties::FromProfile(
                    profile,
                    mesh->GetMeshRawPositionData(),
                    mesh->GetMeshVertexCount(),
                    nullptr);
            }
            else
            {
                VANS_LOG_WARN("[VansScenePhysicsComponentBuilder] LoadClothNode: RenderNode 无 Mesh，无法解析固定点索引。");
                clothProps.stiffness     = profile.m_Stiffness;
                clothProps.damping       = profile.m_Damping;
                clothProps.friction      = profile.m_Friction;
                clothProps.gravity       = profile.m_Gravity;
                clothProps.selfCollision = profile.m_SelfCollision;
                clothProps.enabled       = true;
            }
        }

        // 输出 profilePath 供调用方存入 VansScriptClothComponent
        if (outProfilePath)
            *outProfilePath = profilePath;
    }
    else
    {
        return nullptr;
    }

    // 解析 physicsAttachOffsetY — 无论使用 profilePath 还是旧格式均适用
    // 用于将布料固定点从颈部/领口向下对准角色肩膀位置（单位：米）
    if (config.physicsAttachOffsetY)
        clothProps.attachOffsetY = *config.physicsAttachOffsetY;

    // 通过 objectRef 解析碰撞球实体。优先缓存渲染节点或 Transform ID；
    // sceneObjectName 仅用于实体尚未完成构建时的延迟 Transform 查找。
    if (!config.collisionSpheres.empty())
    {
        for (const auto& collisionSphere : config.collisionSpheres)
        {
            ClothNodeProperties::CollisionSphereRef ref;
            if (!collisionSphere.objectRef.empty())
            {
                std::string objectName = collisionSphere.objectRef;
                ref.sceneObjectName = objectName;

                VansScriptObject* refObj = scene.FindSceneObjectByName(objectName);
                if (refObj)
                {
                    auto* rc = refObj->GetComponent<VansScriptRenderComponent>();
                    if (rc && rc->m_RenderNode)
                    {
                        ref.renderNodeName = rc->m_RenderNode->m_NodeName;
                    }
                    else if (refObj->m_TransformID != 0)
                    {
                        // 无 render 组件但 ScriptObject 有自己的 transformID
                        ref.transformID = refObj->m_TransformID;
                    }
                    // 否则保留实体名称，在运行时实体构建完成后解析其 Transform。
                }
            }
            if (collisionSphere.radius)
                ref.radius = *collisionSphere.radius;
            // 三种解析路径任意满足其一即加入列表
            if (!ref.renderNodeName.empty() || ref.transformID != UINT32_MAX || !ref.sceneObjectName.empty())
                clothProps.collisionSphereRefs.push_back(ref);
        }
    }

    VansClothNode* clothNode = new VansClothNode();
    clothNode->Initialize(clothProps, renderNode);
    // AnimationNode 绑定延迟至 Pass5（VansSceneLoader::LoadSceneObjects 末尾）完成，
    // 届时 m_AnimationNodes 已由 Pass4 完全填充。

    scene.RegisterClothNode(clothNode, renderNode);
    VANS_LOG("[VansScene] Cloth node created for render node '" << renderNode->m_NodeName << "'");

    return clothNode;
}

// ===========================================================================
// Single physics node loading
// ===========================================================================

VansEngine::VansPhysicsNode* VansGraphics::VansScenePhysicsComponentBuilder::LoadPhysicsNode(
    VansScene& scene,
	const Vans::VansScenePhysicsNodeConfig& config,
    VansRenderNode* associatedRenderNode,
    uint32_t standaloneTransformID)
{
    using namespace VansEngine;

    PhysicsNodeProperties properties;

    if (config.enabled)
        properties.enabled = *config.enabled;
    
    if (!properties.enabled)
        return nullptr;

    if (config.bodyType)
        ApplyBodyType(*config.bodyType, properties);
    if (config.colliderType)
        ApplyColliderType(*config.colliderType, properties);

    if (config.mass)
        properties.mass = *config.mass;
    if (config.useMeshCollider)
        properties.useMeshCollider = *config.useMeshCollider;
    if (config.useConvexDecomposition)
        properties.useConvexDecomposition = *config.useConvexDecomposition;

    if (config.material)
    {
        if (config.material->staticFriction)
            properties.material.staticFriction = *config.material->staticFriction;
        if (config.material->dynamicFriction)
            properties.material.dynamicFriction = *config.material->dynamicFriction;
        if (config.material->restitution)
            properties.material.restitution = *config.material->restitution;
    }

    if (config.boxExtents)
        properties.boxExtents = ToVec3(*config.boxExtents);
    if (config.shapeOffset)
        properties.shapeOffset = ToVec3(*config.shapeOffset);
    if (config.colliderOffset)
        properties.shapeOffset = ToVec3(*config.colliderOffset);
    if (config.sphereRadius)
        properties.sphereRadius = *config.sphereRadius;
    if (config.capsuleRadius)
        properties.capsuleRadius = *config.capsuleRadius;
    if (config.capsuleHalfHeight)
        properties.capsuleHalfHeight = *config.capsuleHalfHeight;

    // 解析碰撞 Layer
    if (config.layer)
        properties.layerName = *config.layer;
    properties.layerIndex = VansEngine::VansCollisionLayerManager::Get().GetLayerIndex(properties.layerName);

    // 解析 Trigger 标志
    if (config.isTrigger)
        properties.isTrigger = *config.isTrigger;

	if (associatedRenderNode == nullptr && standaloneTransformID == UINT32_MAX)
    {
		VANS_LOG_WARN("[VansScene] Physics component has no transform, skipping.");
        return nullptr;
    }

	uint32_t transformID = associatedRenderNode ? associatedRenderNode->m_TransformID : standaloneTransformID;

    // Get mesh reference if needed
    VansMesh* mesh = nullptr;
    if (properties.useMeshCollider && config.mesh)
    {
        std::string meshName = *config.mesh;
        mesh = static_cast<VansMesh*>(scene.FindMeshAsset(meshName));
        if (!mesh)
        {
            VANS_LOG_ERROR("[VansScene] Mesh collider source not found for physics node '"
                << config.name.value_or(std::string{})
                << "': mesh='" << meshName << "'");
        }
    }

    VansPhysicsNode* physicsNode = new VansPhysicsNode();
	if (config.name)
		physicsNode->SetName(*config.name);
    physicsNode->Initialize(properties, transformID, mesh);
	if (!physicsNode->IsEnabled() || physicsNode->GetActor() == nullptr)
	{
		VANS_LOG_ERROR("[VansScene] Failed to initialize physics component '"
			<< physicsNode->GetName() << "'");
		delete physicsNode;
		return nullptr;
	}
    scene.RegisterPhysicsNode(physicsNode);
	VANS_LOG("[VansScene] Created physics node '" << physicsNode->GetName()
		<< "' transformID=" << transformID);
    return physicsNode;
}

// ===========================================================================
// Physics → render transform synchronization
// ===========================================================================

void VansGraphics::VansScene::UpdatePhysicsTransforms()
{
    VANS_ASSERT_FRAME_PHASE(VansFramePhase::GameLogic);

    using namespace VansEngine;
    
    // Get physics system
    VansPhysicsSystem& physics = VansPhysicsSystem::GetInstance();
    
    // 1. Acquire the simulation lock FIRST
    // This blocks if the simulation thread is currently inside its update loop (simulate -> fetchResults)
    // Once we have this lock, we know the simulation thread is waiting or sleeping, and NOT writing to the scene.
    // Use std::lock_guard or std::unique_lock with the mutex
    std::lock_guard<std::mutex> simLock(physics.GetSimulationMutex());

    PxScene* scene = physics.GetScene();
    if (!scene)
        return;
    
    // 2. Sync editor / script transform changes back into PhysX first.
    // This is required for gizmo-driven kinematic movement.
    {
        PxSceneWriteLock scopedWriteLock(*scene);

        for (auto* physicsNode : m_PhysicsNodes)
        {
            if (!physicsNode || !physicsNode->IsEnabled())
                continue;

            uint32_t transformID = physicsNode->GetTransformID();
            auto dirtyIt = VansGraphics::VansTransformStore::TransformIDToTransformDirty.find(transformID);
            if (dirtyIt == VansGraphics::VansTransformStore::TransformIDToTransformDirty.end() || !dirtyIt->second)
                continue;

            const auto& properties = physicsNode->GetProperties();
			// Dynamic bodies are simulation-driven. Static editor geometry and
			// kinematic/trigger bodies receive authored transform changes.
			if (properties.bodyType == PhysicsBodyType::Dynamic && !properties.isTrigger)
                continue;

            // const VansTransform& transformData = VansTransformStore::GetTransform(transformID);
            // VANS_LOG("[PhysX Sync] Push transform -> physics: tid=" << transformID
            //          << " pos=(" << transformData.m_Position.x << ", " << transformData.m_Position.y << ", " << transformData.m_Position.z << ")"
            //          << " rot=(" << transformData.m_Rotation.x << ", " << transformData.m_Rotation.y << ", " << transformData.m_Rotation.z << ")"
            //          << " bodyType=" << static_cast<int>(properties.bodyType)
            //          << " isTrigger=" << properties.isTrigger);

            physicsNode->UpdatePhysicsFromTransform();
        }
    }

    // 3. Read back physics simulation results into render transforms.
    PxSceneReadLock scopedLock(*scene);

    // Update all physics nodes from physics simulation
    for (auto* physicsNode : m_PhysicsNodes)
    {
        if (physicsNode && physicsNode->IsEnabled())
        {
            if (physicsNode->UpdateTransformFromPhysics())
            {
                // Record the transform ID if it has changed
                uint32_t transformID = physicsNode->GetTransformID();
                if (transformID != 0) // Invalid ID check
                {
					VansGraphics::VansTransformStore::TransformIDToTransformDirty.insert({ transformID, true });
                }
            }
        }
    }

    // ── Update vehicle render node transforms ────────────────────────────────
    if (m_Vehicle)
    {
        // Helper: convert PxQuat to Euler angles in degrees for VansTransform
        auto PxQuatToEulerDeg = [](const PxQuat& q) -> glm::vec3
        {
            glm::quat gq(q.w, q.x, q.y, q.z);
            return glm::degrees(glm::eulerAngles(gq));
        };

        auto writeTransform = [&](uint32_t transformID, const PxTransform& pose, const PxVec3& pivotLocal = PxVec3(0.0f))
        {
            if (transformID == UINT32_MAX ||
                transformID >= static_cast<uint32_t>(VansTransformStore::GlobalTransforms.size()))
                return false;

            VansTransform& t = VansTransformStore::GetTransform(transformID);
            const PxVec3 correctedPosition = pose.p - pose.q.rotate(pivotLocal);
            t.m_Position = glm::vec3(correctedPosition.x, correctedPosition.y, correctedPosition.z);
            t.m_Rotation = PxQuatToEulerDeg(pose.q);
            VansTransformStore::TransformIDToTransformDirty[transformID] = true;
			m_TransformGraph.MarkWorldDirty(transformID);
            return true;
        };

        // Update car body object transform. Fall back to the legacy render node binding.
        const PxTransform bodyPose = m_Vehicle->GetTransform();
        if (!writeTransform(m_Vehicle->GetBodyTransformID(), bodyPose))
        {
            const std::string& bodyNodeName = m_Vehicle->GetBodyRenderNodeName();
            if (!bodyNodeName.empty())
            {
                VansRenderNode* bodyNode = FindRenderNodeByName(bodyNodeName);
                if (bodyNode)
                    writeTransform(bodyNode->m_TransformID, bodyPose);
            }
        }

        // Update tire object transforms (one per wheel index). Fall back to legacy render node bindings.
        const std::vector<std::vector<VansEngine::VansVehicleVisualBinding>>& wheelVisualBindings =
            m_Vehicle->GetWheelVisualBindings();
        if (!wheelVisualBindings.empty())
        {
            const uint32_t numWheelGroups = static_cast<uint32_t>(wheelVisualBindings.size());
            for (uint32_t wi = 0; wi < numWheelGroups; ++wi)
            {
                PxTransform wheelPose = m_Vehicle->GetWheelVisualWorldPose(wi);
                for (const VansEngine::VansVehicleVisualBinding& binding : wheelVisualBindings[wi])
                    writeTransform(binding.transformID, wheelPose, binding.pivotLocal);
            }
            return;
        }

        const std::vector<uint32_t>& tireTransformIDs = m_Vehicle->GetTireTransformIDs();
        const std::vector<std::string>& tireNodeNames = m_Vehicle->GetTireRenderNodeNames();
        const uint32_t numTires = static_cast<uint32_t>(std::max(tireTransformIDs.size(), tireNodeNames.size()));
        for (uint32_t wi = 0; wi < numTires; ++wi)
        {
            PxTransform wheelPose = m_Vehicle->GetWheelVisualWorldPose(wi);
            if (wi < tireTransformIDs.size() && writeTransform(tireTransformIDs[wi], wheelPose))
                continue;

            if (wi >= tireNodeNames.size())
                continue;
            const std::string& tireName = tireNodeNames[wi];
            if (tireName.empty()) continue;
            VansRenderNode* tireNode = FindRenderNodeByName(tireName);
            if (tireNode)
                writeTransform(tireNode->m_TransformID, wheelPose);
        }
    }
}

// ===========================================================================
// Character Controller transform update
// ===========================================================================

void VansGraphics::VansScene::PrepareCharacterLocomotion(float deltaTime)
{
	VANS_ASSERT_FRAME_PHASE(VansFramePhase::GameLogic);
	for (VansEngine::VansCharacterControllerNode* cct : m_CharControllerNodes)
	{
		if (!cct || !cct->IsEnabled() || !cct->HasMotionIntent())
			continue;

		VansAnimationNode* animation = nullptr;
		for (VansAnimationNode* candidate : m_AnimationNodes)
		{
			if (candidate && candidate->IsEnabled() &&
			    candidate->GetTransformID() == cct->GetTransformID())
			{
				animation = candidate;
				break;
			}
		}

		Vans::VansCharacterMotionSettings motionSettings;
		VansAnimationController* controller =
			animation ? animation->GetLocomotionController() : nullptr;
		if (controller)
			if (const MotionMatchingSettings* mm = controller->GetMotionMatchingSettings())
				motionSettings = mm->motionModel;

		cct->PrepareLocomotion(deltaTime, motionSettings);
		bool rootMotionValid = false;
		bool rootMotionPreferred = false;
		glm::vec3 rootDelta(0.0f);
		glm::quat rootRotation(1.0f, 0.0f, 0.0f, 0.0f);
		if (animation && controller && controller->IsMotionMatchingConfigured())
		{
			animation->PrepareLocomotionFrame(deltaTime, cct->GetTrajectory());
			rootDelta = animation->GetRootMotionDelta();
			rootRotation = animation->GetRootRotationDelta();
			rootMotionValid = animation->IsRootMotionEnabled() && animation->HasRootMotionDelta();
			rootMotionPreferred = controller->MotionMatchingPrefersRootMotion();
		}
		cct->ResolveLocomotion(
			rootDelta, rootRotation, rootMotionValid, rootMotionPreferred, motionSettings);
	}
}

void VansGraphics::VansScene::UpdateCharControllerTransforms()
{
    VANS_ASSERT_FRAME_PHASE(VansFramePhase::GameLogic);

    using namespace VansEngine;

    if (m_CharControllerNodes.empty()) return;

    // 在 SimulationMutex 保护下提交 PxController::move() 并同步 Transform
    VansPhysicsSystem& physics = VansPhysicsSystem::GetInstance();
    std::lock_guard<std::mutex> simLock(physics.GetSimulationMutex());

    for (auto* node : m_CharControllerNodes)
    {
        if (node && node->IsEnabled())
            node->FlushMoveAndSync();
    }
}

// ===========================================================================
// Load a single CharacterController from typed scene component config
// ===========================================================================

VansEngine::VansCharacterControllerNode*
VansGraphics::VansScenePhysicsComponentBuilder::LoadCharacterControllerNode(
    VansScene& scene,
    const Vans::VansSceneCharacterControllerConfig& config,
    VansRenderNode* associatedRenderNode,
    uint32_t standaloneTransformID)
{
    using namespace VansEngine;

    CharControllerProperties props;

    if (config.radius)
        props.m_Radius = *config.radius;
    if (config.height)
        props.m_Height = *config.height;
    if (config.slopeLimit)
        props.m_SlopeLimit = *config.slopeLimit;
    if (config.stepOffset)
        props.m_StepOffset = *config.stepOffset;
    if (config.contactOffset)
        props.m_ContactOffset = *config.contactOffset;
    if (config.layer)
    {
        props.m_LayerName  = *config.layer;
        props.m_LayerIndex = VansCollisionLayerManager::Get()
                                 .GetLayerIndex(props.m_LayerName);
    }
    if (config.climbingMode)
    {
        std::string cm = *config.climbingMode;
        props.m_ClimbingMode = (cm == "constrained")
            ? PxCapsuleClimbingMode::eCONSTRAINED
            : PxCapsuleClimbingMode::eEASY;
    }
    if (config.positionOffset)
        props.m_PositionOffset = ToVec3(*config.positionOffset);

    // 解析初始位置
    uint32_t transformID = 0;
    glm::vec3 spawnPos(0.0f);
    if (associatedRenderNode)
    {
        transformID = associatedRenderNode->m_TransformID;
        const VansTransform& t = VansTransformStore::GetTransform(transformID);
        spawnPos = t.m_Position + props.m_PositionOffset;
    }
    else if (standaloneTransformID != UINT32_MAX)
    {
        transformID = standaloneTransformID;
        const VansTransform& t = VansTransformStore::GetTransform(transformID);
        spawnPos = t.m_Position + props.m_PositionOffset;
    }

    VansPhysicsSystem& physSys = VansPhysicsSystem::GetInstance();
    PxControllerManager* manager = physSys.GetControllerManager();
    if (!manager)
    {
        VANS_LOG_ERROR("[VansScene] CharController: PxControllerManager 未初始化");
        return nullptr;
    }

    VansCharacterControllerNode* node = new VansCharacterControllerNode();
    if (!node->Initialize(props, transformID, manager,
                          physSys.GetDefaultMaterial(), spawnPos))
    {
        delete node;
        return nullptr;
    }

    // ── 延迟绑定标志：ragdoll 在第二阶段加载，先记录意图 ──────────────
    if (config.followRagdoll.value_or(false))
    {
        std::string bone = config.followRagdollBone.value_or("pelvis");
        node->SetPendingFollowRagdoll(true, bone);
    }

    scene.RegisterCharacterControllerNode(node);
    VANS_LOG("[VansScene] CharController 节点已创建，transformID=" << transformID);
    return node;
}

// ===========================================================================
// Cloth simulation update
// ===========================================================================

void VansGraphics::VansScene::UpdateClothSimulation(float dt)
{
    if (m_ClothNodes.empty()) return;

    // ── 子步参数 ──────────────────────────────────────────────────────────────
    // 将每帧仿真拆分为 kSubSteps 个子步：
    //   1. 每子步时间步长缩小为 dt/kSubSteps，约束冲量成比例缩小，避免数值爆炸。
    //   2. 固定点位置在上一帧目标与本帧目标之间线性插值，消除瞬间大位移引发的
    //      约束违反（骨骼动画过渡时尤为重要）。
    // 角色快速移动时 8 步提供足够稳定性；静态场景可降至 4 步节省 CPU。
    static constexpr int kSubSteps = 8;
    const float subDt = dt / static_cast<float>(kSubSteps);

    // 第一步：计算本帧所有固定点的目标世界坐标（不写入粒子缓冲区）
    for (auto* clothNode : m_ClothNodes)
        if (clothNode && clothNode->IsEnabled()) clothNode->ComputePinnedTargets();

    // 第二步：更新碰撞球（每帧一次，不需要随子步变化）
    static bool loggedOnce = false;
    for (auto* clothNode : m_ClothNodes)
    {
        if (!clothNode || !clothNode->IsEnabled()) continue;
        auto& sphereRefs = clothNode->GetCollisionSphereRefs();
        if (sphereRefs.empty()) continue;

        std::vector<physx::PxVec4> spheres;
        spheres.reserve(sphereRefs.size());
        for (auto& ref : sphereRefs)
        {
            // 延迟解析场景实体；骨骼/Socket 挂接已经由 Transform Graph 更新实体世界变换。
            if (ref.renderNodeName.empty() && ref.transformID == UINT32_MAX
                && !ref.sceneObjectName.empty())
            {
				if (VansScriptObject* object = FindObjectByName(ref.sceneObjectName))
					ref.transformID = object->m_TransformID;
            }

            glm::vec3 pos(0.0f);
            bool valid = false;

            if (!ref.renderNodeName.empty())
            {
                // 优先路径：通过 render 节点名查找位置
                VansRenderNode* rn = FindRenderNodeByName(ref.renderNodeName);
                if (rn)
                {
                    pos   = VansTransformStore::GetTransform(rn->m_TransformID).m_Position;
                    valid = true;
                }
            }
            else if (ref.transformID != UINT32_MAX
                     && ref.transformID < static_cast<uint32_t>(VansTransformStore::GlobalTransforms.size()))
            {
                // 回退路径：直接读取 TransformStore（骨骼绑定的纯物理碰撞体）
                pos   = VansTransformStore::GetTransform(ref.transformID).m_Position;
                valid = true;
            }

            if (!valid) continue;

            spheres.push_back(physx::PxVec4(pos.x, pos.y, pos.z, ref.radius));
            if (!loggedOnce)
            {
                VANS_LOG("[VansScene] Cloth collision sphere (world): node='"
                          << (ref.renderNodeName.empty()
                                  ? (ref.sceneObjectName + " tid=" + std::to_string(ref.transformID))
                                  : ref.renderNodeName)
                          << "' pos=(" << pos.x << "," << pos.y << "," << pos.z
                          << ") radius=" << ref.radius);
            }
        }
        clothNode->SetCollisionSpheres(spheres);
    }
    loggedOnce = true;

    // 第三步：子步循环——每步写入插值固定点位置，然后推进仿真
    for (int s = 0; s < kSubSteps; ++s)
    {
        // alpha: 第 1 步=1/N, 第 2 步=2/N, ..., 最后一步=1.0
        const float alpha = static_cast<float>(s + 1) / static_cast<float>(kSubSteps);
        for (auto* clothNode : m_ClothNodes)
            if (clothNode && clothNode->IsEnabled()) clothNode->WritePinnedParticlesLerped(alpha);

        VansEngine::VansClothSystem::GetInstance().SimulateStep(subDt);
    }

    // 第四步：提交本帧目标为"上一帧"，供下帧插值使用
    for (auto* clothNode : m_ClothNodes)
        if (clothNode && clothNode->IsEnabled()) clothNode->CommitPinnedTargets();
}

void VansGraphics::VansScene::WriteClothResultsToStagingBuffers(
	const VansRenderSceneFrameSnapshot& snapshot)
{
	for (const VansRenderClothFrameData& cloth : snapshot.cloth)
    {
		if (cloth.clothNodeIndex >= m_ClothStagingBuffers.size())
			continue;
		VansVKBuffer& staging = m_ClothStagingBuffers[cloth.clothNodeIndex];
		if (!staging.IsMapped()) continue;

		const size_t byteSize = cloth.simulatedVertices.size() * sizeof(uint16_t);
		if (byteSize == 0) continue;
		if (byteSize > static_cast<size_t>(staging.GetBufferSize()))
		{
			VANS_LOG_ERROR("[VansScene] Cloth frame snapshot exceeds its staging buffer.");
			continue;
		}
		std::memcpy(
			staging.GetMappedPtr(), cloth.simulatedVertices.data(), byteSize);
    }
}

void VansGraphics::VansScene::RecordClothVertexUploads(
	VansVKCommandBuffer& cmd,
	const VansRenderSceneFrameSnapshot& snapshot)
{
	for (const VansRenderClothFrameData& cloth : snapshot.cloth)
	{
		if (cloth.clothNodeIndex >= m_ClothNodes.size() ||
			cloth.clothNodeIndex >= m_ClothStagingBuffers.size())
		{
			continue;
		}
		VansEngine::VansClothNode* clothNode = m_ClothNodes[cloth.clothNodeIndex];
		if (!clothNode) continue;

		VansVKBuffer& staging = m_ClothStagingBuffers[cloth.clothNodeIndex];
		if (!staging.IsMapped()) continue;

        VansGraphics::VansRenderNode* renderNode = clothNode->GetTargetRenderNode();
        if (!renderNode || !renderNode->m_Mesh) continue;

        VkBuffer dstBuffer = renderNode->m_Mesh->GetBLASVertexBuffer().GetNativeBuffer();
		VkDeviceSize size = static_cast<VkDeviceSize>(
			cloth.simulatedVertices.size() * sizeof(uint16_t));
        if (size == 0) continue;

        cmd.CopyBuffer(staging.GetNativeBuffer(), dstBuffer, 0, 0, size);

        // TRANSFER_WRITE → VERTEX_ATTRIBUTE_READ barrier
        VkBufferMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.pNext               = nullptr;
        barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask       = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer              = dstBuffer;
        barrier.offset              = 0;
        barrier.size                = VK_WHOLE_SIZE;
        cmd.PipelineBarrier(
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
            {},
            { barrier });
    }
}
