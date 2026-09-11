#pragma once

#include <GLM/glm.hpp>
#include <cstdint>
#include <vector>

namespace VansGraphics
{
    // 只保存世界空间几何，不持有场景、渲染资源或任何 probe 的配置与布局。
    struct VansGeometryTriangle
    {
        glm::vec3 a{}, b{}, c{};
        glm::vec3 normal{};
        uint32_t owner = 0;
        bool twoSided = false;
    };

    struct VansGeometryHit
    {
        glm::vec3 position{};
        glm::vec3 normal{};
        float distance = 0.0f;
        uint32_t triangle = UINT32_MAX;
        uint32_t owner = UINT32_MAX;
        bool backface = false;
        bool twoSided = false;
    };

    struct VansGeometrySurfaceMeasure
    {
        double area = 0.0;
        glm::dvec3 areaNormal{};
    };

    class VansTriangleGeometryQuery
    {
    public:
        // 构建后只读；丢弃退化三角形，非有限坐标作为输入错误报告。
        void Build(std::vector<VansGeometryTriangle> triangles);
        bool Empty() const { return m_Triangles.empty(); }
        const std::vector<VansGeometryTriangle>& GetTriangles() const { return m_Triangles; }
        uint32_t GetDiscardedTriangleCount() const { return m_DiscardedTriangles; }
        bool NearestSurface(const glm::vec3& position, float maxDistance, VansGeometryHit& hit) const;
        bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
            VansGeometryHit& hit, float minDistance = 0.0001f) const;
        bool IntersectsBox(const glm::vec3& minimum, const glm::vec3& maximum) const;
        VansGeometrySurfaceMeasure MeasureSurface(const glm::vec3& minimum, const glm::vec3& maximum) const;
        static bool ClipSurfaceToBox(const VansGeometryTriangle& triangle, const glm::vec3& minimum,
            const glm::vec3& maximum, glm::vec3& centroid, float& area);

    private:
        struct Node
        {
            glm::vec3 minimum{}, maximum{};
            uint32_t first = 0;
            uint32_t count = 0;
            uint32_t left = 0, right = 0;
        };
        uint32_t BuildNode(uint32_t first, uint32_t count);
        void FillHit(uint32_t triangle, const glm::dvec3& position, double distance,
            const glm::dvec3& direction, VansGeometryHit& hit) const;
        std::vector<VansGeometryTriangle> m_Triangles;
        std::vector<uint32_t> m_Order;
        std::vector<Node> m_Nodes;
        uint32_t m_DiscardedTriangles = 0;
    };
}
