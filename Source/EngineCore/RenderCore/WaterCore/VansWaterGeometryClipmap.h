#pragma once

#include "VansWaterConfig.h"
#include "../VulkanCore/VansVKBuffer.h"
#include <glm/glm.hpp>
#include <vector>
#include <vulkan/vulkan.h>

namespace VansGraphics
{
    class VansVKDevice;

    enum WaterPatchEdgeMask : std::uint32_t
    {
        EdgeNone = 0,
        EdgeLeft = 1u << 0,
        EdgeRight = 1u << 1,
        EdgeDown = 1u << 2,
        EdgeUp = 1u << 3,
    };

    struct WaterGeometryPatch
    {
        glm::vec2 worldOrigin = {};
        glm::vec2 worldCenter = {};
        float worldSize = 0.0f;
        int lodLevel = 0;
        std::uint32_t outerEdgeMask = EdgeNone;
    };

    struct WaterPatchPushConstant
    {
        glm::vec2 patchWorldOrigin;
        float patchWorldSize;
        int lodLevel;
        float waterLevel;
        std::uint32_t outerEdgeMask;
        glm::vec2 padding;
    };

    // Geometry clipmap with a fixed 2:1 topology: 4x4 center plus 12-patch rings.
    class VansWaterGeometryClipmap
    {
    public:
        static constexpr int MAX_LOD_COUNT = VansWaterConfig::MAX_GEOMETRY_LODS;
        static constexpr int DEFAULT_MESH_DIM = 65;
        static constexpr float DEFAULT_PATCH_SIZE = 16.0f;

        bool Initialize(VansVKDevice* device, const VansWaterGeometryConfig& config);
        void Shutdown(VkDevice logicDevice);

        // Topology-changing meshDim is immutable after Initialize; other values are cheap.
        void ApplyConfig(const VansWaterGeometryConfig& config);
        void GeneratePatches(const glm::vec3& cameraPos);
        std::uint32_t FrustumCullPatches(const glm::mat4& viewProj, float waterLevel, float displacementBound);

        const std::vector<WaterGeometryPatch>& GetPatches() const { return m_Patches; }
        std::size_t GetPatchCount() const { return m_Patches.size(); }
        VkBuffer GetVertexBuffer() const { return m_VertexBufferCreated ? m_VertexBuffer.GetNativeBuffer() : VK_NULL_HANDLE; }
        VkBuffer GetIndexBuffer() const { return m_IndexBufferCreated ? m_IndexBuffer.GetNativeBuffer() : VK_NULL_HANDLE; }
        std::uint32_t GetIndexCount() const { return m_IndexCount; }
        int GetMeshDim() const { return m_Config.m_MeshDim; }
        int GetLodLevels() const { return m_Config.m_LodCount; }
        float GetBasePatchSize() const { return m_Config.m_BasePatchSize; }
        float GetMorphStartRatio() const { return m_Config.m_MorphStartRatio; }
        std::vector<VkVertexInputBindingDescription>& GetVertexBindings() { return m_VertexBindings; }
        std::vector<VkVertexInputAttributeDescription>& GetVertexAttributes() { return m_VertexAttributes; }

    private:
        bool BuildPatchMesh();
        static std::uint32_t ComputeOuterEdgeMask(int ix, int iz);
        float GetPatchSize(int lod) const;

        VansWaterGeometryConfig m_Config;
        VansVKDevice* m_Device = nullptr;
        VansVKBuffer m_VertexBuffer;
        VansVKBuffer m_IndexBuffer;
        bool m_VertexBufferCreated = false;
        bool m_IndexBufferCreated = false;
        std::uint32_t m_IndexCount = 0;
        std::vector<VkVertexInputBindingDescription> m_VertexBindings;
        std::vector<VkVertexInputAttributeDescription> m_VertexAttributes;
        std::vector<WaterGeometryPatch> m_Patches;
    };
}
