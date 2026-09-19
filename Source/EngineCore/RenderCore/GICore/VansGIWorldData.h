#pragma once
#include "VansGIWorldSettings.h"
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <map>
#include <memory>
#include <string>
#include <functional>
#include <optional>
#include <limits>

namespace Vans { struct VansTerrainAsset; }
namespace VansGraphics
{
    struct GIWorldVoxel
    {
        uint32_t optical = 0; // half 消光；bit16 表面，bit17 已确认内部。
        uint32_t surface = 0; // material16 + octNormal16。
    };
    static_assert(sizeof(GIWorldVoxel) == 8);
    struct GIWorldCell
    {
        int32_t x = 0, y = 0, z = 0, level = 0;
        bool operator<(const GIWorldCell& b) const;
        bool operator==(const GIWorldCell& b) const;
    };
    struct GIWorldTriangle
    {
        std::array<glm::vec3, 3> positions;
        std::array<glm::vec2, 3> uvs;
        uint32_t material = 0;
        bool porous = false;
        float alphaCutoff = 0.5f;
        // 只在烘焙期间访问已读取的纹理快照，运行时查询不依赖源文件。
        std::function<float(glm::vec2)> alpha;
    };
    struct GIWorldTemplateLevel
    {
        float voxelSize = 0.25f;
        glm::vec3 minimum{0}, maximum{0};
        std::map<GIWorldCell, GIWorldVoxel> cells;
        std::map<GIWorldCell, std::vector<GIWorldCell>> spatialBricks;
    };
    struct GIWorldTemplate : GIWorldTemplateLevel
    {
        // 资产空间的光学 mip，与相机世界层级分开；相同模型实例共享。
        std::vector<GIWorldTemplateLevel> coarseLevels;
        size_t CellCount() const;
        const GIWorldTemplateLevel& SelectLevel(float worldVoxelSize, const glm::mat4& transform) const;
    };
    struct GIWorldInstance
    {
        std::shared_ptr<const GIWorldTemplate> model;
        glm::mat4 transform{1}, inverse{1};
        glm::vec3 minimum{0}, maximum{0};
    };
    struct GIWorldBrick
    {
        GIWorldCell key;
        std::array<GIWorldVoxel, 512> voxels{};
    };
    struct GIWorldBounds { glm::vec3 minimum{0},maximum{0}; };
    // 比较实例多重集合，包含增删、移动、重复实例和光学 mip 的保守范围。
    std::vector<GIWorldBounds> GIWorldChangedInstanceRegions(
        const std::vector<GIWorldInstance>& previous,const std::vector<GIWorldInstance>& next);
    // 保留离散的新旧占用范围；后台构造扁平树，页/探针查询不分配内存。
    class GIWorldDirtyRegions
    {
    public:
        GIWorldDirtyRegions() = default;
        explicit GIWorldDirtyRegions(std::vector<GIWorldBounds> regions);
        bool Empty() const { return m_Nodes.empty(); }
        // radius=0 为闭合 AABB 相交；正值为到 AABB 集合的欧氏距离。
        bool Intersects(const GIWorldBounds& bounds,float radius=0) const;
        bool IntersectsPage(const GIWorldCell& page,float voxelSize) const;
    private:
        struct Node { GIWorldBounds bounds; size_t end=0; };
        std::vector<Node> m_Nodes;
        void Build(std::vector<GIWorldBounds>& regions,size_t first,size_t count);
    };
    struct GIWorldTerrainColorData
    {
        static constexpr uint32_t Resolution=128;
        uint32_t width=0,height=0;
        // 每个 GI texel 只保留双线性采样的四个原始 RGBA8 角点；不随源图面积增长。
        std::array<std::vector<uint32_t>,2> corners;
        std::vector<glm::vec4> layers,colors;
        struct Patch { std::vector<glm::uvec2> spans; glm::uvec2 first{Resolution},last{0}; };
        bool Build(uint32_t sourceWidth,uint32_t sourceHeight,std::array<std::vector<uint32_t>,2> sourceCorners,
            std::vector<glm::vec4> layerColors,std::string& error);
        bool ApplyPatch(uint32_t map,uint32_t x,uint32_t y,uint32_t w,uint32_t h,
            const std::vector<uint8_t>& pixels,Patch& changed,std::string& error);
    private:
        glm::vec4 Evaluate(uint32_t x,uint32_t y) const;
    };
    struct GIWorldHeightData
    {
        glm::vec4 parameters{0}; // size, height scale, offset, width
        uint32_t width = 0, height = 0;
        std::vector<float> heights;
        std::vector<glm::vec2> ranges;
        std::vector<glm::uvec4> levels;
        bool Build(const Vans::VansTerrainAsset& source, std::string& error);
        struct Patch
        {
            // 每项为元素首址和数量，供帧内局部 GPU 上传使用。
            std::vector<glm::uvec2> heights, ranges;
            glm::vec3 minimum{0}, maximum{0};
        };
        bool ApplyPatch(uint32_t x,uint32_t z,uint32_t w,uint32_t h,
            const std::vector<uint8_t>& pixels,Patch& changed,std::string& error);
        float Sample(glm::vec2 world) const;
    };

    bool BakeGIWorldTemplate(const std::vector<GIWorldTriangle>& triangles, float voxelSize,
        uint32_t maxCells, GIWorldTemplate& output, std::string& error);
    GIWorldInstance MakeGIWorldInstance(std::shared_ptr<const GIWorldTemplate> model, const glm::mat4& transform);
    uint32_t GIWorldHash(const GIWorldCell& cell);
    glm::vec3 GIWorldDecodeNormal(uint32_t packed);
    uint32_t GIWorldEncodeNormal(glm::vec3 normal);

    // CPU 准备完整砖块，再以原子页发布；预算耗尽不会将未构建页登记为空。
    class VansGIWorldBuilder
    {
    public:
        void Reset(const GIWorldSettings& settings, std::vector<GIWorldInstance> instances, glm::vec3 center);
        // 来源内容未变时共享只读索引和粗页；仅重新规划相机邻域。
        void Recenter(const VansGIWorldBuilder& source, glm::vec3 center);
        // false 也可能表示预算耗尽；未完成砖保留进度，PendingCount 不减少。
        bool BuildNext(GIWorldBrick& brick, uint32_t maxWork = std::numeric_limits<uint32_t>::max());
        const GIWorldCell& NextPage() const { return m_Pages.at(m_Next); }
        void SkipNext() { m_Build.reset(); ++m_Next; }
        const std::vector<GIWorldCell>& Pages() const { return m_Pages; }
        uint32_t PendingCount() const { return uint32_t(m_Pages.size() - m_Next); }
        bool Overflowed() const { return m_Overflow; }
        uint32_t AvailableLevels() const { return m_AvailableLevels; }
        uint32_t LevelCount() const { return m_LevelCount; }
        const std::vector<GIWorldInstance>& Instances() const;
        glm::vec3 Center() const { return m_Center; }
        uint64_t TemplateSamples() const { return m_TemplateSamples; }
        uint64_t PlanningInstanceTests() const { return m_PlanningInstanceTests; }
    private:
        struct SourceSnapshot;
        void PlanPages(glm::vec3 center);
        struct BuildProgress
        {
            GIWorldBrick brick;
            size_t candidate = 0, cell = 0;
            bool instanceReady = false;
            const GIWorldTemplateLevel* model = nullptr;
            std::map<GIWorldCell, std::vector<GIWorldCell>>::const_iterator bucket;
            glm::ivec3 first{0}, last{0};
            glm::mat3 normalMatrix{1};
            glm::vec3 extent{0};
            float volume = 0, scale = 1, minScale = 1, boxVolume = 0;
            std::array<float, 512> dominant{};
            std::array<float, 512> extinction{};
        };
        std::optional<BuildProgress> m_Build;
        GIWorldSettings m_Settings;
        std::shared_ptr<const SourceSnapshot> m_Sources;
        std::vector<GIWorldCell> m_Pages;
        std::map<GIWorldCell, std::vector<uint32_t>> m_Candidates;
        glm::vec3 m_Center{0};
        size_t m_Next = 0;
        bool m_Overflow = false;
        uint32_t m_AvailableLevels = 0;
        uint32_t m_LevelCount = 0;
        uint64_t m_TemplateSamples = 0;
        uint64_t m_PlanningInstanceTests = 0;
    };
}
