#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansWaterLOD.h"
#include "../../Util/VansLog.h"
#include "../VulkanCore/VansVKDevice.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace VansGraphics
{

// ============================================================
// AllocateBuffer — 封装 vkCreateBuffer + vkAllocateMemory
// ============================================================
bool VansWaterLOD::AllocateBuffer(
    VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
    VkBuffer& outBuffer, VkDeviceMemory& outMemory)
{
    VkDevice device = m_Device->GetLogicDevice();

    VkBufferCreateInfo ci = {};
    ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size        = size;
    ci.usage       = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &ci, nullptr, &outBuffer) != VK_SUCCESS)
    {
        VANS_LOG_ERROR("[VansWaterLOD] vkCreateBuffer failed");
        return false;
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, outBuffer, &memReq);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_Device->GetPhysicalDevice(), &memProps);
    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    {
        bool typeBit  = (memReq.memoryTypeBits >> i) & 1u;
        bool propsFit = (memProps.memoryTypes[i].propertyFlags & props) == props;
        if (typeBit && propsFit)
        {
            memTypeIndex = i;
            break;
        }
    }
    if (memTypeIndex == UINT32_MAX)
    {
        VANS_LOG_ERROR("[VansWaterLOD] no suitable memory type");
        vkDestroyBuffer(device, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo ai = {};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = memReq.size;
    ai.memoryTypeIndex = memTypeIndex;
    if (vkAllocateMemory(device, &ai, nullptr, &outMemory) != VK_SUCCESS)
    {
        VANS_LOG_ERROR("[VansWaterLOD] vkAllocateMemory failed");
        vkDestroyBuffer(device, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(device, outBuffer, outMemory, 0);
    return true;
}

// ============================================================
// BuildPatchMesh — 生成 M×M CDLOD 网格（所有 Patch 共用一份）
//
// 顶点格式：vec2（meshX, meshZ）∈ [0, 1]²
// 索引格式：三角形列表（两个三角形 per quad）
// ============================================================
void VansWaterLOD::BuildPatchMesh(VkDevice logicDevice)
{
    const int   M      = m_MeshDim;
    const float step   = 1.0f / static_cast<float>(M - 1);
    // vk:: no destructor wrapper needed — direct Vulkan API

    // ── 生成顶点数据 ──────────────────────────────────────────
    std::vector<float> vertices;
    vertices.reserve(static_cast<size_t>(M * M * 2));
    for (int z = 0; z < M; ++z)
    {
        for (int x = 0; x < M; ++x)
        {
            vertices.push_back(static_cast<float>(x) * step);  // meshX ∈ [0, 1]
            vertices.push_back(static_cast<float>(z) * step);  // meshZ ∈ [0, 1]
        }
    }

    // ── 生成索引数据（三角形列表）────────────────────────────
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>((M - 1) * (M - 1) * 6));
    for (int z = 0; z < M - 1; ++z)
    {
        for (int x = 0; x < M - 1; ++x)
        {
            uint32_t tl = static_cast<uint32_t>(z * M + x);
            uint32_t tr = tl + 1;
            uint32_t bl = static_cast<uint32_t>((z + 1) * M + x);
            uint32_t br = bl + 1;
            // 三角形 1：tl, bl, tr
            indices.push_back(tl);  indices.push_back(bl);  indices.push_back(tr);
            // 三角形 2：tr, bl, br
            indices.push_back(tr);  indices.push_back(bl);  indices.push_back(br);
        }
    }
    m_IndexCount = static_cast<uint32_t>(indices.size());

    // ── 顶点缓冲（HOST_VISIBLE 直接映射）────────────────────
    VkDeviceSize vbSize = static_cast<VkDeviceSize>(vertices.size() * sizeof(float));
    AllocateBuffer(vbSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_VertexBuffer, m_VertexMemory);
    {
        void* data = nullptr;
        vkMapMemory(logicDevice, m_VertexMemory, 0, vbSize, 0, &data);
        std::memcpy(data, vertices.data(), static_cast<size_t>(vbSize));
        vkUnmapMemory(logicDevice, m_VertexMemory);
    }

    // ── 索引缓冲 ──────────────────────────────────────────────
    VkDeviceSize ibSize = static_cast<VkDeviceSize>(indices.size() * sizeof(uint32_t));
    AllocateBuffer(ibSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_IndexBuffer, m_IndexMemory);
    {
        void* data = nullptr;
        vkMapMemory(logicDevice, m_IndexMemory, 0, ibSize, 0, &data);
        std::memcpy(data, indices.data(), static_cast<size_t>(ibSize));
        vkUnmapMemory(logicDevice, m_IndexMemory);
    }

    // ── 顶点输入布局（绑定 0：per-vertex，stride=8B）────────
    m_VertexBindings = { { 0, sizeof(float) * 2, VK_VERTEX_INPUT_RATE_VERTEX } };
    m_VertexAttributes = {
        { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 }   // location=0: vec2 inMeshPos
    };

    VANS_LOG("[VansWaterLOD] BuildPatchMesh: M=" << M
             << " vertices=" << M * M
             << " indices="  << m_IndexCount
             << " basePatchSize=" << m_BasePatchSize);
}

// ============================================================
// Initialize
// ============================================================
bool VansWaterLOD::Initialize(VansVKDevice* device,
                               int maxLodCount,
                               float minLodDist,
                               int meshDim,
                               float basePatchSize)
{
    if (device == nullptr)
    {
        VANS_LOG_ERROR("[VansWaterLOD] Initialize: device is null");
        return false;
    }

    m_Device         = device;
    m_LodLevels      = maxLodCount;
    m_MinLodDist     = minLodDist;
    m_MeshDim        = meshDim;
    m_BasePatchSize  = basePatchSize;

    VkDevice logicDevice = device->GetLogicDevice();
    BuildPatchMesh(logicDevice);

    VANS_LOG("[VansWaterLOD] Initialize: lodLevels=" << m_LodLevels
             << " minLodDist=" << m_MinLodDist
             << " meshDim=" << m_MeshDim
             << " basePatchSize=" << m_BasePatchSize);
    return true;
}

// ============================================================
// Shutdown
// ============================================================
void VansWaterLOD::Shutdown(VkDevice logicDevice)
{
    if (m_VertexBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(logicDevice, m_VertexBuffer, nullptr); m_VertexBuffer = VK_NULL_HANDLE; }
    if (m_VertexMemory  != VK_NULL_HANDLE) { vkFreeMemory(logicDevice, m_VertexMemory, nullptr);  m_VertexMemory  = VK_NULL_HANDLE; }
    if (m_IndexBuffer   != VK_NULL_HANDLE) { vkDestroyBuffer(logicDevice, m_IndexBuffer, nullptr);  m_IndexBuffer   = VK_NULL_HANDLE; }
    if (m_IndexMemory   != VK_NULL_HANDLE) { vkFreeMemory(logicDevice, m_IndexMemory, nullptr);   m_IndexMemory   = VK_NULL_HANDLE; }
    m_IndexCount = 0;
    m_Patches.clear();
    m_Device = nullptr;
    VANS_LOG("[VansWaterLOD] Shutdown");
}

// ============================================================
// InitializeLODRanges — 设计文档 §3.3.2
//
// LOD i 的外边界距离 = minDistance × (detailBalance ^ i)
// ============================================================
void VansWaterLOD::InitializeLODRanges(float minDistance, int lodLevels, float detailBalance)
{
    m_MinLodDist    = minDistance;
    m_LodLevels     = lodLevels;
    m_DetailBalance = detailBalance;
}

float VansWaterLOD::GetLodRange(int level) const
{
    return m_MinLodDist * std::powf(m_DetailBalance, static_cast<float>(level));
}

int VansWaterLOD::GetLodLevelAtDistance(float distance) const
{
    for (int i = 0; i < m_LodLevels; ++i)
    {
        if (distance < GetLodRange(i))
            return i;
    }
    return m_LodLevels - 1;
}

// ============================================================
// GeneratePatches — 每帧 CPU 端同心距离环 Patch 选取
// 设计文档 §3.3.1
//
// CDLOD 回字形环形布局：
//   LOD 0 覆盖 [0, LodRange(0)]   — 最内圈，最高精度
//   LOD i 覆盖 [LodRange(i-1), LodRange(i)] — 第 i 圈
//
// 每个 LOD 的 Patch 按相机吸附对齐后，只保留与目标距离环相交的 Patch。
// 内圈使用 maxDist（最远顶点距离）剔除——仅当整个 patch 完全在 fine LOD
//   覆盖区域内时才剔除，避免 patch 一侧靠近相机而另一侧无法被 fine LOD 覆盖
//   时形成大面积空洞。
// 外圈使用 minDist > outerRange 剔除完全超出 Morph Zone 的 Patch。
// 相邻 LOD 仅在 Morph Zone 外边界有少量重叠（用于顶点 morph 过渡），
// 不再出现所有 LOD 在相机中心堆叠的问题。
// ============================================================
void VansWaterLOD::GeneratePatches(const glm::vec3& cameraPos)
{
    m_Patches.clear();
    m_LastCameraPos = cameraPos;

    for (int lod = 0; lod < m_LodLevels; ++lod)
    {
        float ps = m_BasePatchSize * std::powf(m_DetailBalance, static_cast<float>(lod));
        float hs = ps * 0.5f;

        // ── 相机吸附到当前 LOD 网格 ──────────────────────────
        float sx = std::floorf(cameraPos.x / ps) * ps;
        float sz = std::floorf(cameraPos.z / ps) * ps;

        // ── 当前 LOD 的距离环 [innerRange, outerRange] ──────
        float innerRange = (lod > 0) ? GetLodRange(lod - 1) : 0.0f;
        float outerRange = GetLodRange(lod);

        // ── 网格范围：覆盖到 outerRange + patch 半对角线 + 相机偏移 ─
        float patchDiagonal = hs * 1.41421356f;
        int   maxGrid = static_cast<int>(std::ceilf((outerRange + patchDiagonal + ps) / ps));
        maxGrid = std::min(maxGrid, 4);

        for (int gz = -maxGrid; gz <= maxGrid; ++gz)
        {
            for (int gx = -maxGrid; gx <= maxGrid; ++gx)
            {
                // Patch 世界中心
                float cx = sx + static_cast<float>(gx) * ps + hs;
                float cz = sz + static_cast<float>(gz) * ps + hs;

                // ── 精确最近/最远距离（per-axis clamp）──────
                float closestX = (cameraPos.x < cx - hs) ? (cx - hs) :
                                 (cameraPos.x > cx + hs) ? (cx + hs) : cameraPos.x;
                float closestZ = (cameraPos.z < cz - hs) ? (cz - hs) :
                                 (cameraPos.z > cz + hs) ? (cz + hs) : cameraPos.z;
                float dxc = closestX - cameraPos.x;
                float dzc = closestZ - cameraPos.z;
                float minDist = std::sqrtf(dxc * dxc + dzc * dzc);

                float farthestX = (cameraPos.x - cx > 0.0f) ? (cx - hs) : (cx + hs);
                float farthestZ = (cameraPos.z - cz > 0.0f) ? (cz - hs) : (cz + hs);
                float dxf = farthestX - cameraPos.x;
                float dzf = farthestZ - cameraPos.z;
                float maxDist = std::sqrtf(dxf * dxf + dzf * dzf);

                // ── 内圈剔除（CDLOD 标准）──────────────────
                // 仅当整个 patch 完全在 fine LOD 覆盖区域内时才剔除。
                // 使用 maxDist（最远顶点距离）而非 minDist（最近顶点距离），
                // 避免一侧靠近相机而另一侧在 fine LOD 覆盖之外的 patch 被错误剔除。
                if (lod > 0 && maxDist < innerRange)
                    continue;

                // ── 外圈剔除 ───────────────────────────────
                // 如果最近顶点已超出 outerRange，整个 Patch 恒为 morph=1，无贡献。
                if (minDist > outerRange)
                    continue;

                m_Patches.push_back({ glm::vec2(cx, cz), hs, lod });
            }
        }
    }
}

// ============================================================
// FrustumCullPatches — 设计文档 §3.6.2
//
// 使用视锥平面剔除不可见 Patch，返回剔除后的 Patch 数量。
// 视锥由 viewProjMatrix 定义（6 个平面在 CPU 提取）。
// 每个 Patch 使用包围球测试（球心 = worldCenter，半径 = halfSize × √2）。
// ============================================================
uint32_t VansWaterLOD::FrustumCullPatches(const glm::mat4& viewProjMatrix)
{
    if (m_Patches.empty())
        return 0;

    // 提取 6 个视锥平面 — Gribb/Hartmann 方法
    // GLM 使用列主序存储，mat[i] 返回第 i 列。
    // 行 i = (mat[0][i], mat[1][i], mat[2][i], mat[3][i])
    glm::vec4 row0(viewProjMatrix[0][0], viewProjMatrix[1][0], viewProjMatrix[2][0], viewProjMatrix[3][0]);
    glm::vec4 row1(viewProjMatrix[0][1], viewProjMatrix[1][1], viewProjMatrix[2][1], viewProjMatrix[3][1]);
    glm::vec4 row2(viewProjMatrix[0][2], viewProjMatrix[1][2], viewProjMatrix[2][2], viewProjMatrix[3][2]);
    glm::vec4 row3(viewProjMatrix[0][3], viewProjMatrix[1][3], viewProjMatrix[2][3], viewProjMatrix[3][3]);

    struct Plane { glm::vec3 normal; float d; };
    Plane planes[6];

    // Left:   row3 + row0
    planes[0].normal = glm::vec3(row3 + row0);
    planes[0].d      = row3.w + row0.w;
    // Right:  row3 - row0
    planes[1].normal = glm::vec3(row3 - row0);
    planes[1].d      = row3.w - row0.w;
    // Bottom: row3 + row1
    planes[2].normal = glm::vec3(row3 + row1);
    planes[2].d      = row3.w + row1.w;
    // Top:    row3 - row1
    planes[3].normal = glm::vec3(row3 - row1);
    planes[3].d      = row3.w - row1.w;
    // Near:   row3 + row2
    planes[4].normal = glm::vec3(row3 + row2);
    planes[4].d      = row3.w + row2.w;
    // Far:    row3 - row2
    planes[5].normal = glm::vec3(row3 - row2);
    planes[5].d      = row3.w - row2.w;

    // 归一化所有平面
    for (int i = 0; i < 6; ++i)
    {
        float len = glm::length(planes[i].normal);
        if (len > 0.0001f)
        {
            planes[i].normal /= len;
            planes[i].d      /= len;
        }
    }

    // 逐 Patch 剔除
    auto it = std::remove_if(m_Patches.begin(), m_Patches.end(),
        [&](const CDLODPatch& patch)
        {
            // 包围球：使用 XZ 中心 + Y=0 作为球心，halfSize×√2 作为半径
            glm::vec3 sphereCenter(patch.worldCenter.x, 0.0f, patch.worldCenter.y);
            float radius = patch.worldHalfSize * 1.41421356f;  // √2

            for (int p = 0; p < 6; ++p)
            {
                float dist = glm::dot(planes[p].normal, sphereCenter) + planes[p].d;
                if (dist < -radius)
                    return true;  // 完全在平面外侧，剔除
            }
            return false;
        });

    uint32_t removed = static_cast<uint32_t>(m_Patches.end() - it);
    m_Patches.erase(it, m_Patches.end());

    return static_cast<uint32_t>(m_Patches.size());
}

} // namespace VansGraphics
