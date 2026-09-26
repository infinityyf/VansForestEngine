#pragma once
#include "VansGameplayTargeting.h"
#include <glm/glm.hpp>
#include <cstdint>

namespace Vans
{
enum class VansSurfaceImpactKind { None, Rigid, Regional, Terrain, Unmapped, Render };
// 场景级表面查询只暴露引擎值；provider 的组合与最近命中裁决由场景查询层拥有。
struct VansSurfaceQueryRequest
{
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, 1.0f};
    float maximumDistance = 0.0f;
    std::uint32_t blockingLayerMask = 0u;
    std::uint32_t regionalLayerIndex = UINT32_MAX;
    VansEntityHandle owner;
    VansEntityHandle instigator;
    bool includeColliderlessSurfaces = false;
};
// 表面碰撞与伤害目标分开传递；地形允许没有 Runtime 实体，不伪造人物目标。
struct VansSurfaceImpact
{
    VansSurfaceImpactKind kind = VansSurfaceImpactKind::None;
    VansTargetHitResult hit;
    std::string layerName;
};
VansSurfaceImpact VansToSurfaceImpact(VansTargetHitResult hit,
    VansSurfaceImpactKind kind, std::string layerName = {});
VansTargetHitResult VansToTargetHitResult(const VansSurfaceImpact& impact);
VansSerializedValue VansEncodeSurfaceImpact(const VansSurfaceImpact& impact);
bool VansDecodeSurfaceImpact(const VansSerializedValue& value, VansSurfaceImpact& impact, std::string& error);
}
