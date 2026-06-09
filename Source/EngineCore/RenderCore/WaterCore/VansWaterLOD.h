#pragma once
#include "glm/glm.hpp"
#include <vulkan/vulkan.h>
#include <vector>

// ============================================================
// VansWaterLOD — CDLOD 距离环管理独立类（设计文档 W-02）
//
// 职责：
//   - 生成 CDLOD Patch 列表（CPU 端同心距离环选取）
//   - 构建共用的 M×M 网格顶点/索引缓冲
//   - 视锥剔除
//   - 管理 LOD 距离环参数
//
// 从 VansWaterSystem 拆分，VansWaterSystem 持有 VansWaterLOD* 并委托操作。
// 设计参考：水面渲染模块落地方案 §2.2 / §3.2
// ============================================================

namespace VansGraphics
{
    class VansVKDevice;

    // ── CDLOD Patch 描述 ──────────────────────────────────────
    struct CDLODPatch
    {
        glm::vec2 worldCenter;    // Patch 中心（世界空间 XZ）
        float     worldHalfSize;  // Patch 半边长
        int       lodLevel;       // LOD 级别
    };

    // ── Water GBuffer Push Constant（20B）────────────────────
    struct WaterPatchPushConstant
    {
        glm::vec2 patchWorldOrigin;   // Patch 左下角世界坐标 (x, z)
        float     patchWorldSize;     // Patch 世界空间边长
        int       lodLevel;           // LOD 级别 [0, MAX_LOD_COUNT)
        float     waterLevel;         // 水面 Y 高度（世界空间）
    };

    class VansWaterLOD
    {
    public:
        // CDLOD 参数常量
        static constexpr int   MAX_LOD_COUNT   = 10;
        static constexpr int   WATER_MESH_DIM  = 65;  // W-05: M=65
        static constexpr float MIN_LOD_DIST    = 16.0f;
        static constexpr float BASE_PATCH_SIZE = 16.0f;  // 保持 16m（W-05 方案 B）

        VansWaterLOD()  = default;
        ~VansWaterLOD() = default;

        // ── 生命周期 ─────────────────────────────────────────────
        // 创建顶点/索引缓冲，设置 LOD 距离环参数
        bool Initialize(VansVKDevice* device,
                        int maxLodCount    = MAX_LOD_COUNT,
                        float minLodDist   = MIN_LOD_DIST,
                        int meshDim        = WATER_MESH_DIM,
                        float basePatchSize = BASE_PATCH_SIZE);

        void Shutdown(VkDevice logicDevice);

        // ── 每帧更新 ─────────────────────────────────────────────
        // 根据相机位置重新生成 CDLOD Patch 列表
        void GeneratePatches(const glm::vec3& cameraPos);

        // ── 视锥剔除（设计文档 §3.6.2）───────────────────────────
        // 使用视锥平面剔除不可见 Patch，返回剔除后的 Patch 数量
        uint32_t FrustumCullPatches(const glm::mat4& viewProjMatrix);

        // ── LOD 距离环参数 ───────────────────────────────────────
        void  InitializeLODRanges(float minDistance, int lodLevels, float detailBalance);
        float GetLodRange(int level) const;
        int   GetLodLevelAtDistance(float distance) const;
        void  SetMeshDim(int dim) { m_MeshDim = dim; }
        void  SetBasePatchSize(float size) { m_BasePatchSize = size; }

        // ── 访问 ──────────────────────────────────────────────────
        const std::vector<CDLODPatch>& GetPatches() const { return m_Patches; }
        size_t GetPatchCount() const                    { return m_Patches.size(); }

        // 共用 CDLOD 网格缓冲
        VkBuffer       GetVertexBuffer()  const { return m_VertexBuffer; }
        VkBuffer       GetIndexBuffer()   const { return m_IndexBuffer; }
        uint32_t       GetIndexCount()    const { return m_IndexCount; }
        int            GetMeshDim()       const { return m_MeshDim; }
        float          GetBasePatchSize() const { return m_BasePatchSize; }
        float          GetDetailBalance() const { return m_DetailBalance; }

        // 顶点输入布局
        std::vector<VkVertexInputBindingDescription>&   GetVertexBindings()    { return m_VertexBindings; }
        std::vector<VkVertexInputAttributeDescription>& GetVertexAttributes()   { return m_VertexAttributes; }

    private:
        // ── 网格生成 ─────────────────────────────────────────────
        void BuildPatchMesh(VkDevice logicDevice);

        // ── 原始 Vulkan 缓冲分配 ────────────────────────────────
        bool AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags props,
                            VkBuffer& outBuffer, VkDeviceMemory& outMemory);

        // ── 网格参数 ──────────────────────────────────────────────
        int   m_MeshDim       = WATER_MESH_DIM;
        float m_BasePatchSize = BASE_PATCH_SIZE;

        // ── LOD 距离环参数 ───────────────────────────────────────
        int     m_LodLevels    = MAX_LOD_COUNT;
        float   m_MinLodDist   = MIN_LOD_DIST;
        float   m_DetailBalance = 2.0f;   // 设计文档 §3.3.2

        // ── CDLOD 网格缓冲（所有 Patch 共用一份）────────────────
        VkBuffer       m_VertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_VertexMemory = VK_NULL_HANDLE;
        VkBuffer       m_IndexBuffer  = VK_NULL_HANDLE;
        VkDeviceMemory m_IndexMemory  = VK_NULL_HANDLE;
        uint32_t       m_IndexCount   = 0;

        // 顶点输入布局
        std::vector<VkVertexInputBindingDescription>   m_VertexBindings;
        std::vector<VkVertexInputAttributeDescription> m_VertexAttributes;

        // ── 每帧 CDLOD Patch 列表（CPU 生成）────────────────────
        std::vector<CDLODPatch> m_Patches;
        glm::vec3               m_LastCameraPos = glm::vec3(0.0f);

        // ── 设备引用（仅用于 AllocateBuffer）─────────────────────
        VansVKDevice* m_Device = nullptr;
    };

} // namespace VansGraphics
