#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansTerrain.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include <iostream>
#include <cmath>

namespace VansGraphics
{
    VansTerrain::VansTerrain() {}
    VansTerrain::~VansTerrain() 
    {
        // 清理资源逻辑 (略，需调用 DestroyVulkanBuffer 等)
        if(m_BasePatchMesh) delete m_BasePatchMesh;
        if(m_HeightMap) delete m_HeightMap;
        if (m_TerrainAlbedoMap) delete m_TerrainAlbedoMap;
        if(m_TerrainShader) delete m_TerrainShader;
        m_InstanceBuffer.DestroyVulkanBuffer(m_Device->GetLogicDevice());
    }

    void VansTerrain::Init(VansVKDevice* device, const std::string& heightMapPath, const std::string& albedoMapPath)
    {
        m_Device = device;

        // 1. 加载高度图
        m_HeightMap = new VansTexture();
        m_HeightMap->LoadTexture(device->GetCommandBuffer(), heightMapPath, false, false, false, MID_PRES_16,1); // Linear format for heightmap

        //加载albed
        m_TerrainAlbedoMap= new VansTexture();
        m_TerrainAlbedoMap->LoadTexture(device->GetCommandBuffer(), albedoMapPath, true, true);

        // 2. 创建基础 Patch Mesh (16x16 Grid)
		m_BasePatchMesh = new VansMesh();
		m_BasePatchMesh->LoadMesh(device->GetLogicDevice(), "D:/WorkSpace/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Models/Terrain/TerrainPatch16x16.obj", false, false);

        //添加input的描述支持instance绘制
        m_TerrainInstanceInputAttributeDescriptions = 
		{
            {
                3,
                1,
                VK_FORMAT_R32G32_SFLOAT,
                0
            },
            {
                4,
                1,
                VK_FORMAT_R32_SFLOAT,
                2 * sizeof(float)
            },
            {
                5,
                1,
                VK_FORMAT_R32_SFLOAT,
                3 * sizeof(float)
            },
            { 
                6, 
                1, 
                VK_FORMAT_R32_SFLOAT, 
                4 * sizeof(float) 
            }    
		};

        m_TerrainInstanceInputBindingDescriptions = 
        {
            {
                1,
                sizeof(TerrainInstanceData),
                VK_VERTEX_INPUT_RATE_INSTANCE
            }
        };

        //将上面两个新增的BindingDescriptions合并到mesh中
        m_BasePatchMesh->m_VertexInputAttributeDescriptions.insert(
            m_BasePatchMesh->m_VertexInputAttributeDescriptions.end(),
            m_TerrainInstanceInputAttributeDescriptions.begin(),
            m_TerrainInstanceInputAttributeDescriptions.end());

        m_BasePatchMesh->m_VertexInputBindingDescriptions.insert(
            m_BasePatchMesh->m_VertexInputBindingDescriptions.end(),
            m_TerrainInstanceInputBindingDescriptions.begin(),
            m_TerrainInstanceInputBindingDescriptions.end()
        );

        //vertex bind data
        VkVertexInputBindingDescription m_VertexInputBindingDescription;

        // 3. 创建 Instance Buffer (Host Visible 用于频繁更新)
        // 预估最大 Instance 数量：全部分裂约为 (2048/16)^2 = 16384。
        VkDeviceSize bufferSize = sizeof(TerrainInstanceData) * 20000; 
        m_InstanceBuffer.CreatVulkanBuffer(
            device->GetLogicDevice(), 
            bufferSize, 
            VK_FORMAT_R32_SFLOAT,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, // 作为 Vertex Buffer 绑定
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        // 4. 编译 Shader
        m_TerrainShader = new VansGraphicsShader();
        m_TerrainShader->InitShader(device->GetLogicDevice(), "D:/WorkSpace/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/Terrain");
		//m_TerrainShader->SetPolygonMode(VK_POLYGON_MODE_LINE);

        // 5. 创建 Descriptor Set (绑定高度图)
        // Layout: Binding 0 = HeightMap Sampler
        VkDescriptorSetLayoutBinding heightMapBinding = {
            0, 
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 
            1, 
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
            nullptr
        };
        VkDescriptorSetLayoutBinding albedoMapBinding = {
            1, 
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 
            1, 
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
            nullptr
        };
        VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ heightMapBinding,albedoMapBinding }, m_DescriptorSetLayout);
        VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_DescriptorSetLayout }, m_DescriptorSets);
        

        VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
            {
                m_DescriptorSets[0],
                VansVKDescriptorManager::m_SampleTexture0SetBinding,
                0,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {
                    {
                        m_HeightMap->GetImage().GetSampler(),
                        m_HeightMap->GetImage().GetImageView(),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    }
                }
            }
        );
        VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
            {
                m_DescriptorSets[0],
                VansVKDescriptorManager::m_SampleTexture1SetBinding,
                0,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {
                    {
                        m_TerrainAlbedoMap->GetImage().GetSampler(),
                        m_TerrainAlbedoMap->GetImage().GetImageView(),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    }
                }
            }
        );
        //只需要变化时更新，有的时候会绑定其他资源，所以需要update
        VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
    }

    // 辅助函数：计算切比雪夫距离 (Chebyshev Distance)
    // 返回点 (x, z) 到相机 (camX, camZ) 在 XZ 平面上的最大轴向距离
    float GetChebyshevDistance(float x, float z, const glm::vec3& camPos)
    {
        float dx = std::abs(x - camPos.x);
        float dz = std::abs(z - camPos.z);
        // 如果需要考虑高度 (3D)，可以加入 dy
        // float dy = std::abs(0.0f - camPos.y); 
        // return std::max(std::max(dx, dz), dy);
        
        return std::max(dx, dz);
    }

    // 辅助函数：吸附坐标到指定步长
    float SnapToGrid(float val, float step) {
        return std::floor(val / step) * step;
    }

    bool VansTerrain::ShouldSplit(const TerrainNode& node, const glm::vec3& camPos)
    {
        // 1. 吸附相机位置到当前节点大小的倍数
        // 这样可以保证在同一个 node.size 的网格内，LOD 判定结果是一致的
        // 避免相机微小移动导致的 LOD 闪烁
        float snappedCamX = SnapToGrid(camPos.x, node.size);
        float snappedCamZ = SnapToGrid(camPos.z, node.size);
        
        // 2. 距离判断 (使用切比雪夫距离)
        float centerX = node.x + node.size * 0.5f;
        float centerZ = node.z + node.size * 0.5f;
        
        // 使用吸附后的相机位置计算距离
        float dx = std::abs(centerX - snappedCamX);
        float dz = std::abs(centerZ - snappedCamZ);
        float dist = std::max(dx, dz);

        // 阈值判断
        if (dist < node.size * 2.0f) { 
            return true;
        }

        return false;
    }

    // 辅助函数：检查指定位置如果存在一个 size 大小的节点，是否需要分裂
    bool CheckNodeSplit(float centerX, float centerZ, float size, const glm::vec3& camPos)
    {
        // 同样需要吸附，保持逻辑一致
        float snappedCamX = SnapToGrid(camPos.x, size);
        float snappedCamZ = SnapToGrid(camPos.z, size);

        float dx = std::abs(centerX - snappedCamX);
        float dz = std::abs(centerZ - snappedCamZ);
        float dist = std::max(dx, dz);
        
        return dist < size * 2.0f;
    }

    void VansTerrain::UpdateNode(const TerrainNode& node, const glm::vec3& camPos)
    {
        // 如果已经是最小单元，或者不需要分裂 -> 渲染该节点
        if (node.size <= m_PatchGridSize || !ShouldSplit(node, camPos))
        {
            TerrainInstanceData data;
            data.Offset = glm::vec2(node.x, node.z);
            data.Scale = node.size / (float)m_PatchGridSize;
            data.Lod = static_cast<float>(node.lodLevel);
            
            int stitchFlags = 0;
            
            // 当前节点中心
            float myCenterX = node.x + node.size * 0.5f;
            float myCenterZ = node.z + node.size * 0.5f;
            
            // 父节点尺寸
            float parentSize = node.size * 2.0f;

            // 判断自己在父节点中的位置 (0: 左/上, 1: 右/下)
            // 使用 epsilon 防止浮点误差，或者直接用整数索引如果 node.x 是对齐的
            // 这里假设 node.x / node.size 是整数
            long long gridX = static_cast<long long>(node.x / node.size + 0.5f);
            long long gridZ = static_cast<long long>(node.z / node.size + 0.5f);
            
            bool isLeftChild = (gridX % 2 == 0);
            bool isTopChild  = (gridZ % 2 == 0);

            // Lambda: 检查邻居是否比我粗糙 (即邻居的父节点没分裂)
            auto CheckNeighborIsCoarser = [&](float neighborParentCenterX, float neighborParentCenterZ) -> bool {
                // 检查该父节点是否分裂
                // 如果分裂 (true) -> 邻居同级或更细 -> 不缝合
                // 如果不分裂 (false) -> 邻居粗糙 -> 缝合
                return !CheckNodeSplit(neighborParentCenterX, neighborParentCenterZ, parentSize, camPos);
            };

            // 1. Left Neighbor
            if (node.x > 0) {
                // 如果我是右孩子，左边是亲兄弟 -> 亲兄弟必存在 -> 不缝合
                // 如果我是左孩子，左边是堂兄弟 (属于另一个父节点) -> 需要检查那个父节点
                if (isLeftChild) {
                    // 左边堂兄弟的父节点中心：当前中心 - size (向左跨越半个父节点身位)
                    // 几何推导：
                    // 我是左孩子，我的中心是 ParentCenter - size/2
                    // 左边邻居是另一个父节点的右孩子，它的中心是 NeighborParentCenter + size/2
                    // 距离 = size. 所以 NeighborParentCenter = MyCenter - size
                    if (CheckNeighborIsCoarser(myCenterX - node.size, myCenterZ)) {
                        stitchFlags |= 1;
                    }
                }
            }

            // 2. Right Neighbor
            if (node.x + node.size < m_TerrainSize) {
                // 如果我是左孩子，右边是亲兄弟 -> 不缝合
                // 如果我是右孩子，右边是堂兄弟 -> 检查
                if (!isLeftChild) {
                    // 右边堂兄弟的父节点中心：当前中心 + size
                    if (CheckNeighborIsCoarser(myCenterX + node.size, myCenterZ)) {
                        stitchFlags |= 2;
                    }
                }
            }

            // 3. Top Neighbor
            if (node.z > 0) {
                // 如果我是下孩子，上边是亲兄弟 -> 不缝合
                // 如果我是上孩子，上边是堂兄弟 -> 检查
                if (isTopChild) {
                    if (CheckNeighborIsCoarser(myCenterX, myCenterZ - node.size)) {
                        stitchFlags |= 4;
                    }
                }
            }

            // 4. Bottom Neighbor
            if (node.z + node.size < m_TerrainSize) {
                // 如果我是上孩子，下边是亲兄弟 -> 不缝合
                // 如果我是下孩子，下边是堂兄弟 -> 检查
                if (!isTopChild) {
                    if (CheckNeighborIsCoarser(myCenterX, myCenterZ + node.size)) {
                        stitchFlags |= 8;
                    }
                }
            }

            data.StitchFlags = static_cast<float>(stitchFlags);
            m_InstanceDataCPU.push_back(data);
        }
        else
        {
            // 分裂逻辑保持不变
            float halfSize = node.size * 0.5f;
            UpdateNode({ node.x, node.z, halfSize, node.lodLevel + 1 }, camPos);
            UpdateNode({ node.x + halfSize, node.z, halfSize, node.lodLevel + 1 }, camPos);
            UpdateNode({ node.x, node.z + halfSize, halfSize, node.lodLevel + 1 }, camPos);
            UpdateNode({ node.x + halfSize, node.z + halfSize, halfSize, node.lodLevel + 1 }, camPos);
        }
    }

    void VansTerrain::Update(VansCamera* camera)
    {
        m_InstanceDataCPU.clear();

        // 从根节点开始遍历 (0,0, 2048)
        TerrainNode root = { -m_TerrainSize*0.5f, -m_TerrainSize * 0.5f, m_TerrainSize, 0 };
        UpdateNode(root, camera->GetPosition());

        // 更新 Instance Buffer
        if (!m_InstanceDataCPU.empty())
        {
			m_InstanceBuffer.SetBufferData(m_InstanceDataCPU.data(), 0, sizeof(TerrainInstanceData) * m_InstanceDataCPU.size());
        }
    }

    void VansTerrain::Draw(VansVKCommandBuffer& cmd, GlobalStateData& globalState, std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets)
    {
        if (m_InstanceDataCPU.empty()) return;

        // 4. 绑定 Vertex Buffer (Mesh) - Binding 0
        VkBuffer vertexBuffers[] = { m_BasePatchMesh->GetVertexBufferParameter().Buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd.GetVKCommandBuffer(), 0, 1, vertexBuffers, offsets);

        // 5. 绑定 Instance Buffer - Binding 3 (对应 Shader 中的 layout location 3, 4)
        // 注意：这里需要你的 Pipeline VertexInputState 定义了 Binding 1 为 Per-Instance Rate
        // 假设我们在 Pipeline 创建时将 Binding 1 设为 Instance Input
        VkBuffer instanceBuffers[] = { m_InstanceBuffer.GetNativeBuffer() };
        VkDeviceSize instanceOffsets[] = { 0 };
        // 这里的 binding index 取决于你的 Pipeline 定义，通常 Mesh 是 0，Instance 是 1
        vkCmdBindVertexBuffers(cmd.GetVKCommandBuffer(), 1, 1, instanceBuffers, instanceOffsets);

        // 6. 绑定 Index Buffer
        vkCmdBindIndexBuffer(cmd.GetVKCommandBuffer(), m_BasePatchMesh->GetIndexBufferParameter().Buffer, 0, VK_INDEX_TYPE_UINT32);

        //记录mesh 的bind data，这里需要手动设置index 的input 描述
        globalState.vertexInputAttributeDescriptions = &m_BasePatchMesh->m_VertexInputAttributeDescriptions;
        globalState.vertexInputBindingDescriptions = &m_BasePatchMesh->m_VertexInputBindingDescriptions;

        //apply shader，确认pipeline以及创建完毕
        cmd.EnsureGraphicsShader(*m_TerrainShader, globalState, layouts);

        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_TerrainShader, 0, sets, {});


        cmd.BindGraphicsPipeline(*m_TerrainShader->GetGraphicsPipeline());
        
        // 7. Draw Indexed Indirect or Instanced
        vkCmdDrawIndexed(cmd.GetVKCommandBuffer(), m_BasePatchMesh->GetIndexCount(), (uint32_t)m_InstanceDataCPU.size(), 0, 0, 0);
    }
}