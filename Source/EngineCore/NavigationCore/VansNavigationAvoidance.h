#pragma once
#include "VansNavigationQueryContext.h"
#include <memory>

namespace Vans
{
struct VansNavigationObstacleGeometry;
struct VansNavigationAvoidanceYield
{
    std::uint64_t peer=0;
    glm::vec3 direction{0};
    float seconds=0;
};
struct VansNavigationAvoidanceAgent
{
    std::uint64_t identity=0;
    std::uint32_t map=0;
    std::uint32_t profile=0;
    bool reciprocal=true;
    // Position is the navigation ground anchor; height extends upward from it.
    glm::vec3 position{0},velocity{0};
    float radiusMeters=0,heightMeters=0;
    VansNavigationAvoidanceYield yield;
};
struct VansNavigationAvoidanceResult
{
    glm::vec3 velocity{0};
    std::uint32_t neighbors=0,obstacles=0,scans=0,samples=0;
    bool limited=false;
    VansNavigationAvoidanceYield yield;
};
// Owns only bounded frame facts/hash and one serial velocity sampler.
class VansNavigationAvoidance
{
public:
    VansNavigationAvoidance();
    ~VansNavigationAvoidance();
    bool Initialize(int maximumAgents,const VansNavigationAvoidanceSettings& settings,std::string& error);
    void BeginFrame();
    int AddAgent(const VansNavigationAvoidanceAgent& agent);
    bool AddObstacle(std::uint64_t identity,std::uint32_t map,std::uint32_t profileMask,
        const VansNavigationObstacleGeometry& geometry,const glm::vec3& velocity);
    VansNavigationAvoidanceResult SelectVelocity(int agent,const glm::vec3& preferred,float maximumSpeed,const VansNavigationLocalBoundary& boundary);
private:
    struct State;
    std::unique_ptr<State> m_State;
};
}
