#include "../EngineCore/RenderCore/GeometryCore/VansTriangleGeometryQuery.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

namespace
{
    using namespace VansGraphics;
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    void Near(float value, float expected, const char* message)
    {
        Check(std::abs(value - expected) < 0.0002f, message);
    }
    VansGeometryTriangle Triangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 normal, uint32_t owner = 0)
    {
        VansGeometryTriangle t; t.a = a; t.b = b; t.c = c; t.normal = normal; t.owner = owner; return t;
    }
    void BuildQuery(VansTriangleGeometryQuery& query, std::vector<VansGeometryTriangle> triangles)
    {
        std::string error;
        Check(query.Build(std::move(triangles), error), error.c_str());
    }
}

bool TestTriangleGeometryQueryContract()
{
    try
    {
        VansTriangleGeometryQuery query;
        VansGeometryHit hit;
        Check(!query.Raycast({}, {0, 1, 0}, 10, hit), "empty ray query");
        Check(!query.NearestSurface({}, 10, hit), "empty nearest query");
        Check(!query.IntersectsBox({-1, -1, -1}, {1, 1, 1}), "empty overlap query");

        // 开口单层地面位于负 Y，查询不能用高度阈值区分地上/地下。
        BuildQuery(query, {Triangle({0, -12, 0}, {4, -12, 0}, {0, -12, 4}, {0, 1, 0}, 7)});
        Check(query.Raycast({1, -10, 1}, {0, -5, 0}, 2, hit), "front ray exactly at distance bound");
        Near(hit.distance, 2, "normalized ray distance");
        Check(!hit.backface && hit.owner == 7, "frontface and owner identity");
        Check(query.Raycast({1, -14, 1}, {0, 1, 0}, 2, hit) && hit.backface, "ground underside detection");
        VansGeometryQueryOptions frontFacing;
        frontFacing.backfaces = VansGeometryBackfacePolicy::RejectOneSided;
        Check(!query.Raycast({1, -14, 1}, {0, 1, 0}, 2, hit, 0.0001f, frontFacing),
            "one-sided backface policy accepted a backface");
        auto doubleSided = Triangle({0, -12, 0}, {4, -12, 0}, {0, -12, 4}, {0, 1, 0}, 8);
        doubleSided.twoSided = true;
        BuildQuery(query, {doubleSided});
        Check(query.Raycast({1, -14, 1}, {0, 1, 0}, 2, hit, 0.0001f, frontFacing) &&
            hit.backface && hit.twoSided && hit.owner == 8,
            "two-sided backface policy rejected a material-authorized side");
        BuildQuery(query, {Triangle({0, -12, 0}, {4, -12, 0}, {0, -12, 4}, {0, 1, 0}, 9)});
        frontFacing.twoSidedOverride = true;
        Check(query.Raycast({1, -14, 1}, {0, 1, 0}, 2, hit, 0.0001f, frontFacing) &&
            hit.twoSided && hit.owner == 9,
            "instance sidedness override was not applied");
        frontFacing.twoSidedOverride.reset();
        BuildQuery(query, {Triangle({0, -12, 0}, {4, -12, 0}, {0, -12, 4}, {0, 1, 0}, 7)});
        Check(!query.Raycast({1, -10, 1}, {1, 0, 0}, 100, hit), "parallel ray");
        Check(!query.Raycast({1, -10, 1}, {}, 100, hit), "zero direction");
        Check(!query.Raycast({1, -10, 1}, {0, -1, 0}, 1.99f, hit), "finite ray segment");
        Check(query.NearestSurface({1, -10, 1}, 2, hit), "nearest plane interior");
        Near(hit.distance, 2, "plane distance");
        Check(query.NearestSurface({3, -12, 3}, 2, hit), "nearest hypotenuse");
        Near(hit.distance, std::sqrt(2.0f), "triangle edge rather than triangle AABB");
        Check(query.NearestSurface({-1, -12, -1}, 2, hit), "nearest corner");
        Near(hit.distance, std::sqrt(2.0f), "corner distance");
        Check(query.IntersectsBox({0.9f, -12.01f, 0.9f}, {1.1f, -11.99f, 1.1f}), "surface overlap");
        Check(query.IntersectsBox({2, -12, 2}, {2, -12, 2}), "touching edge overlap");
        Check(!query.IntersectsBox({2.9f, -12.1f, 2.9f}, {3.1f, -11.9f, 3.1f}), "reject empty part of triangle AABB");

        // 多层表面、薄墙与导入反 winding：最近的真实表面必须胜过更远的大片表面。
        BuildQuery(query, {
            Triangle({-10, -14, -10}, {10, -14, -10}, {-10, -14, 10}, {0, 1, 0}, 1),
            Triangle({-10, -10, -10}, {10, -10, -10}, {-10, -10, 10}, {0, -1, 0}, 2),
            Triangle({0, -14, -10}, {0, -10, -10}, {0, -14, 10}, {-1, 0, 0}, 3)});
        Check(query.Raycast({-1, -12, -1}, {0, -1, 0}, 8, hit) && hit.owner == 1 && !hit.backface, "underground floor");
        Check(query.Raycast({-1, -12, -1}, {0, 1, 0}, 8, hit) && hit.owner == 2 && !hit.backface, "underground ceiling");
        Check(query.Raycast({-1, -12, -1}, {1, 0, 0}, 8, hit) && hit.owner == 3 && !hit.backface, "thin wall");

        std::vector<VansGeometryTriangle> triangles;
        for (uint32_t i = 0; i < 4096; ++i)
        {
            const glm::vec3 center(float(i % 16) * 10 - 80, float((i / 16) % 16) * 10 - 80, float(i / 256) * 10 - 80);
            triangles.push_back(Triangle(center + glm::vec3(-1, 0, -1), center + glm::vec3(2, 0, -1),
                center + glm::vec3(-1, 0, 2), {0, 1, 0}, i));
        }
        BuildQuery(query, std::move(triangles));
        std::mt19937 random(70219);
        std::uniform_real_distribution<float> offset(-0.3f, 0.3f);
        for (uint32_t i = 0; i < 16384; ++i)
        {
            const uint32_t owner = random() % 4096;
            const glm::vec3 center(float(owner % 16) * 10 - 80, float((owner / 16) % 16) * 10 - 80, float(owner / 256) * 10 - 80);
            const glm::vec3 origin = center + glm::vec3(offset(random), 0.5f, offset(random));
            Check(query.Raycast(origin, {0, -1, 0}, 1, hit) && hit.owner == owner, "BVH ray coverage and owner");
            Near(hit.distance, 0.5f, "BVH ray distance");
            Check(query.NearestSurface(origin, 1, hit) && hit.owner == owner, "BVH nearest coverage and owner");
            Near(hit.distance, 0.5f, "BVH nearest distance");
            Check(query.IntersectsBox(center - glm::vec3(0.1f), center + glm::vec3(0.1f)), "BVH box coverage");
        }

        auto degenerate = Triangle({}, {}, {}, {});
        BuildQuery(query, {degenerate, Triangle({10000, -10000, 10000}, {10004, -10000, 10000},
            {10000, -10000, 10004}, {0, 1, 0}, 99)});
        Check(query.GetTriangles().size() == 1, "discard degenerate triangle");
        Check(query.Raycast({10001, -9998, 10001}, {0, -1, 0}, 3, hit) && hit.owner == 99, "large coordinate query");
        Near(hit.distance, 2, "large coordinate distance");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        glm::vec3 centroid; float area;
        const auto clipped = Triangle({0, 0, 0}, {4, 0, 0}, {0, 0, 4}, {0, 1, 0});
        Check(VansTriangleGeometryQuery::ClipSurfaceToBox(clipped, {0, -1, 0}, {1, 1, 1}, centroid, area), "clip square out of large triangle");
        Near(area, 1, "clipped surface area"); Near(centroid.x, 0.5f, "clipped centroid x"); Near(centroid.z, 0.5f, "clipped centroid z");
        Check(VansTriangleGeometryQuery::ClipSurfaceToBox(clipped, {2, 0, 0}, {4, 0, 4}, centroid, area), "clip triangle on zero-thickness plane");
        Near(area, 2, "clipped triangle area"); Near(centroid.x, 8.0f / 3, "clipped triangle centroid");
        Check(!VansTriangleGeometryQuery::ClipSurfaceToBox(clipped, {3, -1, 3}, {4, 1, 4}, centroid, area), "triangle AABB void generated receiver");
        Check(!VansTriangleGeometryQuery::ClipSurfaceToBox(clipped, {2, 0, 2}, {2, 0, 2}, centroid, area), "zero-area touch generated receiver");
        Check(!VansTriangleGeometryQuery::ClipSurfaceToBox(Triangle({nan, 0, 0}, {1, 0, 0}, {0, 0, 1}, {}),
            {-2, -2, -2}, {2, 2, 2}, centroid, area), "non-finite clipped geometry accepted");
        BuildQuery(query, {clipped});
        const auto measure = query.MeasureSurface({0,-1,0}, {1,1,1});
        Near(float(measure.area), 1.0f, "BVH clipped area measure");
        Near(float(measure.areaNormal.y), 1.0f, "area-weighted geometric normal measure");
        Near(float(query.MeasureSurface({3,-1,3}, {4,1,4}).area), 0.0f, "surface measure counted triangle AABB void");
        Check(!query.Raycast({nan, 0, 0}, {1, 0, 0}, 10, hit), "non-finite query rejected");
        const auto previousTriangles = query.GetTriangles();
        std::string buildError;
        Check(!query.Build({Triangle({nan, 0, 0}, {1, 0, 0}, {0, 1, 0}, {})}, buildError) &&
            buildError == "Geometry query requires finite triangle data",
            "non-finite geometry did not return the explicit build error");
        Check(query.GetTriangles().size() == previousTriangles.size() &&
            query.Raycast({0.5f, 1.0f, 0.5f}, {0, -1, 0}, 2, hit),
            "failed build replaced the last valid query");
        std::cout << "[GeometryQuery] PASS: 4096 triangles, 16384 analytic ray/nearest/box cases, thin walls, negative-Y rooms, winding, edges, degenerates\n";
        return true;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[GeometryQuery] FAIL: " << exception.what() << '\n';
        return false;
    }
}
