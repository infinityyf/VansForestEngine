#include "../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansScene.h"
#include "../PhysicsCore/VansPhysics.h"
#include "../PhysicsCore/VansPhysicsNode.h"
#include "../PhysicsCore/VansPhysicsVehicle.h"
#include "../PhysicsCore/VansClothNode.h"
#include "../PhysicsCore/VansClothSystem.h"
#include "../PhysicsCore/VansClothProfile.h"
#include "../PhysicsCore/VansCharacterControllerNode.h"
#include "../PhysicsCore/VansCollisionLayerManager.h"
#include "../Configration/VansConfigration.h"
#include "../ScriptCore/VansScriptContext.h"
#include "../AnimationCore/VansBoneAttachmentSystem.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../VansFramePhase.h"

#include "VulkanCore/VansMesh.h"
#include "VulkanCore/VansVKDevice.h"
#include "../Util/VansLog.h"
#include <cstring>

// ===========================================================================
// Vehicle initialization
// ===========================================================================

void VansGraphics::VansScene::InitVehicle(VansEngine::VansPhysicsSystem* physicsSystem, const glm::vec3& position,
    const std::string& bodyRenderNodeName, const std::vector<std::string>& tireRenderNodeNames)
{
    if (m_Vehicle) return; // Already initialized

    m_Vehicle = new VansEngine::VansPhysicsVehicle();
    m_Vehicle->SetBodyRenderNodeName(bodyRenderNodeName);
    m_Vehicle->SetTireRenderNodeNames(tireRenderNodeNames);
    // Convert glm::vec3 to PxTransform (physx uses right-handed Y-up, similar to GLM standard)
    PxTransform startPose(PxVec3(position.x, position.y + 2.0f, position.z), PxQuat(PxIdentity));

    // Initialize with default parameters (empty path triggers built-in defaults)
    m_Vehicle->Initialize(physicsSystem, "", startPose);

    VANS_LOG("[VansScene] Vehicle initialized at " << position.x << ", " << position.y << ", " << position.z
              << ", bodyNode='" << bodyRenderNodeName << "', tires=" << tireRenderNodeNames.size());
}

// ===========================================================================
// Physics node loading from JSON
// ===========================================================================

// ===========================================================================
// Single cloth node loading
// ===========================================================================

VansEngine::VansClothNode* VansGraphics::VansScene::LoadSingleClothNode(const json& clothNodeJson, VansRenderNode* associatedRenderNode, std::string* outProfilePath)
{
    using namespace VansEngine;

    VansRenderNode* renderNode = associatedRenderNode;

    if (!renderNode)
    {
        VANS_LOG_WARN("[VansScene] LoadSingleClothNode: no valid render node, skipping.");
        return nullptr;
    }

    ClothNodeProperties clothProps;

    // ── 新格式：通过 profilePath 从 .clothprofile 文件加载配置 ──────────────
    if (clothNodeJson.contains("profilePath"))
    {
        std::string profilePath = clothNodeJson["profilePath"].get<std::string>();

        VansClothProfile profile;
        if (!profile.LoadFromFile(profilePath))
        {
            VANS_LOG_ERROR("[VansScene] LoadSingleClothNode: 加载 Profile 失败: " << profilePath
                           << "，回退为默认参数。");
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
                VANS_LOG_WARN("[VansScene] LoadSingleClothNode: RenderNode 无 Mesh，无法解析固定点索引。");
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
        // ── 旧格式（向后兼容）：直接从 JSON 内联解析 ───────────────────────
        clothProps.enabled = true;
        if (clothNodeJson.contains("stiffness"))     clothProps.stiffness     = clothNodeJson["stiffness"].get<float>();
        if (clothNodeJson.contains("damping"))       clothProps.damping       = clothNodeJson["damping"].get<float>();
        if (clothNodeJson.contains("friction"))      clothProps.friction      = clothNodeJson["friction"].get<float>();
        if (clothNodeJson.contains("selfCollision")) clothProps.selfCollision = clothNodeJson["selfCollision"].get<bool>();
        if (clothNodeJson.contains("gravity"))
        {
            auto& g = clothNodeJson["gravity"];
            clothProps.gravity = g[1].get<float>();
        }
        if (clothNodeJson.contains("pinnedParticles"))
        {
            for (const auto& idx : clothNodeJson["pinnedParticles"])
                clothProps.pinnedParticleIndices.push_back(idx.get<uint32_t>());
        }
    }

    // 解析 physicsAttachOffsetY — 无论使用 profilePath 还是旧格式均适用
    // 用于将布料固定点从颈部/领口向下对准角色肩膀位置（单位：米）
    if (clothNodeJson.contains("physicsAttachOffsetY"))
        clothProps.attachOffsetY = clothNodeJson["physicsAttachOffsetY"].get<float>();

    // 通过 objectRef 解析碰撞球引用。
    // 三种解析路径（优先级递减）：
    // 1. 对象有 render 组件 → 存 renderNodeName，运行时 FindRenderNodeByName 查找
    // 2. 对象无 render 但有有效 m_TransformID → 存 transformID
    // 3. 对象为纯物理骨骼绑定体（骨骼绑定在第四 pass 才加载）→ 存 sceneObjectName，运行时通过 BoneAttachmentSystem 延迟解析
    if (clothNodeJson.contains("collisionSpheres"))
    {
        for (const auto& csJson : clothNodeJson["collisionSpheres"])
        {
            ClothNodeProperties::CollisionSphereRef ref;
            if (csJson.contains("objectRef"))
            {
                std::string objectName = csJson["objectRef"].get<std::string>();
                ref.sceneObjectName = objectName;  // 始终保存原始名称，供延迟解析使用

                VansScriptObject* refObj = FindObjectByName(objectName);
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
                    // 否则保持未解析状态，第一帧通过 BoneAttachmentSystem 延迟查找
                }
            }
            if (csJson.contains("radius"))
                ref.radius = csJson["radius"].get<float>();
            // 三种解析路径任意满足其一即加入列表
            if (!ref.renderNodeName.empty() || ref.transformID != UINT32_MAX || !ref.sceneObjectName.empty())
                clothProps.collisionSphereRefs.push_back(ref);
        }
    }

    VansClothNode* clothNode = new VansClothNode();
    clothNode->Initialize(clothProps, renderNode);
    // AnimationNode 绑定延迟至 Pass5（VansSceneLoader::LoadSceneObjects 末尾）完成，
    // 届时 m_AnimationNodes 已由 Pass4 完全填充。

    m_ClothNodes.push_back(clothNode);

    // Allocate a scene-owned HOST_VISIBLE staging buffer for this cloth node.
    VkDeviceSize stagingSize =
        static_cast<VkDeviceSize>(renderNode->m_Mesh
            ? renderNode->m_Mesh->GetMeshVertexCount() : 0)
        * static_cast<VkDeviceSize>(renderNode->m_Mesh
            ? renderNode->m_Mesh->GetMeshVertexStride() : 8 * sizeof(uint16_t));
    VansVKDevice* vkDev = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VkDevice nativeDev  = vkDev ? vkDev->GetLogicDevice() : VK_NULL_HANDLE;
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
    VANS_LOG("[VansScene] Cloth node created for render node '" << renderNode->m_NodeName << "'");

    return clothNode;
}

// ===========================================================================
// Single physics node loading
// ===========================================================================

VansEngine::VansPhysicsNode* VansGraphics::VansScene::LoadSinglePhysicsNode(
	const json& physicsNodeJson, VansRenderNode* associatedRenderNode, uint32_t standaloneTransformID)
{
    using namespace VansEngine;

    // Parse physics properties from JSON
    PhysicsNodeProperties properties;

    if (physicsNodeJson.contains("enabled"))
        properties.enabled = physicsNodeJson["enabled"];
    
    if (!properties.enabled)
        return nullptr;

    // Body type: "static", "dynamic", "kinematic"
    if (physicsNodeJson.contains("bodyType"))
    {
        std::string bodyTypeStr = physicsNodeJson["bodyType"];
        if (bodyTypeStr == "static")
            properties.bodyType = PhysicsBodyType::Static;
        else if (bodyTypeStr == "dynamic")
            properties.bodyType = PhysicsBodyType::Dynamic;
        else if (bodyTypeStr == "kinematic")
            properties.bodyType = PhysicsBodyType::Kinematic;
    }

    // Collider type
    if (physicsNodeJson.contains("colliderType"))
    {
        std::string colliderTypeStr = physicsNodeJson["colliderType"];
        if (colliderTypeStr == "box")
            properties.colliderType = PhysicsColliderType::Box;
        else if (colliderTypeStr == "sphere")
            properties.colliderType = PhysicsColliderType::Sphere;
        else if (colliderTypeStr == "capsule")
            properties.colliderType = PhysicsColliderType::Capsule;
        else if (colliderTypeStr == "mesh")
            properties.colliderType = PhysicsColliderType::Mesh;
        else if (colliderTypeStr == "convex")
            properties.colliderType = PhysicsColliderType::ConvexMesh;
    }

    if (physicsNodeJson.contains("mass"))
        properties.mass = physicsNodeJson["mass"];
    if (physicsNodeJson.contains("useMeshCollider"))
        properties.useMeshCollider = physicsNodeJson["useMeshCollider"];
    if (physicsNodeJson.contains("useConvexDecomposition"))
        properties.useConvexDecomposition = physicsNodeJson["useConvexDecomposition"];

    if (physicsNodeJson.contains("material"))
    {
        auto& materialJson = physicsNodeJson["material"];
        if (materialJson.contains("staticFriction"))
            properties.material.staticFriction = materialJson["staticFriction"];
        if (materialJson.contains("dynamicFriction"))
            properties.material.dynamicFriction = materialJson["dynamicFriction"];
        if (materialJson.contains("restitution"))
            properties.material.restitution = materialJson["restitution"];
    }

    if (physicsNodeJson.contains("boxExtents"))
    {
        auto& extents = physicsNodeJson["boxExtents"];
        properties.boxExtents = glm::vec3(extents[0], extents[1], extents[2]);
    }
    if (physicsNodeJson.contains("sphereRadius"))
        properties.sphereRadius = physicsNodeJson["sphereRadius"];
    if (physicsNodeJson.contains("capsuleRadius"))
        properties.capsuleRadius = physicsNodeJson["capsuleRadius"];
    if (physicsNodeJson.contains("capsuleHalfHeight"))
        properties.capsuleHalfHeight = physicsNodeJson["capsuleHalfHeight"];

    // 解析碰撞 Layer
    if (physicsNodeJson.contains("layer"))
        properties.layerName = physicsNodeJson["layer"].get<std::string>();
    properties.layerIndex = VansEngine::VansCollisionLayerManager::Get().GetLayerIndex(properties.layerName);

    // 解析 Trigger 标志
    if (physicsNodeJson.contains("isTrigger"))
        properties.isTrigger = physicsNodeJson["isTrigger"].get<bool>();

	if (associatedRenderNode == nullptr && standaloneTransformID == UINT32_MAX)
    {
		VANS_LOG_WARN("[VansScene] Physics component has no transform, skipping.");
        return nullptr;
    }

	uint32_t transformID = associatedRenderNode ? associatedRenderNode->m_TransformID : standaloneTransformID;

    // Get mesh reference if needed
    VansMesh* mesh = nullptr;
    if (properties.useMeshCollider && physicsNodeJson.contains("mesh"))
    {
        std::string meshName = physicsNodeJson["mesh"];
        mesh = static_cast<VansMesh*>(GetMeshAsset(meshName));
    }

    VansPhysicsNode* physicsNode = new VansPhysicsNode();
	if (physicsNodeJson.contains("name"))
		physicsNode->SetName(physicsNodeJson["name"]);
    physicsNode->Initialize(properties, transformID, mesh);
	if (!physicsNode->IsEnabled() || physicsNode->GetActor() == nullptr)
	{
		VANS_LOG_ERROR("[VansScene] Failed to initialize physics component '"
			<< physicsNode->GetName() << "'");
		delete physicsNode;
		return nullptr;
	}
    m_PhysicsNodes.push_back(physicsNode);
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

        // Update car body render node
        const std::string& bodyNodeName = m_Vehicle->GetBodyRenderNodeName();
        if (!bodyNodeName.empty())
        {
            VansRenderNode* bodyNode = FindRenderNodeByName(bodyNodeName);
            if (bodyNode)
            {
                const PxTransform& bodyPose = m_Vehicle->GetTransform();
                VansTransform& t = VansTransformStore::GetTransform(bodyNode->m_TransformID);
                t.m_Position = glm::vec3(bodyPose.p.x, bodyPose.p.y, bodyPose.p.z);
                t.m_Rotation = PxQuatToEulerDeg(bodyPose.q);
                VansTransformStore::TransformIDToTransformDirty.insert({ bodyNode->m_TransformID, true });
            }
        }

        // Update tire render nodes (one per wheel index)
        const std::vector<std::string>& tireNodeNames = m_Vehicle->GetTireRenderNodeNames();
        const uint32_t numTires = static_cast<uint32_t>(tireNodeNames.size());
        for (uint32_t wi = 0; wi < numTires; ++wi)
        {
            const std::string& tireName = tireNodeNames[wi];
            if (tireName.empty()) continue;
            VansRenderNode* tireNode = FindRenderNodeByName(tireName);
            if (!tireNode) continue;

            PxTransform wheelPose = m_Vehicle->GetWheelWorldPose(wi);
            VansTransform& t = VansTransformStore::GetTransform(tireNode->m_TransformID);
            t.m_Position = glm::vec3(wheelPose.p.x, wheelPose.p.y, wheelPose.p.z);
            t.m_Rotation = PxQuatToEulerDeg(wheelPose.q);
            VansTransformStore::TransformIDToTransformDirty.insert({ tireNode->m_TransformID, true });
        }
    }
}

// ===========================================================================
// Character Controller transform update
// ===========================================================================

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
// Load a single CharacterController from JSON
// ===========================================================================

VansEngine::VansCharacterControllerNode*
VansGraphics::VansScene::LoadSingleCharControllerNode(
    const json& charCtrlJson,
    VansRenderNode* associatedRenderNode)
{
    using namespace VansEngine;

    CharControllerProperties props;

    if (charCtrlJson.contains("radius"))
        props.m_Radius = charCtrlJson["radius"].get<float>();
    if (charCtrlJson.contains("height"))
        props.m_Height = charCtrlJson["height"].get<float>();
    if (charCtrlJson.contains("slopeLimit"))
        props.m_SlopeLimit = charCtrlJson["slopeLimit"].get<float>();
    if (charCtrlJson.contains("stepOffset"))
        props.m_StepOffset = charCtrlJson["stepOffset"].get<float>();
    if (charCtrlJson.contains("contactOffset"))
        props.m_ContactOffset = charCtrlJson["contactOffset"].get<float>();
    if (charCtrlJson.contains("layer"))
    {
        props.m_LayerName  = charCtrlJson["layer"].get<std::string>();
        props.m_LayerIndex = VansCollisionLayerManager::Get()
                                 .GetLayerIndex(props.m_LayerName);
    }
    if (charCtrlJson.contains("climbingMode"))
    {
        std::string cm = charCtrlJson["climbingMode"].get<std::string>();
        props.m_ClimbingMode = (cm == "constrained")
            ? PxCapsuleClimbingMode::eCONSTRAINED
            : PxCapsuleClimbingMode::eEASY;
    }
    if (charCtrlJson.contains("positionOffset"))
    {
        const auto& o = charCtrlJson["positionOffset"];
        props.m_PositionOffset = glm::vec3(
            o[0].get<float>(), o[1].get<float>(), o[2].get<float>());
    }

    // 解析初始位置
    uint32_t transformID = 0;
    glm::vec3 spawnPos(0.0f);
    if (associatedRenderNode)
    {
        transformID = associatedRenderNode->m_TransformID;
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
    if (charCtrlJson.contains("followRagdoll") && charCtrlJson["followRagdoll"].get<bool>())
    {
        std::string bone = "pelvis";
        if (charCtrlJson.contains("followRagdollBone"))
            bone = charCtrlJson["followRagdollBone"].get<std::string>();
        node->SetPendingFollowRagdoll(true, bone);
    }

    m_CharControllerNodes.push_back(node);
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
            // 延迟解析：若前两种路径都未解析，则尝试通过 BoneAttachmentSystem 查找
            if (ref.renderNodeName.empty() && ref.transformID == UINT32_MAX
                && !ref.sceneObjectName.empty())
            {
                ref.transformID = VansEngine::VansBoneAttachmentSystem::GetInstance()
                                      .FindTransformIDByPhysicsObjectName(ref.sceneObjectName);
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

void VansGraphics::VansScene::WriteClothResultsToStagingBuffers()
{
    for (size_t i = 0; i < m_ClothNodes.size(); ++i)
    {
        VansEngine::VansClothNode* clothNode = m_ClothNodes[i];
        if (!clothNode) continue;

        // 1. NvCloth → CPU fp16 buffer (inside ClothNode, no Vulkan)
        clothNode->WriteSimResults();

        // 2. CPU → scene-owned HOST_VISIBLE staging buffer
        if (i >= m_ClothStagingBuffers.size()) continue;
        VansVKBuffer& staging = m_ClothStagingBuffers[i];
        if (!staging.IsMapped()) continue;

        const std::vector<uint16_t>& cpuData = clothNode->GetSimulatedVertexData();
        size_t byteSize = cpuData.size() * sizeof(uint16_t);
        if (byteSize == 0) continue;

        std::memcpy(staging.GetMappedPtr(), cpuData.data(), byteSize);
    }
}

void VansGraphics::VansScene::RecordClothVertexUploads(VkCommandBuffer cmd)
{
    for (size_t i = 0; i < m_ClothNodes.size(); ++i)
    {
        VansEngine::VansClothNode* clothNode = m_ClothNodes[i];
        if (!clothNode || i >= m_ClothStagingBuffers.size()) continue;

        VansVKBuffer& staging = m_ClothStagingBuffers[i];
        if (!staging.IsMapped()) continue;

        VansGraphics::VansRenderNode* renderNode = clothNode->GetTargetRenderNode();
        if (!renderNode || !renderNode->m_Mesh) continue;

        VkBuffer dstBuffer = renderNode->m_Mesh->GetBLASVertexBuffer().GetNativeBuffer();
        VkDeviceSize size   = clothNode->GetSimulatedDataByteSize();
        if (size == 0) continue;

        VkBufferCopy region{};
        region.srcOffset = 0;
        region.dstOffset = 0;
        region.size      = size;
        vkCmdCopyBuffer(cmd, staging.GetNativeBuffer(), dstBuffer, 1, &region);

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
        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
            0,
            0, nullptr,
            1, &barrier,
            0, nullptr);
    }
}
