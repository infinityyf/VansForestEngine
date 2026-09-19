#include "VansEditorSceneQuery.h"
#include "../../EditorCore/VansEditorSceneMath.h"
#include "../../RenderCore/GeometryCore/VansMeshGeometryReadback.h"
#include "../../RenderCore/GeometryCore/VansTriangleGeometryQuery.h"
#include "../../RenderCore/VansRenderNode.h"
#include "../../RenderCore/VansRenderSystem.h"
#include "../../RenderCore/VulkanCore/VansMesh.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../AnimationCore/VansAnimationNode.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../SceneRuntime/VansComponentStorage.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../Util/VansLog.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace Vans::EditorAPI
{
namespace
{
using namespace VansGraphics;
glm::vec3 Vector(Vec3 v) { return {v.x, v.y, v.z}; }
Vec3 Vector(glm::vec3 v) { return {v.x, v.y, v.z}; }
bool Finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

struct Instance
{
    VansRenderNode* node = nullptr;
    VansEntityHandle owner;
};
std::vector<Instance> CollectInstances(VansRuntimeWorld& world, bool enabledOnly)
{
    std::vector<Instance> result;
    auto* storage = static_cast<VansComponentStorage<VansRuntimeRenderComponent>*>(
        world.FindStorage(VansRuntimeComponentType_Render));
    if (!storage) return result;
    std::unordered_set<VansRenderNode*> visited;
    for (const auto& header : storage->Headers())
    {
        const auto* render = storage->Get(header.self);
        if (!render || !world.Entities().IsAlive(header.owner) || (enabledOnly && !header.effectiveEnabled)) continue;
        const auto append = [&](VansRenderNode* node)
        {
            if (node && node->m_Mesh && (!enabledOnly || node->IsEnabled()) && visited.insert(node).second)
                result.push_back({node, header.owner});
        };
        append(render->renderNode);
        for (auto* node : render->renderNodes) append(node);
    }
    return result;
}
std::string ResolveDocumentEntity(const VansRuntimeWorld& world, const Instance& instance,
    const std::unordered_set<std::string>& allowed)
{
    // 优先保留可编辑子网格身份，内部生成节点则沿真正的实体父链向上查找。
    for (const auto& guid : {instance.node->m_EntityGuid, instance.node->m_ParentEntityGuid})
        if (!guid.empty() && allowed.count(guid) && world.Entities().IsAlive(world.Entities().FindByGuid(guid))) return guid;
    auto owner = instance.owner;
    while (const auto* record = world.Entities().Get(owner))
    {
        if (allowed.count(record->stableGuid)) return record->stableGuid;
        owner = record->parent;
    }
    return {};
}
bool SelectedOrDescendant(const VansRuntimeWorld& world, VansEntityHandle owner,
    const std::unordered_set<std::string>& selected)
{
    while (const auto* record = world.Entities().Get(owner))
    {
        if (selected.count(record->stableGuid)) return true;
        owner = record->parent;
    }
    return false;
}
void Expand(EditorSceneBounds& bounds, glm::vec3 point)
{
    if (!Finite(point)) return;
    if (!bounds.available) { bounds.minimum = bounds.maximum = Vector(point); bounds.available = true; }
    else { bounds.minimum = Vector(glm::min(Vector(bounds.minimum), point)); bounds.maximum = Vector(glm::max(Vector(bounds.maximum), point)); }
}

// 事务只调用已有的通用几何读取；没有给 RuntimeCore 添加编辑器缓存或选择状态。
struct GeometryReadState
{
    std::vector<VansMesh*> meshes;
    std::vector<VansMeshGeometryData> geometry;
    std::string error;
};
class GeometryReadTransaction final : public IVansRenderThreadTransaction
{
public:
    explicit GeometryReadTransaction(std::shared_ptr<GeometryReadState> state) : m_State(std::move(state)) {}
    bool Execute(VansGraphicsDevice& backend) override
    {
        auto* device = dynamic_cast<VansVKDevice*>(&backend);
        return device && VansMeshGeometryReadback::Read(*device, m_State->meshes, m_State->geometry, m_State->error);
    }
private:
    std::shared_ptr<GeometryReadState> m_State;
};
std::vector<VansGeometryTriangle> Triangles(const VansMeshGeometryData& mesh, const std::vector<glm::vec3>& positions)
{
    std::vector<VansGeometryTriangle> triangles;
    triangles.reserve(mesh.indices.size() / 3);
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        VansGeometryTriangle triangle;
        triangle.a = positions[mesh.indices[i]];
        triangle.b = positions[mesh.indices[i + 1]];
        triangle.c = positions[mesh.indices[i + 2]];
        triangles.push_back(triangle);
    }
    return triangles;
}
bool SkinPositions(const VansRenderNode& node, const VansMeshGeometryData& mesh,
    std::vector<glm::vec3>& positions, std::string& error)
{
    const auto* owner = node.m_VertexDeformationState.skinningOwner
        ? node.m_VertexDeformationState.skinningOwner : node.m_AnimOwner;
    const auto* source = node.m_SourceMesh;
    const uint32_t index = node.m_VertexDeformationState.HasValidSkeletalSkinningResources()
        ? node.m_VertexDeformationState.submeshIndex : node.m_AnimSubmeshIndex;
    if (!owner || !source || index >= source->m_SubMeshBoneData.size() ||
        source->m_SubMeshBoneData[index].size() != mesh.positions.size())
    { error = "Scene picking could not resolve skeletal vertex weights."; return false; }
    const auto& weights = source->m_SubMeshBoneData[index];
    const auto& matrices = owner->GetBoneSSBO();
    positions.resize(mesh.positions.size());
    for (size_t vertex = 0; vertex < positions.size(); ++vertex)
    {
        glm::vec4 position(0);
        for (uint32_t influence = 0; influence < MAX_BONE_INFLUENCE; ++influence)
        {
            const float weight = weights[vertex].weights[influence];
            if (weight == 0) continue;
            const int bone = weights[vertex].boneIDs[influence];
            if (bone < 0 || bone >= MAX_BONES) { error = "Invalid bone index in Scene picking."; return false; }
            position += weight * matrices.boneMatrices[bone] * glm::vec4(mesh.positions[vertex], 1);
        }
        positions[vertex] = glm::vec3(position);
    }
    return true;
}
}

struct VansEditorSceneQuery::Cache
{
    struct Mesh
    {
        VkBuffer vertices = VK_NULL_HANDLE, indices = VK_NULL_HANDLE;
        uint32_t vertexCount = 0, indexCount = 0;
        VansMeshGeometryData geometry;
        VansTriangleGeometryQuery query;
    };
    std::unordered_map<VansMesh*, Mesh> meshes;

    bool Prepare(const std::vector<Instance>& instances, const std::vector<VansMesh*>& needed,
        IVansRenderThreadTransactionExecutor* renderer, std::string& error)
    {
        std::unordered_set<VansMesh*> alive;
        for (const auto& instance : instances) alive.insert(instance.node->m_Mesh);
        for (auto it = meshes.begin(); it != meshes.end();)
            if (!alive.count(it->first)) it = meshes.erase(it); else ++it;
        auto state = std::make_shared<GeometryReadState>();
        std::unordered_set<VansMesh*> added;
        for (auto* mesh : needed)
        {
            const auto found = meshes.find(mesh);
            if (found != meshes.end() && found->second.vertices == mesh->GetVertexBufferParameter().Buffer &&
                found->second.indices == mesh->GetIndexBufferParameter().Buffer &&
                found->second.vertexCount == mesh->GetMeshVertexCount() && found->second.indexCount == mesh->GetIndexCount()) continue;
            meshes.erase(mesh);
            if (added.insert(mesh).second) state->meshes.push_back(mesh);
        }
        if (state->meshes.empty()) return true;
        if (!renderer || !renderer->ExecuteRenderThreadTransaction(std::make_unique<GeometryReadTransaction>(state)))
        { error = state->error.empty() ? "Scene geometry readback transaction failed." : state->error; return false; }
        try
        {
            for (size_t i = 0; i < state->meshes.size(); ++i)
            {
                auto* mesh = state->meshes[i];
                Mesh cached;
                cached.vertices = mesh->GetVertexBufferParameter().Buffer;
                cached.indices = mesh->GetIndexBufferParameter().Buffer;
                cached.vertexCount = mesh->GetMeshVertexCount(); cached.indexCount = mesh->GetIndexCount();
                cached.geometry = std::move(state->geometry[i]);
                cached.query.Build(Triangles(cached.geometry, cached.geometry.positions));
                meshes.emplace(mesh, std::move(cached));
            }
        }
        catch (const std::exception& exception) { error = exception.what(); return false; }
        return true;
    }
};

VansEditorSceneQuery::VansEditorSceneQuery() : m_Cache(std::make_unique<Cache>()) {}
VansEditorSceneQuery::~VansEditorSceneQuery() = default;

EditorScenePickResult VansEditorSceneQuery::Pick(VansRuntimeWorld& runtimeWorld, const EditorScenePickRequest& request,
    IVansRenderThreadTransactionExecutor* renderer)
{
    EditorScenePickResult result;
    auto* world = &runtimeWorld;
    const glm::vec3 origin = Vector(request.ray.origin), direction = Vector(request.ray.direction);
    const float directionLength = glm::length(direction);
    if (!Finite(origin) || !Finite(direction) ||
        !std::isfinite(directionLength) || directionLength < 1e-6f ||
        !std::isfinite(request.maxDistance) || request.maxDistance <= 0)
    { result.message = "Scene picking requires a valid scene and finite ray."; return result; }
    const glm::vec3 unitDirection = direction / directionLength;
    const auto instances = CollectInstances(*world, true);
    const std::unordered_set<std::string> allowed(request.selectableEntities.begin(), request.selectableEntities.end());
    struct Candidate { Instance instance; std::string guid; glm::vec3 origin, direction; float stretch; };
    std::vector<Candidate> candidates;
    std::vector<VansMesh*> needed;
    for (const auto& instance : instances)
    {
        const std::string guid = ResolveDocumentEntity(*world, instance, allowed);
        if (guid.empty()) continue;
        const glm::mat4 model = instance.node->GetTransformMatrix();
        const float determinant = glm::determinant(model);
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12f) continue;
        const glm::mat4 inverse = glm::inverse(model);
        const glm::vec3 localOrigin(inverse * glm::vec4(origin, 1));
        const glm::vec3 localDirection(inverse * glm::vec4(unitDirection, 0));
        const float stretch = glm::length(localDirection);
        if (!Finite(localOrigin) || !Finite(localDirection) || !std::isfinite(stretch) || stretch <= 0) continue;
        auto* mesh = instance.node->m_Mesh;
        // 动画的 bind-pose 包围盒不能用于排除当前姿态。
        if (!instance.node->HasValidSkeletalSkinningResources() && mesh->HasLocalBounds() &&
            !IntersectEditorBounds(localOrigin, localDirection, mesh->GetLocalBoundsMin(), mesh->GetLocalBoundsMax(), request.maxDistance)) continue;
        candidates.push_back({instance, guid, localOrigin, localDirection, stretch});
        needed.push_back(mesh);
    }
    if (!m_Cache->Prepare(instances, needed, renderer, result.message)) return result;
    float nearest = request.maxDistance;
    for (const auto& candidate : candidates)
    {
        auto& mesh = m_Cache->meshes.at(candidate.instance.node->m_Mesh);
        const VansTriangleGeometryQuery* query = &mesh.query;
        VansTriangleGeometryQuery deformed;
        if (candidate.instance.node->HasValidSkeletalSkinningResources())
        {
            std::vector<glm::vec3> positions;
            if (!SkinPositions(*candidate.instance.node, mesh.geometry, positions, result.message)) return result;
            try { deformed.Build(Triangles(mesh.geometry, positions)); }
            catch (const std::exception& e) { result.message = e.what(); return result; }
            query = &deformed;
        }
        VansGeometryHit hit;
        if (query->Raycast(candidate.origin, candidate.direction, nearest * candidate.stretch, hit, 0))
        {
            const float distance = hit.distance / candidate.stretch;
            if (distance < nearest || (distance == nearest && (result.entityGuid.empty() || candidate.guid < result.entityGuid)))
            { nearest = distance; result.entityGuid = candidate.guid; }
        }
    }
    result.success = true;
    return result;
}

EditorSceneBounds VansEditorSceneQuery::Bounds(VansRuntimeWorld& runtimeWorld, const std::vector<std::string>& entityGuids,
    IVansRenderThreadTransactionExecutor* renderer)
{
    EditorSceneBounds bounds;
    auto* world = &runtimeWorld;
    if (entityGuids.empty()) return bounds;
    const std::unordered_set<std::string> selected(entityGuids.begin(), entityGuids.end());
    const auto instances = CollectInstances(*world, false);
    std::vector<VansMesh*> needed;
    for (const auto& instance : instances)
        if ((SelectedOrDescendant(*world, instance.owner, selected) || selected.count(instance.node->m_EntityGuid)) &&
            (instance.node->HasValidSkeletalSkinningResources() || !instance.node->m_Mesh->HasLocalBounds()))
            needed.push_back(instance.node->m_Mesh);
    std::string error;
    if (!m_Cache->Prepare(instances, needed, renderer, error))
    { VANS_LOG_WARN("[SceneFocus] " << error); return bounds; }
    std::unordered_set<std::string> framedEntities;
    for (const auto& instance : instances)
    {
        if (!SelectedOrDescendant(*world, instance.owner, selected) && !selected.count(instance.node->m_EntityGuid)) continue;
        const glm::mat4 model = instance.node->GetTransformMatrix();
        auto* mesh = instance.node->m_Mesh;
        EditorSceneBounds instanceBounds;
        if (instance.node->HasValidSkeletalSkinningResources())
        {
            const auto cached = m_Cache->meshes.find(mesh);
            if (cached == m_Cache->meshes.end()) continue;
            std::vector<glm::vec3> positions;
            if (!SkinPositions(*instance.node, cached->second.geometry, positions, error))
            { VANS_LOG_WARN("[SceneFocus] " << error); return {}; }
            for (const auto& p : positions) Expand(instanceBounds, glm::vec3(model * glm::vec4(p, 1)));
        }
        else if (mesh->HasLocalBounds())
        {
            const auto lo = mesh->GetLocalBoundsMin(), hi = mesh->GetLocalBoundsMax();
            for (int corner = 0; corner < 8; ++corner)
                Expand(instanceBounds, glm::vec3(model * glm::vec4(corner & 1 ? hi.x : lo.x,
                    corner & 2 ? hi.y : lo.y, corner & 4 ? hi.z : lo.z, 1)));
        }
        else
        {
            const auto cached = m_Cache->meshes.find(mesh);
            if (cached != m_Cache->meshes.end())
                for (const auto& p : cached->second.geometry.positions)
                    Expand(instanceBounds, glm::vec3(model * glm::vec4(p, 1)));
        }
        if (!instanceBounds.available) continue;
        Expand(bounds, Vector(instanceBounds.minimum));
        Expand(bounds, Vector(instanceBounds.maximum));
        if (selected.count(instance.node->m_EntityGuid)) framedEntities.insert(instance.node->m_EntityGuid);
        auto owner = instance.owner;
        while (const auto* record = world->Entities().Get(owner))
        {
            if (selected.count(record->stableGuid)) framedEntities.insert(record->stableGuid);
            owner = record->parent;
        }
    }
    // 每个没有几何的选择使用自己的位置；有几何的子树不额外纳入偏离模型的 pivot。
    for (const auto& guid : entityGuids)
    {
        if (framedEntities.count(guid)) continue;
        const auto entity = world->Entities().FindByGuid(guid);
        const auto component = world->FindComponentOwnedBy(entity, VansRuntimeComponentType_Transform);
        const auto* storage = static_cast<const VansComponentStorage<VansRuntimeTransformComponent>*>(
            world->FindStorage(VansRuntimeComponentType_Transform));
        const auto* transform = storage ? storage->Get(component) : nullptr;
        if (transform && VansTransformStore::IsAllocated(transform->transformStoreId))
            Expand(bounds, VansTransformStore::GetTransform(transform->transformStoreId).m_Position);
    }
    return bounds;
}

}
