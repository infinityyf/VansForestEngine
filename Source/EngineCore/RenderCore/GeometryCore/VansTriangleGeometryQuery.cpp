#include "VansTriangleGeometryQuery.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace VansGraphics
{
    namespace
    {
        bool Finite(const glm::vec3& p)
        {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        }

        double DistanceToBoxSquared(const glm::dvec3& p, const glm::vec3& minimum, const glm::vec3& maximum)
        {
            const auto delta = p - glm::clamp(p, glm::dvec3(minimum), glm::dvec3(maximum));
            return glm::dot(delta, delta);
        }

        glm::dvec3 ClosestPoint(const VansGeometryTriangle& triangle, const glm::dvec3& p)
        {
            const glm::dvec3 a(triangle.a), b(triangle.b), c(triangle.c);
            const auto ab = b - a, ac = c - a, ap = p - a;
            const double d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
            if (d1 <= 0.0 && d2 <= 0.0) return a;
            const auto bp = p - b;
            const double d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
            if (d3 >= 0.0 && d4 <= d3) return b;
            const double vc = d1 * d4 - d3 * d2;
            if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) return a + ab * (d1 / (d1 - d3));
            const auto cp = p - c;
            const double d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
            if (d6 >= 0.0 && d5 <= d6) return c;
            const double vb = d5 * d2 - d1 * d6;
            if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) return a + ac * (d2 / (d2 - d6));
            const double va = d3 * d6 - d5 * d4;
            if (va <= 0.0 && d4 >= d3 && d5 >= d6)
                return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
            const double inverse = 1.0 / (va + vb + vc);
            return a + ab * (vb * inverse) + ac * (vc * inverse);
        }

        bool RayBox(const glm::dvec3& origin, const glm::dvec3& direction,
            const glm::vec3& minimum, const glm::vec3& maximum, double maxDistance)
        {
            double nearDistance = 0.0, farDistance = maxDistance;
            for (int axis = 0; axis < 3; ++axis)
            {
                if (std::abs(direction[axis]) < 1e-15)
                {
                    if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) return false;
                    continue;
                }
                double first = (minimum[axis] - origin[axis]) / direction[axis];
                double last = (maximum[axis] - origin[axis]) / direction[axis];
                if (first > last) std::swap(first, last);
                nearDistance = (std::max)(nearDistance, first);
                farDistance = (std::min)(farDistance, last);
                if (nearDistance > farDistance) return false;
            }
            return true;
        }

        bool TriangleBox(const VansGeometryTriangle& t, const glm::vec3& minimum, const glm::vec3& maximum)
        {
            const glm::dvec3 center = (glm::dvec3(minimum) + glm::dvec3(maximum)) * 0.5;
            const glm::dvec3 half = (glm::dvec3(maximum) - glm::dvec3(minimum)) * 0.5;
            const std::array<glm::dvec3, 3> v{ glm::dvec3(t.a) - center, glm::dvec3(t.b) - center, glm::dvec3(t.c) - center };
            const std::array<glm::dvec3, 3> edges{ v[1] - v[0], v[2] - v[1], v[0] - v[2] };
            auto separated = [&](const glm::dvec3& axis)
            {
                const double a = glm::dot(axis, v[0]), b = glm::dot(axis, v[1]), c = glm::dot(axis, v[2]);
                const double radius = glm::dot(glm::abs(axis), half);
                const double tolerance = 1e-12 * (radius + std::abs(a) + std::abs(b) + std::abs(c) + 1.0);
                return (std::min)({ a, b, c }) > radius + tolerance || (std::max)({ a, b, c }) < -radius - tolerance;
            };
            for (int axis = 0; axis < 3; ++axis)
            {
                glm::dvec3 unit(0.0); unit[axis] = 1.0;
                if (separated(unit)) return false;
                for (const auto& edge : edges) if (separated(glm::cross(edge, unit))) return false;
            }
            return !separated(glm::cross(edges[0], edges[1]));
        }
    }

    void VansTriangleGeometryQuery::Build(std::vector<VansGeometryTriangle> triangles)
    {
        m_Triangles.clear(); m_Order.clear(); m_Nodes.clear(); m_DiscardedTriangles = 0;
        for (const auto& triangle : triangles)
            if (!Finite(triangle.a) || !Finite(triangle.b) || !Finite(triangle.c) || !Finite(triangle.normal))
                throw std::invalid_argument("Geometry query requires finite triangle data");
        triangles.erase(std::remove_if(triangles.begin(), triangles.end(), [&](auto& triangle)
        {
            const auto cross = glm::cross(glm::dvec3(triangle.b) - glm::dvec3(triangle.a),
                glm::dvec3(triangle.c) - glm::dvec3(triangle.a));
            if (glm::dot(cross, cross) <= 1e-24) { ++m_DiscardedTriangles; return true; }
            auto normal = glm::normalize(cross);
            // 材质顶点法线决定正面，负缩放和导入 winding 不应把实体内外颠倒。
            if (glm::dot(normal, glm::dvec3(triangle.normal)) < 0.0) normal = -normal;
            triangle.normal = glm::vec3(normal);
            return false;
        }), triangles.end());
        m_Triangles = std::move(triangles);
        if (m_Triangles.size() > UINT32_MAX / 2u) throw std::length_error("Geometry query exceeds index capacity");
        m_Order.resize(m_Triangles.size());
        std::iota(m_Order.begin(), m_Order.end(), 0u);
        if (!m_Triangles.empty()) BuildNode(0, static_cast<uint32_t>(m_Triangles.size()));
    }

    uint32_t VansTriangleGeometryQuery::BuildNode(uint32_t first, uint32_t count)
    {
        Node node;
        node.minimum = glm::vec3((std::numeric_limits<float>::max)());
        node.maximum = -node.minimum;
        for (uint32_t i = first; i < first + count; ++i)
        {
            const auto& t = m_Triangles[m_Order[i]];
            node.minimum = glm::min(node.minimum, glm::min(t.a, glm::min(t.b, t.c)));
            node.maximum = glm::max(node.maximum, glm::max(t.a, glm::max(t.b, t.c)));
        }
        const uint32_t result = static_cast<uint32_t>(m_Nodes.size());
        m_Nodes.push_back(node);
        if (count <= 8)
        {
            m_Nodes[result].first = first; m_Nodes[result].count = count;
            return result;
        }
        const auto extent = node.maximum - node.minimum;
        int axis = extent.y > extent.x ? 1 : 0;
        if (extent.z > extent[axis]) axis = 2;
        const uint32_t middle = first + count / 2;
        std::nth_element(m_Order.begin() + first, m_Order.begin() + middle, m_Order.begin() + first + count,
            [&](uint32_t a, uint32_t b)
            {
                const auto& x = m_Triangles[a]; const auto& y = m_Triangles[b];
                const double cx = double(x.a[axis]) + x.b[axis] + x.c[axis];
                const double cy = double(y.a[axis]) + y.b[axis] + y.c[axis];
                return cx < cy || (cx == cy && a < b);
            });
        const uint32_t left = BuildNode(first, middle - first);
        const uint32_t right = BuildNode(middle, first + count - middle);
        m_Nodes[result].left = left; m_Nodes[result].right = right;
        return result;
    }

    void VansTriangleGeometryQuery::FillHit(uint32_t triangle, const glm::dvec3& position, double distance,
        const glm::dvec3& direction, VansGeometryHit& hit) const
    {
        const auto& t = m_Triangles[triangle];
        hit.position = glm::vec3(position); hit.normal = t.normal; hit.distance = static_cast<float>(distance);
        hit.triangle = triangle; hit.owner = t.owner; hit.twoSided = t.twoSided;
        hit.backface = glm::dot(direction, glm::dvec3(t.normal)) > 0.0;
    }

    bool VansTriangleGeometryQuery::NearestSurface(const glm::vec3& position, float maxDistance, VansGeometryHit& hit) const
    {
        hit = {};
        if (Empty() || !Finite(position) || !std::isfinite(maxDistance) || maxDistance < 0.0f) return false;
        const glm::dvec3 p(position);
        double bestSquared = double(maxDistance) * maxDistance;
        uint32_t best = UINT32_MAX;
        glm::dvec3 bestPoint{};
        std::array<uint32_t, 64> stack{}; uint32_t size = 1;
        while (size)
        {
            const Node& node = m_Nodes[stack[--size]];
            if (DistanceToBoxSquared(p, node.minimum, node.maximum) > bestSquared) continue;
            if (!node.count)
            {
                const auto& left = m_Nodes[node.left]; const auto& right = m_Nodes[node.right];
                const bool leftFirst = DistanceToBoxSquared(p, left.minimum, left.maximum) < DistanceToBoxSquared(p, right.minimum, right.maximum);
                stack[size++] = leftFirst ? node.right : node.left;
                stack[size++] = leftFirst ? node.left : node.right;
                continue;
            }
            for (uint32_t i = node.first; i < node.first + node.count; ++i)
            {
                const uint32_t index = m_Order[i];
                const auto point = ClosestPoint(m_Triangles[index], p);
                const auto delta = point - p; const double squared = glm::dot(delta, delta);
                if (squared < bestSquared || (squared == bestSquared && index < best))
                { bestSquared = squared; best = index; bestPoint = point; }
            }
        }
        if (best == UINT32_MAX) return false;
        FillHit(best, bestPoint, std::sqrt(bestSquared), bestPoint - p, hit);
        return true;
    }

    bool VansTriangleGeometryQuery::Raycast(const glm::vec3& origin, const glm::vec3& direction,
        float maxDistance, VansGeometryHit& hit, float minDistance) const
    {
        hit = {};
        if (Empty() || !Finite(origin) || !Finite(direction) || !std::isfinite(maxDistance)
            || !std::isfinite(minDistance) || minDistance < 0.0f || maxDistance < minDistance) return false;
        const double length = glm::length(glm::dvec3(direction));
        if (length < 1e-15) return false;
        const glm::dvec3 o(origin), d = glm::dvec3(direction) / length;
        double bestDistance = maxDistance;
        uint32_t best = UINT32_MAX;
        std::array<uint32_t, 64> stack{}; uint32_t size = 1;
        while (size)
        {
            const Node& node = m_Nodes[stack[--size]];
            if (!RayBox(o, d, node.minimum, node.maximum, bestDistance)) continue;
            if (!node.count) { stack[size++] = node.right; stack[size++] = node.left; continue; }
            for (uint32_t i = node.first; i < node.first + node.count; ++i)
            {
                const uint32_t index = m_Order[i]; const auto& t = m_Triangles[index];
                const glm::dvec3 e1 = glm::dvec3(t.b) - glm::dvec3(t.a), e2 = glm::dvec3(t.c) - glm::dvec3(t.a);
                const auto p = glm::cross(d, e2); const double determinant = glm::dot(e1, p);
                if (std::abs(determinant) <= 1e-14 * glm::length(e1) * glm::length(e2)) continue;
                const double inverse = 1.0 / determinant;
                const auto delta = o - glm::dvec3(t.a);
                const double u = glm::dot(delta, p) * inverse;
                if (u < -1e-12 || u > 1.0 + 1e-12) continue;
                const auto q = glm::cross(delta, e1); const double v = glm::dot(d, q) * inverse;
                if (v < -1e-12 || u + v > 1.0 + 1e-12) continue;
                const double distance = glm::dot(e2, q) * inverse;
                if (distance < minDistance || distance > bestDistance) continue;
                if (distance < bestDistance || index < best) { bestDistance = distance; best = index; }
            }
        }
        if (best == UINT32_MAX) return false;
        FillHit(best, o + d * bestDistance, bestDistance, d, hit);
        return true;
    }

    bool VansTriangleGeometryQuery::ClipSurfaceToBox(const VansGeometryTriangle& triangle,
        const glm::vec3& minimum, const glm::vec3& maximum, glm::vec3& centroid, float& area)
    {
        centroid = {}; area = 0.0f;
        if (!Finite(minimum) || !Finite(maximum) || !Finite(triangle.a) || !Finite(triangle.b)
            || !Finite(triangle.c) || glm::any(glm::greaterThan(minimum, maximum))) return false;
        std::array<glm::dvec3, 16> polygon{}, clipped{};
        polygon[0] = triangle.a; polygon[1] = triangle.b; polygon[2] = triangle.c;
        size_t count = 3;
        for (int axis = 0; axis < 3 && count; ++axis) for (int side = 0; side < 2 && count; ++side)
        {
            size_t nextCount = 0;
            const double plane = side ? maximum[axis] : minimum[axis];
            auto distance = [&](const glm::dvec3& p) { return side ? plane - p[axis] : p[axis] - plane; };
            for (size_t i = 0; i < count; ++i)
            {
                const auto& a = polygon[i]; const auto& b = polygon[(i + 1) % count];
                const double da = distance(a), db = distance(b);
                if (da >= 0.0) clipped[nextCount++] = a;
                if ((da >= 0.0) != (db >= 0.0)) clipped[nextCount++] = a + (b - a) * (da / (da - db));
            }
            polygon = clipped; count = nextCount;
        }
        if (count < 3) return false;
        glm::dvec3 weighted(0.0); double total = 0.0;
        for (size_t i = 1; i + 1 < count; ++i)
        {
            const double weight = glm::length(glm::cross(polygon[i] - polygon[0], polygon[i + 1] - polygon[0])) * 0.5;
            weighted += (polygon[0] + polygon[i] + polygon[i + 1]) * (weight / 3.0); total += weight;
        }
        if (!std::isfinite(total) || total < 1e-12 || total > std::numeric_limits<float>::max()) return false;
        centroid = glm::vec3(weighted / total); area = static_cast<float>(total);
        return true;
    }

    VansGeometrySurfaceMeasure VansTriangleGeometryQuery::MeasureSurface(
        const glm::vec3& minimum, const glm::vec3& maximum) const
    {
        VansGeometrySurfaceMeasure measure;
        if (Empty() || !Finite(minimum) || !Finite(maximum) || glm::any(glm::greaterThan(minimum, maximum))) return measure;
        std::array<uint32_t, 64> stack{}; uint32_t size = 1;
        while (size)
        {
            const Node& node = m_Nodes[stack[--size]];
            if (glm::any(glm::lessThan(node.maximum, minimum)) || glm::any(glm::greaterThan(node.minimum, maximum))) continue;
            if (!node.count) { stack[size++] = node.right; stack[size++] = node.left; continue; }
            for (uint32_t i = node.first; i < node.first + node.count; ++i)
            {
                const auto& triangle = m_Triangles[m_Order[i]];
                glm::vec3 centroid; float area;
                if (!ClipSurfaceToBox(triangle, minimum, maximum, centroid, area)) continue;
                // 以裁剪后的真实面积加权，不因同一平面增加三角形而提高细分优先级。
                measure.area += area;
                measure.areaNormal += glm::dvec3(triangle.normal) * double(area);
            }
        }
        return measure;
    }

    bool VansTriangleGeometryQuery::IntersectsBox(const glm::vec3& minimum, const glm::vec3& maximum) const
    {
        if (Empty() || !Finite(minimum) || !Finite(maximum) || glm::any(glm::greaterThan(minimum, maximum))) return false;
        std::array<uint32_t, 64> stack{}; uint32_t size = 1;
        while (size)
        {
            const Node& node = m_Nodes[stack[--size]];
            if (glm::any(glm::lessThan(node.maximum, minimum)) || glm::any(glm::greaterThan(node.minimum, maximum))) continue;
            if (!node.count) { stack[size++] = node.right; stack[size++] = node.left; continue; }
            for (uint32_t i = node.first; i < node.first + node.count; ++i)
                if (TriangleBox(m_Triangles[m_Order[i]], minimum, maximum)) return true;
        }
        return false;
    }
}
