#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansTerrain.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../../Configration/VansConfigration.h"
#include <iostream>
#include <cmath>

namespace VansGraphics
{
    VansTerrain::VansTerrain() {}
    VansTerrain::~VansTerrain() 
    {
        if(m_BasePatchMesh) delete m_BasePatchMesh;
        if(m_HeightMap) delete m_HeightMap;
        if(m_Splatmap0) delete m_Splatmap0;
        if(m_Splatmap1) delete m_Splatmap1;
        if (m_OwnsLayerTextures)
        {
            for (uint32_t i = 0; i < m_LayerCount; ++i)
            {
                if(m_LayerAlbedos[i])    delete m_LayerAlbedos[i];
                if(m_LayerNormals[i])    delete m_LayerNormals[i];
                if(m_LayerRoughness[i])  delete m_LayerRoughness[i];
            }
        }
        if(m_TerrainShader) delete m_TerrainShader;
        m_ParamsUBO.DestroyVulkanBuffer(m_Device->GetLogicDevice());
        m_InstanceBuffer.DestroyVulkanBuffer(m_Device->GetLogicDevice());
    }

    void VansTerrain::Init(VansVKDevice* device, const TerrainConfig& config)
    {
        m_Device = device;

        // -------------------------------------------------------
        // 1. Load heightmap
        // -------------------------------------------------------
        m_HeightMap = new VansTexture();
        m_HeightMap->LoadTexture(device->GetCommandBuffer(), config.heightmapPath, false, false, false, MID_PRES_16, 1);

        // -------------------------------------------------------
        // 2. Load splatmaps
        // -------------------------------------------------------
        m_Splatmap0 = new VansTexture();
        m_Splatmap0->LoadTexture(device->GetCommandBuffer(), config.splatmap0Path, false, false);

        m_Splatmap1 = new VansTexture();
        m_Splatmap1->LoadTexture(device->GetCommandBuffer(), config.splatmap1Path, false, false);

        // -------------------------------------------------------
        // 3. Load per-layer PBR textures
        // -------------------------------------------------------
        m_LayerCount = static_cast<uint32_t>(std::min(config.layers.size(), (size_t)TERRAIN_MAX_LAYERS));

        // Check if pre-loaded textures are provided (from scene texture manager)
        bool hasPreloaded = (m_LayerCount > 0 && config.layers[0].albedoTex != nullptr);
        m_OwnsLayerTextures = !hasPreloaded;

        for (uint32_t i = 0; i < m_LayerCount; ++i)
        {
            const auto& layer = config.layers[i];
            if (layer.albedoTex && layer.normalTex && layer.roughnessTex)
            {
                // Use pre-loaded textures (borrowed, not owned)
                m_LayerAlbedos[i]   = layer.albedoTex;
                m_LayerNormals[i]   = layer.normalTex;
                m_LayerRoughness[i] = layer.roughnessTex;
            }
            else
            {
                // Fallback: load from path
                m_LayerAlbedos[i] = new VansTexture();
                m_LayerAlbedos[i]->LoadTexture(device->GetCommandBuffer(), layer.albedoPath, true, true);

                m_LayerNormals[i] = new VansTexture();
                m_LayerNormals[i]->LoadTexture(device->GetCommandBuffer(), layer.normalPath, false, true);

                m_LayerRoughness[i] = new VansTexture();
                m_LayerRoughness[i]->LoadTexture(device->GetCommandBuffer(), layer.roughnessPath, false, true);
            }
        }

        // -------------------------------------------------------
        // 4. Create base patch mesh (16x16 Grid)
        // -------------------------------------------------------
		auto vansConfigration = VansConfigration::GetInstance();
		std::string projectRoot = vansConfigration->GetProjectRootPath();
		m_BasePatchMesh = new VansMesh();
		m_BasePatchMesh->LoadMesh(device->GetLogicDevice(), device->GetGraphicsQueue(), &(device->GetCommandBuffer()), (projectRoot + "EngineAssets/Models/Terrain/TerrainPatch16x16.obj").c_str(), false);

        // Instance input descriptions
        m_TerrainInstanceInputAttributeDescriptions = 
		{
            { 3, 1, VK_FORMAT_R32G32_SFLOAT, 0 },
            { 4, 1, VK_FORMAT_R32_SFLOAT,    2 * sizeof(float) },
            { 5, 1, VK_FORMAT_R32_SFLOAT,    3 * sizeof(float) },
            { 6, 1, VK_FORMAT_R32_SFLOAT,    4 * sizeof(float) }
		};

        m_TerrainInstanceInputBindingDescriptions = 
        {
            { 1, sizeof(TerrainInstanceData), VK_VERTEX_INPUT_RATE_INSTANCE }
        };

        m_BasePatchMesh->m_VertexInputAttributeDescriptions.insert(
            m_BasePatchMesh->m_VertexInputAttributeDescriptions.end(),
            m_TerrainInstanceInputAttributeDescriptions.begin(),
            m_TerrainInstanceInputAttributeDescriptions.end());

        m_BasePatchMesh->m_VertexInputBindingDescriptions.insert(
            m_BasePatchMesh->m_VertexInputBindingDescriptions.end(),
            m_TerrainInstanceInputBindingDescriptions.begin(),
            m_TerrainInstanceInputBindingDescriptions.end());

        // -------------------------------------------------------
        // 5. Create instance buffer
        // -------------------------------------------------------
        VkDeviceSize bufferSize = sizeof(TerrainInstanceData) * 20000; 
        m_InstanceBuffer.CreatVulkanBuffer(
            device->GetLogicDevice(), bufferSize, VK_FORMAT_R32_SFLOAT,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        // -------------------------------------------------------
        // 6. Create terrain params UBO
        // -------------------------------------------------------
        TerrainParamsGPU params{};
        params.layerCountPacked.x = static_cast<int>(m_LayerCount);
        // std140: each float array element occupies 16 bytes (vec4 stride)
        for (uint32_t i = 0; i < m_LayerCount; ++i)
            params.tilingFactors[i * 4] = config.layers[i].tiling;
        for (uint32_t i = m_LayerCount; i < TERRAIN_MAX_LAYERS; ++i)
            params.tilingFactors[i * 4] = 1.0f;

        m_ParamsUBO.CreatVulkanBuffer(
            device->GetLogicDevice(), sizeof(TerrainParamsGPU), VK_FORMAT_R32_SFLOAT,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_ParamsUBO.SetBufferData(&params, 0, sizeof(TerrainParamsGPU));

        // -------------------------------------------------------
        // 7. Compile shaders
        // -------------------------------------------------------
        m_TerrainShader = new VansGraphicsShader();
        m_TerrainShader->InitShader(device->GetLogicDevice(), (projectRoot + "EngineAssets/Shaders/Terrain").c_str());
        m_TerrainShadowShader = new VansGraphicsShader();
        m_TerrainShadowShader->InitShader(device->GetLogicDevice(), (projectRoot + "EngineAssets/Shaders/Terrain/Shadow").c_str());
        m_TerrainShadowShader->SetPushConstant(sizeof(int)); // cascadeIndex

        // -------------------------------------------------------
        // 8. Create descriptor set
        // -------------------------------------------------------
        VansDescriptorSetLayoutFactory::CreateAndAllocate_Terrain(m_DescriptorSetLayout, m_DescriptorSets, 1);

        auto* descMgr = VansVKDescriptorManager::GetInstance();

        // Binding 0: heightMap
        descMgr->m_ImageDescInfos.push_back({
            m_DescriptorSets[0], TERRAIN_BINDING_HEIGHT_MAP, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_HeightMap->GetImage().GetSampler(), m_HeightMap->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
        });

        // Binding 1: splatMap0
        descMgr->m_ImageDescInfos.push_back({
            m_DescriptorSets[0], TERRAIN_BINDING_SPLATMAP_0, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_Splatmap0->GetImage().GetSampler(), m_Splatmap0->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
        });

        // Binding 2: splatMap1
        descMgr->m_ImageDescInfos.push_back({
            m_DescriptorSets[0], TERRAIN_BINDING_SPLATMAP_1, 0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { { m_Splatmap1->GetImage().GetSampler(), m_Splatmap1->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } }
        });

        // Binding 3: albedo array [8]
        {
            std::vector<VkDescriptorImageInfo> albedoInfos(TERRAIN_MAX_LAYERS);
            for (uint32_t i = 0; i < TERRAIN_MAX_LAYERS; ++i)
            {
                VansTexture* tex = (i < m_LayerCount) ? m_LayerAlbedos[i] : m_LayerAlbedos[0];
                albedoInfos[i] = { tex->GetImage().GetSampler(), tex->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            }
            descMgr->m_ImageDescInfos.push_back({
                m_DescriptorSets[0], TERRAIN_BINDING_ALBEDO_ARRAY, 0,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, albedoInfos
            });
        }

        // Binding 4: normal array [8]
        {
            std::vector<VkDescriptorImageInfo> normalInfos(TERRAIN_MAX_LAYERS);
            for (uint32_t i = 0; i < TERRAIN_MAX_LAYERS; ++i)
            {
                VansTexture* tex = (i < m_LayerCount) ? m_LayerNormals[i] : m_LayerNormals[0];
                normalInfos[i] = { tex->GetImage().GetSampler(), tex->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            }
            descMgr->m_ImageDescInfos.push_back({
                m_DescriptorSets[0], TERRAIN_BINDING_NORMAL_ARRAY, 0,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, normalInfos
            });
        }

        // Binding 5: roughness array [8]
        {
            std::vector<VkDescriptorImageInfo> roughInfos(TERRAIN_MAX_LAYERS);
            for (uint32_t i = 0; i < TERRAIN_MAX_LAYERS; ++i)
            {
                VansTexture* tex = (i < m_LayerCount) ? m_LayerRoughness[i] : m_LayerRoughness[0];
                roughInfos[i] = { tex->GetImage().GetSampler(), tex->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            }
            descMgr->m_ImageDescInfos.push_back({
                m_DescriptorSets[0], TERRAIN_BINDING_ROUGHNESS_ARRAY, 0,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, roughInfos
            });
        }

        // Binding 6: terrain params UBO
        descMgr->m_BufferDescInfos.push_back({
            m_DescriptorSets[0], TERRAIN_BINDING_PARAMS_UBO, 0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { m_ParamsUBO.GetNativeBuffer(), 0, sizeof(TerrainParamsGPU) } }
        });

        descMgr->UpdateDescriptorSets();
    }

    // 辅助函数：计算切比雪夫距离(Chebyshev Distance)
    // 返回从(x, z) 到相机(camX, camZ) 在XZ 平面上的最大轴向距离
    float GetChebyshevDistance(float x, float z, const glm::vec3& camPos)
    {
        float dx = std::abs(x - camPos.x);
        float dz = std::abs(z - camPos.z);
        // 如果需要考虑高度 (3D)，可以加上dy
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
        // 这样可以保证在同一个node.size 的网格内，LOD 判定结果是一致的
        // 避免相机微小移动导致的LOD 闪烁
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

    // 辅助函数：检查指定位置如果存在一个size 大小的节点，是否需要分裂
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
        // 如果已经是最小单元，或者不需要分裂-> 渲染该节点
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
            
            // 鐖惰妭鐐瑰昂瀵?
            float parentSize = node.size * 2.0f;

            // 判断自己在父节点中的位置 (0: 左/上, 1: 右/下)
            // 使用 epsilon 防止浮点误差，或者直接用整数索引如果 node.x 是对齐的
            // 这里假设 node.x / node.size 是整数
            long long gridX = static_cast<long long>(node.x / node.size + 0.5f);
            long long gridZ = static_cast<long long>(node.z / node.size + 0.5f);
            
            bool isLeftChild = (gridX % 2 == 0);
            bool isTopChild  = (gridZ % 2 == 0);

            // Lambda: 检查邻居是否比我粗糙(即邻居的父节点没分裂)
            auto CheckNeighborIsCoarser = [&](float neighborParentCenterX, float neighborParentCenterZ) -> bool {
                // 检查该父节点是否分裂
                // 如果分裂 (true) -> 邻居同级或更细-> 不缝合
                // 如果不分裂(false) -> 邻居粗糙 -> 缝合
                return !CheckNodeSplit(neighborParentCenterX, neighborParentCenterZ, parentSize, camPos);
            };

            // 1. Left Neighbor
            if (node.x > 0) {
                // 如果我是右孩子，左边是亲兄弟 -> 亲兄弟必存在 -> 不缝合
                // 如果我是左孩子，左边是堂兄弟 (属于另一个父节点) -> 需要检查那个父节点
                if (isLeftChild) {
                    // 左边堂兄弟的父节点中心：当前中心 - size (向左跨越半个父节点身位)
                    // 几何推导：
                    // 我是左孩子，我的中心是ParentCenter - size/2
                    // 左边邻居是另一个父节点的右孩子，它的中心是 NeighborParentCenter + size/2
                    // 距离 = size. 所以NeighborParentCenter = MyCenter - size
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

        // 从根节点开始遍历(0,0, 2048)
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
        // 注意：这里需要你的Pipeline VertexInputState 定义了Binding 1 为Per-Instance Rate
        // 假设我们在Pipeline 创建时将 Binding 1 设为 Instance Input
        VkBuffer instanceBuffers[] = { m_InstanceBuffer.GetNativeBuffer() };
        VkDeviceSize instanceOffsets[] = { 0 };
        // 这里的binding index 取决于你的Pipeline 定义，通常 Mesh 是0，Instance 是1
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
    void VansTerrain::DrawShadow(VansVKCommandBuffer& cmd, GlobalStateData& globalState, std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets)
    {
        if (m_InstanceDataCPU.empty()) return;

        // 4. 绑定 Vertex Buffer (Mesh) - Binding 0
        VkBuffer vertexBuffers[] = { m_BasePatchMesh->GetVertexBufferParameter().Buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd.GetVKCommandBuffer(), 0, 1, vertexBuffers, offsets);

        // 5. 绑定 Instance Buffer - Binding 3 (对应 Shader 中的 layout location 3, 4)
        // 注意：这里需要你的Pipeline VertexInputState 定义了Binding 1 为Per-Instance Rate
        // 假设我们在Pipeline 创建时将 Binding 1 设为 Instance Input
        VkBuffer instanceBuffers[] = { m_InstanceBuffer.GetNativeBuffer() };
        VkDeviceSize instanceOffsets[] = { 0 };
        // 这里的binding index 取决于你的Pipeline 定义，通常 Mesh 是0，Instance 是1
        vkCmdBindVertexBuffers(cmd.GetVKCommandBuffer(), 1, 1, instanceBuffers, instanceOffsets);

        // 6. 绑定 Index Buffer
        vkCmdBindIndexBuffer(cmd.GetVKCommandBuffer(), m_BasePatchMesh->GetIndexBufferParameter().Buffer, 0, VK_INDEX_TYPE_UINT32);

        //记录mesh 的bind data，这里需要手动设置index 的input 描述
        globalState.vertexInputAttributeDescriptions = &m_BasePatchMesh->m_VertexInputAttributeDescriptions;
        globalState.vertexInputBindingDescriptions = &m_BasePatchMesh->m_VertexInputBindingDescriptions;

        //apply shader，确认pipeline以及创建完毕
        cmd.EnsureGraphicsShader(*m_TerrainShadowShader, globalState, layouts);

        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_TerrainShadowShader, 0, sets, {});


        cmd.BindGraphicsPipeline(*m_TerrainShadowShader->GetGraphicsPipeline());

        // Push cascade index for terrain shadow shader
        if (m_TerrainShadowShader->GetPushConstantSize() > 0)
        {
            int cascadeIndex = globalState.cascadeIndex;
            cmd.UpdatePushConstants(*m_TerrainShadowShader->GetGraphicsPipeline(),
                VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(int), &cascadeIndex);
        }

        // 7. Draw Indexed Indirect or Instanced
        vkCmdDrawIndexed(cmd.GetVKCommandBuffer(), m_BasePatchMesh->GetIndexCount(), (uint32_t)m_InstanceDataCPU.size(), 0, 0, 0);
    }
}