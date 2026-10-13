#pragma once
#include "VansPcgUpdatePlanner.h"

namespace Vans
{
struct VansPcgCollisionInstance
{
    std::string ownerGuid,componentGuid;
    VansPcgPoint point;
    VansPlantCollisionPrototype prototype;
};
struct VansPcgCollisionExclusion
{
    std::string id;
    std::uint64_t instanceCount=0;
};
struct VansPcgCollisionPlan
{
    std::vector<VansPcgCollisionInstance> instances;
    // Aggregated by region/layer/variant; render-only cells never allocate per-point records.
    std::vector<VansPcgCollisionExclusion> noCollisionPrototype;
};
// Only committed Scene composition consumes this plan. GPU preview/residency does not.
bool BuildPcgCollisionPlan(const VansPcgUpdatePlan& update,
    VansPcgCollisionPlan& output,std::string& error);
}
