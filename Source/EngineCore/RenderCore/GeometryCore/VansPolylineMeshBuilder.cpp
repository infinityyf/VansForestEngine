#include "VansPolylineMeshBuilder.h"
#include <algorithm>
#include <cmath>

namespace VansGraphics
{
namespace
{
bool Finite(const glm::vec3& value)
{ return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
bool Valid(const VansPolylinePoint& point)
{
    return Finite(point.position) && std::isfinite(point.width) && point.width >= 0
        && Finite(glm::vec3(point.color)) && std::isfinite(point.color.a) && std::isfinite(point.u);
}
glm::vec3 Perpendicular(const glm::vec3& tangent, const glm::vec3& candidate)
{ return candidate - tangent * glm::dot(tangent, candidate); }
glm::vec3 SegmentSide(const glm::vec3& tangent, const glm::vec3& view,
    const glm::vec3& previous, const glm::vec3& right, const glm::vec3& up)
{
    glm::vec3 side = glm::cross(tangent, view);
    if (glm::dot(side, side) < 1.0e-10f) side = Perpendicular(tangent, previous);
    if (glm::dot(side, side) < 1.0e-10f) side = Perpendicular(tangent, right);
    if (glm::dot(side, side) < 1.0e-10f) side = Perpendicular(tangent, up);
    if (glm::dot(side, side) < 1.0e-10f)
        side = glm::cross(tangent, std::abs(tangent.x) < 0.8f ? glm::vec3(1,0,0) : glm::vec3(0,1,0));
    side = glm::normalize(side);
    if (glm::dot(side, previous) < 0) side = -side;
    return side;
}
std::uint32_t Pair(const VansPolylinePoint& point, const glm::vec3& offset, VansPolylineMesh& mesh)
{
    const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({point.position-offset, 0, point.color, {point.u,0}, {0,0}});
    mesh.vertices.push_back({point.position+offset, 0, point.color, {point.u,1}, {0,0}});
    return first;
}
void Connect(std::uint32_t a, std::uint32_t b, VansPolylineMesh& mesh)
{ mesh.indices.insert(mesh.indices.end(), {a,a+1,b,b,a+1,b+1}); }
void BuildRun(const std::vector<const VansPolylinePoint*>& run, const glm::vec3& camera,
    const glm::vec3& right, const glm::vec3& up, VansPolylineMesh& mesh)
{
    if (run.size() < 2) return;
    std::vector<glm::vec3> tangents(run.size()-1), sides(run.size()-1);
    glm::vec3 previous(0);
    for (std::size_t i=0; i<tangents.size(); ++i)
    {
        tangents[i] = glm::normalize(run[i+1]->position-run[i]->position);
        sides[i] = SegmentSide(tangents[i], camera-(run[i+1]->position+run[i]->position)*0.5f,
            previous,right,up);
        previous = sides[i];
    }
    auto exitPair = Pair(*run.front(), sides.front()*(0.5f*run.front()->width),mesh);
    for (std::size_t i=1; i<run.size(); ++i)
    {
        const auto& point = *run[i];
        const float halfWidth = point.width*0.5f;
        if (i+1 == run.size())
        { Connect(exitPair,Pair(point,sides[i-1]*halfWidth,mesh),mesh); break; }
        const float turn = glm::dot(tangents[i-1],tangents[i]);
        glm::vec3 miter = sides[i-1]+sides[i];
        const float length = glm::length(miter);
        const float denominator = length > 1.0e-5f ? std::abs(glm::dot(miter/length,sides[i])) : 0;
        if (turn > -0.95f && denominator >= 0.5f && turn > -0.5f)
        {
            const auto pair = Pair(point,miter/length*(halfWidth/denominator),mesh);
            Connect(exitPair,pair,mesh); exitPair = pair;
        }
        else
        {
            const auto entryPair = Pair(point,sides[i-1]*halfWidth,mesh);
            Connect(exitPair,entryPair,mesh);
            exitPair = Pair(point,sides[i]*halfWidth,mesh);
            if (turn > -0.95f) Connect(entryPair,exitPair,mesh);
        }
    }
}
}
VansPolylineBuildResult VansPolylineMeshBuilder::Append(const std::vector<VansPolylinePoint>& points,
    const glm::vec3& camera, const glm::vec3& right, const glm::vec3& up, VansPolylineMesh& mesh)
{
    VansPolylineBuildResult result;
    const std::size_t firstVertex = mesh.vertices.size();
    const std::size_t firstIndex = mesh.indices.size();
    if (!Finite(camera) || !Finite(right) || !Finite(up))
    {
        result.viewValid = false;
        return result;
    }
    std::vector<const VansPolylinePoint*> run;
    run.reserve(points.size());
    for (const auto& point : points)
    {
        if (!Valid(point))
        {
            ++result.rejectedPointCount;
            if (!run.empty())
            {
                ++result.splitRunCount;
                BuildRun(run,camera,right,up,mesh);
                run.clear();
            }
            continue;
        }
        if (!run.empty() && glm::length(point.position-run.back()->position) < 1.0e-5f)
            continue;
        run.push_back(&point);
    }
    BuildRun(run,camera,right,up,mesh);
    result.verticesAdded = mesh.vertices.size() - firstVertex;
    result.indicesAdded = mesh.indices.size() - firstIndex;
    return result;
}
}
