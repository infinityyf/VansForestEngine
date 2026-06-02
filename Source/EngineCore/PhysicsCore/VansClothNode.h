#pragma once

// ─── Engine core ─────────────────────────────────────────────────────────────
#include "../ScriptCore/VansTransform.h"
#include "../RenderCore/VansRenderNode.h"
#include "VansClothProfile.h"

// ─── NvCloth core ─────────────────────────────────────────────────────────────
#include <NvCloth/Cloth.h>
#include <NvCloth/Fabric.h>
#include <NvCloth/Factory.h>

// ─── GLM ─────────────────────────────────────────────────────────────────────
#include <GLM/glm.hpp>
#include <GLM/gtc/matrix_transform.hpp>

#include <vector>
#include <string>
#include <cstdint>

// 前向声明，避免循环包含
namespace VansGraphics { class VansAnimationNode; struct Skeleton; }

namespace VansEngine
{
    // =========================================================================
    // ClothNodePinSkinData  — 单个固定点的骨骼蒙皮数据
    // 由 VansClothProfile::ResolveBoneBindings() 生成，运行时存入 VansClothNode
    // =========================================================================
    static constexpr uint32_t MAX_CLOTH_PIN_BONE_INFLUENCE = 4;

    struct ClothNodePinSkinData
    {
        struct BoneWeight
        {
            int       m_BoneIndex   = -1;
            float     m_Weight      = 1.0f;
            // 固定点在该骨骼局部空间中的偏移（bind-pose 时预计算）
            // 运行时：worldPos += weight × boneWorldMatrix × vec4(boneLocalOffset, 1)
            glm::vec3 m_BoneLocalOffset{ 0.0f };
        };
        BoneWeight m_BoneWeights[MAX_CLOTH_PIN_BONE_INFLUENCE];
        uint32_t   m_BoneCount = 0;  // 0 = 回退到 RenderNode Transform 模式
    };

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

        // 世界空间 Y 轴附着偏移（单位：米）。
        // 正值向上、负值向下，用于微调固定点在世界空间中的高度而无需修改模型。
        // 在 Initialize 和 SyncPinnedParticlesToRenderNode 中均会应用。
        float attachOffsetY = 0.0f;

        // Collision spheres that the cloth should collide against.
        // 位置来源按优先级：renderNodeName → transformID → sceneObjectName（延迟解析）
        struct CollisionSphereRef
        {
            std::string sceneObjectName;             // 原始 objectRef 名称，用于运行时延迟解析
            std::string renderNodeName;              // 已解析：render node 名（优先路径）
            uint32_t    transformID = UINT32_MAX;    // 已解析：直接 TransformStore ID（骨骼绑定体）
            float       radius = 1.0f;               // world-space radius of the collision sphere
        };
        std::vector<CollisionSphereRef> collisionSphereRefs;

        // ── V2：骨骼跟随 ────────────────────────────────────────────────────
        // 启用后固定点通过骨骼蒙皮计算世界坐标，而非整体 RenderNode Transform
        bool followBones = false;

        // 每个固定点的蒙皮数据，与 pinnedParticleIndices 平行索引。
        // 由 FromProfile() 调用 profile.ResolveBoneBindings() 填充。
        std::vector<ClothNodePinSkinData> pinnedSkinData;

        // 从 VansClothProfile 填充属性。
        // pinnedParticleIndices 由 profile.ResolveIndices() 在局部空间近邻匹配填充。
        // rawPosFloat4 来自已挂载 RenderNode 的 VansMesh::GetMeshRawPositionData()（float4 布局）。
        // skeleton 为可选参数；非空且 profile.m_FollowBones==true 时自动解析骨骼绑定数据。
        static ClothNodeProperties FromProfile(
            const VansClothProfile& profile,
            const std::vector<float>& rawPosFloat4,
            int vertexCount,
            const VansGraphics::Skeleton* skeleton = nullptr);
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

        // ── Collision sphere interface ──────────────────────────────────────
        // Called by VansScene each frame BEFORE SimulateStep() to update
        // NvCloth collision spheres from render node world positions.
        // Each PxVec4 is {x, y, z, radius}.
        void SetCollisionSpheres(const std::vector<physx::PxVec4>& spheres);

        // Returns the collision sphere references parsed from JSON,
        // so the scene can resolve render node positions each frame.
        // 非 const 版本供运行时延迟缓存 transformID 使用。
        const std::vector<ClothNodeProperties::CollisionSphereRef>& GetCollisionSphereRefs() const
        { return m_CollisionSphereRefs; }
        std::vector<ClothNodeProperties::CollisionSphereRef>& GetCollisionSphereRefs()
        { return m_CollisionSphereRefs; }

        // ── V2：骨骼跟随接口 ─────────────────────────────────────────────────
        // 由 VansScene::LoadSingleClothNode() 在创建后立即注入。
        // 必须在调用 SyncPinnedParticlesToRenderNode() 之前设置。
        void SetAnimationNode(VansGraphics::VansAnimationNode* animNode)
        {
            m_AnimNode = animNode;
        }

        VansGraphics::VansAnimationNode* GetAnimationNode() const { return m_AnimNode; }

        // V2 延迟骨骼绑定：Pass5（所有 AnimationNode 加载完毕后）由场景调用。
        // 解析骨骼名称→索引映射，填充 m_PinnedBoneSkinData，并注入 AnimNode。
        // 必须在首帧 SyncPinnedParticlesToRenderNode() 之前调用。
        void LateBindBonesFromProfile(const VansClothProfile& profile,
                                      VansGraphics::VansAnimationNode* animNode);

        bool IsFollowBones() const { return m_FollowBones; }

        // ── 子步仿真接口（供 VansScene::UpdateClothSimulation 使用）────────────
        // 第一步：计算本帧所有固定点的目标世界坐标，存入 m_TargetPinnedWorldPos。
        // 不写入 NvCloth 粒子缓冲区。
        void ComputePinnedTargets();

        // 第二步：按子步进度 alpha（0→1）在上一帧目标与本帧目标之间线性插值，
        // 同时写入 getCurrentParticles 和 getPreviousParticles，消除隐式速度。
        void WritePinnedParticlesLerped(float alpha);

        // 第三步：仿真完成后提交本帧目标为"上一帧"，供下帧插值使用。
        void CommitPinnedTargets();

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
        std::vector<float>    m_RestTexCoords;  // u,v per vertex (original space)
        std::vector<uint32_t> m_Indices;        // triangle indices (original vertex space)

        // ── Vertex welding (Assimp duplicates vertices at UV/normal seams, but ──
        // NvCloth needs shared vertices to create constraints across triangles).
        // m_OrigToWelded maps each original vertex to a unique welded particle.
        std::vector<uint32_t> m_OrigToWelded;   // [originalVertexIdx] → weldedParticleIdx
        int m_WeldedVertexCount = 0;            // number of unique NvCloth particles
        std::vector<uint32_t> m_WeldedIndices;  // triangle indices remapped to welded space

        // ── Pinned particle anchoring ─────────────────────────────────────────
        // Local-space anchor positions captured once at Initialize(), relative to
        // the render node's rest-pose world transform.
        std::vector<uint32_t>   m_PinnedIndices;
        std::vector<glm::vec3>  m_PinnedLocalPositions;
        glm::mat4               m_RestNodeTransformInv = glm::mat4(1.0f);

        // ── 世界空间固定点 Y 偏移 ─────────────────────────────────────────────
        // 由 ClothNodeProperties::attachOffsetY 初始化，运行时在 SyncPinnedParticlesToRenderNode
        // 和 Initialize 阶段均会将所有粒子向 Y 方向偏移，将披风附着点对准肩膀位置。
        float m_WorldAttachOffsetY = 0.0f;

        // ── Collision sphere references (populated by scene loader) ───────────
        // Stored so the scene can look up render nodes and build PxVec4 arrays
        // each frame before calling SetCollisionSpheres().
        std::vector<ClothNodeProperties::CollisionSphereRef> m_CollisionSphereRefs;

        // ── V2：骨骼跟随数据 ──────────────────────────────────────────────────
        // 是否启用骨骼跟随模式（由 ClothNodeProperties::followBones 初始化）
        bool m_FollowBones = false;

        // 每个固定点的骨骼蒙皮数据（与 m_PinnedIndices 平行索引）。
        // m_BoneCount == 0 的条目退化为 Transform 模式。
        std::vector<ClothNodePinSkinData> m_PinnedBoneSkinData;

        // 关联的 AnimationNode（外部注入，不拥有生命周期）。
        // 骨骼跟随模式下，每帧从其 Controller::GetCachedGlobalTransforms() 读取骨骼矩阵。
        VansGraphics::VansAnimationNode* m_AnimNode = nullptr;

        // ── 子步插值缓冲 ──────────────────────────────────────────────────────
        // m_TargetPinnedWorldPos：本帧骨骼/Transform 计算出的目标世界坐标
        // m_PrevPinnedWorldPos ：上一帧提交后的目标世界坐标（首帧与 Target 相同）
        std::vector<glm::vec3> m_TargetPinnedWorldPos;
        std::vector<glm::vec3> m_PrevPinnedWorldPos;
        bool m_PinnedPrevInitialized = false; // false = 首帧，直接跳到目标位置

        int  m_VertexCount = 0;
        bool m_Enabled     = false;
        std::string m_Name;
    };
}
