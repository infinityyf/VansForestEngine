#include "VansClothNode.h"
#include "VansClothSystem.h"
#include "../RenderCore/VulkanCore/VansMesh.h"
#include "../ScriptCore/VansTransform.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../AnimationCore/VansAnimationController.h"
#include "../Util/VansLog.h"

// NvCloth extensions cooker
#include <NvClothExt/ClothFabricCooker.h>
#include <NvClothExt/ClothMeshDesc.h>

// GLM packing helpers for float16 conversion
#include <GLM/gtc/packing.hpp>

#include <cassert>
#include <map>
#include <tuple>
#include <cmath>

// ─── float → half helper (same as VansMesh.cpp) ──────────────────────────────
static uint16_t F16(float f) { return glm::packHalf1x16(f); }

namespace VansEngine
{
    VansClothNode::VansClothNode()  = default;
    VansClothNode::~VansClothNode() = default;

    // =========================================================================
    void VansClothNode::Initialize(const ClothNodeProperties& props,
                                   VansGraphics::VansRenderNode* renderNode)
    {
        assert(renderNode && "VansClothNode::Initialize: renderNode must not be null");
        m_TargetRenderNode = renderNode;
        m_Enabled          = props.enabled;

        // ── 1. Obtain mesh CPU data ───────────────────────────────────────────
        VansGraphics::VansMesh* mesh = renderNode->m_Mesh;
        if (!mesh)
        {
            VANS_LOG_ERROR("[VansClothNode] Render node '" << renderNode->m_NodeName << "' has no mesh.");
            return;
        }

        const std::vector<float>& rawPos = mesh->GetMeshRawPositionData(); // float4: x,y,z,pad
        const std::vector<int>&   rawIdx = mesh->GetMeshTriangleIndex();

        m_VertexCount = mesh->GetMeshVertexCount();
        if (m_VertexCount == 0 || rawIdx.empty())
        {
            VANS_LOG_ERROR("[VansClothNode] Mesh '" << renderNode->m_NodeName << "' has no geometry.");
            return;
        }

        // ── 2. Extract rest positions and UV coordinates ──────────────────────
        // rawPos layout: [x,y,z,pad, nx,ny,nz,pad, ...] — 8 floats per vertex
        // (see VansMesh.cpp ProcessNode: position float4, normal float4)
        // rawIdx: plain int32 triangle indices
        m_Indices.resize(rawIdx.size());
        for (size_t i = 0; i < rawIdx.size(); ++i)
            m_Indices[i] = static_cast<uint32_t>(rawIdx[i]);

        // Detect tangent support from mesh vertex stride.
        // Without tangent: 8 × uint16_t = 16 bytes.  With tangent: 14 × uint16_t = 28 bytes.
        m_HasTangent   = (mesh->GetMeshVertexStride() > 8 * sizeof(uint16_t));
        m_VertexStride = m_HasTangent ? 14 : 8;

        // Extract UV coordinates from the mesh's cached CPU-side tex-coord data.
        // When needCPUData=true the mesh retains m_MeshRawTexCoordData (2 floats per vertex).
        const std::vector<float>& rawUV = mesh->GetMeshRawTexCoordData();
        if (static_cast<int>(rawUV.size()) >= m_VertexCount * 2)
        {
            m_RestTexCoords.assign(rawUV.begin(), rawUV.begin() + m_VertexCount * 2);
        }
        else
        {
            VANS_LOG_WARN("[VansClothNode] Mesh '" << renderNode->m_NodeName
                          << "' has no CPU-side UV data; UVs will be zero.");
            m_RestTexCoords.assign(m_VertexCount * 2, 0.0f);
        }

        // ── 2b. Weld vertices by position ────────────────────────────────────
        // Assimp duplicates vertices at UV/normal/tangent seams so each
        // (pos, uv, normal, tangent) tuple is unique.  For NvCloth we need
        // shared particles at the same 3D position to create stretch/bend
        // constraints across the whole mesh, not just per-triangle.
        {
            const float WELD_GRID = 1e5f; // quantise to 0.00001 units
            std::map<std::tuple<int,int,int>, uint32_t> posToWelded;
            m_OrigToWelded.resize(m_VertexCount);
            m_WeldedVertexCount = 0;

            for (int v = 0; v < m_VertexCount; ++v)
            {
                float x = rawPos[v * 8 + 0];
                float y = rawPos[v * 8 + 1];
                float z = rawPos[v * 8 + 2];
                auto key = std::make_tuple(
                    static_cast<int>(std::round(x * WELD_GRID)),
                    static_cast<int>(std::round(y * WELD_GRID)),
                    static_cast<int>(std::round(z * WELD_GRID)));

                auto it = posToWelded.find(key);
                if (it != posToWelded.end())
                {
                    m_OrigToWelded[v] = it->second;
                }
                else
                {
                    uint32_t wIdx = static_cast<uint32_t>(m_WeldedVertexCount++);
                    posToWelded[key] = wIdx;
                    m_OrigToWelded[v] = wIdx;
                }
            }

            VANS_LOG("[VansClothNode] Vertex welding: " << m_VertexCount
                      << " original → " << m_WeldedVertexCount << " welded particles");
        }

        // ── 3. Capture rest-pose transform for world-space particle init ──
        // Apply the render node's rotation & scale (full model matrix) so
        // NvCloth particles live in WORLD SPACE.  This ensures gravity acts
        // in the correct direction and collision spheres (also world space)
        // match up without extra coordinate conversions.
        VansGraphics::VansTransform& restT =
            VansGraphics::VansTransformStore::GetTransform(renderNode->m_TransformID);
        glm::mat4 restMat    = restT.GetModelMatrix();
        m_RestNodeTransformInv = glm::inverse(restMat);

        // 存储世界空间 Y 轴附着偏移（由 JSON physicsAttachOffsetY 配置）
        m_WorldAttachOffsetY = props.attachOffsetY;

        // Build welded NvCloth particle array (PxVec4: x,y,z, invMass).
        // Positions are transformed to WORLD SPACE via the rest-pose model matrix.
        // Default invMass = 1.0 (free particle).
        std::vector<physx::PxVec4> particles(m_WeldedVertexCount);
        for (int v = 0; v < m_VertexCount; ++v)
        {
            uint32_t wIdx = m_OrigToWelded[v];
            float x = rawPos[v * 8 + 0];
            float y = rawPos[v * 8 + 1];
            float z = rawPos[v * 8 + 2];
            glm::vec4 worldPos = restMat * glm::vec4(x, y, z, 1.0f);
            particles[wIdx] = physx::PxVec4(worldPos.x, worldPos.y, worldPos.z, 1.0f);
        }

        // 将整体粒子沿世界 Y 轴偏移，使固定点对准肩膀位置
        if (m_WorldAttachOffsetY != 0.0f)
        {
            for (auto& p : particles)
                p.y += m_WorldAttachOffsetY;
        }

        // Build welded triangle indices and discard degenerate triangles.
        m_WeldedIndices.clear();
        m_WeldedIndices.reserve(m_Indices.size());
        for (size_t t = 0; t < m_Indices.size() / 3; ++t)
        {
            uint32_t w0 = m_OrigToWelded[m_Indices[t * 3 + 0]];
            uint32_t w1 = m_OrigToWelded[m_Indices[t * 3 + 1]];
            uint32_t w2 = m_OrigToWelded[m_Indices[t * 3 + 2]];
            if (w0 == w1 || w1 == w2 || w0 == w2) continue; // degenerate after weld
            m_WeldedIndices.push_back(w0);
            m_WeldedIndices.push_back(w1);
            m_WeldedIndices.push_back(w2);
        }

        // ── 4. Pin requested particles ────────────────────────────────────────
        // pinnedParticleIndices are in original vertex space; convert to welded.
        m_PinnedIndices.clear();
        m_PinnedLocalPositions.clear();
        std::vector<bool> alreadyPinned(m_WeldedVertexCount, false);
        for (uint32_t pi : props.pinnedParticleIndices)
        {
            if (static_cast<int>(pi) >= m_VertexCount)
            {
                VANS_LOG_WARN("[VansClothNode] pinnedParticle index " << pi << " is out of range, skipping.");
                continue;
            }
            uint32_t wIdx = m_OrigToWelded[pi];
            if (alreadyPinned[wIdx]) continue; // avoid pinning same welded particle twice
            alreadyPinned[wIdx] = true;

            particles[wIdx].w = 0.0f; // invMass = 0  →  pinned

            glm::vec4 worldPos(particles[wIdx].x, particles[wIdx].y, particles[wIdx].z, 1.0f);
            glm::vec3 localPos = glm::vec3(m_RestNodeTransformInv * worldPos);
            m_PinnedIndices.push_back(wIdx);
            m_PinnedLocalPositions.push_back(localPos);
        }

        // ── 5. Build NvCloth mesh descriptor and cook fabric ─────────────────
        nv::cloth::ClothMeshDesc meshDesc;
        meshDesc.points.data   = particles.data();
        meshDesc.points.count  = static_cast<uint32_t>(m_WeldedVertexCount);
        meshDesc.points.stride = sizeof(physx::PxVec4);

        // Provide per-vertex invMass so the cooker knows which particles are static
        // (generates tether constraints correctly).
        std::vector<float> invMasses(m_WeldedVertexCount);
        for (int v = 0; v < m_WeldedVertexCount; ++v)
            invMasses[v] = particles[v].w;
        meshDesc.invMasses.data   = invMasses.data();
        meshDesc.invMasses.count  = static_cast<uint32_t>(m_WeldedVertexCount);
        meshDesc.invMasses.stride = sizeof(float);

        meshDesc.triangles.data   = m_WeldedIndices.data();
        meshDesc.triangles.count  = static_cast<uint32_t>(m_WeldedIndices.size() / 3);
        meshDesc.triangles.stride = 3 * sizeof(uint32_t);

        // Gravity direction for the cooker (tells it which edges are vertical vs horizontal).
        physx::PxVec3 gravityDir(0.0f, props.gravity < 0.0f ? -1.0f : 1.0f, 0.0f);

        nv::cloth::Factory* factory = VansClothSystem::GetInstance().GetFactory();
        if (!factory)
        {
            VANS_LOG_ERROR("[VansClothNode] VansClothSystem not initialized.");
            return;
        }

        m_Fabric = NvClothCookFabricFromMesh(factory, meshDesc, gravityDir,
                                              nullptr, /*useGeodesicTether=*/true);
        if (!m_Fabric)
        {
            VANS_LOG_ERROR("[VansClothNode] Fabric cooking failed for mesh '" << renderNode->m_NodeName << "'.");
            return;
        }

        // ── 6. Create cloth instance ──────────────────────────────────────────
        nv::cloth::Range<const physx::PxVec4> particleRange(
            particles.data(), particles.data() + particles.size());
        m_Cloth = factory->createCloth(particleRange, *m_Fabric);
        if (!m_Cloth)
        {
            VANS_LOG_ERROR("[VansClothNode] createCloth() failed.");
            m_Fabric->decRefCount();
            m_Fabric = nullptr;
            return;
        }

        // ── 7. Configure simulation parameters ───────────────────────────────
        // stiffnessFrequency 决定 damping/stiffness 按每秒多少次归一化。
        // NvCloth 默认值约 10 Hz，在 60fps 子步仿真下会导致阻尼几乎为零：
        //   damp_per_step = (1-damping)^(subDt * stiffnessFrequency)
        //   default: (1-0.08)^(1/240*10) ≈ 0.997 → 每帧仅 1.2% 速度衰减
        // 设为 60 Hz 后：每帧衰减 = (1-damping)^1 = 1-damping（与 profile 数值直觉一致）
        m_Cloth->setStiffnessFrequency(60.0f);

        m_Cloth->setGravity(physx::PxVec3(0.0f, props.gravity, 0.0f));
        m_Cloth->setDamping(physx::PxVec3(props.damping));
        m_Cloth->setFriction(props.friction);

        // 本引擎 Cloth 粒子全部处于世界空间，Cloth frame 永不移动（不调用 setTranslation/setRotation）。
        // 将 linearInertia/angularInertia/centrifugalInertia 置零，防止 frame 意外移动时产生额外冲量。
        m_Cloth->setLinearInertia(physx::PxVec3(0.0f));
        m_Cloth->setAngularInertia(physx::PxVec3(0.0f));
        m_Cloth->setCentrifugalInertia(physx::PxVec3(0.0f));

        // Enable continuous collision detection so particles don't tunnel through
        // collision spheres/capsules between simulation steps.
        m_Cloth->enableContinuousCollision(true);

        // Collision mass scale controls how quickly particle mass increases during
        // collision response.  A positive value (default 0) is required for the
        // solver to push particles out of collision shapes.
        m_Cloth->setCollisionMassScale(1.0f);

        // Apply stiffness to all phases via PhaseConfig.
        uint32_t numPhases = m_Fabric->getNumPhases();
        std::vector<nv::cloth::PhaseConfig> phaseConfigs(numPhases);
        for (uint32_t p = 0; p < numPhases; ++p)
        {
            phaseConfigs[p].mPhaseIndex = static_cast<uint16_t>(p);
            phaseConfigs[p].mStiffness  = props.stiffness;
        }
        m_Cloth->setPhaseConfig(nv::cloth::Range<nv::cloth::PhaseConfig>(
            phaseConfigs.data(), phaseConfigs.data() + numPhases));

        // ── 8. Register cloth with the solver ────────────────────────────────
        nv::cloth::Solver* solver = VansClothSystem::GetInstance().GetSolver();
        if (solver)
            solver->addCloth(m_Cloth);

        // ── 9. Allocate the CPU-side simulation result buffer ─────────────────────
        // Layout: m_VertexStride × uint16_t per vertex
        //   8  = pos xyz + uv xy + nrm xyz
        //   14 = pos xyz + uv xy + nrm xyz + tangent xyz + bitangent xyz
        m_SimulatedVertexData.resize(static_cast<size_t>(m_VertexCount) * m_VertexStride, 0);

        // ── 10. Store collision sphere references for per-frame syncing ───────────
        m_CollisionSphereRefs = props.collisionSphereRefs;

        // ── 11. 存储骨骼跟随数据（V2）────────────────────────────────────────
        m_FollowBones = props.followBones;
        if (m_FollowBones && !props.pinnedSkinData.empty())
        {
            // 仅保留与 m_PinnedIndices 对应的蒙皮数据
            // props.pinnedSkinData 按 profile.m_PinnedLocalPositions 索引排列，
            // 而 m_PinnedIndices 是从中去重后的子集；这里按固定点顺序存入。
            m_PinnedBoneSkinData = props.pinnedSkinData;
            // 若数量不匹配则截断至 m_PinnedIndices.size()，保证安全访问
            if (m_PinnedBoneSkinData.size() > m_PinnedIndices.size())
                m_PinnedBoneSkinData.resize(m_PinnedIndices.size());
        }

        VANS_LOG("[VansClothNode] Initialized cloth '" << renderNode->m_NodeName
                  << "', origVerts=" << m_VertexCount
                  << ", weldedParticles=" << m_WeldedVertexCount
                  << ", weldedTris=" << (m_WeldedIndices.size() / 3)
                  << ", hasTangent=" << m_HasTangent
                  << ", pinnedParticles=" << m_PinnedIndices.size());
    }

    // =========================================================================
    void VansClothNode::Shutdown()
    {
        if (m_Cloth)
        {
            nv::cloth::Solver* solver = VansClothSystem::GetInstance().GetSolver();
            if (solver) solver->removeCloth(m_Cloth);
            NV_CLOTH_DELETE(m_Cloth);
            m_Cloth = nullptr;
        }
        if (m_Fabric)
        {
            m_Fabric->decRefCount();
            m_Fabric = nullptr;
        }

        m_SimulatedVertexData.clear();

        m_PinnedIndices.clear();
        m_PinnedLocalPositions.clear();
        m_PinnedBoneSkinData.clear();
        m_OrigToWelded.clear();
        m_WeldedIndices.clear();
        m_WeldedVertexCount = 0;
        m_TargetRenderNode = nullptr;
        m_AnimNode         = nullptr;
        m_FollowBones      = false;
        m_VertexCount      = 0;
        m_Enabled          = false;
    }

    // =========================================================================
    // V2 延迟骨骼绑定：Pass5（所有 AnimationNode 加载完毕后）由场景调用。
    // 解析骨骼名称→索引映射，填充 m_PinnedBoneSkinData，并注入 AnimNode。
    // =========================================================================
    void VansClothNode::LateBindBonesFromProfile(
        const VansEngine::VansClothProfile&     profile,
        VansGraphics::VansAnimationNode*        animNode)
    {
        if (!animNode)
        {
            VANS_LOG_WARN("[VansClothNode] LateBindBonesFromProfile: animNode 为空，跳过。");
            return;
        }
        if (!m_FollowBones)
        {
            VANS_LOG_WARN("[VansClothNode] LateBindBonesFromProfile: m_FollowBones=false，跳过。");
            return;
        }

        m_AnimNode = animNode;

        const VansGraphics::Skeleton& skel = animNode->GetSkeleton();
        VANS_LOG("[VansClothNode] LateBindBonesFromProfile: AnimNode='" << animNode->GetName()
                 << "'，骨骼数=" << skel.bones.size()
                 << "，骨骼名索引表条目数=" << skel.boneNameToIndex.size()
                 << "，固定点数=" << m_PinnedIndices.size());

        if (skel.bones.empty())
        {
            VANS_LOG_ERROR("[VansClothNode] LateBindBonesFromProfile: Skeleton 为空！"
                           " AnimNode 可能未完成加载或 .vanim 文件不含骨骼。");
        }

        m_PinnedBoneSkinData = profile.ResolveBoneBindings(skel);

        // ── 预计算骨骼局部偏移 ────────────────────────────────────────────────
        // m_PinnedLocalPositions 是 Cape.obj 局部空间坐标，需转换到每根骨骼的局部空间。
        // 使用 GetCachedGlobalTransforms()（模型空间骨骼矩阵，不含 offsetMatrix）+ rootWorld
        // 得到骨骼世界矩阵，再取逆即可将 Cape 顶点世界坐标映射到骨骼局部空间。
        VansAnimationController* ctrl = animNode->GetController();
        if (ctrl && !m_PinnedLocalPositions.empty())
        {
            const std::vector<glm::mat4>& cachedGlobals = ctrl->GetCachedGlobalTransforms();

            // Cape Transform 世界矩阵（用于将 Cape 局部坐标转换为世界坐标）
            glm::mat4 capeWorld = glm::mat4(1.0f);
            if (m_TargetRenderNode)
            {
                capeWorld = VansGraphics::VansTransformStore::GetTransform(
                    m_TargetRenderNode->m_TransformID).GetModelMatrix();
            }

            // 角色根节点世界矩阵
            uint32_t rootID = animNode->GetTransformID();
            glm::mat4 rootWorld = VansGraphics::VansTransformStore::GetTransform(rootID).GetModelMatrix();

            for (size_t i = 0; i < m_PinnedBoneSkinData.size() && i < m_PinnedLocalPositions.size(); ++i)
            {
                auto& skin = m_PinnedBoneSkinData[i];
                // Cape 顶点世界坐标
                glm::vec4 vertexWorld = capeWorld * glm::vec4(m_PinnedLocalPositions[i], 1.0f);

                for (uint32_t b = 0; b < skin.m_BoneCount; ++b)
                {
                    int bIdx = skin.m_BoneWeights[b].m_BoneIndex;
                    if (bIdx < 0 || bIdx >= static_cast<int>(cachedGlobals.size()))
                    {
                        skin.m_BoneWeights[b].m_BoneLocalOffset = glm::vec3(0.0f);
                        continue;
                    }
                    // 骨骼世界矩阵 = rootWorld × cachedGlobals[bIdx]
                    glm::mat4 boneWorld = rootWorld * cachedGlobals[bIdx];
                    // 顶点在骨骼局部空间中的偏移 = inverse(boneWorld) × vertexWorld
                    glm::vec4 localOffset = glm::inverse(boneWorld) * vertexWorld;
                    skin.m_BoneWeights[b].m_BoneLocalOffset = glm::vec3(localOffset);
                }
            }
            VANS_LOG("[VansClothNode] LateBindBonesFromProfile: 骨骼局部偏移已预计算，"
                     << "capeWorld[3]=(" << capeWorld[3].x << "," << capeWorld[3].y << "," << capeWorld[3].z << ")"
                     << " rootWorld[3]=(" << rootWorld[3].x << "," << rootWorld[3].y << "," << rootWorld[3].z << ")");
            if (!m_PinnedBoneSkinData.empty() && m_PinnedBoneSkinData[0].m_BoneCount > 0)
            {
                const auto& s0 = m_PinnedBoneSkinData[0];
                VANS_LOG("[VansClothNode] 固定点[0] 骨骼局部偏移[0]=("
                         << s0.m_BoneWeights[0].m_BoneLocalOffset.x << ","
                         << s0.m_BoneWeights[0].m_BoneLocalOffset.y << ","
                         << s0.m_BoneWeights[0].m_BoneLocalOffset.z << ")");
            }
        }
        else if (!ctrl)
        {
            VANS_LOG_WARN("[VansClothNode] LateBindBonesFromProfile: AnimController 为空，"
                          "无法预计算骨骼局部偏移，固定点将使用 Cape-space 坐标（位置可能不正确）。");
        }

        // 打印每个固定点的骨骼解析结果
        for (size_t i = 0; i < m_PinnedBoneSkinData.size(); ++i)
        {
            const auto& skin = m_PinnedBoneSkinData[i];
            if (skin.m_BoneCount == 0)
            {
                VANS_LOG_WARN("[VansClothNode] 固定点[" << i << "] 无骨骼影响（boneCount=0），将退化为 Transform 模式");
            }
            else
            {
                std::ostringstream oss;
                oss << "[VansClothNode] 固定点[" << i << "] boneCount=" << skin.m_BoneCount << " :";
                for (uint32_t b = 0; b < skin.m_BoneCount; ++b)
                    oss << " [idx=" << skin.m_BoneWeights[b].m_BoneIndex
                        << " w=" << skin.m_BoneWeights[b].m_Weight << "]";
                VANS_LOG(oss.str());
            }
        }

        // 确保蒙皮数据长度与固定点数量一致
        if (m_PinnedBoneSkinData.size() > m_PinnedIndices.size())
            m_PinnedBoneSkinData.resize(m_PinnedIndices.size());

        VANS_LOG("[VansClothNode] LateBindBonesFromProfile 完成：'" << m_Name
                 << "' 有效蒙皮数=" << m_PinnedBoneSkinData.size()
                 << "，固定点数=" << m_PinnedIndices.size());
    }

    // =========================================================================
    // 内部辅助：计算固定点 i 的目标世界坐标（不写入 NvCloth 缓冲区）
    // =========================================================================
    static glm::vec3 ComputePinWorldPos(
        size_t                              i,
        const std::vector<ClothNodePinSkinData>& skinData,
        const std::vector<glm::vec3>&       localPositions,
        float                               attachOffsetY,
        VansGraphics::VansRenderNode*       targetRenderNode,
        VansGraphics::VansAnimationNode*    animNode,
        bool                                followBones)
    {
        // ── 骨骼跟随模式 ──────────────────────────────────────────────────────
        if (followBones && animNode && i < skinData.size() && skinData[i].m_BoneCount > 0)
        {
            VansAnimationController* ctrl = animNode->GetController();
            if (ctrl)
            {
                const std::vector<glm::mat4>& cachedGlobals = ctrl->GetCachedGlobalTransforms();
                uint32_t rootID = animNode->GetTransformID();
                glm::mat4 rootWorld = VansGraphics::VansTransformStore::GetTransform(rootID).GetModelMatrix();

                const ClothNodePinSkinData& skin = skinData[i];
                glm::vec4 worldPos(0.0f);
                for (uint32_t b = 0; b < skin.m_BoneCount; ++b)
                {
                    int   boneIdx = skin.m_BoneWeights[b].m_BoneIndex;
                    float weight  = skin.m_BoneWeights[b].m_Weight;
                    if (boneIdx < 0 || boneIdx >= static_cast<int>(cachedGlobals.size()))
                        continue;
                    glm::mat4 boneWorld = rootWorld * cachedGlobals[boneIdx];
                    worldPos += weight * (boneWorld * glm::vec4(skin.m_BoneWeights[b].m_BoneLocalOffset, 1.0f));
                }
                worldPos.y += attachOffsetY;
                return glm::vec3(worldPos);
            }
        }

        // ── 传统模式：退化为 RenderNode Transform ────────────────────────────
        if (!targetRenderNode) return glm::vec3(0.0f);
        VansGraphics::VansTransform& t =
            VansGraphics::VansTransformStore::GetTransform(targetRenderNode->m_TransformID);
        glm::vec4 wp = t.GetModelMatrix() * glm::vec4(localPositions[i], 1.0f);
        wp.y += attachOffsetY;
        return glm::vec3(wp);
    }

    // =========================================================================
    void VansClothNode::ComputePinnedTargets()
    {
        if (!m_Cloth || m_PinnedIndices.empty() || !m_TargetRenderNode) return;

        const size_t count = m_PinnedIndices.size();
        m_TargetPinnedWorldPos.resize(count);

        for (size_t i = 0; i < count; ++i)
        {
            m_TargetPinnedWorldPos[i] = ComputePinWorldPos(
                i, m_PinnedBoneSkinData, m_PinnedLocalPositions,
                m_WorldAttachOffsetY, m_TargetRenderNode, m_AnimNode, m_FollowBones);
        }

        // 首帧：prev = target（无插值，直接到位）
        if (!m_PinnedPrevInitialized)
        {
            m_PrevPinnedWorldPos = m_TargetPinnedWorldPos;
            m_PinnedPrevInitialized = true;
        }
    }

    // =========================================================================
    void VansClothNode::WritePinnedParticlesLerped(float alpha)
    {
        if (!m_Cloth || m_PinnedIndices.empty()) return;
        if (m_TargetPinnedWorldPos.empty()) return;

        nv::cloth::MappedRange<physx::PxVec4> particles     = m_Cloth->getCurrentParticles();
        nv::cloth::MappedRange<physx::PxVec4> prevParticles = m_Cloth->getPreviousParticles();

        const size_t count = m_PinnedIndices.size();
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t vi = m_PinnedIndices[i];

            // 在上一帧目标与本帧目标之间插值
            const glm::vec3& prev   = m_PrevPinnedWorldPos[i];
            const glm::vec3& target = m_TargetPinnedWorldPos[i];
            glm::vec3 pos = prev + alpha * (target - prev);

            // 同时写 current 和 previous，将隐式速度归零，消除 Verlet 冲量
            particles[vi].x     = pos.x; particles[vi].y     = pos.y; particles[vi].z     = pos.z;
            prevParticles[vi].x = pos.x; prevParticles[vi].y = pos.y; prevParticles[vi].z = pos.z;
        }
    }

    // =========================================================================
    void VansClothNode::CommitPinnedTargets()
    {
        // 将本帧目标存为"上一帧"，供下帧子步插值使用
        if (!m_TargetPinnedWorldPos.empty())
            m_PrevPinnedWorldPos = m_TargetPinnedWorldPos;
    }

    // =========================================================================
    // SyncPinnedParticlesToRenderNode：单步兼容路径（场景已改用子步接口，此函数备用）
    // =========================================================================
    void VansClothNode::SyncPinnedParticlesToRenderNode()
    {
        ComputePinnedTargets();
        WritePinnedParticlesLerped(1.0f);  // alpha=1 = 直接跳到目标，无插值
        CommitPinnedTargets();
    }

    // =========================================================================
    void VansClothNode::SetCollisionSpheres(const std::vector<physx::PxVec4>& worldSpaceSpheres)
    {
        if (!m_Cloth) return;

        // NvCloth particles now live in WORLD SPACE (transformed during
        // Initialize), so collision spheres can be passed directly without
        // any coordinate conversion.
        nv::cloth::Range<const physx::PxVec4> sphereRange(
            worldSpaceSpheres.data(), worldSpaceSpheres.data() + worldSpaceSpheres.size());
        m_Cloth->setSpheres(sphereRange, sphereRange);
    }

    // =========================================================================
    void VansClothNode::WriteSimResults()
    {
        if (!m_Cloth || m_SimulatedVertexData.empty()) return;

        // ── Transform world-space particles back to model space ───────────────
        // NvCloth particles are simulated in world space, but the vertex buffer
        // expects model-space positions (the vertex shader applies the model
        // matrix).  Compute the inverse of the CURRENT model matrix each frame
        // so that animated/moving cloth nodes stay correct.
        glm::mat4 invModel(1.0f);
        if (m_TargetRenderNode)
        {
            VansGraphics::VansTransform& tf =
                VansGraphics::VansTransformStore::GetTransform(m_TargetRenderNode->m_TransformID);
            invModel = glm::inverse(tf.GetModelMatrix());
        }

        // Read welded particle positions from NvCloth (world space).
        nv::cloth::MappedRange<physx::PxVec4> weldedParticles = m_Cloth->getCurrentParticles();

        // Convert world-space particle positions to model space.
        std::vector<glm::vec3> modelPositions(m_VertexCount);
        for (int v = 0; v < m_VertexCount; ++v)
        {
            const physx::PxVec4& wp = weldedParticles[m_OrigToWelded[v]];
            glm::vec4 mp = invModel * glm::vec4(wp.x, wp.y, wp.z, 1.0f);
            modelPositions[v] = glm::vec3(mp);
        }

        // ── Recompute smooth per-vertex normals from model-space positions ────
        // Normals are computed in model space so they match the vertex buffer
        // convention (the shader applies the normal matrix to go to world space).
        std::vector<glm::vec3> normals(m_VertexCount, glm::vec3(0.0f));
        std::vector<glm::vec3> tangents;
        std::vector<glm::vec3> bitangents;
        if (m_HasTangent)
        {
            tangents.assign(m_VertexCount, glm::vec3(0.0f));
            bitangents.assign(m_VertexCount, glm::vec3(0.0f));
        }

        const size_t triCount = m_Indices.size() / 3;
        for (size_t t = 0; t < triCount; ++t)
        {
            uint32_t i0 = m_Indices[t * 3 + 0];
            uint32_t i1 = m_Indices[t * 3 + 1];
            uint32_t i2 = m_Indices[t * 3 + 2];
            glm::vec3 p0 = modelPositions[i0];
            glm::vec3 p1 = modelPositions[i1];
            glm::vec3 p2 = modelPositions[i2];
            glm::vec3 n = glm::cross(p1 - p0, p2 - p0); // un-normalised (area-weighted)
            normals[i0] += n;
            normals[i1] += n;
            normals[i2] += n;

            // ── Tangent / bitangent from UV gradients ─────────────────────
            if (m_HasTangent)
            {
                glm::vec2 uv0(m_RestTexCoords[i0 * 2 + 0], m_RestTexCoords[i0 * 2 + 1]);
                glm::vec2 uv1(m_RestTexCoords[i1 * 2 + 0], m_RestTexCoords[i1 * 2 + 1]);
                glm::vec2 uv2(m_RestTexCoords[i2 * 2 + 0], m_RestTexCoords[i2 * 2 + 1]);

                glm::vec3 edge1 = p1 - p0;
                glm::vec3 edge2 = p2 - p0;
                glm::vec2 dUV1  = uv1 - uv0;
                glm::vec2 dUV2  = uv2 - uv0;

                float det = dUV1.x * dUV2.y - dUV2.x * dUV1.y;
                float f   = (glm::abs(det) > 1e-8f) ? (1.0f / det) : 0.0f;

                glm::vec3 T = f * (dUV2.y * edge1 - dUV1.y * edge2);
                glm::vec3 B = f * (-dUV2.x * edge1 + dUV1.x * edge2);

                tangents[i0]   += T;  tangents[i1]   += T;  tangents[i2]   += T;
                bitangents[i0] += B;  bitangents[i1] += B;  bitangents[i2] += B;
            }
        }

        // ── Pack fp16 vertex data into the CPU buffer ─────────────────────────
        // Positions and normals are in model space; each original vertex keeps
        // its own UV, normal, tangent (per-vertex shading data).
        for (int v = 0; v < m_VertexCount; ++v)
        {
            glm::vec3             p  = modelPositions[v];
            glm::vec3             n  = normals[v];
            float                 nl = glm::length(n);
            if (nl > 1e-6f) n /= nl;

            int base = v * m_VertexStride;
            m_SimulatedVertexData[base + 0] = F16(p.x);
            m_SimulatedVertexData[base + 1] = F16(p.y);
            m_SimulatedVertexData[base + 2] = F16(p.z);
            m_SimulatedVertexData[base + 3] = F16(m_RestTexCoords[v * 2 + 0]);
            m_SimulatedVertexData[base + 4] = F16(m_RestTexCoords[v * 2 + 1]);
            m_SimulatedVertexData[base + 5] = F16(n.x);
            m_SimulatedVertexData[base + 6] = F16(n.y);
            m_SimulatedVertexData[base + 7] = F16(n.z);

            if (m_HasTangent)
            {
                // Gram-Schmidt orthogonalise tangent w.r.t. normal
                glm::vec3 tRaw = tangents[v];
                glm::vec3 tOrt = tRaw - n * glm::dot(n, tRaw);
                float     tl   = glm::length(tOrt);
                if (tl > 1e-6f) tOrt /= tl;

                glm::vec3 bRaw = bitangents[v];
                glm::vec3 bOrt = bRaw - n * glm::dot(n, bRaw) - tOrt * glm::dot(tOrt, bRaw);
                float     bl   = glm::length(bOrt);
                if (bl > 1e-6f) bOrt /= bl;

                m_SimulatedVertexData[base +  8] = F16(tOrt.x);
                m_SimulatedVertexData[base +  9] = F16(tOrt.y);
                m_SimulatedVertexData[base + 10] = F16(tOrt.z);
                m_SimulatedVertexData[base + 11] = F16(bOrt.x);
                m_SimulatedVertexData[base + 12] = F16(bOrt.y);
                m_SimulatedVertexData[base + 13] = F16(bOrt.z);
            }
        }
        // MappedRange destructor unlocks particles automatically.
    }

    // =========================================================================
    // ClothNodeProperties::FromProfile — 从 VansClothProfile 填充属性
    // 使用 profile.ResolveIndices() 在局部空间做近邻匹配，填充 pinnedParticleIndices。
    // skeleton 非空且 profile.m_FollowBones==true 时，同时填充骨骼蒙皮数据。
    // =========================================================================
    ClothNodeProperties ClothNodeProperties::FromProfile(
        const VansClothProfile& profile,
        const std::vector<float>& rawPosFloat4,
        int vertexCount,
        const VansGraphics::Skeleton* skeleton)
    {
        ClothNodeProperties props;
        props.enabled       = true;
        props.stiffness     = profile.m_Stiffness;
        props.damping       = profile.m_Damping;
        props.friction      = profile.m_Friction;
        props.gravity       = profile.m_Gravity;
        props.selfCollision = profile.m_SelfCollision;
        props.followBones   = profile.m_FollowBones;

        // 通过局部坐标近邻匹配解析固定点索引
        props.pinnedParticleIndices = profile.ResolveIndices(rawPosFloat4, vertexCount);

        // V2：若启用骨骼跟随且提供了 Skeleton，解析骨骼绑定数据
        if (profile.m_FollowBones && skeleton != nullptr)
        {
            props.pinnedSkinData = profile.ResolveBoneBindings(*skeleton);
        }

        VANS_LOG("[ClothNodeProperties] FromProfile '" << profile.m_Name
                 << "': 固定点=" << props.pinnedParticleIndices.size()
                 << "，骨骼跟随=" << (props.followBones ? "开启" : "关闭"));
        return props;
    }
}
