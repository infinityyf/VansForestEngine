#pragma once
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansTexture.h"
#include "../VulkanCore/VansMesh.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansVKBuffer.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include "../VansCamera.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>
#include <cstdint>

namespace VansGraphics
{
    // ----------------------------------------------------------
    // 从场景 JSON 读取的地形配置。
    // ----------------------------------------------------------
    struct TerrainLayerConfig
    {
        std::string albedoPath;
        std::string normalPath;
        std::string roughnessPath;
        float tiling = 64.0f;

        // 场景纹理管理器预加载的贴图；非空时忽略路径并直接借用。
        VansTexture* albedoTex   = nullptr;
        VansTexture* normalTex   = nullptr;
        VansTexture* roughnessTex = nullptr;
    };

    struct TerrainConfig
    {
        std::string heightmapPath;
        std::string splatmap0Path;
        std::string splatmap1Path;
        std::vector<TerrainLayerConfig> layers;  // 最多 TERRAIN_MAX_LAYERS 层

        float terrainSize = 1024.0f;
        float maxHeight = 500.0f;
        float heightOffset = -23.0f;
        float splitDistMult = 2.0f;
        float lodDistanceRatio = 2.0f;
        float morphStartRatio = 0.70f;
        uint32_t maxPatchInstances = 20000;

        // 细分参数，可由 JSON 配置。
        bool  enableTessellation   = true;
        float tessellationDistance  = 300.0f;
        float maxTessellationLevel  = 64.0f;
        float tessellationPower        = 2.0f;
        float tessLodBias              = 0.5f;
        float tessDisplacementStrength = 0.05f;  // 已弃用：由程序化噪声替代

        // 程序化噪声细节（替代原 tessDisplacementStrength）
        bool  enableNoiseDetail    = true;
        float noiseStrength        = 0.03f;
        float noiseFrequency       = 0.8f;
        float noiseLacunarity      = 2.0f;
        float noiseGain            = 0.52f;
        int   noiseOctaves         = 4;
        float noiseWarpStrength    = 0.0f;
        float noiseFadeStart       = 0.7f;
    };

    // ----------------------------------------------------------
    // 与着色器 TerrainParams 块对齐的 GPU UBO。
    // ----------------------------------------------------------
    struct alignas(16) TerrainParamsGPU
    {
        glm::ivec4 layerCountPacked;             // .x 为 layerCount，.yzw 保留，按 std140 ivec4 对齐
        // std140 中数组元素按 vec4 步长排列，每层 tiling 写入 vec4.x。
        float tilingFactors[TERRAIN_MAX_LAYERS * 4];  // [i*4+0] 为 tiling，其余为 padding
        glm::vec4 heightfieldParams;             // x=terrainSize, y=maxHeight, z=heightOffset, w=patchGridSize
    };

    // 与着色器 TessellationParams 块对齐的 GPU UBO（绑定 7）。
    struct alignas(16) TerrainTessellationParamsGPU
    {
        float maxTessLevel;     // 最大细分等级
        float tessDistance;     // 细分距离
        float tessPower;        // 距离衰减指数
        float padding;          // 原 displacementStrength，现为 padding（程序化噪声替代）
    };

    // 与着色器 NoiseDetailParams 块对齐的 GPU UBO（绑定 8）。
    struct alignas(16) TerrainNoiseDetailParamsGPU
    {
        float noiseStrength     = 0.03f;    // 噪声强度（世界单位）
        float noiseFrequency    = 0.8f;     // 基础频率
        float noiseLacunarity   = 2.0f;     // 频率倍增系数
        float noiseGain         = 0.52f;    // 振幅衰减系数
        int32_t noiseOctaves    = 4;        // octave 数量
        float noiseWarpStrength = 0.0f;     // 域扭曲强度（0=关闭）
        float fadeStart         = 0.7f;     // 距离衰减起始比例
        float noisePadding      = 0.0f;     // std140 对齐
    };

    // 发送给着色器的每个地形实例数据。
    struct TerrainInstanceData
    {
        glm::vec2 Offset; // 世界坐标偏移 (x, z)
        float Scale;      // 缩放倍率 (相对于基础 16x16 patch)
        float Lod;    // LOD 层级
        float StitchFlags; // 边缘缝合位掩码：0 左、1 右、2 上、3 下
        glm::vec3 padding0;
    };

    struct TerrainPatchVertex
    {
        uint16_t position[3];
        uint16_t uv[2];
        uint16_t normal[3];
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

        // 使用完整 splatmap 地形配置初始化。
        void Init(VansVKDevice* device, const TerrainConfig& config);

        // 每帧更新：计算 LOD，更新实例缓冲。
        void Update(VansCamera* camera);

        // 绘制
        void Draw(VansVKCommandBuffer& cmd, GlobalStateData& globalState, std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets);
        
        void DrawShadow(VansVKCommandBuffer& cmd, GlobalStateData& globalState, std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets);

        void DrawMotionVector(VansVKCommandBuffer& cmd, GlobalStateData& globalState, std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets);

        // ── 供植被系统接入地形高度图 ────────────────
        VansTexture* GetHeightMap() const { return m_HeightMap; }
        float GetTerrainSize() const { return m_TerrainSize; }
        float GetMaxHeight() const { return m_MaxHeight; }
        float GetHeightOffset() const { return m_HeightOffset; }

        // ── 供编辑器 Terrain Inspector 读取和修改 ────────────────────────
        bool  IsTessellationEnabled()     const { return m_EnableTessellation; }
        float GetTessellationDistance()   const { return m_TessellationDistance; }
        float GetMaxTessellationLevel()   const { return m_MaxTessellationLevel; }
        float GetTessellationPower()      const { return m_TessellationPower; }
        float GetTessLodBias()            const { return m_TessLodBias; }

        void SetTessellationEnabled(bool v);
        void SetTessellationDistance(float v);
        void SetMaxTessellationLevel(float v);
        void SetTessellationPower(float v);
        void SetTessLodBias(float v);

        // ── 已废弃：法线贴图 Y 位移已被程序化噪声替代 ──
        void  SetTessDisplacementStrength(float v);
        float GetTessDisplacementStrength() const { return m_TessDisplacementStrength; }

        // ── 程序化噪声参数访问接口 ──
        bool  IsNoiseDetailEnabled()  const { return m_EnableNoiseDetail; }
        float GetNoiseStrength()      const { return m_NoiseStrength; }
        float GetNoiseFrequency()     const { return m_NoiseFrequency; }
        float GetNoiseLacunarity()    const { return m_NoiseLacunarity; }
        float GetNoiseGain()          const { return m_NoiseGain; }
        int   GetNoiseOctaves()       const { return m_NoiseOctaves; }
        float GetNoiseWarpStrength()  const { return m_NoiseWarpStrength; }
        float GetNoiseFadeStart()     const { return m_NoiseFadeStart; }

        void SetNoiseDetailEnabled(bool v);
        void SetNoiseStrength(float v);
        void SetNoiseFrequency(float v);
        void SetNoiseLacunarity(float v);
        void SetNoiseGain(float v);
        void SetNoiseOctaves(int v);
        void SetNoiseWarpStrength(float v);
        void SetNoiseFadeStart(float v);

        float GetSplitDistMult()    const { return m_SplitDistMult; }
        void  SetSplitDistMult(float v)  { m_SplitDistMult = std::max(v, 0.1f); }
        float GetLodDistanceRatio() const { return m_LodDistanceRatio; }
        void  SetLodDistanceRatio(float v) { m_LodDistanceRatio = std::max(v, 1.0f); }

    public:

        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;

        std::vector<VkDescriptorSet> m_DescriptorSets;

    private:

        // 递归收集初始四叉树叶子节点
        void CollectLeafNodes(const TerrainNode& node, const glm::vec3& camPos, std::vector<TerrainNode>& outNodes);

        void BuildPatchMesh();
        void EnsureInstanceBufferCapacity(uint32_t requiredCapacity);

        // 执行 2:1 平衡，保证着色器边缘吸附的前提成立。
        void BalanceLeafNodes(std::vector<TerrainNode>& nodes);

        // 根据最终叶子集合计算边缘缝合标记
        int ComputeStitchFlags(const TerrainNode& node, const std::vector<TerrainNode>& nodes) const;

        // 判断四叉树节点是否需要继续细分。
        bool ShouldSplit(const TerrainNode& node, const glm::vec3& camPos);

        float GetMinPatchWorldSize() const;
    private:
        VansVKDevice* m_Device = nullptr;

        // --- 贴图资源 ---
        VansTexture* m_HeightMap = nullptr;
        VansTexture* m_Splatmap0 = nullptr;
        VansTexture* m_Splatmap1 = nullptr;
        VansTexture* m_LayerAlbedos[TERRAIN_MAX_LAYERS]    = {};
        VansTexture* m_LayerNormals[TERRAIN_MAX_LAYERS]    = {};
        VansTexture* m_LayerRoughness[TERRAIN_MAX_LAYERS]  = {};
        uint32_t     m_LayerCount = 0;
        bool         m_OwnsLayerTextures = true; // false 表示贴图由场景持有，本类只借用

        // 地形参数 UBO
        VansVKBuffer m_ParamsUBO;

        VansMesh* m_BasePatchMesh = nullptr; // 16x16 基础网格

        // 实例顶点属性描述。
        std::vector<VkVertexInputAttributeDescription> m_TerrainInstanceInputAttributeDescriptions;

        // 实例顶点绑定描述。
        std::vector <VkVertexInputBindingDescription> m_TerrainInstanceInputBindingDescriptions;
        
        // 着色器和管线
        VansGraphicsShader* m_TerrainShader = nullptr;
        VansGraphicsShader* m_TerrainShadowShader = nullptr;
        VansGraphicsShader* m_TerrainMotionVectorShader = nullptr;
        VansGraphicsShader* m_TerrainTessShader = nullptr;       // DeferredTess 管线

        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_Pipeline = VK_NULL_HANDLE;

        // 实例缓冲（每帧更新）
        VansVKBuffer m_InstanceBuffer;
        std::vector<TerrainInstanceData> m_InstanceDataCPU;

        // 按近场细分和远场 VS-only 拆分的实例缓冲。
        VansVKBuffer m_NearInstanceBuffer;
        VansVKBuffer m_FarInstanceBuffer;
        std::vector<TerrainInstanceData> m_NearInstanceDataCPU;
        std::vector<TerrainInstanceData> m_FarInstanceDataCPU;

        // 细分参数 UBO（binding 7）
        VansVKBuffer m_TessParamsUBO;

        // 噪声细节 UBO（binding 8）
        VansVKBuffer m_NoiseDetailUBO;

        // 地形参数
        float m_TerrainSize = 1024.0f;
        float m_MaxHeight = 500.0f; // 地形最大高度
        float m_HeightOffset = -23.0f;
        float m_SplitDistMult = 2.0f;
        float m_LodDistanceRatio = 2.0f;
        float m_MorphStartRatio = 0.70f;
        uint32_t m_MaxPatchInstances = 20000;
        uint32_t m_InstanceBufferCapacity = 0;

        // 细分配置
        bool  m_EnableTessellation = true;
        float m_TessellationDistance = 300.0f;
        float m_MaxTessellationLevel = 64.0f;
        float m_TessellationPower = 2.0f;
        float m_TessLodBias = 0.5f;
        float m_TessDisplacementStrength = 0.05f;

        // 程序化噪声参数
        bool  m_EnableNoiseDetail  = true;
        float m_NoiseStrength      = 0.03f;
        float m_NoiseFrequency     = 0.8f;
        float m_NoiseLacunarity    = 2.0f;
        float m_NoiseGain          = 0.52f;
        int   m_NoiseOctaves       = 4;
        float m_NoiseWarpStrength  = 0.0f;
        float m_NoiseFadeStart     = 0.7f;

        // 辅助：统一更新噪声 UBO
        void UpdateNoiseDetailUBO();

        const int m_PatchGridSize = 16;   // 基础网格分辨率
    };
}
