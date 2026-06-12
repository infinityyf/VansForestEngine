#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansWaterLOD.h"
#include "../../Util/VansLog.h"
#include "../VulkanCore/VansVKDevice.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace VansGraphics
{

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
        const bool typeBit = (memReq.memoryTypeBits >> i) & 1u;
        const bool propsFit = (memProps.memoryTypes[i].propertyFlags & props) == props;
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

void VansWaterLOD::BuildPatchMesh(VkDevice logicDevice)
{
    const int M = m_MeshDim;
    const float step = 1.0f / static_cast<float>(M - 1);

    std::vector<float> vertices;
    vertices.reserve(static_cast<size_t>(M * M * 2));
    for (int z = 0; z < M; ++z)
    {
        for (int x = 0; x < M; ++x)
        {
            vertices.push_back(static_cast<float>(x) * step);
            vertices.push_back(static_cast<float>(z) * step);
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>((M - 1) * (M - 1) * 6));
    for (int z = 0; z < M - 1; ++z)
    {
        for (int x = 0; x < M - 1; ++x)
        {
            const uint32_t tl = static_cast<uint32_t>(z * M + x);
            const uint32_t tr = tl + 1;
            const uint32_t bl = static_cast<uint32_t>((z + 1) * M + x);
            const uint32_t br = bl + 1;
            indices.push_back(tl); indices.push_back(bl); indices.push_back(tr);
            indices.push_back(tr); indices.push_back(bl); indices.push_back(br);
        }
    }
    m_IndexCount = static_cast<uint32_t>(indices.size());

    const VkDeviceSize vbSize = static_cast<VkDeviceSize>(vertices.size() * sizeof(float));
    if (AllocateBuffer(vbSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_VertexBuffer, m_VertexMemory))
    {
        void* data = nullptr;
        vkMapMemory(logicDevice, m_VertexMemory, 0, vbSize, 0, &data);
        std::memcpy(data, vertices.data(), static_cast<size_t>(vbSize));
        vkUnmapMemory(logicDevice, m_VertexMemory);
    }

    const VkDeviceSize ibSize = static_cast<VkDeviceSize>(indices.size() * sizeof(uint32_t));
    if (AllocateBuffer(ibSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_IndexBuffer, m_IndexMemory))
    {
        void* data = nullptr;
        vkMapMemory(logicDevice, m_IndexMemory, 0, ibSize, 0, &data);
        std::memcpy(data, indices.data(), static_cast<size_t>(ibSize));
        vkUnmapMemory(logicDevice, m_IndexMemory);
    }

    m_VertexBindings = { { 0, sizeof(float) * 2, VK_VERTEX_INPUT_RATE_VERTEX } };
    m_VertexAttributes = { { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 } };

    VANS_LOG("[VansWaterLOD] BuildPatchMesh: M=" << M
        << " vertices=" << M * M
        << " indices=" << m_IndexCount
        << " basePatchSize=" << m_BasePatchSize);
}

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

    m_Device = device;
    m_MinLodDist = minLodDist;

    VansWaterLODConfig config;
    config.m_MaxLOD = maxLodCount;
    config.m_MeshDim = meshDim;
    config.m_BasePatchSize = basePatchSize;
    SetLodConfig(config);

    BuildPatchMesh(device->GetLogicDevice());

    VANS_LOG("[VansWaterLOD] Initialize: lodLevels=" << m_LodLevels
        << " meshDim=" << m_MeshDim
        << " basePatchSize=" << m_BasePatchSize);
    return true;
}

void VansWaterLOD::Shutdown(VkDevice logicDevice)
{
    if (m_VertexBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(logicDevice, m_VertexBuffer, nullptr); m_VertexBuffer = VK_NULL_HANDLE; }
    if (m_VertexMemory != VK_NULL_HANDLE) { vkFreeMemory(logicDevice, m_VertexMemory, nullptr); m_VertexMemory = VK_NULL_HANDLE; }
    if (m_IndexBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(logicDevice, m_IndexBuffer, nullptr); m_IndexBuffer = VK_NULL_HANDLE; }
    if (m_IndexMemory != VK_NULL_HANDLE) { vkFreeMemory(logicDevice, m_IndexMemory, nullptr); m_IndexMemory = VK_NULL_HANDLE; }
    m_IndexCount = 0;
    m_Patches.clear();
    m_Device = nullptr;
    VANS_LOG("[VansWaterLOD] Shutdown");
}

void VansWaterLOD::SetLodConfig(const VansWaterLODConfig& config)
{
    const int previousMeshDim = m_MeshDim;
    m_LodLevels = std::clamp(config.m_MaxLOD, 1, MAX_LOD_COUNT);
    m_BasePatchSize = std::max(config.m_BasePatchSize, 0.001f);
    m_MeshDim = std::max(config.m_MeshDim, 3);
    if (((m_MeshDim - 1) % 2) != 0)
        ++m_MeshDim;

    if (std::abs(config.m_DetailBalance - 2.0f) > 0.001f)
        VANS_LOG_WARN("[VansWaterLOD] Ring CDLOD requires 2:1 detail balance; forcing detailBalance=2.0");
    m_DetailBalance = 2.0f;
    m_MorphWidthRatio = std::clamp(config.m_MorphWidthRatio, 0.001f, 1.0f);

    if (m_Device != nullptr && m_VertexBuffer != VK_NULL_HANDLE && m_MeshDim != previousMeshDim)
    {
        const VkDevice logicDevice = m_Device->GetLogicDevice();
        vkDestroyBuffer(logicDevice, m_VertexBuffer, nullptr);
        vkFreeMemory(logicDevice, m_VertexMemory, nullptr);
        vkDestroyBuffer(logicDevice, m_IndexBuffer, nullptr);
        vkFreeMemory(logicDevice, m_IndexMemory, nullptr);
        m_VertexBuffer = VK_NULL_HANDLE;
        m_VertexMemory = VK_NULL_HANDLE;
        m_IndexBuffer = VK_NULL_HANDLE;
        m_IndexMemory = VK_NULL_HANDLE;
        m_IndexCount = 0;
        BuildPatchMesh(logicDevice);
    }
}

float VansWaterLOD::GetPatchSize(int lod) const
{
    return m_BasePatchSize * std::powf(m_DetailBalance, static_cast<float>(lod));
}

uint32_t VansWaterLOD::ComputeOuterEdgeMask(int ix, int iz)
{
    uint32_t mask = EdgeNone;
    if (ix == 0) mask |= EdgeLeft;
    if (ix == 3) mask |= EdgeRight;
    if (iz == 0) mask |= EdgeDown;
    if (iz == 3) mask |= EdgeUp;
    return mask;
}

uint32_t VansWaterLOD::ComputeInnerEdgeMask(int lod, int ix, int iz)
{
    if (lod == 0)
        return EdgeNone;

    uint32_t mask = EdgeNone;
    if (ix == 0 && (iz == 1 || iz == 2)) mask |= EdgeRight;
    if (ix == 3 && (iz == 1 || iz == 2)) mask |= EdgeLeft;
    if (iz == 0 && (ix == 1 || ix == 2)) mask |= EdgeUp;
    if (iz == 3 && (ix == 1 || ix == 2)) mask |= EdgeDown;
    return mask;
}

void VansWaterLOD::GeneratePatches(const glm::vec3& cameraPos)
{
    m_Patches.clear();
    m_LastCameraPos = cameraPos;

    struct RingBounds
    {
        glm::vec2 min = glm::vec2(0.0f);
        glm::vec2 max = glm::vec2(0.0f);
    };

    const int lodCount = std::clamp(m_LodLevels, 1, MAX_LOD_COUNT);
    m_Patches.reserve(static_cast<size_t>(16 + 12 * std::max(lodCount - 1, 0)));

    const float ps0 = GetPatchSize(0);
    const float parentPs = (lodCount > 1) ? GetPatchSize(1) : ps0;
    const glm::vec2 cameraXZ(cameraPos.x, cameraPos.z);
    const glm::vec2 parentSnap(
        std::floor(cameraXZ.x / parentPs) * parentPs,
        std::floor(cameraXZ.y / parentPs) * parentPs);

    RingBounds prev;
    prev.min = parentSnap - glm::vec2(parentPs);
    prev.max = prev.min + glm::vec2(4.0f * ps0);

    auto emitRing = [&](int lod, const RingBounds& bounds)
    {
        const float ps = GetPatchSize(lod);
        for (int iz = 0; iz < 4; ++iz)
        {
            for (int ix = 0; ix < 4; ++ix)
            {
                const bool innerHole = lod > 0 && (ix == 1 || ix == 2) && (iz == 1 || iz == 2);
                if (innerHole)
                    continue;

                CDLODPatch patch = {};
                patch.worldOrigin = bounds.min + glm::vec2(static_cast<float>(ix) * ps, static_cast<float>(iz) * ps);
                patch.worldSize = ps;
                patch.worldCenter = patch.worldOrigin + glm::vec2(ps * 0.5f);
                patch.lodLevel = lod;
                patch.outerEdgeMask = ComputeOuterEdgeMask(ix, iz);
                patch.innerEdgeMask = ComputeInnerEdgeMask(lod, ix, iz);
                m_Patches.push_back(patch);
            }
        }
    };

    emitRing(0, prev);
    for (int lod = 1; lod < lodCount; ++lod)
    {
        const float ps = GetPatchSize(lod);
        RingBounds bounds;
        bounds.min = prev.min - glm::vec2(ps);
        bounds.max = prev.max + glm::vec2(ps);
        emitRing(lod, bounds);
        prev = bounds;
    }
}

uint32_t VansWaterLOD::FrustumCullPatches(const glm::mat4& viewProjMatrix)
{
    if (m_Patches.empty())
        return 0;

    glm::vec4 row0(viewProjMatrix[0][0], viewProjMatrix[1][0], viewProjMatrix[2][0], viewProjMatrix[3][0]);
    glm::vec4 row1(viewProjMatrix[0][1], viewProjMatrix[1][1], viewProjMatrix[2][1], viewProjMatrix[3][1]);
    glm::vec4 row2(viewProjMatrix[0][2], viewProjMatrix[1][2], viewProjMatrix[2][2], viewProjMatrix[3][2]);
    glm::vec4 row3(viewProjMatrix[0][3], viewProjMatrix[1][3], viewProjMatrix[2][3], viewProjMatrix[3][3]);

    struct Plane { glm::vec3 normal; float d; };
    Plane planes[6];
    planes[0].normal = glm::vec3(row3 + row0); planes[0].d = row3.w + row0.w;
    planes[1].normal = glm::vec3(row3 - row0); planes[1].d = row3.w - row0.w;
    planes[2].normal = glm::vec3(row3 + row1); planes[2].d = row3.w + row1.w;
    planes[3].normal = glm::vec3(row3 - row1); planes[3].d = row3.w - row1.w;
    planes[4].normal = glm::vec3(row3 + row2); planes[4].d = row3.w + row2.w;
    planes[5].normal = glm::vec3(row3 - row2); planes[5].d = row3.w - row2.w;

    for (int i = 0; i < 6; ++i)
    {
        const float len = glm::length(planes[i].normal);
        if (len > 0.0001f)
        {
            planes[i].normal /= len;
            planes[i].d /= len;
        }
    }

    auto it = std::remove_if(m_Patches.begin(), m_Patches.end(),
        [&](const CDLODPatch& patch)
        {
            const float halfSize = patch.worldSize * 0.5f;
            const glm::vec3 sphereCenter(patch.worldCenter.x, 0.0f, patch.worldCenter.y);
            const float radius = halfSize * 1.41421356f;

            for (int p = 0; p < 6; ++p)
            {
                const float dist = glm::dot(planes[p].normal, sphereCenter) + planes[p].d;
                if (dist < -radius)
                    return true;
            }
            return false;
        });

    m_Patches.erase(it, m_Patches.end());
    return static_cast<uint32_t>(m_Patches.size());
}

} // namespace VansGraphics
