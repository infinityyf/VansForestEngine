#include "VansTerrain.h"

#include "../VansRenderBounds.h"
#include "../VansShaderManager.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../../Util/VansLog.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <glm/gtc/packing.hpp>
#include <stdexcept>

namespace VansGraphics
{
    namespace
    {
        bool IsFinitePositive(float value)
        {
            return std::isfinite(value) && value > 0.0f;
        }

        bool HasCompleteLayer(const TerrainLayerConfig& layer)
        {
            return layer.albedo && layer.normal && layer.roughness;
        }
    }

    VansTerrain::~VansTerrain()
    {
        delete m_BasePatchMesh;
        delete m_HeightMap;
        delete m_Splatmap0;
        delete m_Splatmap1;

        m_TerrainShader = nullptr;
        m_TerrainShadowShader = nullptr;
        m_TerrainTessShader = nullptr;

        if (m_Device)
        {
            VkDevice device = m_Device->GetLogicDevice();
            m_ParamsUBO.DestroyVulkanBuffer(device);
            m_InstanceBuffer.DestroyVulkanBuffer(device);
            m_TessParamsUBO.DestroyVulkanBuffer(device);
            m_HeightDetailUBO.DestroyVulkanBuffer(device);
        }

        if (m_DescriptorSetLayout != VK_NULL_HANDLE)
        {
            auto* descriptorManager = VansVKDescriptorManager::GetInstance();
            descriptorManager->DestroyDescriptorSet(m_DescriptorSets);
            descriptorManager->ReleaseDescriptorSetLayout(m_DescriptorSetLayout);
        }
    }

    void VansTerrain::ValidateConfig(const TerrainConfig& config) const
    {
        if (!config.assetGuid.IsValid() || !config.asset || !config.asset->HasPixelData())
            throw std::invalid_argument("Terrain requires a complete immutable terrain asset snapshot.");
        if (config.layers.empty() || config.layers.size() > TERRAIN_MAX_LAYERS)
            throw std::invalid_argument("Terrain requires between one and eight complete material layers.");
        const Vans::VansTerrainAssetSettings& settings = config.asset->settings;
        if (!IsFinitePositive(settings.terrainSize) || !IsFinitePositive(settings.maxHeight))
            throw std::invalid_argument("Terrain size and maximum height must be finite positive values.");
        if (!std::isfinite(settings.heightOffset) || !IsFinitePositive(settings.lodBaseDistance) ||
            !std::isfinite(settings.lodRangeRatio) || settings.lodRangeRatio < 2.0f ||
            !std::isfinite(settings.morphStartRatio) || settings.morphStartRatio <= 0.0f || settings.morphStartRatio >= 1.0f)
        {
            throw std::invalid_argument("Terrain CDLOD settings are invalid.");
        }
        if (!IsFinitePositive(settings.tessellationDistance) || !IsFinitePositive(settings.maxTessellationLevel) ||
            settings.maxTessellationLevel > 64.0f || !IsFinitePositive(settings.tessellationTargetPixels))
        {
            throw std::invalid_argument("Terrain tessellation settings are invalid.");
        }
        if (!std::isfinite(settings.heightDetailStrength) || settings.heightDetailStrength < 0.0f ||
            !std::isfinite(settings.heightDetailFadeStart) || settings.heightDetailFadeStart < 0.0f || settings.heightDetailFadeStart >= 1.0f)
        {
            throw std::invalid_argument("Terrain material height detail settings are invalid.");
        }
        const auto& wetness=settings.riverWetness;
        if (!std::isfinite(wetness.albedoScale) || wetness.albedoScale<0 || wetness.albedoScale>1 ||
            !std::isfinite(wetness.roughness) || wetness.roughness<0 || wetness.roughness>1 ||
            !std::isfinite(wetness.detailNormalScale) || wetness.detailNormalScale<0 || wetness.detailNormalScale>1)
        {
            throw std::invalid_argument("Terrain river wetness material settings are invalid.");
        }

        for (const TerrainLayerConfig& layer : config.layers)
        {
            if (!HasCompleteLayer(layer) || !IsFinitePositive(layer.tiling))
            {
                throw std::invalid_argument(
                    "Each terrain layer requires three preloaded texture assets and a positive tiling value.");
            }
        }
    }

    void VansTerrain::ConfigureLodSelector()
    {
        TerrainLodSettings settings;
        settings.terrainSize = m_TerrainSize;
        settings.minPatchSize = MinPatchWorldSize;
        settings.minHeight = m_HeightOffset;
        settings.maxHeight = m_HeightOffset + m_MaxHeight;
        settings.baseDistance = m_LodBaseDistance;
        settings.distanceRatio = m_LodRangeRatio;
        settings.morphStartRatio = m_MorphStartRatio;

        std::string error;
        if (!m_LodSelector.Configure(settings, &error))
            throw std::invalid_argument("Terrain CDLOD configuration failed: " + error);
    }

    void VansTerrain::Init(VansVKDevice* device, const TerrainConfig& config)
    {
        ValidateConfig(config);
        const Vans::VansTerrainAssetSettings& settings = config.asset->settings;
        m_Device = device;
        m_AssetGuid = config.assetGuid;
        m_AssetSnapshot = config.asset;
        m_TerrainSize = settings.terrainSize;
        m_MaxHeight = settings.maxHeight;
        m_HeightOffset = settings.heightOffset;
        m_LodBaseDistance = settings.lodBaseDistance;
        m_LodRangeRatio = settings.lodRangeRatio;
        m_MorphStartRatio = settings.morphStartRatio;
        m_EnableTessellation = settings.tessellationEnabled;
        m_TessellationDistance = settings.tessellationDistance;
        m_MaxTessellationLevel = settings.maxTessellationLevel;
        m_TessellationTargetPixels = settings.tessellationTargetPixels;
        m_EnableHeightDetail = settings.heightDetailEnabled;
        m_HeightDetailStrength = settings.heightDetailStrength;
        m_HeightDetailFadeStart = settings.heightDetailFadeStart;
        m_RiverWetAlbedoScale = settings.riverWetness.albedoScale;
        m_RiverWetRoughness = settings.riverWetness.roughness;
        m_RiverWetDetailNormalScale = settings.riverWetness.detailNormalScale;
        ConfigureLodSelector();

        m_HeightMap = new VansTexture();
        m_HeightMap->LoadFromMemory(device->GetCommandBuffer(), config.asset->heights.data(),
            config.asset->heights.size() * sizeof(std::uint16_t),
            static_cast<int>(config.asset->width), static_cast<int>(config.asset->height),
            VK_FORMAT_R16_UNORM, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        m_Splatmap0 = new VansTexture();
        m_Splatmap0->LoadFromMemory(device->GetCommandBuffer(), config.asset->splatPixels[0].data(),
            config.asset->splatPixels[0].size(), static_cast<int>(config.asset->width),
            static_cast<int>(config.asset->height), VK_FORMAT_R8G8B8A8_UNORM,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        m_Splatmap1 = new VansTexture();
        m_Splatmap1->LoadFromMemory(device->GetCommandBuffer(), config.asset->splatPixels[1].data(),
            config.asset->splatPixels[1].size(), static_cast<int>(config.asset->width),
            static_cast<int>(config.asset->height), VK_FORMAT_R8G8B8A8_UNORM,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

        m_LayerCount = static_cast<uint32_t>(config.layers.size());
        for (uint32_t i = 0; i < m_LayerCount; ++i)
        {
            const TerrainLayerConfig& layer = config.layers[i];
            m_LayerAlbedos[i] = layer.albedo;
            m_LayerNormals[i] = layer.normal;
            m_LayerRoughness[i] = layer.roughness;
        }

        // 材质纹理原先只供片元阶段使用，在首次细分采样前发布上传和 mip 写入。
        // 只在地形初始化执行，不增加每帧同步，也不改变其他材质的上传流程。
        auto& uploadCommand = device->GetCommandBuffer();
        if (!uploadCommand.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
            throw std::runtime_error("Terrain height texture publication could not begin.");
        VkMemoryBarrier heightTexturesReady{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        heightTexturesReady.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        heightTexturesReady.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        uploadCommand.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT, { heightTexturesReady }, {}, {});
        VkQueue uploadQueue = device->GetGraphicsQueue();
        VkDevice uploadDevice = device->GetLogicDevice();
        if (!uploadCommand.EndCommandBufferRecord() ||
            !VansVKCommandBuffer::SubmitCommands(uploadQueue, uploadDevice,
                { uploadCommand.GetVKCommandBuffer() }, {}, {}, uploadCommand.m_CommandBufferFinishSubmitFence) ||
            !uploadCommand.ResetCommandBuffer(false))
            throw std::runtime_error("Terrain height texture publication failed.");

        BuildPatchMesh();
        m_TerrainInstanceInputAttributeDescriptions = {
            { 3, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(TerrainInstanceData, offset) },
            { 4, 1, VK_FORMAT_R32_SFLOAT, offsetof(TerrainInstanceData, scale) },
            { 5, 1, VK_FORMAT_R32_UINT, offsetof(TerrainInstanceData, edgeFlags) },
            { 6, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(TerrainInstanceData, morphRange) }
        };
        m_TerrainInstanceInputBindingDescriptions = {
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
        EnsureInstanceBufferCapacity(1);

        TerrainParamsGPU params{};
        params.layerCountPacked.x = static_cast<int>(m_LayerCount);
        for (uint32_t i = 0; i < TERRAIN_MAX_LAYERS; ++i)
            params.tilingFactors[i * 4] = i < m_LayerCount ? config.layers[i].tiling : 1.0f;
        params.heightfieldParams = glm::vec4(
            m_TerrainSize, m_MaxHeight, m_HeightOffset, static_cast<float>(PatchGridResolution));
        params.riverWetnessParams = glm::vec4(
            m_RiverWetAlbedoScale,m_RiverWetRoughness,m_RiverWetDetailNormalScale,0.0f);
        m_ParamsUBO.CreatVulkanBuffer(device->GetLogicDevice(), sizeof(TerrainParamsGPU),
            VK_FORMAT_R32_SFLOAT, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_ParamsUBO.SetBufferData(&params, 0, sizeof(params));

        auto& shaderManager = VansShaderManager::Get();
        m_TerrainShader = shaderManager.FindGraphicsShader("Terrain");
        m_TerrainShadowShader = shaderManager.FindGraphicsShader("TerrainShadow");
        m_TerrainTessShader = shaderManager.FindGraphicsShader("TerrainTess");
        if (!m_TerrainShader || !m_TerrainShadowShader || !m_TerrainTessShader)
            throw std::runtime_error("One or more managed terrain shaders are unavailable.");

        m_TessParamsUBO.CreatVulkanBuffer(device->GetLogicDevice(), sizeof(TerrainTessellationParamsGPU),
            VK_FORMAT_R32_SFLOAT, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        UpdateTessellationUBO();
        m_HeightDetailUBO.CreatVulkanBuffer(device->GetLogicDevice(), sizeof(TerrainHeightDetailParamsGPU),
            VK_FORMAT_R32_SFLOAT, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        UpdateHeightDetailUBO();

        VansDescriptorSetLayoutFactory::CreateAndAllocate_Terrain(m_DescriptorSetLayout, m_DescriptorSets, 1);
        if (m_DescriptorSets.empty())
            throw std::runtime_error("Terrain descriptor allocation failed.");

        auto* descriptorManager = VansVKDescriptorManager::GetInstance();
        descriptorManager->BeginDescriptorUpdate();
        descriptorManager->WriteImageDescriptor(m_DescriptorSets[0], TERRAIN_BINDING_HEIGHT_MAP,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{ m_HeightMap->GetImage().GetSampler(), m_HeightMap->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
        descriptorManager->WriteImageDescriptor(m_DescriptorSets[0], TERRAIN_BINDING_SPLATMAP_0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{ m_Splatmap0->GetImage().GetSampler(), m_Splatmap0->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
        descriptorManager->WriteImageDescriptor(m_DescriptorSets[0], TERRAIN_BINDING_SPLATMAP_1,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{ m_Splatmap1->GetImage().GetSampler(), m_Splatmap1->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});

        std::vector<VkDescriptorImageInfo> albedoInfos(TERRAIN_MAX_LAYERS);
        std::vector<VkDescriptorImageInfo> normalInfos(TERRAIN_MAX_LAYERS);
        std::vector<VkDescriptorImageInfo> roughnessInfos(TERRAIN_MAX_LAYERS);
        for (uint32_t i = 0; i < TERRAIN_MAX_LAYERS; ++i)
        {
            const uint32_t source = std::min(i, m_LayerCount - 1u);
            albedoInfos[i] = { m_LayerAlbedos[source]->GetImage().GetSampler(),
                m_LayerAlbedos[source]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            normalInfos[i] = { m_LayerNormals[source]->GetImage().GetSampler(),
                m_LayerNormals[source]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            roughnessInfos[i] = { m_LayerRoughness[source]->GetImage().GetSampler(),
                m_LayerRoughness[source]->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        }
        descriptorManager->WriteImageDescriptor(m_DescriptorSets[0], TERRAIN_BINDING_ALBEDO_ARRAY,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, albedoInfos);
        descriptorManager->WriteImageDescriptor(m_DescriptorSets[0], TERRAIN_BINDING_NORMAL_ARRAY,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, normalInfos);
        descriptorManager->WriteImageDescriptor(m_DescriptorSets[0], TERRAIN_BINDING_ROUGHNESS_ARRAY,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, roughnessInfos);
        descriptorManager->WriteBufferDescriptor(m_DescriptorSets[0], TERRAIN_BINDING_PARAMS_UBO,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            {{ m_ParamsUBO.GetNativeBuffer(), 0, sizeof(TerrainParamsGPU) }});
        descriptorManager->WriteBufferDescriptor(m_DescriptorSets[0], TERRAIN_BINDING_TESSELLATION_PARAMS,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            {{ m_TessParamsUBO.GetNativeBuffer(), 0, sizeof(TerrainTessellationParamsGPU) }});
        descriptorManager->WriteBufferDescriptor(m_DescriptorSets[0], TERRAIN_BINDING_HEIGHT_DETAIL_PARAMS,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            {{ m_HeightDetailUBO.GetNativeBuffer(), 0, sizeof(TerrainHeightDetailParamsGPU) }});
        descriptorManager->CommitDescriptorUpdates();
    }

    void VansTerrain::BuildPatchMesh()
    {
        delete m_BasePatchMesh;
        m_BasePatchMesh = new VansMesh();

        const int dimension = PatchGridResolution + 1;
        std::vector<TerrainPatchVertex> vertices;
        std::vector<float> rawPositions;
        vertices.reserve(static_cast<size_t>(dimension * dimension));
        rawPositions.reserve(static_cast<size_t>(dimension * dimension * 8));

        for (int z = 0; z < dimension; ++z)
        for (int x = 0; x < dimension; ++x)
        {
            const float fx = static_cast<float>(x);
            const float fz = static_cast<float>(z);
            TerrainPatchVertex vertex{};
            vertex.position[0] = glm::packHalf1x16(fx);
            vertex.position[1] = glm::packHalf1x16(0.0f);
            vertex.position[2] = glm::packHalf1x16(fz);
            vertex.uv[0] = glm::packHalf1x16(fx / static_cast<float>(PatchGridResolution));
            vertex.uv[1] = glm::packHalf1x16(fz / static_cast<float>(PatchGridResolution));
            vertex.normal[0] = glm::packHalf1x16(0.0f);
            vertex.normal[1] = glm::packHalf1x16(1.0f);
            vertex.normal[2] = glm::packHalf1x16(0.0f);
            vertices.push_back(vertex);

            rawPositions.insert(rawPositions.end(), { fx, 0.0f, fz, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f });
        }

        std::vector<uint32_t> indices;
        indices.reserve(static_cast<size_t>(PatchGridResolution * PatchGridResolution * 6));
        for (int z = 0; z < PatchGridResolution; ++z)
        for (int x = 0; x < PatchGridResolution; ++x)
        {
            const uint32_t topLeft = static_cast<uint32_t>(z * dimension + x);
            const uint32_t topRight = topLeft + 1u;
            const uint32_t bottomLeft = static_cast<uint32_t>((z + 1) * dimension + x);
            const uint32_t bottomRight = bottomLeft + 1u;
            indices.insert(indices.end(), { topLeft, bottomLeft, topRight, topRight, bottomLeft, bottomRight });
        }

        std::vector<VkVertexInputBindingDescription> bindings = {
            { 0, sizeof(TerrainPatchVertex), VK_VERTEX_INPUT_RATE_VERTEX }
        };
        std::vector<VkVertexInputAttributeDescription> attributes = {
            { 0, 0, VK_FORMAT_R16G16B16_SFLOAT, offsetof(TerrainPatchVertex, position) },
            { 1, 0, VK_FORMAT_R16G16_SFLOAT, offsetof(TerrainPatchVertex, uv) },
            { 2, 0, VK_FORMAT_R16G16B16_SFLOAT, offsetof(TerrainPatchVertex, normal) }
        };
        m_BasePatchMesh->InitFromRawData(m_Device->GetLogicDevice(), vertices.data(),
            static_cast<uint32_t>(vertices.size()), sizeof(TerrainPatchVertex),
            indices.data(), static_cast<uint32_t>(indices.size()), bindings, attributes, rawPositions);
    }

    void VansTerrain::EnsureInstanceBufferCapacity(uint32_t requiredCapacity)
    {
        requiredCapacity = std::max(requiredCapacity, 1u);
        if (requiredCapacity <= m_InstanceBufferCapacity)
            return;

        uint32_t newCapacity = std::max(m_InstanceBufferCapacity, 1u);
        while (newCapacity < requiredCapacity)
            newCapacity *= 2u;
        if (m_InstanceBufferCapacity > 0)
            m_InstanceBuffer.DestroyVulkanBuffer(m_Device->GetLogicDevice());

        m_InstanceBuffer.CreatVulkanBuffer(m_Device->GetLogicDevice(),
            sizeof(TerrainInstanceData) * static_cast<VkDeviceSize>(newCapacity), VK_FORMAT_R32_SFLOAT,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_InstanceBufferCapacity = newCapacity;
    }

    TerrainInstanceData VansTerrain::BuildInstanceData(const TerrainLodPatch& patch) const
    {
        TerrainInstanceData data;
        data.offset = m_LodSelector.GetPatchWorldOrigin(patch);
        data.scale = m_LodSelector.GetPatchWorldSize(patch) / static_cast<float>(PatchGridResolution);
        data.edgeFlags = patch.edgeFlags;
        data.morphRange = glm::vec2(patch.morphStart, patch.morphEnd);
        return data;
    }

    void VansTerrain::Update(const glm::vec3& cameraPosition, const glm::mat4& viewProjection)
    {
        m_LodSelector.Select(cameraPosition, m_EnableTessellation,
            m_TessellationDistance, m_SelectedPatches);

#ifndef NDEBUG
        std::string validationError;
        if (!m_LodSelector.ValidateSelection(m_SelectedPatches, &validationError))
            throw std::runtime_error("Terrain CDLOD selection failed validation: " + validationError);
#endif

        m_FarInstanceScratch.clear();
        m_NearInstanceScratch.clear();
        m_ShadowInstanceScratch.clear();
        m_FarInstanceScratch.reserve(m_SelectedPatches.size());
        m_NearInstanceScratch.reserve(m_SelectedPatches.size());
        m_ShadowInstanceScratch.reserve(m_SelectedPatches.size());

        float heightDetailPadding = 0.0f;
        if (m_EnableHeightDetail)
            heightDetailPadding = std::abs(m_HeightDetailStrength);

        for (const TerrainLodPatch& patch : m_SelectedPatches)
        {
            const TerrainInstanceData data = BuildInstanceData(patch);
            m_ShadowInstanceScratch.push_back(data);

            const glm::vec2 origin = m_LodSelector.GetPatchWorldOrigin(patch);
            const float size = m_LodSelector.GetPatchWorldSize(patch);
            const glm::vec3 boundsMin(origin.x, m_HeightOffset - heightDetailPadding, origin.y);
            const glm::vec3 boundsMax(origin.x + size, m_HeightOffset + m_MaxHeight + heightDetailPadding, origin.y + size);
            if (!RenderAABBIntersectsClipFrustum(boundsMin, boundsMax, viewProjection))
                continue;

            if (patch.tessellated)
                m_NearInstanceScratch.push_back(data);
            else
                m_FarInstanceScratch.push_back(data);
        }

        m_FarInstanceCount = static_cast<uint32_t>(m_FarInstanceScratch.size());
        m_NearInstanceCount = static_cast<uint32_t>(m_NearInstanceScratch.size());
        m_ShadowInstanceOffset = m_FarInstanceCount + m_NearInstanceCount;
        m_ShadowInstanceCount = static_cast<uint32_t>(m_ShadowInstanceScratch.size());

        m_InstanceDataCPU.clear();
        m_InstanceDataCPU.reserve(
            m_FarInstanceScratch.size() + m_NearInstanceScratch.size() + m_ShadowInstanceScratch.size());
        m_InstanceDataCPU.insert(m_InstanceDataCPU.end(), m_FarInstanceScratch.begin(), m_FarInstanceScratch.end());
        m_InstanceDataCPU.insert(m_InstanceDataCPU.end(), m_NearInstanceScratch.begin(), m_NearInstanceScratch.end());
        m_InstanceDataCPU.insert(m_InstanceDataCPU.end(), m_ShadowInstanceScratch.begin(), m_ShadowInstanceScratch.end());

        EnsureInstanceBufferCapacity(static_cast<uint32_t>(m_InstanceDataCPU.size()));
        if (!m_InstanceDataCPU.empty())
        m_InstanceBuffer.SetBufferData(m_InstanceDataCPU.data(), 0,
            sizeof(TerrainInstanceData) * m_InstanceDataCPU.size());
    }

    bool VansTerrain::RecordRegionUpload(
        VansVKCommandBuffer& cmd,
        std::uint32_t textureIndex,
        std::uint32_t x,
        std::uint32_t y,
        std::uint32_t width,
        std::uint32_t height,
        const std::vector<std::uint8_t>& bytes)
    {
        VansTexture* texture = textureIndex == 0u ? m_HeightMap :
            textureIndex == 1u ? m_Splatmap0 : textureIndex == 2u ? m_Splatmap1 : nullptr;
        const std::size_t bytesPerPixel = textureIndex == 0u ? 2u : 4u;
        if (!m_Device || !texture || width == 0 || height == 0 ||
            x + width > static_cast<std::uint32_t>(texture->GetWidth()) ||
            y + height > static_cast<std::uint32_t>(texture->GetHeight()) ||
            bytes.size() != static_cast<std::size_t>(width) * height * bytesPerPixel)
            return false;
        return m_Device->RecordDeviceImageData(
            texture->GetImage(), cmd, bytes.data(), static_cast<int>(bytes.size()),
            { static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), 0 },
            { width, height, 1u }, 0, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
            VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }

    void VansTerrain::Draw(VansVKCommandBuffer& cmd, GlobalStateData& globalState,
        std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets)
    {
        auto bindGeometry = [&]()
        {
            VkBuffer vertexBuffers[] = { m_BasePatchMesh->GetVertexBufferParameter().Buffer };
            VkBuffer instanceBuffers[] = { m_InstanceBuffer.GetNativeBuffer() };
            VkDeviceSize offsets[] = { 0 };
            cmd.BindVertexBuffers(0, 1, vertexBuffers, offsets);
            cmd.BindVertexBuffers(1, 1, instanceBuffers, offsets);
            cmd.BindIndexBuffer(m_BasePatchMesh->GetIndexBufferParameter().Buffer, 0, VK_INDEX_TYPE_UINT32);
            globalState.vertexInputAttributeDescriptions = &m_BasePatchMesh->m_VertexInputAttributeDescriptions;
            globalState.vertexInputBindingDescriptions = &m_BasePatchMesh->m_VertexInputBindingDescriptions;
        };

        if (m_FarInstanceCount > 0)
        {
            bindGeometry();
            cmd.EnsureGraphicsShader(*m_TerrainShader, globalState, layouts);
            cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_TerrainShader, 0, sets, {});
            cmd.BindGraphicsPipeline(*m_TerrainShader->GetGraphicsPipeline());
            cmd.DrawIndexed(m_BasePatchMesh->GetIndexCount(), m_FarInstanceCount, 0, 0, 0);
        }

        if (m_EnableTessellation && m_NearInstanceCount > 0)
        {
            bindGeometry();
            cmd.EnsureGraphicsShader(*m_TerrainTessShader, globalState, layouts);
            cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_TerrainTessShader, 0, sets, {});
            cmd.BindGraphicsPipeline(*m_TerrainTessShader->GetGraphicsPipeline());
            cmd.DrawIndexed(m_BasePatchMesh->GetIndexCount(), m_NearInstanceCount, 0, 0, m_FarInstanceCount);
        }
    }

    void VansTerrain::DrawShadow(VansVKCommandBuffer& cmd, GlobalStateData& globalState,
        std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets)
    {
        if (m_ShadowInstanceCount == 0)
            return;

        VkBuffer vertexBuffers[] = { m_BasePatchMesh->GetVertexBufferParameter().Buffer };
        VkBuffer instanceBuffers[] = { m_InstanceBuffer.GetNativeBuffer() };
        VkDeviceSize offsets[] = { 0 };
        cmd.BindVertexBuffers(0, 1, vertexBuffers, offsets);
        cmd.BindVertexBuffers(1, 1, instanceBuffers, offsets);
        cmd.BindIndexBuffer(m_BasePatchMesh->GetIndexBufferParameter().Buffer, 0, VK_INDEX_TYPE_UINT32);
        globalState.vertexInputAttributeDescriptions = &m_BasePatchMesh->m_VertexInputAttributeDescriptions;
        globalState.vertexInputBindingDescriptions = &m_BasePatchMesh->m_VertexInputBindingDescriptions;

        cmd.EnsureGraphicsShader(*m_TerrainShadowShader, globalState, layouts);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_TerrainShadowShader, 0, sets, {});
        cmd.BindGraphicsPipeline(*m_TerrainShadowShader->GetGraphicsPipeline());
        if (m_TerrainShadowShader->GetPushConstantSize() > 0)
        {
            const int cascadeIndex = globalState.cascadeIndex;
            cmd.UpdatePushConstants(*m_TerrainShadowShader->GetGraphicsPipeline(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(cascadeIndex), &cascadeIndex);
        }
        cmd.DrawIndexed(m_BasePatchMesh->GetIndexCount(), m_ShadowInstanceCount,
            0, 0, m_ShadowInstanceOffset);
    }

    void VansTerrain::UpdateTessellationUBO()
    {
        TerrainTessellationParamsGPU params{};
        params.maxTessLevel = m_MaxTessellationLevel;
        params.tessDistance = m_TessellationDistance;
        params.targetEdgePixels = m_TessellationTargetPixels;
        m_TessParamsUBO.SetBufferData(&params, 0, sizeof(params));
    }

    void VansTerrain::SetTessellationEnabled(bool value)
    {
        m_EnableTessellation = value;
    }

    void VansTerrain::SetTessellationDistance(float value)
    {
        m_TessellationDistance = std::max(value, 1.0f);
        UpdateTessellationUBO();
    }

    void VansTerrain::SetMaxTessellationLevel(float value)
    {
        m_MaxTessellationLevel = std::clamp(value, 1.0f, 64.0f);
        UpdateTessellationUBO();
    }

    void VansTerrain::SetTessellationTargetPixels(float value)
    {
        m_TessellationTargetPixels = std::max(value, 1.0f);
        UpdateTessellationUBO();
    }

    void VansTerrain::SetLodBaseDistance(float value)
    {
        m_LodBaseDistance = std::max(value, 1.0f);
        ConfigureLodSelector();
    }

    void VansTerrain::SetLodRangeRatio(float value)
    {
        m_LodRangeRatio = std::max(value, 2.0f);
        ConfigureLodSelector();
    }

    void VansTerrain::SetMorphStartRatio(float value)
    {
        m_MorphStartRatio = std::clamp(value, 0.05f, 0.95f);
        ConfigureLodSelector();
    }

    void VansTerrain::UpdateHeightDetailUBO()
    {
        TerrainHeightDetailParamsGPU params{};
        params.heightDetailStrength = m_EnableHeightDetail ? m_HeightDetailStrength : 0.0f;
        params.fadeStart = m_HeightDetailFadeStart;
        m_HeightDetailUBO.SetBufferData(&params, 0, sizeof(params));
    }

    void VansTerrain::SetHeightDetailEnabled(bool value)
    {
        m_EnableHeightDetail = value;
        UpdateHeightDetailUBO();
    }

    void VansTerrain::SetHeightDetailStrength(float value)
    {
        m_HeightDetailStrength = std::max(value, 0.0f);
        UpdateHeightDetailUBO();
    }

    void VansTerrain::SetHeightDetailFadeStart(float value)
    {
        m_HeightDetailFadeStart = std::clamp(value, 0.0f, 0.95f);
        UpdateHeightDetailUBO();
    }

    void VansTerrain::UpdateRiverWetnessUBO()
    {
        const glm::vec4 params(
            m_RiverWetAlbedoScale,m_RiverWetRoughness,m_RiverWetDetailNormalScale,0.0f);
        m_ParamsUBO.SetBufferData(
            &params,offsetof(TerrainParamsGPU,riverWetnessParams),sizeof(params));
    }

    void VansTerrain::SetRiverWetnessResponse(
        float albedoScale,float roughness,float detailNormalScale)
    {
        m_RiverWetAlbedoScale=std::clamp(albedoScale,0.0f,1.0f);
        m_RiverWetRoughness=std::clamp(roughness,0.0f,1.0f);
        m_RiverWetDetailNormalScale=std::clamp(detailNormalScale,0.0f,1.0f);
        UpdateRiverWetnessUBO();
    }
}
