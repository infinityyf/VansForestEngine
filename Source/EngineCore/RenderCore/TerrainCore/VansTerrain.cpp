#include "VansTerrain.h"
#include "../VansShaderManager.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../../Configration/VansConfigration.h"
#include "../../Util/VansLog.h"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <cstddef>
#include <glm/gtc/packing.hpp>

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
		// Shader programs are owned by VansShaderManager.
		m_TerrainShader = nullptr;
		m_TerrainShadowShader = nullptr;
		m_TerrainMotionVectorShader = nullptr;
		m_TerrainTessShader = nullptr;
        m_ParamsUBO.DestroyVulkanBuffer(m_Device->GetLogicDevice());
        m_InstanceBuffer.DestroyVulkanBuffer(m_Device->GetLogicDevice());
        m_TessParamsUBO.DestroyVulkanBuffer(m_Device->GetLogicDevice());
        m_NoiseDetailUBO.DestroyVulkanBuffer(m_Device->GetLogicDevice());

        // 释放地形专属描述符集和布局。
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

        // 细分参数
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
        // 1. 加载高度图
        // -------------------------------------------------------
        auto loadTexture = [&](VansTexture* texture,
            const std::string& path,
            bool isSRGB,
            bool useCompress,
            bool needMip = false,
            TexturePrecision precision = LOW_PRES_8,
            int importChannel = 4,
            VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT)
        {
            VansTexture::TextureLoadDesc desc{};
            desc.path = path;
            desc.isSRGB = isSRGB;
            desc.useCompress = useCompress;
            desc.needMip = needMip;
            desc.precision = precision;
            desc.importChannel = importChannel;
            desc.addressMode = addressMode;
            texture->LoadTexture(device->GetCommandBuffer(), desc);
        };

        m_HeightMap = new VansTexture();
        loadTexture(m_HeightMap, config.heightmapPath, false, false, false, MID_PRES_16, 1, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

        // -------------------------------------------------------
        // 2. 加载 splatmap
        // -------------------------------------------------------
        m_Splatmap0 = new VansTexture();
        loadTexture(m_Splatmap0, config.splatmap0Path, false, false, false, LOW_PRES_8, 4, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

        m_Splatmap1 = new VansTexture();
        loadTexture(m_Splatmap1, config.splatmap1Path, false, false, false, LOW_PRES_8, 4, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

        // -------------------------------------------------------
        // 3. 加载各地表层的 PBR 贴图
        // -------------------------------------------------------
        m_LayerCount = static_cast<uint32_t>(std::min(config.layers.size(), (size_t)TERRAIN_MAX_LAYERS));

        // 如果场景纹理管理器已经预加载贴图，则直接借用。
        bool hasPreloaded = (m_LayerCount > 0 && config.layers[0].albedoTex != nullptr);
        m_OwnsLayerTextures = !hasPreloaded;

        for (uint32_t i = 0; i < m_LayerCount; ++i)
        {
            const auto& layer = config.layers[i];
            if (layer.albedoTex && layer.normalTex && layer.roughnessTex)
            {
                // 使用预加载贴图，不接管生命周期。
                m_LayerAlbedos[i]   = layer.albedoTex;
                m_LayerNormals[i]   = layer.normalTex;
                m_LayerRoughness[i] = layer.roughnessTex;
            }
            else
            {
                // 未预加载时退回到路径加载。
                m_LayerAlbedos[i] = new VansTexture();
                loadTexture(m_LayerAlbedos[i], layer.albedoPath, true, true);

                m_LayerNormals[i] = new VansTexture();
                loadTexture(m_LayerNormals[i], layer.normalPath, false, true);

                m_LayerRoughness[i] = new VansTexture();
                loadTexture(m_LayerRoughness[i], layer.roughnessPath, false, true);
            }
        }

        // -------------------------------------------------------
        // 4. 创建 CDLOD 基础 patch 网格（16x16 单元）
        // -------------------------------------------------------
		auto vansConfigration = VansConfigration::GetInstance();
		std::string projectRoot = vansConfigration->GetProjectRootPath();
        BuildPatchMesh();

        // 实例输入描述
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
        // 5. 创建实例缓冲；GBuffer 会按近场细分和远场非细分分流。
        // -------------------------------------------------------
        EnsureInstanceBufferCapacity(m_MaxPatchInstances);

        // -------------------------------------------------------
        // 6. 创建地形参数 UBO
        // -------------------------------------------------------
        TerrainParamsGPU params{};
        params.layerCountPacked.x = static_cast<int>(m_LayerCount);
        // std140 中 float 数组按 vec4 步长对齐。
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
        // 7. 获取统一管理的着色器程序
        // -------------------------------------------------------
        auto& shaderManager = VansShaderManager::Get();
        m_TerrainShader = shaderManager.FindGraphicsShader("Terrain");
        m_TerrainShadowShader = shaderManager.FindGraphicsShader("TerrainShadow");
        m_TerrainMotionVectorShader = shaderManager.FindGraphicsShader("TerrainMotionVector");

        // -------------------------------------------------------
        // 7b. 创建细分地形着色器
        // -------------------------------------------------------
        m_TerrainTessShader = shaderManager.FindGraphicsShader("TerrainTess");
        if (!m_TerrainShader || !m_TerrainShadowShader ||
            !m_TerrainMotionVectorShader || !m_TerrainTessShader)
        {
            VANS_LOG_ERROR("[Terrain] One or more managed terrain shaders are unavailable");
            return;
        }

        // -------------------------------------------------------
        // 7c. 创建细分参数 UBO（binding 7）
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
        // 7d. 创建噪声细节参数 UBO（binding 8）
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
        // 8. 创建描述符集
        // -------------------------------------------------------
        VansDescriptorSetLayoutFactory::CreateAndAllocate_Terrain(m_DescriptorSetLayout, m_DescriptorSets, 1);

        auto* descMgr = VansVKDescriptorManager::GetInstance();
        descMgr->BeginDescriptorUpdate();

        // 绑定 0：高度图
        descMgr->WriteImageDescriptor(
            m_DescriptorSets[0], TERRAIN_BINDING_HEIGHT_MAP,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{ m_HeightMap->GetImage().GetSampler(), m_HeightMap->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});

        // 绑定 1：splatMap0
        descMgr->WriteImageDescriptor(
            m_DescriptorSets[0], TERRAIN_BINDING_SPLATMAP_0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{ m_Splatmap0->GetImage().GetSampler(), m_Splatmap0->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});

        // 绑定 2：splatMap1
        descMgr->WriteImageDescriptor(
            m_DescriptorSets[0], TERRAIN_BINDING_SPLATMAP_1,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{ m_Splatmap1->GetImage().GetSampler(), m_Splatmap1->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});

        // 绑定 3：albedo 数组 [8]
        {
            std::vector<VkDescriptorImageInfo> albedoInfos(TERRAIN_MAX_LAYERS);
            for (uint32_t i = 0; i < TERRAIN_MAX_LAYERS; ++i)
            {
                VansTexture* tex = (i < m_LayerCount) ? m_LayerAlbedos[i] : m_LayerAlbedos[0];
                albedoInfos[i] = { tex->GetImage().GetSampler(), tex->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            }
            descMgr->WriteImageDescriptor(
                m_DescriptorSets[0], TERRAIN_BINDING_ALBEDO_ARRAY,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, albedoInfos);
        }

        // 绑定 4：normal 数组 [8]
        {
            std::vector<VkDescriptorImageInfo> normalInfos(TERRAIN_MAX_LAYERS);
            for (uint32_t i = 0; i < TERRAIN_MAX_LAYERS; ++i)
            {
                VansTexture* tex = (i < m_LayerCount) ? m_LayerNormals[i] : m_LayerNormals[0];
                normalInfos[i] = { tex->GetImage().GetSampler(), tex->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            }
            descMgr->WriteImageDescriptor(
                m_DescriptorSets[0], TERRAIN_BINDING_NORMAL_ARRAY,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, normalInfos);
        }

        // 绑定 5：roughness 数组 [8]
        {
            std::vector<VkDescriptorImageInfo> roughInfos(TERRAIN_MAX_LAYERS);
            for (uint32_t i = 0; i < TERRAIN_MAX_LAYERS; ++i)
            {
                VansTexture* tex = (i < m_LayerCount) ? m_LayerRoughness[i] : m_LayerRoughness[0];
                roughInfos[i] = { tex->GetImage().GetSampler(), tex->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            }
            descMgr->WriteImageDescriptor(
                m_DescriptorSets[0], TERRAIN_BINDING_ROUGHNESS_ARRAY,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, roughInfos);
        }

        // 绑定 6：地形参数 UBO
        descMgr->WriteBufferDescriptor(
            m_DescriptorSets[0], TERRAIN_BINDING_PARAMS_UBO,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            {{ m_ParamsUBO.GetNativeBuffer(), 0, sizeof(TerrainParamsGPU) }});

        // 绑定 7：细分参数 UBO（TCS 与 TES 读取）
        descMgr->WriteBufferDescriptor(
            m_DescriptorSets[0], TERRAIN_BINDING_TESSELLATION_PARAMS,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            {{ m_TessParamsUBO.GetNativeBuffer(), 0, sizeof(TerrainTessellationParamsGPU) }});

        // 绑定 8：噪声细节参数 UBO（TES 与 FS 读取）
        descMgr->WriteBufferDescriptor(
            m_DescriptorSets[0], TERRAIN_BINDING_NOISE_DETAIL_PARAMS,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            {{ m_NoiseDetailUBO.GetNativeBuffer(), 0, sizeof(TerrainNoiseDetailParamsGPU) }});

        descMgr->CommitDescriptorUpdates();
    }

    void VansTerrain::BuildPatchMesh()
    {
        delete m_BasePatchMesh;
        m_BasePatchMesh = new VansMesh();

        const int dim = m_PatchGridSize + 1;
        std::vector<TerrainPatchVertex> vertices;
        vertices.reserve(static_cast<size_t>(dim * dim));

        std::vector<float> rawPositions;
        rawPositions.reserve(static_cast<size_t>(dim * dim * 8));

        for (int z = 0; z < dim; ++z)
        {
            for (int x = 0; x < dim; ++x)
            {
                const float fx = static_cast<float>(x);
                const float fz = static_cast<float>(z);
                const float u = fx / static_cast<float>(m_PatchGridSize);
                const float v = fz / static_cast<float>(m_PatchGridSize);

                TerrainPatchVertex vertex{};
                vertex.position[0] = glm::packHalf1x16(fx);
                vertex.position[1] = glm::packHalf1x16(0.0f);
                vertex.position[2] = glm::packHalf1x16(fz);
                vertex.uv[0] = glm::packHalf1x16(u);
                vertex.uv[1] = glm::packHalf1x16(v);
                vertex.normal[0] = glm::packHalf1x16(0.0f);
                vertex.normal[1] = glm::packHalf1x16(1.0f);
                vertex.normal[2] = glm::packHalf1x16(0.0f);
                vertices.push_back(vertex);

                rawPositions.push_back(fx);
                rawPositions.push_back(0.0f);
                rawPositions.push_back(fz);
                rawPositions.push_back(0.0f);
                rawPositions.push_back(0.0f);
                rawPositions.push_back(1.0f);
                rawPositions.push_back(0.0f);
                rawPositions.push_back(0.0f);
            }
        }

        std::vector<uint32_t> indices;
        indices.reserve(static_cast<size_t>(m_PatchGridSize * m_PatchGridSize * 6));
        for (int z = 0; z < m_PatchGridSize; ++z)
        {
            for (int x = 0; x < m_PatchGridSize; ++x)
            {
                const uint32_t tl = static_cast<uint32_t>(z * dim + x);
                const uint32_t tr = tl + 1;
                const uint32_t bl = static_cast<uint32_t>((z + 1) * dim + x);
                const uint32_t br = bl + 1;
                indices.push_back(tl);
                indices.push_back(bl);
                indices.push_back(tr);
                indices.push_back(tr);
                indices.push_back(bl);
                indices.push_back(br);
            }
        }

        std::vector<VkVertexInputBindingDescription> bindings =
        {
            { 0, sizeof(TerrainPatchVertex), VK_VERTEX_INPUT_RATE_VERTEX }
        };
        std::vector<VkVertexInputAttributeDescription> attributes =
        {
            { 0, 0, VK_FORMAT_R16G16B16_SFLOAT, offsetof(TerrainPatchVertex, position) },
            { 1, 0, VK_FORMAT_R16G16_SFLOAT,    offsetof(TerrainPatchVertex, uv) },
            { 2, 0, VK_FORMAT_R16G16B16_SFLOAT, offsetof(TerrainPatchVertex, normal) }
        };

        m_BasePatchMesh->InitFromRawData(
            m_Device->GetLogicDevice(),
            vertices.data(),
            static_cast<uint32_t>(vertices.size()),
            sizeof(TerrainPatchVertex),
            indices.data(),
            static_cast<uint32_t>(indices.size()),
            bindings,
            attributes,
            rawPositions);

        VANS_LOG("[Terrain] Procedural CDLOD patch mesh built: grid=" << m_PatchGridSize
            << " vertices=" << vertices.size()
            << " indices=" << indices.size());
    }

    float VansTerrain::GetMinPatchWorldSize() const
    {
        return static_cast<float>(m_PatchGridSize);
    }

    void VansTerrain::EnsureInstanceBufferCapacity(uint32_t requiredCapacity)
    {
        requiredCapacity = std::max(requiredCapacity, 1u);
        if (requiredCapacity <= m_InstanceBufferCapacity)
        {
            return;
        }

        uint32_t newCapacity = std::max(m_InstanceBufferCapacity, 1u);
        while (newCapacity < requiredCapacity)
        {
            newCapacity *= 2u;
        }

        if (m_InstanceBufferCapacity > 0)
        {
            m_InstanceBuffer.DestroyVulkanBuffer(m_Device->GetLogicDevice());
        }

        const VkDeviceSize bufferSize = sizeof(TerrainInstanceData) * newCapacity;
        m_InstanceBuffer.CreatVulkanBuffer(
            m_Device->GetLogicDevice(), bufferSize, VK_FORMAT_R32_SFLOAT,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        m_InstanceBufferCapacity = newCapacity;
        VANS_LOG("[Terrain] 实例缓冲容量调整为 " << m_InstanceBufferCapacity);
    }

    bool VansTerrain::ShouldSplit(const TerrainNode& node, const glm::vec3& camPos)
    {
        const float minPatchSize = GetMinPatchWorldSize();
        if (node.size <= minPatchSize + 0.001f)
        {
            return false;
        }

        const float maxPatchSize = minPatchSize * std::max(m_LodDistanceRatio, 1.0f) * 2.0f;
        if (node.size > maxPatchSize)
        {
            return true;
        }

        const float camX = camPos.x;
        const float camZ = camPos.z;
        const float dx = std::max(std::max(node.x - camX, 0.0f), camX - (node.x + node.size));
        const float dz = std::max(std::max(node.z - camZ, 0.0f), camZ - (node.z + node.size));
        const float dist = std::max(dx, dz);

        float effectiveSplitMult = m_SplitDistMult;

        // 细分范围内放宽 CPU 切分条件，用更大的 patch 降低实例数量；
        // 近场几何细节由 GPU tessellation 补足。
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
        if (!ShouldSplit(node, camPos))
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
                    if (coarserNode.size <= GetMinPatchWorldSize() + TerrainLodEpsilon)
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

    VansTerrain::TerrainLodGrid VansTerrain::BuildLodGrid(const std::vector<TerrainNode>& nodes) const
    {
        TerrainLodGrid grid;
        grid.terrainMin = -m_TerrainSize * 0.5f;
        grid.cellSize = GetMinPatchWorldSize();
        grid.cellCount = std::max(1u, static_cast<uint32_t>(std::ceil(m_TerrainSize / grid.cellSize)));
        grid.lodByCell.assign(static_cast<size_t>(grid.cellCount) * grid.cellCount, -1);

        auto toCellFloor = [&](float value)
        {
            const float local = (value - grid.terrainMin) / grid.cellSize;
            return std::clamp(static_cast<int>(std::floor(local + TerrainLodEpsilon)), 0, static_cast<int>(grid.cellCount));
        };
        auto toCellCeil = [&](float value)
        {
            const float local = (value - grid.terrainMin) / grid.cellSize;
            return std::clamp(static_cast<int>(std::ceil(local - TerrainLodEpsilon)), 0, static_cast<int>(grid.cellCount));
        };

        for (const TerrainNode& node : nodes)
        {
            const int x0 = toCellFloor(node.x);
            const int x1 = toCellCeil(node.x + node.size);
            const int z0 = toCellFloor(node.z);
            const int z1 = toCellCeil(node.z + node.size);
            for (int z = z0; z < z1; ++z)
            for (int x = x0; x < x1; ++x)
            {
                grid.lodByCell[static_cast<size_t>(z) * grid.cellCount + static_cast<size_t>(x)] = node.lodLevel;
            }
        }

        return grid;
    }

    int VansTerrain::ComputeStitchFlags(const TerrainNode& node, const TerrainLodGrid& lodGrid) const
    {
        int stitchFlags = 0;
        if (lodGrid.cellCount == 0 || lodGrid.lodByCell.empty())
            return stitchFlags;

        const float terrainMin = lodGrid.terrainMin;
        const float terrainMax = lodGrid.terrainMin + m_TerrainSize;

        auto toCellFloor = [&](float value)
        {
            const float local = (value - lodGrid.terrainMin) / lodGrid.cellSize;
            return std::clamp(static_cast<int>(std::floor(local + TerrainLodEpsilon)), 0, static_cast<int>(lodGrid.cellCount));
        };
        auto toCellCeil = [&](float value)
        {
            const float local = (value - lodGrid.terrainMin) / lodGrid.cellSize;
            return std::clamp(static_cast<int>(std::ceil(local - TerrainLodEpsilon)), 0, static_cast<int>(lodGrid.cellCount));
        };
        auto sampleLod = [&](int x, int z)
        {
            if (x < 0 || z < 0 || x >= static_cast<int>(lodGrid.cellCount) || z >= static_cast<int>(lodGrid.cellCount))
                return -1;
            return lodGrid.lodByCell[static_cast<size_t>(z) * lodGrid.cellCount + static_cast<size_t>(x)];
        };
        auto anyCoarserAlongZ = [&](int xCell, int z0, int z1)
        {
            for (int z = z0; z < z1; ++z)
            {
                const int neighborLod = sampleLod(xCell, z);
                if (neighborLod >= 0 && neighborLod < node.lodLevel)
                    return true;
            }
            return false;
        };
        auto anyCoarserAlongX = [&](int zCell, int x0, int x1)
        {
            for (int x = x0; x < x1; ++x)
            {
                const int neighborLod = sampleLod(x, zCell);
                if (neighborLod >= 0 && neighborLod < node.lodLevel)
                    return true;
            }
            return false;
        };

        const int x0 = toCellFloor(node.x);
        const int x1 = toCellCeil(node.x + node.size);
        const int z0 = toCellFloor(node.z);
        const int z1 = toCellCeil(node.z + node.size);

        if (node.x > terrainMin + TerrainLodEpsilon && anyCoarserAlongZ(x0 - 1, z0, z1))
            stitchFlags |= 1;

        if (node.x + node.size < terrainMax - TerrainLodEpsilon && anyCoarserAlongZ(x1, z0, z1))
            stitchFlags |= 2;

        if (node.z > terrainMin + TerrainLodEpsilon && anyCoarserAlongX(z0 - 1, x0, x1))
            stitchFlags |= 4;

        if (node.z + node.size < terrainMax - TerrainLodEpsilon && anyCoarserAlongX(z1, x0, x1))
            stitchFlags |= 8;

        return stitchFlags;
    }

    VansTerrain::TerrainLodBuildSignature VansTerrain::BuildLodSignature(const glm::vec3& cameraPosition) const
    {
        const float cameraCellSize = std::max(1.0f, GetMinPatchWorldSize() * 0.5f);
        TerrainLodBuildSignature signature;
        signature.cameraCellX = static_cast<int64_t>(std::floor(cameraPosition.x / cameraCellSize));
        signature.cameraCellZ = static_cast<int64_t>(std::floor(cameraPosition.z / cameraCellSize));
        signature.tessellationEnabled = m_EnableTessellation;
        signature.splitDistMult = m_SplitDistMult;
        signature.lodDistanceRatio = m_LodDistanceRatio;
        signature.tessellationDistance = m_TessellationDistance;
        signature.tessLodBias = m_TessLodBias;
        return signature;
    }

    bool VansTerrain::CanReuseLodResult(const TerrainLodBuildSignature& signature) const
    {
        return m_LodCacheValid &&
            signature.cameraCellX == m_LastLodSignature.cameraCellX &&
            signature.cameraCellZ == m_LastLodSignature.cameraCellZ &&
            signature.tessellationEnabled == m_LastLodSignature.tessellationEnabled &&
            signature.splitDistMult == m_LastLodSignature.splitDistMult &&
            signature.lodDistanceRatio == m_LastLodSignature.lodDistanceRatio &&
            signature.tessellationDistance == m_LastLodSignature.tessellationDistance &&
            signature.tessLodBias == m_LastLodSignature.tessLodBias;
    }

    void VansTerrain::Update(VansCamera* camera)
    {
        if (!camera)
            return;

        const TerrainLodBuildSignature signature = BuildLodSignature(camera->GetPosition());
        if (CanReuseLodResult(signature))
            return;

        m_InstanceDataCPU.clear();
        m_FarInstanceCount = 0;
        m_NearInstanceCount = 0;

        TerrainNode root = { -m_TerrainSize * 0.5f, -m_TerrainSize * 0.5f, m_TerrainSize, 0 };
        std::vector<TerrainNode> leafNodes;
        CollectLeafNodes(root, camera->GetPosition(), leafNodes);
        BalanceLeafNodes(leafNodes);
        const TerrainLodGrid lodGrid = BuildLodGrid(leafNodes);

        const glm::vec3& camPos = camera->GetPosition();
        std::vector<TerrainInstanceData> farInstances;
        std::vector<TerrainInstanceData> nearInstances;
        farInstances.reserve(leafNodes.size());
        nearInstances.reserve(leafNodes.size());
        for (const TerrainNode& node : leafNodes)
        {
            float centerX = node.x + node.size * 0.5f;
            float centerZ = node.z + node.size * 0.5f;
            float dist = std::max(std::abs(centerX - camPos.x), std::abs(centerZ - camPos.z));

            TerrainInstanceData data;
            data.Offset      = glm::vec2(node.x, node.z);
            data.Scale       = node.size / static_cast<float>(m_PatchGridSize);
            data.Lod         = static_cast<float>(node.lodLevel);
            data.StitchFlags = static_cast<float>(ComputeStitchFlags(node, lodGrid));
            data.padding0    = glm::vec3(0.0);

            if (m_EnableTessellation && dist < m_TessellationDistance)
            {
                nearInstances.push_back(data);
            }
            else
            {
                farInstances.push_back(data);
            }
        }

        m_FarInstanceCount = static_cast<uint32_t>(farInstances.size());
        m_NearInstanceCount = static_cast<uint32_t>(nearInstances.size());
        m_InstanceDataCPU.reserve(farInstances.size() + nearInstances.size());
        m_InstanceDataCPU.insert(m_InstanceDataCPU.end(), farInstances.begin(), farInstances.end());
        m_InstanceDataCPU.insert(m_InstanceDataCPU.end(), nearInstances.begin(), nearInstances.end());

        EnsureInstanceBufferCapacity(static_cast<uint32_t>(m_InstanceDataCPU.size()));
        if (!m_InstanceDataCPU.empty())
            m_InstanceBuffer.SetBufferData(m_InstanceDataCPU.data(), 0,
                sizeof(TerrainInstanceData) * m_InstanceDataCPU.size());

        m_LastLodSignature = signature;
        m_LodCacheValid = true;
    }

    void VansTerrain::Draw(VansVKCommandBuffer& cmd, GlobalStateData& globalState, std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets)
    {
        // 绑定基础 patch 的顶点、索引和输入描述。
        auto bindMeshBuffers = [&]() {
            VkBuffer vertexBuffers[] = { m_BasePatchMesh->GetVertexBufferParameter().Buffer };
            VkDeviceSize offsets[] = { 0 };
            cmd.BindVertexBuffers(0, 1, vertexBuffers, offsets);
            cmd.BindIndexBuffer(m_BasePatchMesh->GetIndexBufferParameter().Buffer, 0, VK_INDEX_TYPE_UINT32);
            globalState.vertexInputAttributeDescriptions = &m_BasePatchMesh->m_VertexInputAttributeDescriptions;
            globalState.vertexInputBindingDescriptions = &m_BasePatchMesh->m_VertexInputBindingDescriptions;
        };

        // 1. 远场 patch：普通 VS 管线（TRIANGLE_LIST），使用单实例缓冲的前半段。
        if (m_FarInstanceCount > 0)
        {
            bindMeshBuffers();

            VkBuffer instanceBuffers[] = { m_InstanceBuffer.GetNativeBuffer() };
            VkDeviceSize instanceOffsets[] = { 0 };
            cmd.BindVertexBuffers(1, 1, instanceBuffers, instanceOffsets);

            cmd.EnsureGraphicsShader(*m_TerrainShader, globalState, layouts);
            cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_TerrainShader, 0, sets, {});
            cmd.BindGraphicsPipeline(*m_TerrainShader->GetGraphicsPipeline());

            cmd.DrawIndexed(m_BasePatchMesh->GetIndexCount(),
                m_FarInstanceCount, 0, 0, 0);
        }

        // 2. 近场 patch：tessellation 管线（PATCH_LIST），通过 firstInstance 读取单缓冲后半段。
        if (m_EnableTessellation && m_NearInstanceCount > 0)
        {
            bindMeshBuffers();

            VkBuffer instanceBuffers[] = { m_InstanceBuffer.GetNativeBuffer() };
            VkDeviceSize instanceOffsets[] = { 0 };
            cmd.BindVertexBuffers(1, 1, instanceBuffers, instanceOffsets);

            cmd.EnsureGraphicsShader(*m_TerrainTessShader, globalState, layouts);
            cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_TerrainTessShader, 0, sets, {});
            cmd.BindGraphicsPipeline(*m_TerrainTessShader->GetGraphicsPipeline());

            cmd.DrawIndexed(m_BasePatchMesh->GetIndexCount(),
                m_NearInstanceCount, 0, 0, m_FarInstanceCount);
        }
    }
    void VansTerrain::DrawShadow(VansVKCommandBuffer& cmd, GlobalStateData& globalState, std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets)
    {
        if (m_InstanceDataCPU.empty()) return;

        // 4. 绑定顶点缓冲（网格）到绑定槽 0。
        VkBuffer vertexBuffers[] = { m_BasePatchMesh->GetVertexBufferParameter().Buffer };
        VkDeviceSize offsets[] = { 0 };
        cmd.BindVertexBuffers(0, 1, vertexBuffers, offsets);

        // 5. 绑定实例缓冲；管线顶点输入已将绑定槽 1 配置为逐实例读取。
        VkBuffer instanceBuffers[] = { m_InstanceBuffer.GetNativeBuffer() };
        VkDeviceSize instanceOffsets[] = { 0 };
        // 网格使用绑定槽 0，实例数据使用绑定槽 1。
        cmd.BindVertexBuffers(1, 1, instanceBuffers, instanceOffsets);

        // 6. 绑定索引缓冲。
        cmd.BindIndexBuffer(m_BasePatchMesh->GetIndexBufferParameter().Buffer, 0, VK_INDEX_TYPE_UINT32);

        //记录mesh 的bind data，这里需要手动设置index 的input 描述
        globalState.vertexInputAttributeDescriptions = &m_BasePatchMesh->m_VertexInputAttributeDescriptions;
        globalState.vertexInputBindingDescriptions = &m_BasePatchMesh->m_VertexInputBindingDescriptions;

        //apply shader，确认pipeline以及创建完毕
        cmd.EnsureGraphicsShader(*m_TerrainShadowShader, globalState, layouts);

        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_TerrainShadowShader, 0, sets, {});


        cmd.BindGraphicsPipeline(*m_TerrainShadowShader->GetGraphicsPipeline());

        // 向地形阴影 shader 写入级联索引。
        if (m_TerrainShadowShader->GetPushConstantSize() > 0)
        {
            int cascadeIndex = globalState.cascadeIndex;
            cmd.UpdatePushConstants(*m_TerrainShadowShader->GetGraphicsPipeline(),
                VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(int), &cascadeIndex);
        }

        // 7. 执行实例化索引绘制。
        cmd.DrawIndexed(m_BasePatchMesh->GetIndexCount(), static_cast<uint32_t>(m_InstanceDataCPU.size()), 0, 0, 0);
    }

    void VansTerrain::DrawMotionVector(VansVKCommandBuffer& cmd, GlobalStateData& globalState, std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets)
    {
        if (m_InstanceDataCPU.empty()) return;

        // 绑定基础 patch 顶点缓冲。
        VkBuffer vertexBuffers[] = { m_BasePatchMesh->GetVertexBufferParameter().Buffer };
        VkDeviceSize offsets[] = { 0 };
        cmd.BindVertexBuffers(0, 1, vertexBuffers, offsets);

        // 绑定实例缓冲。
        VkBuffer instanceBuffers[] = { m_InstanceBuffer.GetNativeBuffer() };
        VkDeviceSize instanceOffsets[] = { 0 };
        cmd.BindVertexBuffers(1, 1, instanceBuffers, instanceOffsets);

        // 绑定索引缓冲。
        cmd.BindIndexBuffer(m_BasePatchMesh->GetIndexBufferParameter().Buffer, 0, VK_INDEX_TYPE_UINT32);

        // 设置顶点输入描述。
        globalState.vertexInputAttributeDescriptions = &m_BasePatchMesh->m_VertexInputAttributeDescriptions;
        globalState.vertexInputBindingDescriptions = &m_BasePatchMesh->m_VertexInputBindingDescriptions;

        // 应用 motion vector shader。
        cmd.EnsureGraphicsShader(*m_TerrainMotionVectorShader, globalState, layouts);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_TerrainMotionVectorShader, 0, sets, {});
        cmd.BindGraphicsPipeline(*m_TerrainMotionVectorShader->GetGraphicsPipeline());

        // 执行实例化绘制。
        cmd.DrawIndexed(m_BasePatchMesh->GetIndexCount(), static_cast<uint32_t>(m_InstanceDataCPU.size()), 0, 0, 0);
    }

    // ── 编辑器 Inspector Setter ──────────────────────────────────────────
    // 每次修改都会同步写入 UBO，保证下一帧 GPU 读取到最新参数。

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
