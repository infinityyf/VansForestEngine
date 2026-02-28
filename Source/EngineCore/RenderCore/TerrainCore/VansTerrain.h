#pragma once
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansTexture.h"
#include "../VulkanCore/VansMesh.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansVKBuffer.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../VansCamera.h"
#include <vector>
#include <memory>

namespace VansGraphics
{
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

        // 初始化：加载高度图，生成基础网格，编译 Shader
        void Init(VansVKDevice* device, const std::string& heightMapPath, const std::string& albedoMapPath);

        // 每帧更新：计算 LOD，更新 Instance Buffer
        void Update(VansCamera* camera);

        // 绘制
        void Draw(VansVKCommandBuffer& cmd, GlobalStateData& globalState, std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets);
        
        void DrawShadow(VansVKCommandBuffer& cmd, GlobalStateData& globalState, std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets);

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

        // 资源
        VansTexture* m_HeightMap = nullptr;
        VansTexture* m_TerrainAlbedoMap = nullptr;
        VansMesh* m_BasePatchMesh = nullptr; // 16x16 grid

        //attribute data
        std::vector<VkVertexInputAttributeDescription> m_TerrainInstanceInputAttributeDescriptions;

        //vertex bind data
        std::vector <VkVertexInputBindingDescription> m_TerrainInstanceInputBindingDescriptions;
        
        // Shader & Pipeline
        VansGraphicsShader* m_TerrainShader = nullptr;
        VansGraphicsShader* m_TerrainShadowShader = nullptr;

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