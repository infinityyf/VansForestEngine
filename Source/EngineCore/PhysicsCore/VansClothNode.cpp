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

        // UV comes from the fp16 vertex buffer which is not CPU-accessible after upload.
        // We reconstruct UVs from the half-float raw data stored in m_MeshRawData before
        // it was cleared.  Because needCPUData=true only keeps position+index data, we
        // set UVs to (0,0) here; if the render node's raw data is available, a caller may
        // populate m_RestTexCoords before Initialize() or leave it zeroed for a plain cloth.
        m_RestTexCoords.assign(m_VertexCount * 2, 0.0f);

        // Build NvCloth particle array (PxVec4: x,y,z, invMass).
        // Default invMass = 1.0 (free particle).
        std::vector<physx::PxVec4> particles;
        particles.resize(m_VertexCount);
        for (int v = 0; v < m_VertexCount; ++v)
        {
            float x = rawPos[v * 8 + 0];
            float y = rawPos[v * 8 + 1];
            float z = rawPos[v * 8 + 2];
            particles[v] = physx::PxVec4(x, y, z, 1.0f);
        }

        // ── 3. Capture rest-pose transform for pinned particle anchoring ──────
        VansGraphics::VansTransform& restT =
            VansGraphics::VansTransformStore::GetTransform(renderNode->m_TransformID);
        glm::mat4 restMat    = restT.GetModelMatrix();
        m_RestNodeTransformInv = glm::inverse(restMat);

        // ── 4. Pin requested particles ────────────────────────────────────────
        m_PinnedIndices.clear();
        m_PinnedLocalPositions.clear();
        for (uint32_t pi : props.pinnedParticleIndices)
        {
            if (static_cast<int>(pi) >= m_VertexCount)
            {
                VANS_LOG_WARN("[VansClothNode] pinnedParticle index " << pi << " is out of range, skipping.");
                continue;
            }
            particles[pi].w = 0.0f; // invMass = 0  →  pinned

            glm::vec4 worldPos(particles[pi].x, particles[pi].y, particles[pi].z, 1.0f);
            glm::vec3 localPos = glm::vec3(m_RestNodeTransformInv * worldPos);
            m_PinnedIndices.push_back(pi);
            m_PinnedLocalPositions.push_back(localPos);
        }

        // ── 5. Build NvCloth mesh descriptor and cook fabric ─────────────────
        nv::cloth::ClothMeshDesc meshDesc;
        meshDesc.points.data   = particles.data();
        meshDesc.points.count  = static_cast<uint32_t>(m_VertexCount);
        meshDesc.points.stride = sizeof(physx::PxVec4);

        // Provide per-vertex invMass so the cooker knows which particles are static
        // (generates tether constraints correctly).
        std::vector<float> invMasses(m_VertexCount);
        for (int v = 0; v < m_VertexCount; ++v)
            invMasses[v] = particles[v].w;
        meshDesc.invMasses.data   = invMasses.data();
        meshDesc.invMasses.count  = static_cast<uint32_t>(m_VertexCount);
        meshDesc.invMasses.stride = sizeof(float);

        meshDesc.triangles.data   = m_Indices.data();
        meshDesc.triangles.count  = static_cast<uint32_t>(m_Indices.size() / 3);
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
        // Layout: 8 × uint16_t per vertex (pos xyz + uv xy + nrm xyz)
        m_SimulatedVertexData.resize(static_cast<size_t>(m_VertexCount) * 8, 0);

        VANS_LOG("[VansClothNode] Initialized cloth '" << renderNode->m_NodeName
                  << "', vertices=" << m_VertexCount
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
            particles[vi].x = worldPos.x;
            particles[vi].y = worldPos.y;
            particles[vi].z = worldPos.z;
            // invMass stays 0 — NvCloth will not integrate this particle position.
        }
        // MappedRange destructor calls unlockParticles() automatically.
    }

    // =========================================================================
    void VansClothNode::WriteSimResults()
    {
        if (!m_Cloth || m_SimulatedVertexData.empty()) return;

        // Read-only access to current particle positions.
        nv::cloth::MappedRange<physx::PxVec4> particles = m_Cloth->getCurrentParticles();

        // ── Recompute smooth per-vertex normals ──────────────────────────────
        std::vector<glm::vec3> normals(m_VertexCount, glm::vec3(0.0f));
        const size_t triCount = m_Indices.size() / 3;
        for (size_t t = 0; t < triCount; ++t)
        {
            uint32_t i0 = m_Indices[t * 3 + 0];
            uint32_t i1 = m_Indices[t * 3 + 1];
            uint32_t i2 = m_Indices[t * 3 + 2];
            glm::vec3 p0(particles[i0].x, particles[i0].y, particles[i0].z);
            glm::vec3 p1(particles[i1].x, particles[i1].y, particles[i1].z);
            glm::vec3 p2(particles[i2].x, particles[i2].y, particles[i2].z);
            glm::vec3 n = glm::cross(p1 - p0, p2 - p0); // un-normalised (area-weighted)
            normals[i0] += n;
            normals[i1] += n;
            normals[i2] += n;
        }

        // ── Pack fp16 vertex data into the CPU buffer ─────────────────────────
        // Layout: [pos.x pos.y pos.z uv.x uv.y nrm.x nrm.y nrm.z] × uint16_t
        for (int v = 0; v < m_VertexCount; ++v)
        {
            const physx::PxVec4& p  = particles[v];
            glm::vec3             n  = normals[v];
            float                 nl = glm::length(n);
            if (nl > 1e-6f) n /= nl;

            int base = v * 8;
            m_SimulatedVertexData[base + 0] = F16(p.x);
            m_SimulatedVertexData[base + 1] = F16(p.y);
            m_SimulatedVertexData[base + 2] = F16(p.z);
            m_SimulatedVertexData[base + 3] = F16(m_RestTexCoords[v * 2 + 0]);
            m_SimulatedVertexData[base + 4] = F16(m_RestTexCoords[v * 2 + 1]);
            m_SimulatedVertexData[base + 5] = F16(n.x);
            m_SimulatedVertexData[base + 6] = F16(n.y);
            m_SimulatedVertexData[base + 7] = F16(n.z);
        }
        // MappedRange destructor unlocks particles automatically.
    }
}
