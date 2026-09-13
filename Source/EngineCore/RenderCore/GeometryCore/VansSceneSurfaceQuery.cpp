#include "VansSceneSurfaceQuery.h"
#include "VansMeshGeometryReadback.h"
#include "../VansRenderNode.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../SceneRuntime/VansComponentStorage.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../PhysicsCore/VansPhysicsNode.h"
#include "../../PhysicsCore/VansCollisionLayerManager.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansProfiler.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace VansGraphics
{
namespace
{
using namespace Vans;
uint64_t Key(VansEntityHandle h) { return (uint64_t(h.generation) << 32) | h.index; }
template<class T> auto* Storage(VansRuntimeWorld& w, uint16_t type)
{ return static_cast<VansComponentStorage<T>*>(w.FindStorage(type)); }
bool BelongsTo(const VansRuntimeWorld& world, VansEntityHandle entity, VansEntityHandle parent)
{
    while (const auto* record = world.Entities().Get(entity))
    { if (entity == parent) return true; entity = record->parent; }
    return false;
}
std::vector<VansRenderNode*> Nodes(const VansRuntimeRenderComponent& c)
{
    auto nodes = c.renderNodes;
    if (c.renderNode && std::find(nodes.begin(), nodes.end(), c.renderNode) == nodes.end()) nodes.push_back(c.renderNode);
    return nodes;
}
bool Supported(const VansRenderNode* node)
{
    const auto* material = node ? dynamic_cast<const VansPBRMaterial*>(node->m_Material) : nullptr;
    return material && material->m_MaterialType == VAN_PBR && !material->m_AlphaTestEnabled &&
        node->GetNodeType() == OPAQUE_NODE && node->m_Mesh && !node->m_HasSkeletonBone &&
        !node->m_AnimationEnabled && !node->m_AnimOwner && node->m_VertexDeformationState.BuildFeatureMask() == 0;
}
}

bool VansSceneSurfaceQuery::Prepare(VansVKDevice& device, std::string& error)
{
    using namespace Vans;
    m_Instances.clear(); m_Meshes.clear(); m_PreciseColliders.clear();
    auto* renders = Storage<VansRuntimeRenderComponent>(m_World, VansRuntimeComponentType_Render);
    auto* physics = Storage<VansRuntimePhysicsComponent>(m_World, VansRuntimeComponentType_Physics);
    if (!renders) return true;
    std::vector<VansMesh*> meshes;
    std::unordered_map<VansMesh*, uint32_t> meshIndices;
    std::unordered_set<uint64_t> unsupportedColliders;
    for (const auto& header : renders->Headers())
    {
        VansComponentHandle collider;
        VansEntityHandle colliderOwner;
        bool moving = false;
        auto ancestor = header.owner;
        while (const auto* entity = m_World.Entities().Get(ancestor))
        {
            for (const auto component : m_World.CollectComponentsOwnedBy(ancestor))
            {
                if (component.typeId == VansRuntimeComponentType_Animation ||
                    component.typeId == VansRuntimeComponentType_CharacterController) moving = true;
                if (component.typeId != VansRuntimeComponentType_Physics || !physics) continue;
                const auto* body = physics->Get(component);
                if (!body || !body->physicsNode) continue;
                if (!collider.IsValid()) { collider = component; colliderOwner = ancestor; }
                const auto& properties = body->physicsNode->GetProperties();
                if (properties.bodyType != VansEngine::PhysicsBodyType::Static || properties.isTrigger) moving = true;
            }
            ancestor = entity->parent;
        }
        const auto* render = renders->Get(header.self);
        if (!render) continue;
        for (auto* node : Nodes(*render))
        {
            if (moving || !Supported(node))
            { if (collider.IsValid()) unsupportedColliders.insert(Key(colliderOwner)); continue; }
            const auto inserted = meshIndices.emplace(node->m_Mesh, static_cast<uint32_t>(meshes.size()));
            if (inserted.second) meshes.push_back(node->m_Mesh);
            m_Instances.push_back({header.self, collider, node, inserted.first->second});
            if (collider.IsValid()) m_PreciseColliders.insert(Key(colliderOwner));
        }
    }
    for (auto key : unsupportedColliders) m_PreciseColliders.erase(key);
    std::vector<VansMeshGeometryData> data;
    if (!VansMeshGeometryReadback::Read(device, meshes, data, error)) return false;
    m_Meshes.resize(data.size());
    size_t triangles = 0;
    try
    {
        for (size_t i = 0; i < data.size(); ++i)
        {
            const auto& mesh = data[i];
            std::vector<VansGeometryTriangle> geometry;
            geometry.reserve(mesh.indices.size() / 3);
            for (size_t j = 0; j < mesh.indices.size(); j += 3)
            {
                const auto a = mesh.indices[j], b = mesh.indices[j+1], c = mesh.indices[j+2];
                VansGeometryTriangle triangle;
                triangle.a = mesh.positions[a]; triangle.b = mesh.positions[b]; triangle.c = mesh.positions[c];
                if (!mesh.normals.empty()) triangle.normal = mesh.normals[a] + mesh.normals[b] + mesh.normals[c];
                geometry.push_back(triangle);
            }
            m_Meshes[i].Build(std::move(geometry));
            triangles += m_Meshes[i].GetTriangles().size();
        }
    }
    catch (const std::exception& e) { error = e.what(); return false; }
    VANS_LOG("[Combat] Prepared precise surfaces instances=" << m_Instances.size() << " meshes=" << meshes.size()
        << " triangles=" << triangles << " replacedColliders=" << m_PreciseColliders.size());
    return true;
}

bool VansSceneSurfaceQuery::HasPreciseCollider(Vans::VansEntityHandle owner) const
{ return m_PreciseColliders.count(Key(owner)) != 0; }

bool VansSceneSurfaceQuery::Raycast(const glm::vec3& origin, const glm::vec3& direction, float range,
    uint32_t layers, Vans::VansEntityHandle owner, Vans::VansEntityHandle instigator, Vans::VansSurfaceImpact& impact) const
{
    VANS_PROFILE_SCOPE("Combat::PreciseSurfaceRaycast", Vans::ProfileCategory::RenderPrepare);
    using namespace Vans;
    impact = {};
    auto* renders = Storage<VansRuntimeRenderComponent>(m_World, VansRuntimeComponentType_Render);
    auto* physics = Storage<VansRuntimePhysicsComponent>(m_World, VansRuntimeComponentType_Physics);
    if (!renders) return false;
    float nearest = range;
    for (const auto& instance : m_Instances)
    {
        const auto* header = renders->GetHeader(instance.render);
        const auto* render = renders->Get(instance.render);
        if (!header || !header->effectiveEnabled || !render || BelongsTo(m_World, header->owner, owner) ||
            BelongsTo(m_World, header->owner, instigator)) continue;
        if (render->renderNode != instance.node &&
            std::find(render->renderNodes.begin(), render->renderNodes.end(), instance.node) == render->renderNodes.end()) continue;
        auto* node = instance.node;
        if (!node->IsEnabled() || !Supported(node)) continue;
        int layer = 0;
        if (instance.collider.IsValid())
        {
            const auto* body = physics ? physics->Get(instance.collider) : nullptr;
            if (!body || !body->physicsNode || !body->physicsNode->IsEnabled() ||
                !m_World.IsComponentEffectivelyEnabled(instance.collider)) continue;
            const auto& properties = body->physicsNode->GetProperties();
            if (properties.bodyType != VansEngine::PhysicsBodyType::Static || properties.isTrigger ||
                !VansEngine::VansCollisionLayerManager::Get().TryGetLayerIndex(properties.layerName, layer)) continue;
        }
        if ((layers & (1u << layer)) == 0) continue;
        const glm::mat4 model = node->GetTransformMatrix();
        if (!std::isfinite(glm::determinant(model)) || std::abs(glm::determinant(model)) < 1e-8f) continue;
        const glm::mat4 inverse = glm::inverse(model);
        const glm::vec3 localOrigin(inverse * glm::vec4(origin, 1));
        const glm::vec3 localDirection(inverse * glm::vec4(direction, 0));
        const float stretch = glm::length(localDirection);
        VansGeometryHit hit;
        if (!m_Meshes[instance.mesh].Raycast(localOrigin, localDirection, nearest * stretch, hit, 0.0001f * stretch)) continue;
        const float distance = hit.distance / stretch;
        const glm::vec3 normal = glm::normalize(glm::transpose(glm::mat3(inverse)) * hit.normal);
        // 双向几何命中阻挡射线，外侧法线来自实际三角形，不能使用碰撞盒轴向。
        nearest = distance;
        impact.kind = VansSurfaceImpactKind::Render;
        impact.layerName = VansEngine::VansCollisionLayerManager::Get().GetLayerName(layer);
        impact.hit.entity = impact.hit.hitEntity = header->owner;
        impact.hit.componentGuid = header->stableGuid;
        const glm::vec3 point = origin + direction * distance;
        impact.hit.position = {point.x, point.y, point.z};
        impact.hit.normal = {normal.x, normal.y, normal.z};
        impact.hit.distance = distance;
    }
    return impact.kind != VansSurfaceImpactKind::None;
}
}
