#pragma once
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansTexture.h"
#include "../VulkanCore/VansMesh.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansVKBuffer.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include "../VansCamera.h"
#include <vector>
#include <memory>
#include <string>

namespace VansGraphics
{
    // ----------------------------------------------------------
    // Configuration structs (populated from JSON)
    // ----------------------------------------------------------
    struct TerrainLayerConfig
    {
        std::string albedoPath;
        std::string normalPath;
        std::string roughnessPath;
        float tiling = 64.0f;

        // Pre-loaded texture pointers (from scene texture manager)
        // When set, paths are ignored and these are used directly.
        VansTexture* albedoTex   = nullptr;
        VansTexture* normalTex   = nullptr;
        VansTexture* roughnessTex = nullptr;
    };

    struct TerrainConfig
    {
        std::string heightmapPath;
        std::string splatmap0Path;
        std::string splatmap1Path;
        std::vector<TerrainLayerConfig> layers;  // up to TERRAIN_MAX_LAYERS
    };

    // ----------------------------------------------------------
    // GPU-side UBO matching the shader TerrainParams block
    // ----------------------------------------------------------
    struct TerrainParamsGPU
    {
        glm::ivec4 layerCountPacked;             // .x = layerCount, .yzw = unused (16 bytes, matches std140 ivec4)
        // std140: array elements have vec4 (16-byte) stride
        // Each tilingFactor is stored as .x of a vec4
        float tilingFactors[TERRAIN_MAX_LAYERS * 4];  // [i*4+0] = tiling, [i*4+1..3] = padding
    };

    // 发送给 Shader 的每个 Instance 的数据
    struct TerrainInstanceData
    {
        glm::vec2 Offset; // 世界坐标偏移 (x, z)
        float Scale;      // 缩放倍率 (相对于基础 16x16 patch)
        float Lod;    // lod
        float StitchFlags; // 新增：位掩码 (Bit 0: Left, 1: Right, 2: Top, 3: Bottom)
        glm::vec3 padding0;
    };

    // 四叉树节点结构
    struct TerrainNode
    {
        float x, z;       // 中心点或左上角
        float size;       // 覆盖区域大小
        int lodLevel;     // LOD 层级
        
        // 简单的包围盒高度信息，用于视锥剔除或粗糙度判断
        float minHeight = 0.0f;
        float maxHeight = 0.0f; 
    };

    class VansTerrain
    {
    public:
        VansTerrain();
        ~VansTerrain();

        // Initialize with full splatmap terrain config
        void Init(VansVKDevice* device, const TerrainConfig& config);

        // 每帧更新：计算 LOD，更新 Instance Buffer
        void Update(VansCamera* camera);

        // 绘制
        void Draw(VansVKCommandBuffer& cmd, GlobalStateData& globalState, std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets);
        
        void DrawShadow(VansVKCommandBuffer& cmd, GlobalStateData& globalState, std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets);

        void DrawMotionVector(VansVKCommandBuffer& cmd, GlobalStateData& globalState, std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets);

        // ── Accessors for vegetation terrain integration ────────────────
        VansTexture* GetHeightMap() const { return m_HeightMap; }
        float GetTerrainSize() const { return m_TerrainSize; }
        float GetMaxHeight() const { return m_MaxHeight; }

    public:

        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;

        std::vector<VkDescriptorSet> m_DescriptorSets;

    private:

        // 递归更新四叉树
        void UpdateNode(const TerrainNode& node, const glm::vec3& camPos);

        // 检查节点是否需要细分
        bool ShouldSplit(const TerrainNode& node, const glm::vec3& camPos);
    private:
        VansVKDevice* m_Device = nullptr;

        // --- Textures ---
        VansTexture* m_HeightMap = nullptr;
        VansTexture* m_Splatmap0 = nullptr;
        VansTexture* m_Splatmap1 = nullptr;
        VansTexture* m_LayerAlbedos[TERRAIN_MAX_LAYERS]    = {};
        VansTexture* m_LayerNormals[TERRAIN_MAX_LAYERS]    = {};
        VansTexture* m_LayerRoughness[TERRAIN_MAX_LAYERS]  = {};
        uint32_t     m_LayerCount = 0;
        bool         m_OwnsLayerTextures = true; // false when textures are borrowed from scene

        // Terrain params UBO
        VansVKBuffer m_ParamsUBO;

        VansMesh* m_BasePatchMesh = nullptr; // 16x16 grid

        //attribute data
        std::vector<VkVertexInputAttributeDescription> m_TerrainInstanceInputAttributeDescriptions;

        //vertex bind data
        std::vector <VkVertexInputBindingDescription> m_TerrainInstanceInputBindingDescriptions;
        
        // Shader & Pipeline
        VansGraphicsShader* m_TerrainShader = nullptr;
        VansGraphicsShader* m_TerrainShadowShader = nullptr;
        VansGraphicsShader* m_TerrainMotionVectorShader = nullptr;

        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_Pipeline = VK_NULL_HANDLE;

        // Instance Buffer (每帧更新)
        VansVKBuffer m_InstanceBuffer;
        std::vector<TerrainInstanceData> m_InstanceDataCPU;

        // 地形参数
        const float m_TerrainSize = 1024.0f;
        const float m_MaxHeight = 200.0f; // 地形最大高度
        const int m_PatchGridSize = 16;   // 基础网格分辨率
    };
}