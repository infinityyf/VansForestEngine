#pragma once

#include "VansWaterConfig.h"
#include "glm/glm.hpp"
#include <vulkan/vulkan.h>
#include <vector>

namespace VansGraphics
{
    class VansVKDevice;

    enum WaterPatchEdgeMask : uint32_t
    {
        EdgeNone  = 0,
        EdgeLeft  = 1 << 0,
        EdgeRight = 1 << 1,
        EdgeDown  = 1 << 2,
        EdgeUp    = 1 << 3,
    };

    struct CDLODPatch
    {
        glm::vec2 worldOrigin = glm::vec2(0.0f);
        glm::vec2 worldCenter = glm::vec2(0.0f);
        float     worldSize = 0.0f;
        int       lodLevel = 0;
        uint32_t  outerEdgeMask = EdgeNone;
        uint32_t  innerEdgeMask = EdgeNone;
    };

    struct WaterPatchPushConstant
    {
        glm::vec2 patchWorldOrigin;
        float     patchWorldSize;
        int       lodLevel;
        float     waterLevel;
        uint32_t  outerEdgeMask;
        uint32_t  innerEdgeMask;
        glm::vec2 pad;
    };

    class VansWaterLOD
    {
    public:
        static constexpr int   MAX_LOD_COUNT   = 10;
        static constexpr int   WATER_MESH_DIM  = 65;
        static constexpr float MIN_LOD_DIST    = 16.0f;
        static constexpr float BASE_PATCH_SIZE = 16.0f;

        VansWaterLOD()  = default;
        ~VansWaterLOD() = default;

        bool Initialize(VansVKDevice* device,
                        int maxLodCount     = MAX_LOD_COUNT,
                        float minLodDist    = MIN_LOD_DIST,
                        int meshDim         = WATER_MESH_DIM,
                        float basePatchSize = BASE_PATCH_SIZE);

        void Shutdown(VkDevice logicDevice);
        void SetLodConfig(const VansWaterLODConfig& config);
        void GeneratePatches(const glm::vec3& cameraPos);
        uint32_t FrustumCullPatches(const glm::mat4& viewProjMatrix);

        const std::vector<CDLODPatch>& GetPatches() const { return m_Patches; }
        size_t GetPatchCount() const { return m_Patches.size(); }

        VkBuffer GetVertexBuffer() const { return m_VertexBuffer; }
        VkBuffer GetIndexBuffer() const { return m_IndexBuffer; }
        uint32_t GetIndexCount() const { return m_IndexCount; }
        int GetMeshDim() const { return m_MeshDim; }
        int GetLodLevels() const { return m_LodLevels; }
        float GetBasePatchSize() const { return m_BasePatchSize; }
        float GetDetailBalance() const { return m_DetailBalance; }
        float GetMorphWidthRatio() const { return m_MorphWidthRatio; }

        std::vector<VkVertexInputBindingDescription>& GetVertexBindings() { return m_VertexBindings; }
        std::vector<VkVertexInputAttributeDescription>& GetVertexAttributes() { return m_VertexAttributes; }

    private:
        void BuildPatchMesh(VkDevice logicDevice);
        bool AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags props,
                            VkBuffer& outBuffer, VkDeviceMemory& outMemory);

        static uint32_t ComputeOuterEdgeMask(int ix, int iz);
        static uint32_t ComputeInnerEdgeMask(int lod, int ix, int iz);
        float GetPatchSize(int lod) const;

        int   m_MeshDim       = WATER_MESH_DIM;
        float m_BasePatchSize = BASE_PATCH_SIZE;
        int   m_LodLevels     = MAX_LOD_COUNT;
        float m_MinLodDist    = MIN_LOD_DIST;
        float m_DetailBalance = 2.0f;
        float m_MorphWidthRatio = 0.5f;

        VkBuffer       m_VertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_VertexMemory = VK_NULL_HANDLE;
        VkBuffer       m_IndexBuffer  = VK_NULL_HANDLE;
        VkDeviceMemory m_IndexMemory  = VK_NULL_HANDLE;
        uint32_t       m_IndexCount   = 0;

        std::vector<VkVertexInputBindingDescription>   m_VertexBindings;
        std::vector<VkVertexInputAttributeDescription> m_VertexAttributes;

        std::vector<CDLODPatch> m_Patches;
        glm::vec3               m_LastCameraPos = glm::vec3(0.0f);

        VansVKDevice* m_Device = nullptr;
    };
}
