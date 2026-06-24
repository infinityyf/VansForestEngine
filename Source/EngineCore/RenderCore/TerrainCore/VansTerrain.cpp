#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansTerrain.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../../Configration/VansConfigration.h"
#include "../../Util/VansLog.h"
#include <algorithm>
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
        if(m_TerrainShadowShader) delete m_TerrainShadowShader;
        if(m_TerrainMotionVectorShader) delete m_TerrainMotionVectorShader;
        if(m_TerrainTessShader) delete m_TerrainTessShader;
        m_ParamsUBO.DestroyVulkanBuffer(m_Device->GetLogicDevice());
        m_InstanceBuffer.DestroyVulkanBuffer(m_Device->GetLogicDevice());
        m_NearInstanceBuffer.DestroyVulkanBuffer(m_Device->GetLogicDevice());
        m_FarInstanceBuffer.DestroyVulkanBuffer(m_Device->GetLogicDevice());
        m_TessParamsUBO.DestroyVulkanBuffer(m_Device->GetLogicDevice());
        m_NoiseDetailUBO.DestroyVulkanBuffer(m_Device->GetLogicDevice());

        // 释放地形专属 descriptor set 和 layout
        auto descMgr = VansVKDescriptorManager::GetInstance();
        descMgr->DestroyDescriptorSet(m_DescriptorSets);
        descMgr->DestroyDescriptorSetLayout(m_DescriptorSetLayout);
    }

    void VansTerrain::Init(VansVKDevice* device, const TerrainConfig& config)
    {
        m_Device = device;
        m_TerrainSize = std::max(config.terrainSize, static_cast<float>(m_PatchGridSize));
        m_MaxHeight = config.maxHeight;
        m_HeightOffset = config.heightOffset;
        m_SplitDistMult = std::max(config.splitDistMult, 0.1f);
        m_LodDistanceRatio = std::max(config.lodDistanceRatio, 1.0f);
        m_MorphStartRatio = std::clamp(config.morphStartRatio, 0.0f, 1.0f);
        m_MaxPatchInstances = std::max(config.maxPatchInstances, 1u);

        // Tessellation config
        m_EnableTessellation   = config.enableTessellation;
        m_TessellationDistance = config.tessellationDistance;
        m_MaxTessellationLevel = config.maxTessellationLevel;
        m_TessellationPower    = config.tessellationPower;
        m_TessLodBias              = config.tessLodBias;
        m_TessDisplacementStrength = config.tessDisplacementStrength;

        // 程序化噪声参数
        m_EnableNoiseDetail = config.enableNoiseDetail;
        m_NoiseStrength     = config.noiseStrength;
        m_NoiseFrequency    = config.noiseFrequency;
        m_NoiseLacunarity   = config.noiseLacunarity;
        m_NoiseGain         = config.noiseGain;
        m_NoiseOctaves      = config.noiseOctaves;
        m_NoiseWarpStrength = config.noiseWarpStrength;
        m_NoiseFadeStart    = config.noiseFadeStart;

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
        // 5. Create instance buffers (split near/far for tessellation)
        // -------------------------------------------------------
        VkDeviceSize bufferSize = sizeof(TerrainInstanceData) * m_MaxPatchInstances;
        m_InstanceBuffer.CreatVulkanBuffer(
            device->GetLogicDevice(), bufferSize, VK_FORMAT_R32_SFLOAT,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_NearInstanceBuffer.CreatVulkanBuffer(
            device->GetLogicDevice(), bufferSize, VK_FORMAT_R32_SFLOAT,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_FarInstanceBuffer.CreatVulkanBuffer(
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
        params.heightfieldParams = glm::vec4(m_TerrainSize, m_MaxHeight, m_HeightOffset, static_cast<float>(m_PatchGridSize));

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

        m_TerrainMotionVectorShader = new VansGraphicsShader();
        m_TerrainMotionVectorShader->InitShader(device->GetLogicDevice(), (projectRoot + "EngineAssets/Shaders/Terrain/MotionVector").c_str());

        // -------------------------------------------------------
        // 7b. Create Tessellation Shader (DeferredTess folder)
        // -------------------------------------------------------
        m_TerrainTessShader = new VansGraphicsShader();
        m_TerrainTessShader->InitShader(device->GetLogicDevice(),
            (projectRoot + "EngineAssets/Shaders/Terrain/DeferredTess").c_str());
        m_TerrainTessShader->SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
        m_TerrainTessShader->SetPatchControlPoints(3);
        m_TerrainTessShader->SetDrawStateData(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS, VK_CULL_MODE_BACK_BIT);
        m_TerrainTessShader->SetColorAttachmentCount(4); // 4 MRT outputs for GBuffer (DeferredTess has no /Deferred subdir)

        // -------------------------------------------------------
        // 7c. Create Tessellation Params UBO (binding 7)
        // -------------------------------------------------------
        {
            TerrainTessellationParamsGPU tessParams{};
            tessParams.maxTessLevel = config.maxTessellationLevel;
            tessParams.tessDistance = config.tessellationDistance;
            tessParams.tessPower    = config.tessellationPower;
            tessParams.padding      = 0.0f;  // 原 displacementStrength，现为 padding

            m_TessParamsUBO.CreatVulkanBuffer(
                device->GetLogicDevice(), sizeof(TerrainTessellationParamsGPU),
                VK_FORMAT_R32_SFLOAT,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            m_TessParamsUBO.SetBufferData(&tessParams, 0, sizeof(tessParams));
        }

        // -------------------------------------------------------
        // 7d. Create NoiseDetail Params UBO (binding 8)
        // -------------------------------------------------------
        {
            m_NoiseDetailUBO.CreatVulkanBuffer(
                device->GetLogicDevice(), sizeof(TerrainNoiseDetailParamsGPU),
                VK_FORMAT_R32_SFLOAT,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            UpdateNoiseDetailUBO();
        }

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

        // Binding 7: TessellationParams UBO (read by TCS + TES)
        descMgr->m_BufferDescInfos.push_back({
            m_DescriptorSets[0], TERRAIN_BINDING_TESSELLATION_PARAMS, 0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { m_TessParamsUBO.GetNativeBuffer(), 0, sizeof(TerrainTessellationParamsGPU) } }
        });

        // Binding 8: NoiseDetailParams UBO (read by TES + FS)
        descMgr->m_BufferDescInfos.push_back({
            m_DescriptorSets[0], TERRAIN_BINDING_NOISE_DETAIL_PARAMS, 0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            { { m_NoiseDetailUBO.GetNativeBuffer(), 0, sizeof(TerrainNoiseDetailParamsGPU) } }
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
        float snappedCamX = SnapToGrid(camPos.x, node.size);
        float snappedCamZ = SnapToGrid(camPos.z, node.size);

        float centerX = node.x + node.size * 0.5f;
        float centerZ = node.z + node.size * 0.5f;

        float dx = std::abs(centerX - snappedCamX);
        float dz = std::abs(centerZ - snappedCamZ);
        float dist = std::max(dx, dz);

        float effectiveSplitMult = m_SplitDistMult;

        // In tessellation range, relax split condition → coarser patches → fewer instances.
        // Tessellation compensates geometric detail on these larger patches.
        if (m_EnableTessellation && dist < m_TessellationDistance) {
            effectiveSplitMult *= m_TessLodBias;
        }

        return dist < node.size * effectiveSplitMult;
    }

    namespace
    {
        constexpr float TerrainLodEpsilon = 0.001f;

        bool TerrainRangesOverlap(float aMin, float aMax, float bMin, float bMax)
        {
            return aMin < bMax - TerrainLodEpsilon && bMin < aMax - TerrainLodEpsilon;
        }

        bool TerrainNodesAdjacent(const TerrainNode& a, const TerrainNode& b)
        {
            bool xTouch = std::abs((a.x + a.size) - b.x) < TerrainLodEpsilon || std::abs((b.x + b.size) - a.x) < TerrainLodEpsilon;
            bool zOverlap = TerrainRangesOverlap(a.z, a.z + a.size, b.z, b.z + b.size);
            bool zTouch = std::abs((a.z + a.size) - b.z) < TerrainLodEpsilon || std::abs((b.z + b.size) - a.z) < TerrainLodEpsilon;
            bool xOverlap = TerrainRangesOverlap(a.x, a.x + a.size, b.x, b.x + b.size);
            return (xTouch && zOverlap) || (zTouch && xOverlap);
        }

        void TerrainSplitNode(const TerrainNode& node, std::vector<TerrainNode>& outNodes)
        {
            float halfSize = node.size * 0.5f;
            outNodes.push_back({ node.x, node.z, halfSize, node.lodLevel + 1 });
            outNodes.push_back({ node.x + halfSize, node.z, halfSize, node.lodLevel + 1 });
            outNodes.push_back({ node.x, node.z + halfSize, halfSize, node.lodLevel + 1 });
            outNodes.push_back({ node.x + halfSize, node.z + halfSize, halfSize, node.lodLevel + 1 });
        }
    }

    void VansTerrain::CollectLeafNodes(const TerrainNode& node, const glm::vec3& camPos, std::vector<TerrainNode>& outNodes)
    {
        if (node.size <= m_PatchGridSize || !ShouldSplit(node, camPos))
        {
            outNodes.push_back(node);
            return;
        }

        float halfSize = node.size * 0.5f;
        CollectLeafNodes({ node.x, node.z, halfSize, node.lodLevel + 1 }, camPos, outNodes);
        CollectLeafNodes({ node.x + halfSize, node.z, halfSize, node.lodLevel + 1 }, camPos, outNodes);
        CollectLeafNodes({ node.x, node.z + halfSize, halfSize, node.lodLevel + 1 }, camPos, outNodes);
        CollectLeafNodes({ node.x + halfSize, node.z + halfSize, halfSize, node.lodLevel + 1 }, camPos, outNodes);
    }

    void VansTerrain::BalanceLeafNodes(std::vector<TerrainNode>& nodes)
    {
        const uint32_t maxIterations = 32;
        for (uint32_t iteration = 0; iteration < maxIterations; ++iteration)
        {
            bool changed = false;

            for (size_t i = 0; i < nodes.size() && !changed; ++i)
            {
                for (size_t j = i + 1; j < nodes.size() && !changed; ++j)
                {
                    if (!TerrainNodesAdjacent(nodes[i], nodes[j]))
                    {
                        continue;
                    }

                    int lodDiff = std::abs(nodes[i].lodLevel - nodes[j].lodLevel);
                    if (lodDiff <= 1)
                    {
                        continue;
                    }

                    size_t coarserIndex = nodes[i].lodLevel < nodes[j].lodLevel ? i : j;
                    TerrainNode coarserNode = nodes[coarserIndex];
                    if (coarserNode.size <= m_PatchGridSize)
                    {
                        continue;
                    }

                    nodes.erase(nodes.begin() + static_cast<std::ptrdiff_t>(coarserIndex));
                    TerrainSplitNode(coarserNode, nodes);
                    changed = true;
                }
            }

            if (!changed)
            {
                return;
            }
        }

        VANS_LOG_WARN("Terrain LOD 2:1 balance reached iteration limit, node count=" << nodes.size());
    }

    int VansTerrain::ComputeStitchFlags(const TerrainNode& node, const std::vector<TerrainNode>& nodes) const
    {
        int stitchFlags = 0;
        const float terrainMin = -m_TerrainSize * 0.5f;
        const float terrainMax = m_TerrainSize * 0.5f;

        for (const TerrainNode& neighbor : nodes)
        {
            if (&neighbor == &node || neighbor.lodLevel >= node.lodLevel)
            {
                continue;
            }

            if (node.x > terrainMin + TerrainLodEpsilon && std::abs((neighbor.x + neighbor.size) - node.x) < TerrainLodEpsilon &&
                TerrainRangesOverlap(node.z, node.z + node.size, neighbor.z, neighbor.z + neighbor.size))
            {
                stitchFlags |= 1;
            }

            if (node.x + node.size < terrainMax - TerrainLodEpsilon && std::abs((node.x + node.size) - neighbor.x) < TerrainLodEpsilon &&
                TerrainRangesOverlap(node.z, node.z + node.size, neighbor.z, neighbor.z + neighbor.size))
            {
                stitchFlags |= 2;
            }

            if (node.z > terrainMin + TerrainLodEpsilon && std::abs((neighbor.z + neighbor.size) - node.z) < TerrainLodEpsilon &&
                TerrainRangesOverlap(node.x, node.x + node.size, neighbor.x, neighbor.x + neighbor.size))
            {
                stitchFlags |= 4;
            }

            if (node.z + node.size < terrainMax - TerrainLodEpsilon && std::abs((node.z + node.size) - neighbor.z) < TerrainLodEpsilon &&
                TerrainRangesOverlap(node.x, node.x + node.size, neighbor.x, neighbor.x + neighbor.size))
            {
                stitchFlags |= 8;
            }
        }

        return stitchFlags;
    }

    void VansTerrain::AppendInstanceData(const std::vector<TerrainNode>& nodes)
    {
        // Legacy: builds full instance list for backward compat.
        // Near/far split is done in Update() directly.
        bool overflowWarned = false;
        for (const TerrainNode& node : nodes)
        {
            if (m_InstanceDataCPU.size() >= m_MaxPatchInstances)
            {
                if (!overflowWarned)
                {
                    VANS_LOG_WARN("Terrain patch instance count exceeds maxPatchInstances=" << m_MaxPatchInstances << ", extra patches skipped");
                    overflowWarned = true;
                }
                break;
            }

            TerrainInstanceData data;
            data.Offset = glm::vec2(node.x, node.z);
            data.Scale = node.size / static_cast<float>(m_PatchGridSize);
            data.Lod = static_cast<float>(node.lodLevel);
            data.StitchFlags = static_cast<float>(ComputeStitchFlags(node, nodes));
            m_InstanceDataCPU.push_back(data);
        }
    }

    void VansTerrain::Update(VansCamera* camera)
    {
        m_InstanceDataCPU.clear();
        m_NearInstanceDataCPU.clear();
        m_FarInstanceDataCPU.clear();

        TerrainNode root = { -m_TerrainSize*0.5f, -m_TerrainSize * 0.5f, m_TerrainSize, 0 };
        std::vector<TerrainNode> leafNodes;
        CollectLeafNodes(root, camera->GetPosition(), leafNodes);
        BalanceLeafNodes(leafNodes);

        const glm::vec3& camPos = camera->GetPosition();
        for (const TerrainNode& node : leafNodes)
        {
            float centerX = node.x + node.size * 0.5f;
            float centerZ = node.z + node.size * 0.5f;
            float dist = std::max(std::abs(centerX - camPos.x), std::abs(centerZ - camPos.z));

            TerrainInstanceData data;
            data.Offset      = glm::vec2(node.x, node.z);
            data.Scale       = node.size / static_cast<float>(m_PatchGridSize);
            data.Lod         = static_cast<float>(node.lodLevel);
            data.StitchFlags = static_cast<float>(ComputeStitchFlags(node, leafNodes));
            data.padding0    = glm::vec3(0.0);

            if (m_EnableTessellation && dist < m_TessellationDistance)
            {
                if (m_NearInstanceDataCPU.size() < m_MaxPatchInstances)
                    m_NearInstanceDataCPU.push_back(data);
            }
            else
            {
                if (m_FarInstanceDataCPU.size() < m_MaxPatchInstances)
                    m_FarInstanceDataCPU.push_back(data);
            }
        }

        // Upload to respective buffers
        if (!m_NearInstanceDataCPU.empty())
            m_NearInstanceBuffer.SetBufferData(m_NearInstanceDataCPU.data(), 0,
                sizeof(TerrainInstanceData) * m_NearInstanceDataCPU.size());
        if (!m_FarInstanceDataCPU.empty())
            m_FarInstanceBuffer.SetBufferData(m_FarInstanceDataCPU.data(), 0,
                sizeof(TerrainInstanceData) * m_FarInstanceDataCPU.size());

        // Legacy: full set (used by DrawShadow, DrawMotionVector which don't split)
        m_InstanceDataCPU = m_FarInstanceDataCPU;
        if (!m_InstanceDataCPU.empty())
            m_InstanceBuffer.SetBufferData(m_InstanceDataCPU.data(), 0,
                sizeof(TerrainInstanceData) * m_InstanceDataCPU.size());
    }

    void VansTerrain::Draw(VansVKCommandBuffer& cmd, GlobalStateData& globalState, std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets)
    {
        // Helper: bind mesh-level buffers (vertex, index, descriptions)
        auto bindMeshBuffers = [&]() {
            VkBuffer vertexBuffers[] = { m_BasePatchMesh->GetVertexBufferParameter().Buffer };
            VkDeviceSize offsets[] = { 0 };
            cmd.BindVertexBuffers(0, 1, vertexBuffers, offsets);
            cmd.BindIndexBuffer(m_BasePatchMesh->GetIndexBufferParameter().Buffer, 0, VK_INDEX_TYPE_UINT32);
            globalState.vertexInputAttributeDescriptions = &m_BasePatchMesh->m_VertexInputAttributeDescriptions;
            globalState.vertexInputBindingDescriptions = &m_BasePatchMesh->m_VertexInputBindingDescriptions;
        };

        // ---- 1. Far-field patches: existing VS pipeline (TRIANGLE_LIST) ----
        if (!m_FarInstanceDataCPU.empty())
        {
            bindMeshBuffers();

            VkBuffer instanceBuffers[] = { m_FarInstanceBuffer.GetNativeBuffer() };
            VkDeviceSize instanceOffsets[] = { 0 };
            cmd.BindVertexBuffers(1, 1, instanceBuffers, instanceOffsets);

            cmd.EnsureGraphicsShader(*m_TerrainShader, globalState, layouts);
            cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_TerrainShader, 0, sets, {});
            cmd.BindGraphicsPipeline(*m_TerrainShader->GetGraphicsPipeline());

            cmd.DrawIndexed(m_BasePatchMesh->GetIndexCount(),
                static_cast<uint32_t>(m_FarInstanceDataCPU.size()), 0, 0, 0);
        }

        // ---- 2. Near-field patches: tessellation pipeline (PATCH_LIST) ----
        if (m_EnableTessellation && !m_NearInstanceDataCPU.empty())
        {
            bindMeshBuffers();

            VkBuffer instanceBuffers[] = { m_NearInstanceBuffer.GetNativeBuffer() };
            VkDeviceSize instanceOffsets[] = { 0 };
            cmd.BindVertexBuffers(1, 1, instanceBuffers, instanceOffsets);

            cmd.EnsureGraphicsShader(*m_TerrainTessShader, globalState, layouts);
            cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_TerrainTessShader, 0, sets, {});
            cmd.BindGraphicsPipeline(*m_TerrainTessShader->GetGraphicsPipeline());

            cmd.DrawIndexed(m_BasePatchMesh->GetIndexCount(),
                static_cast<uint32_t>(m_NearInstanceDataCPU.size()), 0, 0, 0);
        }
    }
    void VansTerrain::DrawShadow(VansVKCommandBuffer& cmd, GlobalStateData& globalState, std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets)
    {
        if (m_InstanceDataCPU.empty()) return;

        // 4. 绑定 Vertex Buffer (Mesh) - Binding 0
        VkBuffer vertexBuffers[] = { m_BasePatchMesh->GetVertexBufferParameter().Buffer };
        VkDeviceSize offsets[] = { 0 };
        cmd.BindVertexBuffers(0, 1, vertexBuffers, offsets);

        // 5. 绑定 Instance Buffer - Binding 3 (对应 Shader 中的 layout location 3, 4)
        // 注意：这里需要你的Pipeline VertexInputState 定义了Binding 1 为Per-Instance Rate
        // 假设我们在Pipeline 创建时将 Binding 1 设为 Instance Input
        VkBuffer instanceBuffers[] = { m_InstanceBuffer.GetNativeBuffer() };
        VkDeviceSize instanceOffsets[] = { 0 };
        // 这里的binding index 取决于你的Pipeline 定义，通常 Mesh 是0，Instance 是1
        cmd.BindVertexBuffers(1, 1, instanceBuffers, instanceOffsets);

        // 6. 绑定 Index Buffer
        cmd.BindIndexBuffer(m_BasePatchMesh->GetIndexBufferParameter().Buffer, 0, VK_INDEX_TYPE_UINT32);

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
        cmd.DrawIndexed(m_BasePatchMesh->GetIndexCount(), static_cast<uint32_t>(m_InstanceDataCPU.size()), 0, 0, 0);
    }

    void VansTerrain::DrawMotionVector(VansVKCommandBuffer& cmd, GlobalStateData& globalState, std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets)
    {
        if (m_InstanceDataCPU.empty()) return;

        // Bind Vertex Buffer (Mesh) - Binding 0
        VkBuffer vertexBuffers[] = { m_BasePatchMesh->GetVertexBufferParameter().Buffer };
        VkDeviceSize offsets[] = { 0 };
        cmd.BindVertexBuffers(0, 1, vertexBuffers, offsets);

        // Bind Instance Buffer - Binding 1
        VkBuffer instanceBuffers[] = { m_InstanceBuffer.GetNativeBuffer() };
        VkDeviceSize instanceOffsets[] = { 0 };
        cmd.BindVertexBuffers(1, 1, instanceBuffers, instanceOffsets);

        // Bind Index Buffer
        cmd.BindIndexBuffer(m_BasePatchMesh->GetIndexBufferParameter().Buffer, 0, VK_INDEX_TYPE_UINT32);

        // Set vertex input descriptions
        globalState.vertexInputAttributeDescriptions = &m_BasePatchMesh->m_VertexInputAttributeDescriptions;
        globalState.vertexInputBindingDescriptions = &m_BasePatchMesh->m_VertexInputBindingDescriptions;

        // Apply motion vector shader
        cmd.EnsureGraphicsShader(*m_TerrainMotionVectorShader, globalState, layouts);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_TerrainMotionVectorShader, 0, sets, {});
        cmd.BindGraphicsPipeline(*m_TerrainMotionVectorShader->GetGraphicsPipeline());

        // Draw instanced
        cmd.DrawIndexed(m_BasePatchMesh->GetIndexCount(), static_cast<uint32_t>(m_InstanceDataCPU.size()), 0, 0, 0);
    }

    // ── Editor Inspector Setters ──────────────────────────────────────────
    // Each writes the member and immediately uploads the changed param to the UBO
    // so the GPU sees the updated value on the next frame.

    void VansTerrain::SetTessellationEnabled(bool v)
    {
        m_EnableTessellation = v;
    }

    void VansTerrain::SetTessellationDistance(float v)
    {
        m_TessellationDistance = std::max(v, 1.0f);
        TerrainTessellationParamsGPU p{};
        p.maxTessLevel = m_MaxTessellationLevel;
        p.tessDistance = m_TessellationDistance;
        p.tessPower    = m_TessellationPower;
        p.padding = 0.0f;  // 原 displacementStrength，现为 padding
        m_TessParamsUBO.SetBufferData(&p, 0, sizeof(p));
    }

    void VansTerrain::SetMaxTessellationLevel(float v)
    {
        m_MaxTessellationLevel = std::clamp(v, 1.0f, 64.0f);
        TerrainTessellationParamsGPU p{};
        p.maxTessLevel = m_MaxTessellationLevel;
        p.tessDistance = m_TessellationDistance;
        p.tessPower    = m_TessellationPower;
        p.padding = 0.0f;  // 原 displacementStrength，现为 padding
        m_TessParamsUBO.SetBufferData(&p, 0, sizeof(p));
    }

    void VansTerrain::SetTessellationPower(float v)
    {
        m_TessellationPower = std::max(v, 0.1f);
        TerrainTessellationParamsGPU p{};
        p.maxTessLevel = m_MaxTessellationLevel;
        p.tessDistance = m_TessellationDistance;
        p.tessPower    = m_TessellationPower;
        p.padding = 0.0f;  // 原 displacementStrength，现为 padding
        m_TessParamsUBO.SetBufferData(&p, 0, sizeof(p));
    }

    void VansTerrain::SetTessLodBias(float v)
    {
        m_TessLodBias = std::clamp(v, 0.1f, 5.0f);
    }

    void VansTerrain::SetTessDisplacementStrength(float v)
    {
        // no-op: 法线贴图 Y 位移已被程序化噪声替代
        // 使用 SetNoiseStrength() 代替
        m_TessDisplacementStrength = v;  // 仅存值，不写 UBO
    }

    // ──────────────────────────────────────────────────────────
    // 程序化噪声参数 Setter 实现
    // ──────────────────────────────────────────────────────────

    void VansTerrain::UpdateNoiseDetailUBO()
    {
        TerrainNoiseDetailParamsGPU p{};
        p.noiseStrength     = m_EnableNoiseDetail ? m_NoiseStrength : 0.0f;
        p.noiseFrequency    = m_NoiseFrequency;
        p.noiseLacunarity   = m_NoiseLacunarity;
        p.noiseGain         = m_NoiseGain;
        p.noiseOctaves      = m_NoiseOctaves;
        p.noiseWarpStrength = m_NoiseWarpStrength;
        p.fadeStart         = m_NoiseFadeStart;
        p.noisePadding      = 0.0f;
        m_NoiseDetailUBO.SetBufferData(&p, 0, sizeof(p));
    }

    void VansTerrain::SetNoiseDetailEnabled(bool v)
    {
        m_EnableNoiseDetail = v;
        UpdateNoiseDetailUBO();
    }

    void VansTerrain::SetNoiseStrength(float v)
    {
        m_NoiseStrength = std::max(v, 0.0f);
        UpdateNoiseDetailUBO();
    }

    void VansTerrain::SetNoiseFrequency(float v)
    {
        m_NoiseFrequency = std::max(v, 0.01f);
        UpdateNoiseDetailUBO();
    }

    void VansTerrain::SetNoiseLacunarity(float v)
    {
        m_NoiseLacunarity = std::max(v, 1.0f);
        UpdateNoiseDetailUBO();
    }

    void VansTerrain::SetNoiseGain(float v)
    {
        m_NoiseGain = std::clamp(v, 0.01f, 1.0f);
        UpdateNoiseDetailUBO();
    }

    void VansTerrain::SetNoiseOctaves(int v)
    {
        m_NoiseOctaves = std::clamp(v, 1, 8);
        UpdateNoiseDetailUBO();
    }

    void VansTerrain::SetNoiseWarpStrength(float v)
    {
        m_NoiseWarpStrength = std::max(v, 0.0f);
        UpdateNoiseDetailUBO();
    }

    void VansTerrain::SetNoiseFadeStart(float v)
    {
        m_NoiseFadeStart = std::clamp(v, 0.0f, 1.0f);
        UpdateNoiseDetailUBO();
    }
}