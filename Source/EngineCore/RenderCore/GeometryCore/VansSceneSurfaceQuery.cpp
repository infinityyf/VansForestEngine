#include "VansSceneSurfaceQuery.h"

#include "VansMeshGeometryReadback.h"
#include "../VansRenderNode.h"
#include "../VulkanCore/VansRenderPass.h"
#include "../VulkanCore/VansShader.h"
#include "../../PhysicsCore/VansCollisionLayerManager.h"
#include "../../PhysicsCore/VansPhysicsNode.h"
#include "../../PhysicsCore/VansPhysicsQuery.h"
#include "../../SceneRuntime/Transform/VansTransformStore.h"
#include "../../SceneRuntime/VansComponentStorage.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansProfiler.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace VansGraphics
{
namespace
{
using namespace Vans;

std::uint64_t EntityKey(VansEntityHandle handle)
{
    return (std::uint64_t(handle.generation) << 32u) | handle.index;
}

template<class T>
auto* Storage(VansRuntimeWorld& world, std::uint16_t type)
{
    return world.FindStorage<T>(type);
}

bool Finite(const glm::vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool BelongsTo(const VansRuntimeWorld& world, VansEntityHandle entity, VansEntityHandle parent)
{
    if (!parent.IsValid()) return false;
    while (const auto* record = world.Entities().Get(entity))
    {
        if (entity == parent) return true;
        entity = record->parent;
    }
    return false;
}

std::vector<VansRenderNode*> Nodes(const VansRuntimeRenderComponent& component)
{
    auto nodes = component.renderNodes;
    if (component.renderNode &&
        std::find(nodes.begin(), nodes.end(), component.renderNode) == nodes.end())
        nodes.push_back(component.renderNode);
    return nodes;
}

bool Supported(const VansRenderNode* node)
{
    const auto* material = node ? dynamic_cast<const VansPBRMaterial*>(node->m_Material) : nullptr;
    return material && material->m_MaterialType == VAN_PBR && !material->m_AlphaTestEnabled &&
		node->GetNodeType() == OPAQUE_NODE && node->m_Mesh && !node->m_HasSkeletonBone &&
		!node->m_AnimationEnabled &&
        node->m_VertexDeformationState.BuildFeatureMask() == 0;
}

}

bool VansSceneSurfaceQuery::Prepare(VansVKDevice& device, std::string& error)
{
    using namespace Vans;
    error.clear();
    VansSceneSurfaceQuery pending(m_World);
    const auto publish = [&]()
    {
        m_GeometryProvider = {};
        m_Instances = std::move(pending.m_Instances);
        m_Triangles = std::move(pending.m_Triangles);
        m_GeometryOwners = std::move(pending.m_GeometryOwners);
        m_TransformRevision = pending.m_TransformRevision;
        m_IsPrepared = true;
    };

    auto* renders = Storage<VansRuntimeRenderComponent>(m_World, VansRuntimeComponentType_Render);
    auto* physics = Storage<VansRuntimePhysicsComponent>(m_World, VansRuntimeComponentType_Physics);
    if (!renders)
    {
        pending.m_TransformRevision = VansTransformStore::GetRevision();
        publish();
        return true;
    }

    std::vector<VansMesh*> meshes;
    std::unordered_set<VansMesh*> meshSet;
    std::unordered_set<std::uint64_t> unsupportedOwners;
    for (const auto& header : renders->Headers())
    {
        VansComponentHandle collider;
        VansEntityHandle colliderOwner;
        bool moving = false;
        auto ancestor = header.owner;
        while (const auto* entity = m_World.Entities().Get(ancestor))
        {
            for (const VansComponentHandle component : m_World.CollectComponentsOwnedBy(ancestor))
            {
                if (component.typeId == VansRuntimeComponentType_Animation ||
                    component.typeId == VansRuntimeComponentType_CharacterController)
                    moving = true;
                if (component.typeId != VansRuntimeComponentType_Physics || !physics) continue;
                const auto* body = physics->Get(component);
                if (!body || !body->physicsNode) continue;
                if (!collider.IsValid())
                {
                    collider = component;
                    colliderOwner = ancestor;
                }
                const auto& properties = body->physicsNode->GetProperties();
                if (properties.bodyType != VansEngine::PhysicsBodyType::Static || properties.isTrigger)
                    moving = true;
            }
            ancestor = entity->parent;
        }

        const auto* render = renders->Get(header.self);
        if (!render) continue;
        for (VansRenderNode* node : Nodes(*render))
        {
            if (moving || !Supported(node))
            {
                if (collider.IsValid()) unsupportedOwners.insert(EntityKey(colliderOwner));
                continue;
            }
            if (meshSet.insert(node->m_Mesh).second) meshes.push_back(node->m_Mesh);
            const auto* shader = node->m_Material->GetPassShader(VansPass::GBUFFER);
            const bool twoSided = shader &&
                shader->GetPipelineProgramDesc().graphicsState.cullMode == VK_CULL_MODE_NONE;
            pending.m_Instances.push_back({header.self, collider, node, 0u, twoSided});
            if (collider.IsValid()) pending.m_GeometryOwners.insert(EntityKey(colliderOwner));
        }
    }
    for (const std::uint64_t owner : unsupportedOwners) pending.m_GeometryOwners.erase(owner);

    std::vector<VansMeshGeometryData> data;
    if (!VansMeshGeometryReadback::Read(device, meshes, data, error)) return false;
    if (!pending.m_Triangles.Store(meshes, std::move(data), false, error)) return false;
    if (!pending.RebuildInstanceIndex(error)) return false;
    publish();
    VANS_LOG("[SurfaceQuery] Prepared instances=" << m_Instances.size()
        << " indexed=" << m_Triangles.InstanceCount() << " meshes=" << m_Triangles.MeshCount()
        << " triangles=" << m_Triangles.TriangleCount()
        << " geometryOwners=" << m_GeometryOwners.size());
    return true;
}

bool VansSceneSurfaceQuery::RebuildInstanceIndex(std::string& error)
{
    error.clear();
    std::vector<VansSceneTriangleInstance> instances;
    instances.reserve(m_Instances.size());
    for (std::uint32_t index = 0; index < m_Instances.size(); ++index)
    {
        Instance& instance = m_Instances[index];
        instance.transformRevision = instance.node
            ? Vans::VansTransformStore::GetTransformRevision(instance.node->m_TransformID) : 0u;
        if (!instance.node || !instance.node->m_Mesh ||
            !Vans::VansTransformStore::IsAllocated(instance.node->m_TransformID))
            continue;
        instances.push_back({instance.node->m_Mesh, instance.node->GetTransformMatrix(),
            index, uint64_t(UINT32_MAX - index), instance.twoSided});
    }
    if (!m_Triangles.BuildInstances(std::move(instances), error)) return false;
    m_TransformRevision = Vans::VansTransformStore::GetRevision();
    return true;
}

bool VansSceneSurfaceQuery::GeometryCovers(Vans::VansEntityHandle owner) const
{
    if (m_GeometryProvider.coversStaticOwner)
        return m_GeometryProvider.coversStaticOwner(owner);
    return m_GeometryOwners.count(EntityKey(owner)) != 0u;
}

bool VansSceneSurfaceQuery::QueryGeometry(const Vans::VansSurfaceQueryRequest& request,
    Vans::VansSurfaceImpact& impact, std::string& error)
{
    using namespace Vans;
    impact = {};
    error.clear();
    if (m_GeometryProvider.query)
        return m_GeometryProvider.query(request, impact, error);
    if (!m_IsPrepared)
    {
        error = "Scene geometry surface provider is not prepared";
        return false;
    }
    const std::uint64_t transformRevision = VansTransformStore::GetRevision();
    if (m_TransformRevision != transformRevision)
    {
        const bool indexedTransformChanged = std::any_of(
            m_Instances.begin(), m_Instances.end(), [](const Instance& instance)
            {
                return instance.node && instance.transformRevision !=
                    VansTransformStore::GetTransformRevision(instance.node->m_TransformID);
            });
        if (indexedTransformChanged)
        {
            if (!RebuildInstanceIndex(error)) return false;
        }
        else
        {
            m_TransformRevision = transformRevision;
        }
    }
    auto* renders = Storage<VansRuntimeRenderComponent>(m_World, VansRuntimeComponentType_Render);
    auto* physics = Storage<VansRuntimePhysicsComponent>(m_World, VansRuntimeComponentType_Physics);
    if (!renders) return true;

    auto resolveLayer = [&](std::uint32_t instanceIndex, int& layer)
    {
        if (instanceIndex >= m_Instances.size()) return false;
        const Instance& instance = m_Instances[instanceIndex];
        const auto* header = renders->GetHeader(instance.render);
        const auto* render = renders->Get(instance.render);
        if (!header || !header->effectiveEnabled || !render ||
            BelongsTo(m_World, header->owner, request.owner) ||
            BelongsTo(m_World, header->owner, request.instigator)) return false;
        if (render->renderNode != instance.node &&
            std::find(render->renderNodes.begin(), render->renderNodes.end(), instance.node) ==
                render->renderNodes.end()) return false;
        if (!instance.node->IsEnabled() || !Supported(instance.node)) return false;
        layer = 0;
        if (instance.collider.IsValid())
        {
            const auto* body = physics ? physics->Get(instance.collider) : nullptr;
            if (!body || !body->physicsNode || !body->physicsNode->IsEnabled() ||
                !m_World.IsComponentEffectivelyEnabled(instance.collider)) return false;
            const auto& properties = body->physicsNode->GetProperties();
            if (properties.bodyType != VansEngine::PhysicsBodyType::Static || properties.isTrigger ||
                !VansEngine::VansCollisionLayerManager::Get().TryGetLayerIndex(
                    properties.layerName, layer)) return false;
            return (request.blockingLayerMask & (1u << layer)) != 0u;
        }
        return request.includeColliderlessSurfaces;
    };

    VansSceneTriangleHit geometryHit;
    VansGeometryQueryOptions surfaceOptions;
    surfaceOptions.backfaces = VansGeometryBackfacePolicy::Report;
    if (!m_Triangles.Raycast(request.origin, request.direction, request.maximumDistance,
        [&](std::uint32_t instance)
        {
            int layer = 0;
            return resolveLayer(instance, layer);
        }, geometryHit, 0.0001f, surfaceOptions)) return true;

    int layer = 0;
    if (!resolveLayer(geometryHit.instance, layer)) return true;
    const Instance& instance = m_Instances[geometryHit.instance];
    const auto* header = renders->GetHeader(instance.render);
    if (!header) return true;
    VansTargetHitResult targetHit;
    targetHit.entity = targetHit.hitEntity = header->owner;
    targetHit.componentGuid = header->stableGuid;
    targetHit.position = {geometryHit.position.x, geometryHit.position.y, geometryHit.position.z};
    targetHit.normal = {geometryHit.normal.x, geometryHit.normal.y, geometryHit.normal.z};
    targetHit.distance = geometryHit.distance;
    impact = VansToSurfaceImpact(std::move(targetHit), VansSurfaceImpactKind::Render,
        VansEngine::VansCollisionLayerManager::Get().GetLayerName(layer));
    return true;
}

bool VansSceneSurfaceQuery::QueryPhysics(const Vans::VansSurfaceQueryRequest& request,
    const Vans::VansSurfaceImpact& geometryImpact,
    Vans::VansSurfaceImpact& impact, std::string& error) const
{
    using namespace Vans;
    struct Body
    {
        bool query = false;
        bool regional = false;
        std::string layerName;
        VansTargetHitResult hit;
    };

    impact = geometryImpact;
    auto& layers = VansEngine::VansCollisionLayerManager::Get();
    std::unordered_map<const void*, Body> bodies;
    if (const auto* storage = Storage<VansRuntimePhysicsComponent>(m_World,
        VansRuntimeComponentType_Physics))
    {
        const auto& headers = storage->Headers();
        const auto& data = storage->DenseData();
        for (std::size_t index = 0; index < headers.size() && index < data.size(); ++index)
        {
            const auto* physicsNode = data[index].physicsNode;
            if (!physicsNode || !physicsNode->GetActorIdentity()) continue;
            Body& body = bodies[physicsNode->GetActorIdentity()];
            if (!headers[index].effectiveEnabled || !physicsNode->IsEnabled() ||
                BelongsTo(m_World, headers[index].owner, request.owner) ||
                BelongsTo(m_World, headers[index].owner, request.instigator)) continue;
            const auto& properties = physicsNode->GetProperties();
            int layer = 0;
            if (!layers.TryGetLayerIndex(properties.layerName, layer)) continue;
            body.regional = properties.isTrigger && !properties.hitRegion.empty() &&
                static_cast<std::uint32_t>(layer) == request.regionalLayerIndex;
            body.query = body.regional || (!properties.isTrigger &&
                (request.blockingLayerMask & (1u << layer)) != 0u);
            // 几何 provider 对静态 owner 具有覆盖权，代理碰撞体不再作为第二份表面参与。
            if (!body.regional && properties.bodyType == VansEngine::PhysicsBodyType::Static &&
                GeometryCovers(headers[index].owner)) body.query = false;
            body.hit.entity = body.hit.hitEntity = headers[index].owner;
            body.hit.componentGuid = headers[index].stableGuid;
            body.hit.region = properties.hitRegion;
            body.layerName = properties.layerName;
        }
    }

    if (!VansEngine::VansPhysicsQuery::IsAvailable())
    {
        error = "Scene surface physics provider is unavailable";
        return false;
    }
    std::uint32_t physicsMask = request.blockingLayerMask;
    if (request.regionalLayerIndex < 32u)
        physicsMask |= 1u << request.regionalLayerIndex;
    if (physicsMask == 0u) return true;

    VansEngine::VansPhysicsRaycastRequest physicsRequest;
    physicsRequest.origin = request.origin;
    physicsRequest.direction = request.direction;
    physicsRequest.distance = request.maximumDistance;
    physicsRequest.filter.layerMask = physicsMask;
    physicsRequest.filter.includeControllers = false;
    physicsRequest.filter.accept = [&bodies, blockingMask = request.blockingLayerMask](
        const VansEngine::VansPhysicsQueryCandidate& candidate)
    {
        const auto found = bodies.find(candidate.actorIdentity);
        if (found != bodies.end()) return found->second.query;
        return !candidate.isTrigger && candidate.layerIndex < 32u &&
            (blockingMask & (1u << candidate.layerIndex)) != 0u;
    };

    VansEngine::VansPhysicsQueryHit physicsHit;
    if (!VansEngine::VansPhysicsQuery::RaycastClosest(physicsRequest, physicsHit) ||
        (geometryImpact.kind != VansSurfaceImpactKind::None &&
            physicsHit.distance > geometryImpact.hit.distance))
        return true;

    VansTargetHitResult surfaceHit;
    VansSurfaceImpactKind kind = VansSurfaceImpactKind::Unmapped;
    std::string layerName = physicsHit.layerIndex < 32u
        ? layers.GetLayerName(static_cast<int>(physicsHit.layerIndex)) : std::string{};
    const auto body = bodies.find(physicsHit.actorIdentity);
    if (body != bodies.end())
    {
        kind = body->second.regional ? VansSurfaceImpactKind::Regional
            : VansSurfaceImpactKind::Rigid;
        surfaceHit = body->second.hit;
        layerName = body->second.layerName;
    }
    else if (physicsHit.geometry == VansEngine::VansPhysicsGeometryType::HeightField)
    {
        kind = VansSurfaceImpactKind::Terrain;
    }
    surfaceHit.position = {physicsHit.position.x, physicsHit.position.y, physicsHit.position.z};
    surfaceHit.normal = {physicsHit.normal.x, physicsHit.normal.y, physicsHit.normal.z};
    surfaceHit.distance = physicsHit.distance;
    impact = VansToSurfaceImpact(std::move(surfaceHit), kind, std::move(layerName));
    return true;
}

bool VansSceneSurfaceQuery::Query(const Vans::VansSurfaceQueryRequest& request,
    Vans::VansSurfaceImpact& impact, std::string& error)
{
    VANS_PROFILE_SCOPE("Scene::SurfaceQuery", Vans::ProfileCategory::RenderPrepare);
    impact = {};
    error.clear();
    const float directionLength = glm::length(request.direction);
    if (!Finite(request.origin) || !Finite(request.direction) ||
        !std::isfinite(directionLength) || directionLength <= 1.0e-6f ||
        !std::isfinite(request.maximumDistance) || request.maximumDistance <= 0.0f)
    {
        error = "Scene surface query requires a finite ray and positive distance";
        return false;
    }

    Vans::VansSurfaceQueryRequest normalized = request;
    normalized.direction /= directionLength;
    Vans::VansSurfaceImpact geometryImpact;
    if (!QueryGeometry(normalized, geometryImpact, error)) return false;
    if (geometryImpact.kind != Vans::VansSurfaceImpactKind::None &&
        (!std::isfinite(geometryImpact.hit.distance) || geometryImpact.hit.distance < 0.0f ||
            geometryImpact.hit.distance > normalized.maximumDistance))
    {
        error = "Geometry surface provider returned an invalid hit distance";
        return false;
    }
    return QueryPhysics(normalized, geometryImpact, impact, error);
}
}
