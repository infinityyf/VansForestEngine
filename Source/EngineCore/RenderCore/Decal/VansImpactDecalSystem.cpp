#include "VansImpactDecalSystem.h"
#include "../VansRenderNode.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../SceneRuntime/VansComponentStorage.h"
#include "../../PhysicsCore/VansPhysicsNode.h"
#include "../../Util/VansProfiler.h"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace VansGraphics
{
namespace
{
template<class T> Vans::VansComponentStorage<T>* Storage(Vans::VansRuntimeWorld& world, uint16_t type)
{ return static_cast<Vans::VansComponentStorage<T>*>(world.FindStorage(type)); }
bool Supported(const VansRenderNode* node)
{
    return node && node->IsEnabled() && node->GetNodeType() == OPAQUE_NODE && node->m_Material &&
        node->m_Material->m_MaterialType == VAN_PBR && !node->m_HasSkeletonBone && !node->m_AnimationEnabled;
}
std::vector<VansRenderNode*> Nodes(const Vans::VansRuntimeRenderComponent& c)
{
    auto nodes = c.renderNodes;
    if (c.renderNode && std::find(nodes.begin(), nodes.end(), c.renderNode) == nodes.end()) nodes.push_back(c.renderNode);
    return nodes;
}
bool Finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}

VansImpactDecalSystem::VansImpactDecalSystem(Vans::VansRuntimeWorld& world, VansRenderNode* terrain)
    : m_World(world), m_Terrain(terrain) {}

bool VansImpactDecalSystem::AddPool(std::string source, const Vans::VansSceneImpactDecalConfig& config,
    const std::vector<VansDecalRenderNode*>& nodes, std::string& error)
{
    if (!config.IsValid() || nodes.size() != config.capacity || m_Entries.size() + nodes.size() > MaximumInstances ||
        std::any_of(nodes.begin(), nodes.end(), [](auto* node) { return !node; }) ||
        std::any_of(m_Pools.begin(), m_Pools.end(), [&](const auto& pool) { return pool.source == source; }))
    { error = "Impact decal pool configuration is invalid or exceeds scene capacity"; return false; }
    const size_t pool = m_Pools.size();
    m_Pools.push_back({std::move(source), config, m_Entries.size(), nodes.size()});
    for (auto* node : nodes)
    {
        node->SetEnabled(false);
        Entry entry;
        entry.node = node;
        entry.pool = pool;
        m_Entries.push_back(entry);
    }
    return true;
}

std::vector<Vans::VansComponentHandle> VansImpactDecalSystem::FindReceivers(Vans::VansEntityHandle entity) const
{
    using namespace Vans;
    auto* renders = Storage<VansRuntimeRenderComponent>(m_World, VansRuntimeComponentType_Render);
    auto* physics = m_World.FindStorage(VansRuntimeComponentType_Physics);
    if (!renders) return {};
    std::vector<VansComponentHandle> result;
    auto collect = [&](VansEntityHandle current) {
        for (const auto component : m_World.CollectComponentsOwnedBy(current))
        {
            if (component.typeId != VansRuntimeComponentType_Render || !m_World.IsComponentEffectivelyEnabled(component)) continue;
            const auto* data = renders->Get(component);
            if (data)
            {
                const auto nodes = Nodes(*data);
                if (std::any_of(nodes.begin(), nodes.end(), Supported)) result.push_back(component);
            }
        }
    };
    // 子渲染实体可以属于同一刚体，但有独立碰撞体的子树必须排除。
    std::vector<VansEntityHandle> pending{entity};
    while (!pending.empty())
    {
        const auto current = pending.back(); pending.pop_back();
        collect(current);
        if (const auto* record = m_World.Entities().Get(current))
            for (auto child : record->children)
                if (!physics || !physics->FindFirstOwnedBy(child).IsValid()) pending.push_back(child);
    }
    // 独立 collider 子实体可引用最近的渲染祖先；不跨越另一个刚体根。
    while (result.empty())
    {
        const auto* record = m_World.Entities().Get(entity);
        if (!record || !m_World.IsAlive(record->parent)) break;
        entity = record->parent;
        collect(entity);
        if (physics && physics->FindFirstOwnedBy(entity).IsValid()) break;
    }
    return result;
}

bool VansImpactDecalSystem::SetReceiverGroup(const ReceiverGroup& group, uint32_t id)
{
    auto* storage = Storage<Vans::VansRuntimeRenderComponent>(m_World, Vans::VansRuntimeComponentType_Render);
    bool any = false;
    if (storage) for (auto component : group.components)
    {
        if (const auto* data = storage->Get(component)) for (auto* node : Nodes(*data))
        {
            if (id == 0) { if (node) node->m_DecalReceiverId = 0; }
            else if (m_World.IsComponentEffectivelyEnabled(component) && Supported(node))
            { node->m_DecalReceiverId = id; any = true; }
        }
    }
    return any;
}

bool VansImpactDecalSystem::BuildPose(const glm::mat4& anchor, const glm::vec3& point,
    const glm::vec3& normal, const glm::vec3& tangent, const Vans::VansSceneImpactDecalConfig& config, VansTransform& pose)
{
    if (!config.IsValid() || !std::isfinite(glm::determinant(anchor)) || std::abs(glm::determinant(anchor)) < 1e-8f) return false;
    const glm::vec3 position = glm::vec3(anchor * glm::vec4(point, 1));
    glm::vec3 n = glm::transpose(glm::inverse(glm::mat3(anchor))) * normal;
    glm::vec3 t = glm::mat3(anchor) * tangent;
    if (!Finite(position) || !Finite(n) || !Finite(t) || glm::length(n) < 1e-6f) return false;
    n = glm::normalize(n);
    t -= n * glm::dot(n, t);
    if (glm::length(t) < 1e-6f) return false;
    t = glm::normalize(t);
    // 局部 +Y 为投影法线；XZ 足迹和世界尺寸不继承父级拉伸。
    const glm::mat3 rotation(t, n, glm::normalize(glm::cross(t, n)));
    pose.m_Position = position;
    pose.m_Rotation = glm::degrees(glm::eulerAngles(glm::quat_cast(rotation)));
    pose.m_Scale = {config.diameter * 0.5f, config.depth * 0.5f, config.diameter * 0.5f};
    return Finite(pose.m_Rotation);
}

void VansImpactDecalSystem::Retire(Entry& entry)
{
    if (entry.receiver && entry.receiver != TerrainReceiver)
    {
        auto& group = m_Groups[entry.receiver-1];
        if (group.references && --group.references == 0) { SetReceiverGroup(group, 0); group.components.clear(); }
    }
    entry.receiver = 0;
    entry.node->m_DecalReceiverId = 0;
    entry.node->SetEnabled(false);
}

bool VansImpactDecalSystem::Refresh(Entry& entry, bool force)
{
    glm::mat4 matrix(1);
    if (entry.receiver == TerrainReceiver)
    {
        if (!m_Terrain || !m_Terrain->IsEnabled()) return false;
        if (!force) return true;
    }
    else
    {
        bool anchorEnabled = false;
        if (entry.anchorComponent.typeId == Vans::VansRuntimeComponentType_Render)
        {
            auto* storage = Storage<Vans::VansRuntimeRenderComponent>(m_World, Vans::VansRuntimeComponentType_Render);
            const auto* render = storage ? storage->Get(entry.anchorComponent) : nullptr;
            if (render) for (auto* node : Nodes(*render))
                anchorEnabled |= Supported(node) && node->m_TransformID == entry.transform;
        }
        else
        {
            auto* storage = Storage<Vans::VansRuntimePhysicsComponent>(m_World, Vans::VansRuntimeComponentType_Physics);
            const auto* body = storage ? storage->Get(entry.anchorComponent) : nullptr;
            anchorEnabled = body && body->physicsNode && body->physicsNode->IsEnabled();
        }
        if (!anchorEnabled || !m_World.IsComponentEffectivelyEnabled(entry.anchorComponent) ||
            !VansTransformStore::IsAllocated(entry.transform) ||
            VansTransformStore::GetGeneration(entry.transform) != entry.transformGeneration) return false;
        const auto pose = VansTransformStore::GetTransform(entry.transform);
        if (!force && pose.m_Position == entry.lastAnchor.m_Position && pose.m_Rotation == entry.lastAnchor.m_Rotation &&
            pose.m_Scale == entry.lastAnchor.m_Scale) return true;
        entry.lastAnchor = pose;
        matrix = entry.lastAnchor.GetModelMatrix();
    }
    VansTransform pose;
    if (!BuildPose(matrix, entry.localPosition, entry.localNormal, entry.localTangent, m_Pools[entry.pool].config, pose)) return false;
    entry.node->SetTransformData(pose.m_Position, pose.m_Rotation, pose.m_Scale);
    return true;
}

bool VansImpactDecalSystem::Spawn(const std::string& source, const Vans::VansSurfaceImpact& impact, std::string& error)
{
    VANS_PROFILE_SCOPE("Decal::SpawnImpact", Vans::ProfileCategory::RenderPrepare);
    using namespace Vans;
    const auto pool = std::find_if(m_Pools.begin(), m_Pools.end(), [&](const auto& p) { return p.source == source; });
    if (pool == m_Pools.end()) { error = "Impact decal template is not prepared"; return false; }
    if (impact.kind != VansSurfaceImpactKind::Rigid && impact.kind != VansSurfaceImpactKind::Terrain &&
        impact.kind != VansSurfaceImpactKind::Render) return false;
    VansComponentHandle collider;
    glm::mat4 anchor(1);
    uint32_t transform = UINT32_MAX;
    std::vector<VansComponentHandle> receivers;
    if (impact.kind == VansSurfaceImpactKind::Render)
    {
        auto* storage = Storage<VansRuntimeRenderComponent>(m_World, VansRuntimeComponentType_Render);
        collider = storage ? storage->FindByStableGuid(impact.hit.componentGuid) : VansComponentHandle{};
        const auto* header = storage ? storage->GetHeader(collider) : nullptr;
        const auto* render = storage ? storage->Get(collider) : nullptr;
        if (!header || header->owner != impact.hit.hitEntity || !header->effectiveEnabled || !render) return false;
        for (auto* node : Nodes(*render)) if (Supported(node)) { transform = node->m_TransformID; break; }
        if (!VansTransformStore::IsAllocated(transform)) return false;
        anchor = VansTransformStore::GetTransform(transform).GetModelMatrix();
        receivers = {collider};
    }
    else if (impact.kind == VansSurfaceImpactKind::Rigid)
    {
        auto* storage = Storage<VansRuntimePhysicsComponent>(m_World, VansRuntimeComponentType_Physics);
        collider = storage ? storage->FindByStableGuid(impact.hit.componentGuid) : VansComponentHandle{};
        const auto* header = storage ? storage->GetHeader(collider) : nullptr;
        const auto* body = storage ? storage->Get(collider) : nullptr;
        if (!header || header->owner != impact.hit.hitEntity || !header->effectiveEnabled || !body || !body->physicsNode ||
            !body->physicsNode->IsEnabled() || body->physicsNode->GetProperties().isTrigger) return false;
        transform = body->physicsNode->GetTransformID();
        if (!VansTransformStore::IsAllocated(transform)) return false;
        anchor = VansTransformStore::GetTransform(transform).GetModelMatrix();
        receivers = FindReceivers(header->owner);
        if (receivers.empty()) return false;
    }
    else if (!m_Terrain || !m_Terrain->IsEnabled()) return false;
    glm::vec3 point(impact.hit.position[0],impact.hit.position[1],impact.hit.position[2]);
    glm::vec3 normal(impact.hit.normal[0],impact.hit.normal[1],impact.hit.normal[2]);
    if (!Finite(point) || !Finite(normal) || glm::length(normal) < 1e-6f || std::abs(glm::determinant(anchor)) < 1e-8f) return false;
    normal = glm::normalize(normal);
    const glm::vec3 reference = std::abs(normal.y) < 0.9f ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
    glm::vec3 tangent = glm::normalize(glm::cross(reference, normal));
    const float angle = static_cast<float>((m_SpawnCount * 2654435761ull) % 360) * 0.01745329252f;
    tangent = glm::vec3(glm::rotate(glm::mat4(1), angle, normal) * glm::vec4(tangent, 0));
    auto begin = m_Entries.begin() + pool->start, end = begin + pool->count;
    auto selected = std::find_if(begin, end, [](const auto& e) { return e.receiver == 0; });
    if (selected == end) selected = std::min_element(begin, end, [](const auto& a, const auto& b) { return a.sequence < b.sequence; });
    Retire(*selected);
    uint32_t receiver = TerrainReceiver;
    if (impact.kind == VansSurfaceImpactKind::Rigid || impact.kind == VansSurfaceImpactKind::Render)
    {
        size_t groupIndex = m_Groups.size();
        for (size_t i=0; i<m_Groups.size(); ++i)
            if (m_Groups[i].references && m_Groups[i].components == receivers) { groupIndex=i; break; }
        if (groupIndex == m_Groups.size())
        {
            for (size_t i=0; i<m_Groups.size(); ++i) if (!m_Groups[i].references) { groupIndex=i; break; }
            if (groupIndex == m_Groups.size()) return false;
            // 若映射出现部分重叠，不能覆盖仍被其他弹坑引用的接收组。
            for (const auto& group : m_Groups) if (group.references)
                for (auto component : receivers)
                    if (std::find(group.components.begin(), group.components.end(), component) != group.components.end()) return false;
            m_Groups[groupIndex].components = std::move(receivers);
        }
        receiver = static_cast<uint32_t>(groupIndex+1);
        ++m_Groups[groupIndex].references;
        SetReceiverGroup(m_Groups[groupIndex], receiver);
    }
    auto& entry = *selected;
    entry.receiver = receiver;
    entry.anchorComponent = collider;
    entry.transform = transform;
    entry.transformGeneration = VansTransformStore::GetGeneration(transform);
    entry.localPosition = glm::vec3(glm::inverse(anchor)*glm::vec4(point,1));
    entry.localNormal = glm::transpose(glm::mat3(anchor))*normal;
    entry.localTangent = glm::inverse(glm::mat3(anchor))*tangent;
    entry.expires = m_Time + pool->config.lifetimeSeconds;
    entry.sequence = ++m_SpawnCount;
    entry.node->m_DecalReceiverId = receiver;
    entry.node->m_DecalMinimumNormalDot = pool->config.minimumNormalDot;
    if (!Refresh(entry, true)) { Retire(entry); return false; }
    entry.node->SetEnabled(true);
    return true;
}

void VansImpactDecalSystem::Tick(double deltaSeconds)
{
    VANS_PROFILE_SCOPE("Decal::UpdateAttachments", Vans::ProfileCategory::RenderPrepare);
    if (std::isfinite(deltaSeconds) && deltaSeconds > 0) m_Time += deltaSeconds;
    for (size_t i=0; i<m_Groups.size(); ++i)
        if (m_Groups[i].references && !SetReceiverGroup(m_Groups[i], static_cast<uint32_t>(i+1)))
            for (auto& entry : m_Entries) if (entry.receiver == i+1) Retire(entry);
    for (auto& entry : m_Entries)
        if (entry.receiver && (entry.expires <= m_Time || !Refresh(entry, false))) Retire(entry);
}

std::vector<VansImpactDecalDebugEntry> VansImpactDecalSystem::CaptureDebug() const
{
    std::vector<VansImpactDecalDebugEntry> result;
    for (const auto& entry : m_Entries)
    {
        auto pose = VansTransformStore::GetTransform(entry.node->m_TransformID);
        result.push_back({entry.receiver != 0, entry.receiver, std::max(0.0, entry.expires-m_Time),
            pose.m_Position, glm::normalize(glm::vec3(pose.GetModelMatrix()[1])), pose.m_Scale});
    }
    return result;
}
}
