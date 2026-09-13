#pragma once
#include "VansGameplayTargeting.h"

namespace Vans
{
enum class VansSurfaceImpactKind { None, Rigid, Regional, Terrain, Unmapped, Render };
// 表面碰撞与伤害目标分开传递；地形允许没有 Runtime 实体，不伪造人物目标。
struct VansSurfaceImpact
{
    VansSurfaceImpactKind kind = VansSurfaceImpactKind::None;
    VansTargetHitResult hit;
    std::string layerName;
};
VansSerializedValue VansEncodeSurfaceImpact(const VansSurfaceImpact& impact);
bool VansDecodeSurfaceImpact(const VansSerializedValue& value, VansSurfaceImpact& impact, std::string& error);
}
