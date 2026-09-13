#pragma once
#include "VansTriangleGeometryQuery.h"
#include "../../GameplayTargeting/VansSurfaceImpact.h"
#include <unordered_set>

namespace Vans { class VansRuntimeWorld; }
namespace VansGraphics
{
class VansMesh;
class VansRenderNode;
class VansVKDevice;
// 每个共享网格只建立一份局部 BVH；查询时读取当前实例变换和组件有效性。
class VansSceneSurfaceQuery
{
public:
    explicit VansSceneSurfaceQuery(Vans::VansRuntimeWorld& world) : m_World(world) {}
    bool Prepare(VansVKDevice& device, std::string& error);
    bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float range, uint32_t layers,
        Vans::VansEntityHandle owner, Vans::VansEntityHandle instigator, Vans::VansSurfaceImpact& impact) const;
    bool HasPreciseCollider(Vans::VansEntityHandle owner) const;
private:
    struct Instance
    {
        Vans::VansComponentHandle render;
        Vans::VansComponentHandle collider;
        VansRenderNode* node = nullptr;
        uint32_t mesh = 0;
    };
    Vans::VansRuntimeWorld& m_World;
    std::vector<Instance> m_Instances;
    std::vector<VansTriangleGeometryQuery> m_Meshes;
    std::unordered_set<uint64_t> m_PreciseColliders;
};
}
