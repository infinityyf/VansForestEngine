#pragma once

// ─── Engine core ─────────────────────────────────────────────────────────────
#include "../ScriptCore/VansTransform.h"
#include "../RenderCore/VansRenderNode.h"

// ─── NvCloth core ─────────────────────────────────────────────────────────────
#include <NvCloth/Cloth.h>
#include <NvCloth/Fabric.h>
#include <NvCloth/Factory.h>

// ─── GLM ─────────────────────────────────────────────────────────────────────
#include <GLM/glm.hpp>
#include <GLM/gtc/matrix_transform.hpp>

#include <vector>
#include <string>

namespace VansEngine
{
    // =========================================================================
    // ClothNodeProperties  — data parsed from the scene JSON
    // =========================================================================
    struct ClothNodeProperties
    {
        bool    enabled       = false;
        float   stiffness     = 0.8f;   // stretch stiffness applied to all phases [0,1]
        float   damping       = 0.1f;   // velocity damping magnitude (replicated on all axes)
        float   friction      = 0.0f;   // cloth self-collision friction
        float   gravity       = -9.81f; // world-space gravity Y component
        bool    selfCollision = false;  // (future: enable self-collision handling)

        // Zero-based vertex indices whose particles are pinned.
        // Pinned particles have invMass = 0 and their world position is driven
        // by the bound render node's VansTransform every frame.
        std::vector<uint32_t> pinnedParticleIndices;
    };

    // =========================================================================
    // VansClothNode
    // One instance per cloth mesh in the scene.  Binds to a VansRenderNode:
    //  • Gets the mesh (position data + index data) for fabric cooking.
    //  • Writes simulated vertex positions each frame into a staging buffer.
    //  • The render node's VansTransform drives the positions of pinned particles.
    // =========================================================================
    class VansClothNode
    {
    public:
        VansClothNode();
        ~VansClothNode();

        // ── Lifecycle ─────────────────────────────────────────────────────────
        // renderNode must have its mesh loaded with needCPUData = true so that
        // GetMeshRawPositionData() / GetMeshTriangleIndex() are still populated.
        void Initialize(const ClothNodeProperties&       props,
                        VansGraphics::VansRenderNode* renderNode);

        void Shutdown();

        // ── Per-frame interface (call in order) ───────────────────────────────

        // Step 0: call BEFORE VansClothSystem::SimulateStep().
        // Teleports pinned particles to the render node's current world-space positions.
        void SyncPinnedParticlesToRenderNode();

        // Step 1: call AFTER VansClothSystem::SimulateStep().
        // Reads current particle positions, recomputes smooth normals,
        // and packs the result (fp16) into the CPU-side m_SimulatedVertexData buffer.
        void WriteSimResults();

        // ── CPU data accessors (used by VansScene to upload to GPU) ──────────
        // Returns the packed fp16 vertex data written by WriteSimResults().
        // Layout per vertex (no tangent): [pos.x pos.y pos.z uv.x uv.y nrm.x nrm.y nrm.z] × uint16_t  (8)
        // Layout per vertex (tangent):    [pos.x pos.y pos.z uv.x uv.y nrm.x nrm.y nrm.z tan.x tan.y tan.z bitan.x bitan.y bitan.z] × uint16_t (14)
        const std::vector<uint16_t>& GetSimulatedVertexData() const { return m_SimulatedVertexData; }
        size_t                       GetSimulatedDataByteSize() const
        {
            return m_SimulatedVertexData.size() * sizeof(uint16_t);
        }
        bool HasTangent() const { return m_HasTangent; }
        VansGraphics::VansRenderNode* GetTargetRenderNode() const { return m_TargetRenderNode; }

        // ── Accessors ─────────────────────────────────────────────────────────
        bool               IsEnabled()  const { return m_Enabled; }
        void               SetEnabled(bool v) { m_Enabled = v; }
        const std::string& GetName()    const { return m_Name; }
        void               SetName(const std::string& n) { m_Name = n; }

    private:
        // ── NvCloth objects ───────────────────────────────────────────────────
        nv::cloth::Fabric* m_Fabric = nullptr;
        nv::cloth::Cloth*  m_Cloth  = nullptr;

        // ── CPU simulation result buffer ──────────────────────────────────────
        // Written by WriteSimResults() each frame; read by VansScene to upload to GPU.
        // Layout per vertex: 8 uint16_t (no tangent) or 14 uint16_t (with tangent).
        std::vector<uint16_t> m_SimulatedVertexData;

        // True when the bound mesh was loaded with tangent/bitangent attributes.
        bool m_HasTangent = false;
        // Number of uint16_t per vertex: 8 (no tangent) or 14 (with tangent).
        int  m_VertexStride = 8;

        // ── Bound render node (source of mesh + transform) ────────────────────
        VansGraphics::VansRenderNode* m_TargetRenderNode = nullptr;

        // ── Rest-pose CPU data (kept alive for normal recomputation) ──────────
        // Float3 per vertex: x,y,z world-space rest position.
        // Extracted from VansMesh::GetMeshRawPositionData() (which is float4: x,y,z,pad).
        std::vector<float>    m_RestTexCoords;  // u,v per vertex
        std::vector<uint32_t> m_Indices;        // triangle indices

        // ── Pinned particle anchoring ─────────────────────────────────────────
        // Local-space anchor positions captured once at Initialize(), relative to
        // the render node's rest-pose world transform.
        std::vector<uint32_t>   m_PinnedIndices;
        std::vector<glm::vec3>  m_PinnedLocalPositions;
        glm::mat4               m_RestNodeTransformInv = glm::mat4(1.0f);

        int  m_VertexCount = 0;
        bool m_Enabled     = false;
        std::string m_Name;
    };
}
