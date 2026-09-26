#include "../SceneRuntime/Transform/VansTransformStore.h"
#include "VansScene.h"

#include "SceneBuild/VansScenePhysicsComponentBuilder.h"

#include "../PhysicsCore/VansPhysics.h"
#include "../PhysicsCore/VansPhysicsNativeAccess.h"
#include "../PhysicsCore/VansPhysicsNode.h"
#include "../PhysicsCore/VansPhysicsVehicle.h"

#include "../PhysicsCore/VansClothNode.h"
#include "../PhysicsCore/VansClothSystem.h"
#include "../AssetCore/VansClothProfile.h"
#include "../AssetCore/VansAssetObjectRepository.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../PhysicsCore/VansCharacterControllerNode.h"
#include "../PhysicsCore/VansCollisionLayerManager.h"
#include "../ScriptCore/VansScriptContext.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../RuntimeCore/VansFramePhase.h"
#include "../RuntimeCore/VansThreadContract.h"

#include "VulkanCore/VansMesh.h"
#include "VulkanCore/VansVKDevice.h"
#include "../Util/VansLog.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

using namespace physx;
using namespace physx::vehicle2;


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

VansEngine::VansPhysicsVehicle* VansGraphics::VansScene::BuildVehicleRuntime(
    VansEngine::VansPhysicsSystem* physicsSystem, const glm::vec3& position,
    const std::string& bodyRenderNodeName, const std::vector<std::string>& tireRenderNodeNames,
    uint32_t bodyTransformID, const std::vector<uint32_t>& tireTransformIDs,
    const VansEngine::VansVehicleTuning& tuning,
    const std::vector<std::vector<VansEngine::VansVehicleVisualBinding>>& wheelVisualBindings,
    std::string& error)
{
    error.clear();
    if (m_Vehicle)
    {
        error = "Scene already owns a vehicle runtime";
        return nullptr;
    }
    if (!tuning.IsValid(error))
        return nullptr;

    auto vehicle = std::make_unique<VansEngine::VansPhysicsVehicle>();
    vehicle->SetTuning(tuning);
    vehicle->SetBodyRenderNodeName(bodyRenderNodeName);
    vehicle->SetTireRenderNodeNames(tireRenderNodeNames);
    vehicle->SetBodyTransformID(bodyTransformID);
    vehicle->SetTireTransformIDs(tireTransformIDs);
    vehicle->SetWheelVisualBindings(wheelVisualBindings);

    const PxVec3 upAxis = tuning.BuildFrame().getVrtAxis();
    const PxVec3 startPosition(position.x, position.y, position.z);
    PxTransform startPose(startPosition + upAxis * tuning.startHeightOffset, PxQuat(PxIdentity));

    if (!vehicle->Initialize(physicsSystem, startPose, error))
        return nullptr;

    m_Vehicle = vehicle.release();
    return m_Vehicle;
}

void VansGraphics::VansScene::RegisterPhysicsNode(VansEngine::VansPhysicsNode* physicsNode)
{
    if (physicsNode)
        m_PhysicsNodes.push_back(physicsNode);
}

bool VansGraphics::VansScene::RegisterClothNode(
	VansEngine::VansClothNode* clothNode,
	VansRenderNode* renderNodeForStaging,
	std::string& error)
{
	error.clear();
	if (!clothNode || !renderNodeForStaging || !renderNodeForStaging->m_Mesh)
	{
		error = "Cloth registration requires a node and render mesh";
		return false;
	}
	auto runtime = std::make_unique<VansSceneClothRuntime>();
	runtime->node = clothNode;
	runtime->renderNode = renderNodeForStaging;

	const VkDeviceSize meshVertexBytes =
		static_cast<VkDeviceSize>(renderNodeForStaging->m_Mesh->GetMeshVertexCount()) *
		static_cast<VkDeviceSize>(renderNodeForStaging->m_Mesh->GetMeshVertexStride());
	const VansEngine::VansClothVertexView clothVertices = clothNode->GetRenderData();
	const VkDeviceSize stagingSize = static_cast<VkDeviceSize>(clothVertices.ByteSize());
    VansVKDevice* vkDev = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VkDevice nativeDev = vkDev ? vkDev->GetLogicDevice() : VK_NULL_HANDLE;
	if (stagingSize == 0 || meshVertexBytes == 0 || nativeDev == VK_NULL_HANDLE)
	{
		error = "Cloth registration requires a non-empty mesh and Vulkan device";
		return false;
	}
	if (stagingSize != meshVertexBytes ||
		stagingSize != renderNodeForStaging->m_Mesh->GetBLASVertexBuffer().GetBufferSize())
	{
		error = "Cloth render payload does not match the target mesh vertex buffer ABI";
		return false;
	}
	if (!runtime->stagingBuffer.CreatVulkanBuffer(
		nativeDev,
		stagingSize,
		VK_FORMAT_UNDEFINED,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
    {
		error = "Cloth staging buffer creation failed";
		return false;
    }
	if (!runtime->stagingBuffer.PersistentMap())
	{
		runtime->stagingBuffer.DestroyVulkanBuffer(nativeDev);
		error = "Cloth staging buffer mapping failed";
		return false;
	}
	m_ClothRuntimes.push_back(std::move(runtime));
	return true;
}

void VansGraphics::VansScene::RegisterCharacterControllerNode(VansEngine::VansCharacterControllerNode* controllerNode)
{
    if (controllerNode)
        m_CharControllerNodes.push_back(controllerNode);
}

// ===========================================================================
// Physics node creation from typed scene component config
// ===========================================================================

// ===========================================================================
// Single cloth node creation
// ===========================================================================

std::unique_ptr<VansEngine::VansClothNode>
VansGraphics::VansScenePhysicsComponentBuilder::CreateClothNode(
    VansScene& scene,
	const Vans::VansSceneClothNodeConfig& config,
	VansRenderNode* associatedRenderNode,
	std::string& profileGuidText,
	std::string& error)
{
	using namespace VansEngine;
	error.clear();
	VansRenderNode* renderNode = associatedRenderNode;
	if (!renderNode || !renderNode->m_Mesh)
	{
		error = "Cloth component requires a render node with retained CPU mesh data";
		return {};
	}
	if (!config.profileGuid)
	{
		error = "Cloth component requires a Profile asset GUID";
		return {};
	}

	Vans::VansAssetGuid profileGuid;
	if (!Vans::VansAssetGuid::TryParse(*config.profileGuid, profileGuid))
	{
		error = "Cloth component has an invalid Profile asset GUID: " + *config.profileGuid;
		return {};
	}
	const auto profile = Vans::VansProjectManager::Get().GetAssetObjectRepository()
		.ResolveLatest<VansClothProfile>(profileGuid);
	if (!profile)
	{
		error = "Cloth Profile is not loaded in memory: " + *config.profileGuid;
		return {};
	}
	VansClothNodeConfig clothConfig;
	clothConfig.enabled = true;
	clothConfig.simulation.stiffness = profile->m_Stiffness;
	clothConfig.simulation.stiffnessFrequency = profile->m_StiffnessFrequency;
	clothConfig.simulation.damping = profile->m_Damping;
	clothConfig.simulation.friction = profile->m_Friction;
	clothConfig.simulation.gravity = profile->m_Gravity;
	clothConfig.followBones = profile->m_FollowBones;

    // physicsAttachOffsetY is component-local runtime tuning layered on the
    // immutable in-memory cloth profile.
    // 用于将布料固定点从颈部/领口向下对准角色肩膀位置（单位：米）
    if (config.physicsAttachOffsetY)
		clothConfig.attachOffsetY = *config.physicsAttachOffsetY;

    // 通过 objectRef 解析碰撞球实体。优先缓存渲染节点或 Transform ID；
    // sceneObjectName 仅用于实体尚未完成构建时的延迟 Transform 查找。
    if (!config.collisionSpheres.empty())
    {
        for (const auto& collisionSphere : config.collisionSpheres)
        {
			VansClothCollisionBinding binding;
            if (!collisionSphere.objectRef.empty())
            {
                std::string objectName = collisionSphere.objectRef;
				binding.sceneObjectName = objectName;

                VansScriptObject* refObj = scene.FindSceneObjectByName(objectName);
                if (refObj)
                {
                    auto* rc = refObj->GetComponent<VansScriptRenderComponent>();
                    if (rc && rc->m_RenderNode)
                    {
						binding.renderNodeName = rc->m_RenderNode->m_NodeName;
                    }
                    else if (refObj->m_TransformID != 0)
                    {
                        // 无 render 组件但 ScriptObject 有自己的 transformID
						binding.transformId = refObj->m_TransformID;
                    }
                    // 否则保留实体名称，在运行时实体构建完成后解析其 Transform。
                }
            }
            if (collisionSphere.radius)
				binding.radius = *collisionSphere.radius;
            // 三种解析路径任意满足其一即加入列表
			if (!binding.renderNodeName.empty() || binding.transformId != UINT32_MAX ||
				!binding.sceneObjectName.empty())
			{
				clothConfig.collisionBindings.push_back(std::move(binding));
			}
        }
    }

	VansMesh* mesh = renderNode->m_Mesh;
	VansClothMeshSource meshSource;
	meshSource.positionsAndNormals = &mesh->GetMeshRawPositionData();
	meshSource.texCoords = &mesh->GetMeshRawTexCoordData();
	meshSource.triangleIndices = &mesh->GetMeshTriangleIndex();
	meshSource.vertexCount = mesh->GetMeshVertexCount();
	meshSource.vertexStrideBytes = mesh->GetMeshVertexStride();
	meshSource.modelMatrix = Vans::VansTransformStore::Read(
		renderNode->m_TransformID).GetModelMatrix();
	VansClothMeshData meshData;
	if (!VansClothMeshPrep::Build(
		meshSource,
		profile->m_WeldTolerance,
		profile->m_PinnedMatchTolerance,
		clothConfig.attachOffsetY,
		profile->m_PinnedLocalPositions,
		meshData,
		error))
	{
		error = "Cloth mesh preparation failed for '" + renderNode->m_NodeName + "': " + error;
		return {};
	}

	auto clothNode = std::make_unique<VansClothNode>();
	if (!clothNode->Initialize(
		clothConfig,
		std::move(meshData),
		renderNode->m_TransformID,
		renderNode->m_NodeName,
		error))
	{
		error = "Cloth runtime initialization failed for '" +
			renderNode->m_NodeName + "': " + error;
		return {};
	}
	profileGuidText = *config.profileGuid;
	return clothNode;
}

// ===========================================================================
// Single physics node creation
// ===========================================================================

std::unique_ptr<VansEngine::VansPhysicsNode>
VansGraphics::VansScenePhysicsComponentBuilder::CreatePhysicsNode(
    VansScene& scene,
	const Vans::VansScenePhysicsNodeConfig& config,
    VansRenderNode* associatedRenderNode,
	uint32_t standaloneTransformID,
	std::string& error)
{
    using namespace VansEngine;

    PhysicsNodeProperties properties;

    if (config.enabled)
        properties.enabled = *config.enabled;
    
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
    int physicsLayerIndex = -1;
    if (!VansEngine::VansCollisionLayerManager::Get().TryGetLayerIndex(
        properties.layerName, physicsLayerIndex))
    {
        error = "Physics component references unknown collision layer '" +
            properties.layerName + "'";
        return {};
    }

    // 解析 Trigger 标志
    if (config.isTrigger)
        properties.isTrigger = *config.isTrigger;
    if (config.hitRegion)
        properties.hitRegion = *config.hitRegion;

	if (associatedRenderNode == nullptr && standaloneTransformID == UINT32_MAX)
    {
		error = "Physics component requires an entity transform";
		return {};
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

	auto physicsNode = std::make_unique<VansPhysicsNode>();
	if (config.name)
		physicsNode->SetName(*config.name);
    physicsNode->Initialize(properties, transformID, mesh);
	if (properties.enabled && (!physicsNode->IsEnabled() || !physicsNode->HasActor()))
	{
		error = "Physics runtime initialization failed for component '" +
			physicsNode->GetName() + "'";
		return {};
	}
    return physicsNode;
}

// ===========================================================================
// Physics → render transform synchronization
// ===========================================================================

void VansGraphics::VansScene::UpdatePhysicsTransforms()
{
	VANS_ASSERT_MAIN_THREAD();
    VANS_ASSERT_FRAME_PHASE(VansFramePhase::GameLogic);

    using namespace VansEngine;
    
    // Get physics system
    VansPhysicsSystem& physics = VansPhysicsSystem::GetInstance();
    
    // 1. Acquire the simulation lock FIRST
    // This blocks if the simulation thread is currently inside its update loop (simulate -> fetchResults)
    // Once we have this lock, we know the simulation thread is waiting or sleeping, and NOT writing to the scene.
    // Use std::lock_guard or std::unique_lock with the mutex
    std::lock_guard<std::mutex> simLock(physics.GetSimulationMutex());

    PxScene* scene = VansPhysicsNativeAccess::Scene(physics);
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
            if (!Vans::VansTransformStore::IsDirty(transformID))
                continue;

            const auto& properties = physicsNode->GetProperties();
			// Dynamic bodies are simulation-driven. Static editor geometry and
			// kinematic/trigger bodies receive authored transform changes.
			if (properties.bodyType == PhysicsBodyType::Dynamic && !properties.isTrigger)
                continue;

            // const Vans::VansTransform& transformData = Vans::VansTransformStore::Read(transformID);
            // VANS_LOG("[PhysX Sync] Push transform -> physics: tid=" << transformID
            //          << " pos=(" << transformData.m_Position.x << ", " << transformData.m_Position.y << ", " << transformData.m_Position.z << ")"
            //          << " rot=(" << transformData.m_Rotation.x << ", " << transformData.m_Rotation.y << ", " << transformData.m_Rotation.z << ")"
            //          << " bodyType=" << static_cast<int>(properties.bodyType)
            //          << " isTrigger=" << properties.isTrigger);

            physicsNode->SyncActorFromTransformLocked();
        }
    }

    // 3. Read back physics simulation results into render transforms.
    PxSceneReadLock scopedLock(*scene);

    // Update all physics nodes from physics simulation
    for (auto* physicsNode : m_PhysicsNodes)
    {
        if (physicsNode && physicsNode->IsEnabled())
        {
            if (physicsNode->SyncTransformFromActorLocked())
            {
                // Record the transform ID if it has changed
                uint32_t transformID = physicsNode->GetTransformID();
                if (transformID != 0) // Invalid ID check
                {
					Vans::VansTransformStore::MarkDirty(transformID);
                }
            }
        }
    }

    // ── Update vehicle render node transforms ────────────────────────────────
    if (m_Vehicle)
    {
        // Helper: convert PxQuat to Euler angles in degrees for Vans::VansTransform
        auto PxQuatToEulerDeg = [](const PxQuat& q) -> glm::vec3
        {
            glm::quat gq(q.w, q.x, q.y, q.z);
            return glm::degrees(glm::eulerAngles(gq));
        };

        auto writeTransform = [&](uint32_t transformID, const PxTransform& pose, const PxVec3& pivotLocal = PxVec3(0.0f))
        {
            if (!Vans::VansTransformStore::IsAllocated(transformID))
                return false;

            Vans::VansTransform t = Vans::VansTransformStore::Read(transformID);
            const PxVec3 correctedPosition = pose.p - pose.q.rotate(pivotLocal);
            t.m_Position = glm::vec3(correctedPosition.x, correctedPosition.y, correctedPosition.z);
            t.m_Rotation = PxQuatToEulerDeg(pose.q);
            Vans::VansTransformStore::Write(transformID, t);
            Vans::VansTransformStore::MarkDirty(transformID);
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
	VANS_ASSERT_MAIN_THREAD();
	VANS_ASSERT_FRAME_PHASE(VansFramePhase::GameLogic);
	for (VansEngine::VansCharacterControllerNode* cct : m_CharControllerNodes)
	{
		if (!cct || !cct->IsEnabled())
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
			animation ? animation->GetCharacterMotionController() : nullptr;
		const bool hasConfiguredMotionModel =
			(animation && animation->TryGetCharacterMotionSettings(motionSettings)) ||
			(controller && controller->TryGetCharacterMotionSettings(motionSettings));

		const bool animationRoutesOwnerMotion = animation && controller &&
			animation->IsRootMotionEnabled() &&
			controller->ShouldApplyRootMotionToOwner();
		if (!cct->HasMotionIntent() && !animationRoutesOwnerMotion)
			continue;

		// 未配置独立运动模型的普通 Animation Graph 保持原有的全量 Root
		// Motion 语义；配置了运动模型时仍遵守 Capsule/RootMotion/Hybrid 策略。
		// 这使 Root Motion 能力与 Motion Matching 的存在完全解耦。
		glm::vec3 animationToWorldScale(motionSettings.rootMotionToWorldScale);
		if (!hasConfiguredMotionModel && animationRoutesOwnerMotion)
		{
			motionSettings.driveMode = Vans::VansLocomotionDriveMode::RootMotion;
			if (Vans::VansTransformStore::IsAllocated(cct->GetTransformID()))
				animationToWorldScale =
					Vans::VansTransformStore::Read(cct->GetTransformID()).m_Scale;
		}

		cct->PrepareLocomotion(deltaTime, motionSettings);
		bool rootMotionValid = false;
		bool rootMotionPreferred = false;
		bool motionMatchingUsed = false;
		glm::vec3 rootDelta(0.0f);
		glm::quat rootRotation(1.0f, 0.0f, 0.0f, 0.0f);
		if (animation && controller)
		{
			// 只要该动画与 CCT 共享 Transform，就在 CCT flush 前完成一次评估。
			// 当前 Graph 是否包含 Motion Matching 不再影响 Root Motion 提交。
			animation->PrepareCharacterMotionFrame(deltaTime, cct->GetTrajectory());
			rootDelta = animation->GetRootMotionDelta();
			rootRotation = animation->GetRootRotationDelta();
			rootMotionValid = animationRoutesOwnerMotion && animation->HasRootMotionDelta();
			rootMotionPreferred = controller->CharacterMotionPrefersRootMotion();
			motionMatchingUsed = controller->IsMotionMatchingUsedThisFrame();
			if (hasConfiguredMotionModel && !controller->IsMotionMatchingConfigured())
				rootMotionPreferred = true;
		}
		const Vans::VansLocomotionAuthority authority = Vans::SelectLocomotionAuthority(
			motionSettings, rootMotionValid, motionMatchingUsed, rootMotionPreferred);
		cct->ResolveLocomotion(
			rootDelta, rootRotation, rootMotionValid, authority,
			motionSettings, animationToWorldScale);
	}
}

void VansGraphics::VansScene::SyncAnimatedHurtBodies()
{
	VANS_ASSERT_MAIN_THREAD();
	VANS_ASSERT_FRAME_PHASE(VansFramePhase::RenderPrep);
    auto& physics = VansEngine::VansPhysicsSystem::GetInstance();
    std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
    PxScene* scene = VansEngine::VansPhysicsNativeAccess::Scene(physics);
    if (!scene) return;
    PxSceneWriteLock sceneLock(*scene);
    // 动画最终姿态与 Transform Graph 已完成，立即更新骨骼受击体的查询姿态。
    for (auto* node : m_PhysicsNodes)
        if (node && node->IsEnabled() && !node->GetProperties().hitRegion.empty() &&
            m_TransformGraph.HasParent(node->GetTransformID()))
            node->SyncActorFromTransformLocked();
}

void VansGraphics::VansScene::UpdateCharControllerTransforms()
{
	VANS_ASSERT_MAIN_THREAD();
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
// Create a single CharacterController from typed scene component config
// ===========================================================================

std::unique_ptr<VansEngine::VansCharacterControllerNode>
VansGraphics::VansScenePhysicsComponentBuilder::CreateCharacterControllerNode(
    const Vans::VansSceneCharacterControllerConfig& config,
    VansRenderNode* associatedRenderNode,
	uint32_t standaloneTransformID,
	std::string& error)
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
        props.m_LayerName  = *config.layer;
    int controllerLayerIndex = -1;
    if (!VansCollisionLayerManager::Get().TryGetLayerIndex(
        props.m_LayerName, controllerLayerIndex))
    {
        error = "CharacterController references unknown collision layer '" +
            props.m_LayerName + "'";
        return {};
    }
    if (config.climbingMode)
    {
        std::string cm = *config.climbingMode;
        props.m_ClimbingMode = (cm == "constrained")
            ? VansCharacterClimbingMode::Constrained
            : VansCharacterClimbingMode::Easy;
    }
    if (config.positionOffset)
        props.m_PositionOffset = ToVec3(*config.positionOffset);

    // 解析初始位置
    uint32_t transformID = 0;
    glm::vec3 spawnPos(0.0f);
    if (associatedRenderNode)
    {
        transformID = associatedRenderNode->m_TransformID;
        const Vans::VansTransform& t = Vans::VansTransformStore::Read(transformID);
        spawnPos = t.m_Position + props.m_PositionOffset;
    }
    else if (standaloneTransformID != UINT32_MAX)
    {
        transformID = standaloneTransformID;
        const Vans::VansTransform& t = Vans::VansTransformStore::Read(transformID);
        spawnPos = t.m_Position + props.m_PositionOffset;
    }

	auto node = std::make_unique<VansCharacterControllerNode>();
    if (!node->Initialize(props, transformID, spawnPos))
    {
		error = "CharacterController runtime initialization failed";
		return {};
    }

    // ── 延迟绑定标志：ragdoll 在第二阶段加载，先记录意图 ──────────────
    if (config.followRagdoll.value_or(false))
    {
        std::string bone = config.followRagdollBone.value_or("pelvis");
        node->SetPendingFollowRagdoll(true, bone);
    }

    return node;
}

// ===========================================================================
// Cloth simulation update
// ===========================================================================

void VansGraphics::VansScene::UpdateClothSimulation(float dt, std::uint32_t substeps)
{
	VANS_ASSERT_MAIN_THREAD();
	VANS_ASSERT_FRAME_PHASE(VansFramePhase::RenderPrep);
	if (m_ClothRuntimes.empty() || substeps == 0u) return;

    // ── 子步参数 ──────────────────────────────────────────────────────────────
    // 将每帧仿真拆分为配置的 substeps 个子步：
    //   1. 每子步时间步长缩小为 dt/substeps，约束冲量成比例缩小，避免数值爆炸。
    //   2. 固定点位置在上一帧目标与本帧目标之间线性插值，消除瞬间大位移引发的
    //      约束违反（骨骼动画过渡时尤为重要）。
    // 角色快速移动时 8 步提供足够稳定性；静态场景可降至 4 步节省 CPU。
	const float subDt = dt / static_cast<float>(substeps);

    // 第一步：计算本帧所有固定点的目标世界坐标（不写入粒子缓冲区）
	for (const auto& runtime : m_ClothRuntimes)
		if (runtime && runtime->node && runtime->node->IsEnabled())
			runtime->node->ComputePinnedTargets();

    // 第二步：更新碰撞球（每帧一次，不需要随子步变化）
	for (const auto& runtime : m_ClothRuntimes)
    {
		VansEngine::VansClothNode* clothNode = runtime ? runtime->node : nullptr;
        if (!clothNode || !clothNode->IsEnabled()) continue;
		const auto& bindings = clothNode->GetCollisionBindings();
		if (bindings.empty()) continue;

		std::vector<glm::vec4> spheres;
		spheres.reserve(bindings.size());
		for (std::size_t bindingIndex = 0; bindingIndex < bindings.size(); ++bindingIndex)
        {
			const auto& binding = bindings[bindingIndex];
			std::uint32_t transformId = binding.transformId;
            // 延迟解析场景实体；骨骼/Socket 挂接已经由 Transform Graph 更新实体世界变换。
			if (binding.renderNodeName.empty() && transformId == UINT32_MAX &&
				!binding.sceneObjectName.empty())
            {
				if (VansScriptObject* object = FindObjectByName(binding.sceneObjectName))
				{
					transformId = object->m_TransformID;
					clothNode->CacheCollisionTransform(bindingIndex, transformId);
				}
            }

            glm::vec3 pos(0.0f);
            bool valid = false;

			if (!binding.renderNodeName.empty())
            {
                // 优先路径：通过 render 节点名查找位置
				VansRenderNode* rn = FindRenderNodeByName(binding.renderNodeName);
                if (rn)
                {
                    pos   = Vans::VansTransformStore::Read(rn->m_TransformID).m_Position;
                    valid = true;
                }
            }
			else if (Vans::VansTransformStore::IsAllocated(transformId))
            {
                // 回退路径：直接读取 TransformStore（骨骼绑定的纯物理碰撞体）
				pos = Vans::VansTransformStore::Read(transformId).m_Position;
                valid = true;
            }

            if (!valid) continue;

			spheres.emplace_back(pos.x, pos.y, pos.z, binding.radius);
        }
        clothNode->SetCollisionSpheres(spheres);
    }

    // 第三步：子步循环——每步写入插值固定点位置，然后推进仿真
	for (std::uint32_t stepIndex = 0; stepIndex < substeps; ++stepIndex)
    {
        // alpha: 第 1 步=1/N, 第 2 步=2/N, ..., 最后一步=1.0
		const float alpha = static_cast<float>(stepIndex + 1u) /
			static_cast<float>(substeps);
		for (const auto& runtime : m_ClothRuntimes)
			if (runtime && runtime->node && runtime->node->IsEnabled())
				runtime->node->WritePinnedParticlesLerped(alpha);

        VansEngine::VansClothSystem::GetInstance().SimulateStep(subDt);
    }

    // 第四步：提交本帧目标为"上一帧"，供下帧插值使用
	for (const auto& runtime : m_ClothRuntimes)
		if (runtime && runtime->node && runtime->node->IsEnabled())
			runtime->node->CommitPinnedTargets();
}

void VansGraphics::VansScene::WriteClothResultsToStagingBuffers(
	const VansRenderSceneFrameSnapshot& snapshot)
{
	for (const VansRenderClothFrameData& cloth : snapshot.cloth)
    {
		if (cloth.clothRuntimeIndex >= m_ClothRuntimes.size())
			continue;
		const auto& runtime = m_ClothRuntimes[cloth.clothRuntimeIndex];
		if (!runtime) continue;
		VansVKBuffer& staging = runtime->stagingBuffer;
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
		if (cloth.clothRuntimeIndex >= m_ClothRuntimes.size())
		{
			continue;
		}
		const auto& runtime = m_ClothRuntimes[cloth.clothRuntimeIndex];
		if (!runtime) continue;
		VansVKBuffer& staging = runtime->stagingBuffer;
		if (!staging.IsMapped()) continue;

		VansGraphics::VansRenderNode* renderNode = runtime->renderNode;
        if (!renderNode || !renderNode->m_Mesh) continue;

        VkBuffer dstBuffer = renderNode->m_Mesh->GetBLASVertexBuffer().GetNativeBuffer();
		VkDeviceSize size = static_cast<VkDeviceSize>(
			cloth.simulatedVertices.size() * sizeof(uint16_t));
        if (size == 0) continue;
		const VkDeviceSize destinationSize =
			renderNode->m_Mesh->GetBLASVertexBuffer().GetBufferSize();
		if (size != staging.GetBufferSize() || size != destinationSize)
		{
			VANS_LOG_ERROR("[VansScene] Cloth upload payload no longer matches its vertex buffers.");
			continue;
		}

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
