#pragma once

#include "VansSceneTriangleRaycastCache.h"
#include "../../GameplayTargeting/VansSurfaceImpact.h"

#include <functional>
#include <unordered_set>

namespace Vans { class VansRuntimeWorld; }
namespace VansGraphics
{
class VansRenderNode;
class VansVKDevice;

struct VansGeometrySurfaceProvider
{
    std::function<bool(const Vans::VansSurfaceQueryRequest&,
        Vans::VansSurfaceImpact&, std::string&)> query;
    std::function<bool(Vans::VansEntityHandle)> coversStaticOwner;
};

// 场景表面查询统一组合几何与物理 provider，并拥有最近命中裁决。
// 每个共享网格只建一份局部 BVH；静态实例使用世界 AABB BVH 筛选候选。
class VansSceneSurfaceQuery
{
public:
    explicit VansSceneSurfaceQuery(Vans::VansRuntimeWorld& world) : m_World(world) {}
    VansSceneSurfaceQuery(Vans::VansRuntimeWorld& world, VansGeometrySurfaceProvider geometryProvider)
        : m_World(world), m_GeometryProvider(std::move(geometryProvider)), m_IsPrepared(true) {}

    bool Prepare(VansVKDevice& device, std::string& error);
    bool Query(const Vans::VansSurfaceQueryRequest& request,
        Vans::VansSurfaceImpact& impact, std::string& error);

private:
    struct Instance
    {
        Vans::VansComponentHandle render;
        Vans::VansComponentHandle collider;
        VansRenderNode* node = nullptr;
        std::uint64_t transformRevision = 0;
        bool twoSided = false;
    };

    bool QueryGeometry(const Vans::VansSurfaceQueryRequest& request,
        Vans::VansSurfaceImpact& impact, std::string& error);
    bool QueryPhysics(const Vans::VansSurfaceQueryRequest& request,
        const Vans::VansSurfaceImpact& geometryImpact,
        Vans::VansSurfaceImpact& impact, std::string& error) const;
    bool RebuildInstanceIndex(std::string& error);
    bool GeometryCovers(Vans::VansEntityHandle owner) const;

    Vans::VansRuntimeWorld& m_World;
    VansGeometrySurfaceProvider m_GeometryProvider;
    std::vector<Instance> m_Instances;
    VansSceneTriangleRaycastCache m_Triangles;
    std::unordered_set<std::uint64_t> m_GeometryOwners;
    std::uint64_t m_TransformRevision = 0;
    bool m_IsPrepared = false;
};
}
