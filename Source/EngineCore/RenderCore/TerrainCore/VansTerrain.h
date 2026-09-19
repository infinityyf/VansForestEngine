#pragma once

#include "VansTerrainLod.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansTexture.h"
#include "../VulkanCore/VansMesh.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansVKBuffer.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include "../../TerrainCore/VansTerrainAsset.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace VansGraphics
{
    struct TerrainLayerConfig
    {
        VansTexture* albedo = nullptr;
        VansTexture* normal = nullptr;
        VansTexture* roughness = nullptr;
        float tiling = 64.0f;
    };

    struct TerrainConfig
    {
        Vans::VansAssetGuid assetGuid;
        std::shared_ptr<const Vans::VansTerrainAsset> asset;
        std::vector<TerrainLayerConfig> layers;
    };

    struct alignas(16) TerrainParamsGPU
    {
        glm::ivec4 layerCountPacked;
        float tilingFactors[TERRAIN_MAX_LAYERS * 4];
        glm::vec4 heightfieldParams; // x=terrainSize, y=maxHeight, z=heightOffset, w=patchGridResolution
        glm::vec4 riverWetnessParams; // x=albedoScale, y=roughness, z=detailNormalScale
    };

    struct alignas(16) TerrainTessellationParamsGPU
    {
        float maxTessLevel;
        float tessDistance;
        float targetEdgePixels;
        float padding;
    };

    struct alignas(16) TerrainHeightDetailParamsGPU
    {
        float heightDetailStrength = 0.03f;
        float fadeStart = 0.7f;
        float padding[2]{};
    };

    static_assert(sizeof(TerrainHeightDetailParamsGPU) == 16, "Terrain height detail UBO must match std140");

    struct TerrainInstanceData
    {
        glm::vec2 offset;
        float scale = 1.0f;
        uint32_t edgeFlags = 0;
        glm::vec2 morphRange = glm::vec2(0.0f);
    };

    struct TerrainPatchVertex
    {
        uint16_t position[3];
        uint16_t uv[2];
        uint16_t normal[3];
    };

    class VansTerrain
    {
    public:
        VansTerrain() = default;
        ~VansTerrain();

        void Init(VansVKDevice* device, const TerrainConfig& config);
        void Update(const glm::vec3& cameraPosition, const glm::mat4& viewProjection);

        void Draw(VansVKCommandBuffer& cmd, GlobalStateData& globalState,
            std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets);
        void DrawShadow(VansVKCommandBuffer& cmd, GlobalStateData& globalState,
            std::vector<VkDescriptorSetLayout>& layouts, std::vector<VkDescriptorSet>& sets);
        bool RecordRegionUpload(
            VansVKCommandBuffer& cmd,
            std::uint32_t textureIndex,
            std::uint32_t x,
            std::uint32_t y,
            std::uint32_t width,
            std::uint32_t height,
            const std::vector<std::uint8_t>& bytes);

        VansTexture* GetHeightMap() const { return m_HeightMap; }
        VansTexture* GetSplatmap(std::uint32_t index) const { return index == 0 ? m_Splatmap0 : index == 1 ? m_Splatmap1 : nullptr; }
        Vans::VansAssetGuid GetAssetGuid() const { return m_AssetGuid; }
        const std::shared_ptr<const Vans::VansTerrainAsset>& GetAssetSnapshot() const { return m_AssetSnapshot; }
        float GetTerrainSize() const { return m_TerrainSize; }
        float GetMaxHeight() const { return m_MaxHeight; }
        float GetHeightOffset() const { return m_HeightOffset; }

        bool IsTessellationEnabled() const { return m_EnableTessellation; }
        float GetTessellationDistance() const { return m_TessellationDistance; }
        float GetMaxTessellationLevel() const { return m_MaxTessellationLevel; }
        float GetTessellationTargetPixels() const { return m_TessellationTargetPixels; }
        float GetLodBaseDistance() const { return m_LodBaseDistance; }
        float GetLodRangeRatio() const { return m_LodRangeRatio; }
        float GetMorphStartRatio() const { return m_MorphStartRatio; }

        void SetTessellationEnabled(bool value);
        void SetTessellationDistance(float value);
        void SetMaxTessellationLevel(float value);
        void SetTessellationTargetPixels(float value);
        void SetLodBaseDistance(float value);
        void SetLodRangeRatio(float value);
        void SetMorphStartRatio(float value);

        bool IsHeightDetailEnabled() const { return m_EnableHeightDetail; }
        float GetHeightDetailStrength() const { return m_HeightDetailStrength; }
        float GetHeightDetailFadeStart() const { return m_HeightDetailFadeStart; }

        void SetHeightDetailEnabled(bool value);
        void SetHeightDetailStrength(float value);
        void SetHeightDetailFadeStart(float value);
        void SetRiverWetnessResponse(float albedoScale, float roughness, float detailNormalScale);

        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_DescriptorSets;

    private:
        void ValidateConfig(const TerrainConfig& config) const;
        void ConfigureLodSelector();
        void BuildPatchMesh();
        void EnsureInstanceBufferCapacity(uint32_t requiredCapacity);
        TerrainInstanceData BuildInstanceData(const TerrainLodPatch& patch) const;
        void UpdateTessellationUBO();
        void UpdateHeightDetailUBO();
        void UpdateRiverWetnessUBO();

        VansVKDevice* m_Device = nullptr;
        Vans::VansAssetGuid m_AssetGuid;
        std::shared_ptr<const Vans::VansTerrainAsset> m_AssetSnapshot;

        VansTexture* m_HeightMap = nullptr;
        VansTexture* m_Splatmap0 = nullptr;
        VansTexture* m_Splatmap1 = nullptr;
        std::array<VansTexture*, TERRAIN_MAX_LAYERS> m_LayerAlbedos{};
        std::array<VansTexture*, TERRAIN_MAX_LAYERS> m_LayerNormals{};
        std::array<VansTexture*, TERRAIN_MAX_LAYERS> m_LayerRoughness{};
        uint32_t m_LayerCount = 0;

        VansVKBuffer m_ParamsUBO;
        VansVKBuffer m_TessParamsUBO;
        VansVKBuffer m_HeightDetailUBO;

        VansMesh* m_BasePatchMesh = nullptr;
        std::vector<VkVertexInputAttributeDescription> m_TerrainInstanceInputAttributeDescriptions;
        std::vector<VkVertexInputBindingDescription> m_TerrainInstanceInputBindingDescriptions;

        VansGraphicsShader* m_TerrainShader = nullptr;
        VansGraphicsShader* m_TerrainShadowShader = nullptr;
        VansGraphicsShader* m_TerrainTessShader = nullptr;

        VansVKBuffer m_InstanceBuffer;
        std::vector<TerrainInstanceData> m_InstanceDataCPU;
        std::vector<TerrainInstanceData> m_FarInstanceScratch;
        std::vector<TerrainInstanceData> m_NearInstanceScratch;
        std::vector<TerrainInstanceData> m_ShadowInstanceScratch;
        std::vector<TerrainLodPatch> m_SelectedPatches;
        uint32_t m_InstanceBufferCapacity = 0;
        uint32_t m_FarInstanceCount = 0;
        uint32_t m_NearInstanceCount = 0;
        uint32_t m_ShadowInstanceOffset = 0;
        uint32_t m_ShadowInstanceCount = 0;

        VansTerrainLodSelector m_LodSelector;
        float m_TerrainSize = 1024.0f;
        float m_MaxHeight = 500.0f;
        float m_HeightOffset = -23.0f;
        float m_LodBaseDistance = 64.0f;
        float m_LodRangeRatio = 2.0f;
        float m_MorphStartRatio = 0.70f;

        bool m_EnableTessellation = true;
        float m_TessellationDistance = 300.0f;
        float m_MaxTessellationLevel = 32.0f;
        float m_TessellationTargetPixels = 12.0f;

        bool m_EnableHeightDetail = true;
        float m_HeightDetailStrength = 0.03f;
        float m_HeightDetailFadeStart = 0.7f;
        float m_RiverWetAlbedoScale = 0.72f;
        float m_RiverWetRoughness = 0.18f;
        float m_RiverWetDetailNormalScale = 0.70f;

        // A 33x33 regular grid is the conventional CDLOD patch topology. The
        // finest patch remains 16 world units wide, giving 0.5-unit vertices
        // before the optional near-field tessellation stage.
        static constexpr int PatchGridResolution = 32;
        static constexpr float MinPatchWorldSize = 16.0f;
    };
}
