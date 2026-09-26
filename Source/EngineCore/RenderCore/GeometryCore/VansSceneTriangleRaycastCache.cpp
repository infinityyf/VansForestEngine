#include "VansSceneTriangleRaycastCache.h"

#include "../VansRenderBounds.h"
#include "../VulkanCore/VansMesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace VansGraphics
{
namespace
{
    bool Finite(const glm::vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool Finite(const glm::mat4& value)
    {
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                if (!std::isfinite(value[column][row])) return false;
        return true;
    }

    bool RayIntersectsBox(const glm::vec3& origin, const glm::vec3& direction,
        const glm::vec3& minimum, const glm::vec3& maximum, float maximumDistance)
    {
        double nearDistance = 0.0;
        double farDistance = maximumDistance;
        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::abs(direction[axis]) < 1.0e-12f)
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

}

struct VansSceneTriangleRaycastCache::State
{
    struct Mesh
    {
        VkBuffer vertices = VK_NULL_HANDLE;
        VkBuffer indices = VK_NULL_HANDLE;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        VansMeshGeometryData geometry;
        VansTriangleGeometryQuery query;
        glm::vec3 minimum{0.0f};
        glm::vec3 maximum{0.0f};
    };

    struct Instance
    {
        const Mesh* mesh = nullptr;
        glm::mat4 inverse{1.0f};
        glm::mat3 normalMatrix{1.0f};
        glm::vec3 minimum{0.0f};
        glm::vec3 maximum{0.0f};
        uint32_t id = 0;
        uint64_t priority = 0;
        bool twoSided = false;
    };

    struct Node
    {
        glm::vec3 minimum{0.0f};
        glm::vec3 maximum{0.0f};
        uint32_t first = 0;
        uint32_t count = 0;
        uint32_t left = 0;
        uint32_t right = 0;
    };

    std::unordered_map<VansMesh*, Mesh> meshes;
    std::vector<Instance> instances;
    std::vector<uint32_t> order;
    std::vector<Node> nodes;

    uint32_t BuildNode(uint32_t first, uint32_t count)
    {
        Node node;
        node.minimum = glm::vec3((std::numeric_limits<float>::max)());
        node.maximum = -node.minimum;
        for (uint32_t offset = first; offset < first + count; ++offset)
        {
            const Instance& instance = instances[order[offset]];
            node.minimum = glm::min(node.minimum, instance.minimum);
            node.maximum = glm::max(node.maximum, instance.maximum);
        }
        const uint32_t result = static_cast<uint32_t>(nodes.size());
        nodes.push_back(node);
        if (count <= 8u)
        {
            nodes[result].first = first;
            nodes[result].count = count;
            return result;
        }
        const glm::vec3 extent = node.maximum - node.minimum;
        int axis = extent.y > extent.x ? 1 : 0;
        if (extent.z > extent[axis]) axis = 2;
        const uint32_t middle = first + count / 2u;
        std::nth_element(order.begin() + first, order.begin() + middle,
            order.begin() + first + count, [&](uint32_t left, uint32_t right)
            {
                const Instance& a = instances[left];
                const Instance& b = instances[right];
                const float aCenter = a.minimum[axis] + a.maximum[axis];
                const float bCenter = b.minimum[axis] + b.maximum[axis];
                return aCenter < bCenter || (aCenter == bCenter && a.priority < b.priority);
            });
        const uint32_t left = BuildNode(first, middle - first);
        const uint32_t right = BuildNode(middle, first + count - middle);
        nodes[result].left = left;
        nodes[result].right = right;
        return result;
    }
};

VansSceneTriangleRaycastCache::VansSceneTriangleRaycastCache()
    : m_State(std::make_unique<State>())
{
}

VansSceneTriangleRaycastCache::~VansSceneTriangleRaycastCache() = default;
VansSceneTriangleRaycastCache::VansSceneTriangleRaycastCache(
    VansSceneTriangleRaycastCache&&) noexcept = default;
VansSceneTriangleRaycastCache& VansSceneTriangleRaycastCache::operator=(
    VansSceneTriangleRaycastCache&&) noexcept = default;

std::vector<VansMesh*> VansSceneTriangleRaycastCache::FindStale(
    const std::vector<VansMesh*>& alive, const std::vector<VansMesh*>& needed)
{
    std::unordered_set<VansMesh*> aliveSet(alive.begin(), alive.end());
    for (auto iterator = m_State->meshes.begin(); iterator != m_State->meshes.end();)
        if (!aliveSet.count(iterator->first)) iterator = m_State->meshes.erase(iterator);
        else ++iterator;

    std::vector<VansMesh*> stale;
    std::unordered_set<VansMesh*> visited;
    for (VansMesh* mesh : needed)
    {
        if (!mesh || !visited.insert(mesh).second) continue;
        const auto found = m_State->meshes.find(mesh);
        if (found != m_State->meshes.end() &&
            found->second.vertices == mesh->GetVertexBufferParameter().Buffer &&
            found->second.indices == mesh->GetIndexBufferParameter().Buffer &&
            found->second.vertexCount == mesh->GetMeshVertexCount() &&
            found->second.indexCount == mesh->GetIndexCount())
            continue;
        stale.push_back(mesh);
    }
    return stale;
}

bool VansSceneTriangleRaycastCache::Store(const std::vector<VansMesh*>& meshes,
    std::vector<VansMeshGeometryData> geometry, bool retainGeometry, std::string& error)
{
    error.clear();
    if (meshes.size() != geometry.size())
    {
        error = "Scene triangle cache mesh and geometry counts differ";
        return false;
    }
    std::vector<std::pair<VansMesh*, State::Mesh>> pending;
    pending.reserve(meshes.size());
    std::unordered_set<VansMesh*> unique;
    for (size_t index = 0; index < meshes.size(); ++index)
    {
        VansMesh* mesh = meshes[index];
        if (!mesh || !unique.insert(mesh).second)
        {
            error = "Scene triangle cache requires unique valid meshes";
            return false;
        }
        State::Mesh cached;
        cached.vertices = mesh->GetVertexBufferParameter().Buffer;
        cached.indices = mesh->GetIndexBufferParameter().Buffer;
        cached.vertexCount = mesh->GetMeshVertexCount();
        cached.indexCount = mesh->GetIndexCount();
        if (!BuildMeshQuery(geometry[index], geometry[index].positions, cached.query, error)) return false;
        if (!cached.query.Empty())
        {
            cached.minimum = glm::vec3((std::numeric_limits<float>::max)());
            cached.maximum = -cached.minimum;
            for (const VansGeometryTriangle& triangle : cached.query.GetTriangles())
            {
                cached.minimum = glm::min(cached.minimum,
                    glm::min(triangle.a, glm::min(triangle.b, triangle.c)));
                cached.maximum = glm::max(cached.maximum,
                    glm::max(triangle.a, glm::max(triangle.b, triangle.c)));
            }
        }
        if (retainGeometry) cached.geometry = std::move(geometry[index]);
        pending.emplace_back(mesh, std::move(cached));
    }
    for (auto& entry : pending)
        m_State->meshes.insert_or_assign(entry.first, std::move(entry.second));
    return true;
}

bool VansSceneTriangleRaycastCache::BuildMeshQuery(const VansMeshGeometryData& mesh,
    const std::vector<glm::vec3>& positions, VansTriangleGeometryQuery& query,
    std::string& error)
{
    error.clear();
    if (positions.size() != mesh.positions.size())
    {
        error = "Scene triangle mesh position count changed";
        return false;
    }
    std::vector<VansGeometryTriangle> triangles;
    triangles.reserve(mesh.indices.size() / 3u);
    for (size_t index = 0; index + 2u < mesh.indices.size(); index += 3u)
    {
        const uint32_t a = mesh.indices[index];
        const uint32_t b = mesh.indices[index + 1u];
        const uint32_t c = mesh.indices[index + 2u];
        if (a >= positions.size() || b >= positions.size() || c >= positions.size())
        {
            error = "Scene triangle mesh index is outside the position array";
            return false;
        }
        VansGeometryTriangle triangle;
        triangle.a = positions[a];
        triangle.b = positions[b];
        triangle.c = positions[c];
        if (!mesh.normals.empty())
        {
            if (a >= mesh.normals.size() || b >= mesh.normals.size() || c >= mesh.normals.size())
            {
                error = "Scene triangle mesh index is outside the normal array";
                return false;
            }
            triangle.normal = mesh.normals[a] + mesh.normals[b] + mesh.normals[c];
        }
        triangles.push_back(triangle);
    }
    return query.Build(std::move(triangles), error);
}

const VansMeshGeometryData* VansSceneTriangleRaycastCache::FindGeometry(VansMesh* mesh) const
{
    const auto found = m_State->meshes.find(mesh);
    if (found == m_State->meshes.end() || found->second.geometry.positions.empty()) return nullptr;
    return &found->second.geometry;
}

bool VansSceneTriangleRaycastCache::BuildInstances(
    std::vector<VansSceneTriangleInstance> sources, std::string& error)
{
    error.clear();
    std::vector<State::Instance> instances;
    instances.reserve(sources.size());
    for (const VansSceneTriangleInstance& source : sources)
    {
        const auto found = m_State->meshes.find(source.mesh);
        if (found == m_State->meshes.end())
        {
            error = "Scene triangle instance references an uncached mesh";
            return false;
        }
        if (found->second.query.Empty()) continue;
        const float determinant = glm::determinant(glm::mat3(source.model));
        if (!Finite(source.model) || !std::isfinite(determinant) ||
            std::abs(determinant) < 1.0e-8f) continue;
        const VansRenderBounds bounds = MakeRenderBoundsFromLocalAABB(
            found->second.minimum, found->second.maximum, source.model);
        if (!bounds.IsValid()) continue;
        State::Instance instance;
        instance.mesh = &found->second;
        instance.inverse = glm::inverse(source.model);
        instance.normalMatrix = glm::transpose(glm::mat3(instance.inverse));
        instance.minimum = bounds.aabb.min;
        instance.maximum = bounds.aabb.max;
        instance.id = source.id;
        instance.priority = source.priority;
        instance.twoSided = source.twoSided;
        instances.push_back(std::move(instance));
    }

    std::vector<uint32_t> order(instances.size());
    std::iota(order.begin(), order.end(), 0u);
    std::vector<State::Node> nodes;
    m_State->instances = std::move(instances);
    m_State->order = std::move(order);
    m_State->nodes = std::move(nodes);
    if (!m_State->order.empty())
        m_State->BuildNode(0u, static_cast<uint32_t>(m_State->order.size()));
    return true;
}

bool VansSceneTriangleRaycastCache::Raycast(const glm::vec3& origin,
    const glm::vec3& direction, float maxDistance,
    const std::function<bool(uint32_t)>& accept, VansSceneTriangleHit& hit,
    float minDistance, const VansGeometryQueryOptions& options) const
{
    hit = {};
    if (m_State->nodes.empty() || !Finite(origin) || !Finite(direction) ||
        !std::isfinite(maxDistance) || !std::isfinite(minDistance) ||
        minDistance < 0.0f || maxDistance < minDistance) return false;
    const float directionLength = glm::length(direction);
    if (!std::isfinite(directionLength) || directionLength <= 1.0e-8f) return false;
    const glm::vec3 rayDirection = direction / directionLength;
    float nearest = maxDistance;
    uint64_t chosenPriority = UINT64_MAX;
    std::array<uint32_t, 64> stack{};
    uint32_t stackSize = 1u;
    while (stackSize)
    {
        const State::Node& node = m_State->nodes[stack[--stackSize]];
        if (!RayIntersectsBox(origin, rayDirection, node.minimum, node.maximum, nearest)) continue;
        if (node.count == 0u)
        {
            stack[stackSize++] = node.right;
            stack[stackSize++] = node.left;
            continue;
        }
        for (uint32_t offset = node.first; offset < node.first + node.count; ++offset)
        {
            const State::Instance& instance = m_State->instances[m_State->order[offset]];
            if (accept && !accept(instance.id)) continue;
            if (!RayIntersectsBox(origin, rayDirection,
                instance.minimum, instance.maximum, nearest)) continue;
            const glm::vec3 localOrigin(instance.inverse * glm::vec4(origin, 1.0f));
            const glm::vec3 localDirection(instance.inverse * glm::vec4(rayDirection, 0.0f));
            const float stretch = glm::length(localDirection);
            if (!Finite(localOrigin) || !Finite(localDirection) ||
                !std::isfinite(stretch) || stretch <= 1.0e-8f) continue;
            VansGeometryQueryOptions localOptions = options;
            localOptions.twoSidedOverride = instance.twoSided;
            VansGeometryHit localHit;
            if (!instance.mesh->query.Raycast(localOrigin, localDirection,
                nearest * stretch, localHit, minDistance * stretch, localOptions)) continue;
            const float distance = localHit.distance / stretch;
            if (distance > nearest ||
                (distance == nearest && instance.priority >= chosenPriority)) continue;
            const glm::vec3 normal = glm::normalize(instance.normalMatrix * localHit.normal);
            if (!Finite(normal)) continue;
            nearest = distance;
            chosenPriority = instance.priority;
            hit.instance = instance.id;
            hit.position = origin + rayDirection * distance;
            hit.normal = normal;
            hit.distance = distance;
            hit.triangle = localHit;
        }
    }
    return hit.instance != UINT32_MAX;
}

size_t VansSceneTriangleRaycastCache::MeshCount() const
{
    return m_State->meshes.size();
}

size_t VansSceneTriangleRaycastCache::TriangleCount() const
{
    size_t count = 0;
    for (const auto& entry : m_State->meshes)
        count += entry.second.query.GetTriangles().size();
    return count;
}

size_t VansSceneTriangleRaycastCache::InstanceCount() const
{
    return m_State->instances.size();
}
}
