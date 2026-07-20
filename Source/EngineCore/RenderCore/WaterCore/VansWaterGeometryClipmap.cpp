#include "VansWaterGeometryClipmap.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../../Util/VansLog.h"
#include <algorithm>
#include <cmath>

namespace VansGraphics
{
    bool VansWaterGeometryClipmap::Initialize(VansVKDevice* device, const VansWaterGeometryConfig& config)
    {
        if (!device)
            return false;
        m_Device = device;
        m_Config = config;
        m_Config.m_LodCount = std::clamp(m_Config.m_LodCount, 1, MAX_LOD_COUNT);
        m_Config.m_MeshDim = std::clamp(m_Config.m_MeshDim, 17, 257);
        if (((m_Config.m_MeshDim - 1) & 1) != 0)
            ++m_Config.m_MeshDim;
        m_Config.m_BasePatchSize = (std::max)(m_Config.m_BasePatchSize, 0.25f);
        m_Config.m_MorphStartRatio = std::clamp(m_Config.m_MorphStartRatio, 0.05f, 0.95f);
        return BuildPatchMesh();
    }

    bool VansWaterGeometryClipmap::BuildPatchMesh()
    {
        const int dim = m_Config.m_MeshDim;
        const float invCells = 1.0f / float(dim - 1);
        std::vector<glm::vec2> vertices;
        vertices.reserve(std::size_t(dim) * std::size_t(dim));
        for (int z = 0; z < dim; ++z)
            for (int x = 0; x < dim; ++x)
                vertices.emplace_back(float(x) * invCells, float(z) * invCells);

        std::vector<std::uint32_t> indices;
        indices.reserve(std::size_t(dim - 1) * std::size_t(dim - 1) * 6u);
        for (int z = 0; z < dim - 1; ++z)
        {
            for (int x = 0; x < dim - 1; ++x)
            {
                const std::uint32_t tl = std::uint32_t(z * dim + x);
                const std::uint32_t tr = tl + 1;
                const std::uint32_t bl = std::uint32_t((z + 1) * dim + x);
                const std::uint32_t br = bl + 1;
                indices.insert(indices.end(), { tl, bl, tr, tr, bl, br });
            }
        }

        VkDevice logicDevice = m_Device->GetLogicDevice();
        const VkDeviceSize vbSize = VkDeviceSize(vertices.size() * sizeof(glm::vec2));
        m_VertexBufferCreated = m_VertexBuffer.CreatVulkanBuffer(logicDevice, vbSize, VK_FORMAT_UNDEFINED,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (m_VertexBufferCreated)
            m_VertexBuffer.SetBufferData(vertices.data(), 0, vbSize);

        const VkDeviceSize ibSize = VkDeviceSize(indices.size() * sizeof(std::uint32_t));
        m_IndexBufferCreated = m_IndexBuffer.CreatVulkanBuffer(logicDevice, ibSize, VK_FORMAT_UNDEFINED,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (m_IndexBufferCreated)
            m_IndexBuffer.SetBufferData(indices.data(), 0, ibSize);

        m_IndexCount = std::uint32_t(indices.size());
        m_VertexBindings = { { 0, sizeof(glm::vec2), VK_VERTEX_INPUT_RATE_VERTEX } };
        m_VertexAttributes = { { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 } };
        if (!m_VertexBufferCreated || !m_IndexBufferCreated)
        {
            VANS_LOG_ERROR("[WaterGeometryClipmap] immutable mesh creation failed");
            return false;
        }
        VANS_LOG("[WaterGeometryClipmap] mesh=" << dim << "x" << dim << " indices=" << m_IndexCount);
        return true;
    }

    void VansWaterGeometryClipmap::Shutdown(VkDevice logicDevice)
    {
        if (m_VertexBufferCreated)
            m_VertexBuffer.DestroyVulkanBuffer(logicDevice);
        if (m_IndexBufferCreated)
            m_IndexBuffer.DestroyVulkanBuffer(logicDevice);
        m_VertexBufferCreated = m_IndexBufferCreated = false;
        m_IndexCount = 0;
        m_Patches.clear();
        m_Device = nullptr;
    }

    void VansWaterGeometryClipmap::ApplyConfig(const VansWaterGeometryConfig& config)
    {
        if (config.m_MeshDim != m_Config.m_MeshDim)
            VANS_LOG_WARN("[WaterGeometryClipmap] meshDim is immutable at runtime; recreate WaterSystem to change it");
        m_Config.m_LodCount = std::clamp(config.m_LodCount, 1, MAX_LOD_COUNT);
        m_Config.m_BasePatchSize = (std::max)(config.m_BasePatchSize, 0.25f);
        m_Config.m_MorphStartRatio = std::clamp(config.m_MorphStartRatio, 0.05f, 0.95f);
    }

    float VansWaterGeometryClipmap::GetPatchSize(int lod) const
    {
        return std::ldexp(m_Config.m_BasePatchSize, lod);
    }

    std::uint32_t VansWaterGeometryClipmap::ComputeOuterEdgeMask(int ix, int iz)
    {
        std::uint32_t mask = EdgeNone;
        if (ix == 0) mask |= EdgeLeft;
        if (ix == 3) mask |= EdgeRight;
        if (iz == 0) mask |= EdgeDown;
        if (iz == 3) mask |= EdgeUp;
        return mask;
    }

    void VansWaterGeometryClipmap::GeneratePatches(const glm::vec3& cameraPos)
    {
        m_Patches.clear();
        const int lodCount = m_Config.m_LodCount;
        m_Patches.reserve(std::size_t(16 + 12 * (lodCount - 1)));

        struct Bounds { glm::vec2 min; glm::vec2 max; };
        const glm::vec2 cameraXZ(cameraPos.x, cameraPos.z);
        const float base = GetPatchSize(0);
        const float parent = lodCount > 1 ? GetPatchSize(1) : base;
        const glm::vec2 anchor(std::floor(cameraXZ.x / parent) * parent,
                               std::floor(cameraXZ.y / parent) * parent);
        Bounds previous{ anchor - glm::vec2(parent), anchor - glm::vec2(parent) + glm::vec2(4.0f * base) };

        auto emit = [&](int lod, const Bounds& bounds)
        {
            const float size = GetPatchSize(lod);
            for (int z = 0; z < 4; ++z)
            {
                for (int x = 0; x < 4; ++x)
                {
                    if (lod > 0 && (x == 1 || x == 2) && (z == 1 || z == 2))
                        continue;
                    WaterGeometryPatch patch;
                    patch.worldOrigin = bounds.min + glm::vec2(float(x) * size, float(z) * size);
                    patch.worldCenter = patch.worldOrigin + glm::vec2(size * 0.5f);
                    patch.worldSize = size;
                    patch.lodLevel = lod;
                    patch.outerEdgeMask = lod + 1 < lodCount ? ComputeOuterEdgeMask(x, z) : EdgeNone;
                    m_Patches.push_back(patch);
                }
            }
        };

        emit(0, previous);
        for (int lod = 1; lod < lodCount; ++lod)
        {
            const float size = GetPatchSize(lod);
            Bounds current{ previous.min - glm::vec2(size), previous.max + glm::vec2(size) };
            emit(lod, current);
            previous = current;
        }
    }

    std::uint32_t VansWaterGeometryClipmap::FrustumCullPatches(
        const glm::mat4& viewProj, float waterLevel, float displacementBound)
    {
        const glm::vec4 row0(viewProj[0][0], viewProj[1][0], viewProj[2][0], viewProj[3][0]);
        const glm::vec4 row1(viewProj[0][1], viewProj[1][1], viewProj[2][1], viewProj[3][1]);
        const glm::vec4 row2(viewProj[0][2], viewProj[1][2], viewProj[2][2], viewProj[3][2]);
        const glm::vec4 row3(viewProj[0][3], viewProj[1][3], viewProj[2][3], viewProj[3][3]);
        glm::vec4 planes[6] = { row3 + row0, row3 - row0, row3 + row1, row3 - row1, row3 + row2, row3 - row2 };
        for (auto& plane : planes)
        {
            const float length = glm::length(glm::vec3(plane));
            if (length > 1e-5f) plane /= length;
        }

        const float verticalExtent = (std::max)(displacementBound, 0.0f);
        m_Patches.erase(std::remove_if(m_Patches.begin(), m_Patches.end(), [&](const WaterGeometryPatch& patch)
        {
            const glm::vec3 center(patch.worldCenter.x, waterLevel, patch.worldCenter.y);
            const float radius = std::sqrt(0.5f * patch.worldSize * patch.worldSize + verticalExtent * verticalExtent);
            for (const auto& plane : planes)
                if (glm::dot(glm::vec3(plane), center) + plane.w < -radius)
                    return true;
            return false;
        }), m_Patches.end());
        return std::uint32_t(m_Patches.size());
    }
}
