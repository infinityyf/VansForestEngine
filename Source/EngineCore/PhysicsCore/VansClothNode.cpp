#include "VansClothNode.h"
#include "VansClothSystem.h"
#include "../RenderCore/VulkanCore/VansMesh.h"
#include "../ScriptCore/VansTransform.h"
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
        m_Cloth->setGravity(physx::PxVec3(0.0f, props.gravity, 0.0f));
        m_Cloth->setDamping(physx::PxVec3(props.damping));
        m_Cloth->setFriction(props.friction);

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
        m_OrigToWelded.clear();
        m_WeldedIndices.clear();
        m_WeldedVertexCount = 0;
        m_TargetRenderNode = nullptr;
        m_VertexCount      = 0;
        m_Enabled          = false;
    }

    // =========================================================================
    void VansClothNode::SyncPinnedParticlesToRenderNode()
    {
        if (!m_Cloth || m_PinnedIndices.empty() || !m_TargetRenderNode) return;

        // Rebuild current world matrix from the render node's live VansTransform.
        VansGraphics::VansTransform& t =
            VansGraphics::VansTransformStore::GetTransform(m_TargetRenderNode->m_TransformID);
        glm::mat4 M = t.GetModelMatrix();

        // NvCloth's getCurrentParticles() returns a MappedRange that locks/unlocks on construction/destruction.
        // Writing to pinned particles (invMass==0) before beginSimulation is the supported pattern.
        nv::cloth::MappedRange<physx::PxVec4> particles = m_Cloth->getCurrentParticles();

        for (size_t i = 0; i < m_PinnedIndices.size(); ++i)
        {
            uint32_t vi = m_PinnedIndices[i];
            glm::vec4 worldPos = M * glm::vec4(m_PinnedLocalPositions[i], 1.0f);
            // 应用世界空间 Y 轴附着偏移，将固定点对准角色肩膀位置
            worldPos.y += m_WorldAttachOffsetY;
            particles[vi].x = worldPos.x;
            particles[vi].y = worldPos.y;
            particles[vi].z = worldPos.z;
            // invMass stays 0 — NvCloth will not integrate this particle position.
        }
        // MappedRange destructor calls unlockParticles() automatically.
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
    // =========================================================================
    ClothNodeProperties ClothNodeProperties::FromProfile(
        const VansClothProfile& profile,
        const std::vector<float>& rawPosFloat4,
        int vertexCount)
    {
        ClothNodeProperties props;
        props.enabled       = true;
        props.stiffness     = profile.m_Stiffness;
        props.damping       = profile.m_Damping;
        props.friction      = profile.m_Friction;
        props.gravity       = profile.m_Gravity;
        props.selfCollision = profile.m_SelfCollision;

        // 通过局部坐标近邻匹配解析固定点索引
        props.pinnedParticleIndices = profile.ResolveIndices(rawPosFloat4, vertexCount);

        VANS_LOG("[ClothNodeProperties] FromProfile '" << profile.m_Name
                 << "': 固定点=" << props.pinnedParticleIndices.size());
        return props;
    }
}
