#include "../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansScene.h"
#include "../PhysicsCore/VansPhysics.h"
#include "../PhysicsCore/VansPhysicsNode.h"
#include "../PhysicsCore/VansPhysicsVehicle.h"
#include "../PhysicsCore/VansClothNode.h"
#include "../PhysicsCore/VansClothSystem.h"
#include "../Configration/VansConfigration.h"
#include "../ScriptCore/VansScriptContext.h"

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
// Single cloth node loading (extracted from LoadPhysicsNodes)
// ===========================================================================

VansEngine::VansClothNode* VansGraphics::VansScene::LoadSingleClothNode(const json& clothNodeJson, VansRenderNode* associatedRenderNode)
{
    using namespace VansEngine;

    VansRenderNode* renderNode = associatedRenderNode;

    // If no associated render node was passed, try to find one by name (legacy path)
    if (!renderNode && clothNodeJson.contains("renderNode"))
    {
        std::string renderNodeName = clothNodeJson["renderNode"].get<std::string>();
        renderNode = FindRenderNodeByName(renderNodeName);
    }

    if (!renderNode)
    {
        VANS_LOG_WARN("[VansScene] LoadSingleClothNode: no valid render node, skipping.");
        return nullptr;
    }

    ClothNodeProperties clothProps;
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

    // Parse collision sphere references — supports both old ("renderNode") and new ("objectRef") keys
    if (clothNodeJson.contains("collisionSpheres"))
    {
        for (const auto& csJson : clothNodeJson["collisionSpheres"])
        {
            ClothNodeProperties::CollisionSphereRef ref;
            if (csJson.contains("renderNode"))
                ref.renderNodeName = csJson["renderNode"].get<std::string>();
            else if (csJson.contains("objectRef"))
            {
                // New path: resolve objectRef → render node name
                std::string objectName = csJson["objectRef"].get<std::string>();
                VansScriptObject* refObj = FindObjectByName(objectName);
                if (refObj)
                {
                    auto* rc = refObj->GetComponent<VansScriptRenderComponent>();
                    if (rc && rc->m_RenderNode)
                        ref.renderNodeName = rc->m_RenderNode->m_NodeName;
                }
            }
            if (csJson.contains("radius"))
                ref.radius = csJson["radius"].get<float>();
            if (!ref.renderNodeName.empty())
                clothProps.collisionSphereRefs.push_back(ref);
        }
    }

    VansClothNode* clothNode = new VansClothNode();
    clothNode->Initialize(clothProps, renderNode);
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
// Single physics node loading (extracted from LoadPhysicsNodes)
// ===========================================================================

VansEngine::VansPhysicsNode* VansGraphics::VansScene::LoadSinglePhysicsNode(const json& physicsNodeJson, VansRenderNode* associatedRenderNode)
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

    // Resolve transform ID
    uint32_t transformID = 0;
    if (associatedRenderNode)
    {
        // New path: use the associated render node from the same ScriptObject
        transformID = associatedRenderNode->m_TransformID;
    }
    else if (physicsNodeJson.contains("transformID"))
    {
        transformID = physicsNodeJson["transformID"];
    }
    else if (physicsNodeJson.contains("renderNode"))
    {
        std::string renderNodeName = physicsNodeJson["renderNode"].get<std::string>();
        VansRenderNode* rn = FindRenderNodeByName(renderNodeName);
        if (rn)
            transformID = rn->m_TransformID;
        else
            VANS_LOG_WARN("[VansScene] Physics node: renderNode '" << renderNodeName << "' not found.");
    }
    else if (physicsNodeJson.contains("name"))
    {
        std::string nodeName = physicsNodeJson["name"];
        for (auto* renderNode : m_OpaqueRenderNodes)
        {
            if (renderNode->m_NodeName == nodeName)
            { transformID = renderNode->m_TransformID; break; }
        }
        for (auto* renderNode : m_TransParentRenderNodes)
        {
            if (renderNode->m_NodeName == nodeName)
            { transformID = renderNode->m_TransformID; break; }
        }
    }

    // Get mesh reference if needed
    VansMesh* mesh = nullptr;
    if (properties.useMeshCollider && physicsNodeJson.contains("mesh"))
    {
        std::string meshName = physicsNodeJson["mesh"];
        mesh = static_cast<VansMesh*>(GetMeshAsset(meshName));
    }

    VansPhysicsNode* physicsNode = new VansPhysicsNode();
    physicsNode->Initialize(properties, transformID, mesh);

    if (physicsNodeJson.contains("name"))
        physicsNode->SetName(physicsNodeJson["name"]);

    m_PhysicsNodes.push_back(physicsNode);
    return physicsNode;
}

// ===========================================================================
// Physics node loading from JSON (delegates to single-node helpers)
// ===========================================================================

void VansGraphics::VansScene::LoadPhysicsNodes(json& physics_node)
{
    using namespace VansEngine;

    for (const auto& physicsNodeJson : physics_node)
    {
        // ── Vehicle physics node ─────────────────────────────────────────────
        if (physicsNodeJson.contains("nodeType") && physicsNodeJson["nodeType"].get<std::string>() == "vehicle")
        {
            if (m_Vehicle)
            {
                VANS_LOG_WARN("[VansScene] LoadPhysicsNodes: vehicle node already initialized, skipping.");
                continue;
            }

            glm::vec3 spawnPos(0.0f, 5.0f, 0.0f);
            if (physicsNodeJson.contains("position"))
            {
                auto& p = physicsNodeJson["position"];
                spawnPos = glm::vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
            }

            std::string bodyNodeName;
            if (physicsNodeJson.contains("bodyRenderNode"))
                bodyNodeName = physicsNodeJson["bodyRenderNode"].get<std::string>();

            std::vector<std::string> tireNodeNames;
            if (physicsNodeJson.contains("tireRenderNodes"))
            {
                for (const auto& t : physicsNodeJson["tireRenderNodes"])
                    tireNodeNames.push_back(t.get<std::string>());
            }

            InitVehicle(&VansPhysicsSystem::GetInstance(), spawnPos, bodyNodeName, tireNodeNames);
            continue;
        }
        // ── Cloth simulation node ────────────────────────────────────────────
        if (physicsNodeJson.contains("nodeType") && physicsNodeJson["nodeType"].get<std::string>() == "cloth")
        {
            if (!physicsNodeJson.contains("renderNode"))
            {
                VANS_LOG_WARN("[VansScene] Cloth node missing 'renderNode' field, skipping.");
                continue;
            }
            LoadSingleClothNode(physicsNodeJson, nullptr);
            continue;
        }
        // ── Regular physics node ─────────────────────────────────────────────
        if (!physicsNodeJson.contains("enabled"))
            continue;
        LoadSinglePhysicsNode(physicsNodeJson, nullptr);
    }
}

// ===========================================================================
// Physics → render transform synchronization
// ===========================================================================

void VansGraphics::VansScene::UpdatePhysicsTransforms()
{
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
    
    // 2. Now it is safe to acquire the PhysX read lock
    // Even though we have the mutex, proper PhysX usage still prefers using the ReadLock
    // just in case internal PxScene operations require it.
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
// Cloth simulation update
// ===========================================================================

void VansGraphics::VansScene::UpdateClothSimulation(float dt)
{
    if (m_ClothNodes.empty()) return;

    // Sync all pinned particles to their render node transforms first
    for (auto* clothNode : m_ClothNodes)
    {
        if (clothNode) clothNode->SyncPinnedParticlesToRenderNode();
    }

    // Sync collision spheres from render node world positions for each cloth node.
    // NOTE: sphere positions are passed in WORLD SPACE, matching the cloth's
    // world-space particle positions (NvCloth particles are initialized in
    // world space via the render node's rest-pose transform).
    static bool loggedOnce = false;
    for (auto* clothNode : m_ClothNodes)
    {
        if (!clothNode) continue;
        const auto& sphereRefs = clothNode->GetCollisionSphereRefs();
        if (sphereRefs.empty()) continue;

        std::vector<physx::PxVec4> spheres;
        spheres.reserve(sphereRefs.size());
        for (const auto& ref : sphereRefs)
        {
            VansRenderNode* rn = FindRenderNodeByName(ref.renderNodeName);
            if (!rn) continue;
            const VansTransform& t = VansTransformStore::GetTransform(rn->m_TransformID);
            // World-space sphere: centre = render node position, radius from JSON
            // (passed directly in world space — cloth particles are also in world space)
            spheres.push_back(physx::PxVec4(t.m_Position.x, t.m_Position.y, t.m_Position.z,
                                            ref.radius));
            if (!loggedOnce)
            {
                VANS_LOG("[VansScene] Cloth collision sphere (world): node='" << ref.renderNodeName
                          << "' pos=(" << t.m_Position.x << "," << t.m_Position.y << "," << t.m_Position.z
                          << ") radius=" << ref.radius);
            }
        }
        clothNode->SetCollisionSpheres(spheres);
    }
    loggedOnce = true;
    

    // Advance NvCloth simulation by dt
    VansEngine::VansClothSystem::GetInstance().SimulateStep(dt);
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
