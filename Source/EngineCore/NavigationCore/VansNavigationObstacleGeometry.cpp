#include "VansNavigationSourceSnapshot.h"
#include <cmath>
#include <algorithm>

namespace Vans
{
bool ValidateNavigationObstacleRecord(const VansNavigationObstacleRecord& obstacle,
    const VansNavigationSettings& settings,std::string& error)
{
    if(NavigationObstacleModeName(obstacle.mode).empty()) { error="Navigation obstacle mode is invalid"; return false; }
    if(!ValidateNavigationObstacleGeometry(obstacle.shape,error)) return false;
    const auto* map=FindNavigationMap(settings,obstacle.mapId);
    if(!map || obstacle.profiles.empty() || obstacle.profiles.size()>16)
    { error="Navigation obstacle has invalid Map/Profile"; return false; }
    for(std::size_t i=0;i<obstacle.profiles.size();++i)
    {
        const auto& profile=obstacle.profiles[i];
        if(profile.empty() || profile.size()>256 ||
            std::any_of(profile.begin(),profile.end(),[](unsigned char c){return c<32 || c==127;}) ||
            std::find(obstacle.profiles.begin(),obstacle.profiles.begin()+i,profile)!=obstacle.profiles.begin()+i)
        { error="Navigation obstacle has invalid or duplicate Profile"; return false; }
        if(std::find(map->profiles.begin(),map->profiles.end(),profile)==map->profiles.end())
        { error="Navigation obstacle references an unavailable Profile"; return false; }
    }
    if(obstacle.enabled && NavigationObstacleCarves(obstacle.mode) && map->updateMode!=VansNavigationMapUpdateMode::Runtime)
    { error="Navigation Carve requires a Runtime Map"; return false; }
    if(NavigationObstacleAvoids(obstacle.mode))
    {
        glm::vec3 minimum,maximum; NavigationObstacleGeometryBounds(obstacle.shape,minimum,maximum);
        std::array<int,4> cells{};
        if(!NavigationAvoidanceGridBounds(minimum,maximum,settings.avoidance,cells,error)) return false;
    }
    error.clear(); return true;
}
bool ValidateNavigationObstacleGeometry(const VansNavigationObstacleGeometry& shape,std::string& error)
{
    if(shape.id.empty() || shape.id.size()>256 ||
        (shape.kind!=VansNavigationObstacleGeometryKind::Box && shape.kind!=VansNavigationObstacleGeometryKind::Cylinder))
    { error="Navigation obstacle shape has invalid identity or kind"; return false; }
    for(int axis=0;axis<3;++axis) if(!std::isfinite(shape.center[axis]) || !std::isfinite(shape.halfExtents[axis]))
    { error="Navigation obstacle shape requires finite world dimensions"; return false; }
    if(!std::isfinite(shape.radius) || !std::isfinite(shape.height) || !std::isfinite(shape.yawRadians) ||
        shape.radius<=0 || shape.height<=0 || glm::any(glm::lessThanEqual(shape.halfExtents,glm::vec3(0))))
    { error="Navigation obstacle shape dimensions must be finite and positive"; return false; }
    glm::vec3 minimum,maximum; NavigationObstacleGeometryBounds(shape,minimum,maximum);
    for(int axis=0;axis<3;++axis) if(!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]))
    { error="Navigation obstacle shape world bounds overflow"; return false; }
    error.clear(); return true;
}
void NavigationObstacleGeometryBounds(const VansNavigationObstacleGeometry& shape,glm::vec3& minimum,glm::vec3& maximum)
{
    if(shape.kind==VansNavigationObstacleGeometryKind::Cylinder)
    { minimum=shape.center-glm::vec3(shape.radius,0,shape.radius); maximum=shape.center+glm::vec3(shape.radius,shape.height,shape.radius); }
    else
    {
        const float cosine=std::abs(std::cos(shape.yawRadians)),sine=std::abs(std::sin(shape.yawRadians));
        const glm::vec3 extent(cosine*shape.halfExtents.x+sine*shape.halfExtents.z,shape.halfExtents.y,
            sine*shape.halfExtents.x+cosine*shape.halfExtents.z);
        minimum=shape.center-extent; maximum=shape.center+extent;
    }
}
bool FreezeNavigationObstacleGeometry(const VansNavigationObstacleShape& shape,const glm::mat4& worldTransform,
    const std::string& id,VansNavigationObstacleGeometry& output,std::string& error)
{
    const auto* box=std::get_if<VansNavigationObstacleBox>(&shape);
    const auto* cylinder=std::get_if<VansNavigationObstacleCylinder>(&shape);
    if(!box && !cylinder) { error="NavigationObstacle Collider binding must be resolved by its composition owner"; return false; }
    for(int c=0;c<4;++c) for(int r=0;r<4;++r) if(!std::isfinite(worldTransform[c][r]))
    { error="NavigationObstacle requires a finite Transform"; return false; }
    const glm::vec3 axes[3]{glm::vec3(worldTransform[0]),glm::vec3(worldTransform[1]),glm::vec3(worldTransform[2])};
    const glm::vec3 scale(glm::length(axes[0]),glm::length(axes[1]),glm::length(axes[2]));
    if(glm::any(glm::lessThanEqual(scale,glm::vec3(1.e-6f))) ||
        std::abs(worldTransform[0][3])+std::abs(worldTransform[1][3])+std::abs(worldTransform[2][3])+std::abs(worldTransform[3][3]-1)>1.e-5f)
    { error="NavigationObstacle requires a nondegenerate affine Transform"; return false; }
    const auto x=axes[0]/scale.x,y=axes[1]/scale.y,z=axes[2]/scale.z;
    if(std::abs(x.y)+std::abs(z.y)>1.e-4f || glm::length(y-glm::vec3(0,1,0))>1.e-4f ||
        std::abs(glm::dot(x,z))>1.e-4f || glm::dot(glm::cross(x,y),z)<.9999f ||
        (cylinder && std::abs(scale.x-scale.z)>1.e-4f*(std::max)(scale.x,scale.z)))
    { error="NavigationObstacle supports upright Box/Cylinder without shear; Cylinder requires equal X/Z scale"; return false; }
    VansNavigationObstacleGeometry candidate;
    candidate.id=id; candidate.kind=box?VansNavigationObstacleGeometryKind::Box:VansNavigationObstacleGeometryKind::Cylinder;
    candidate.center=glm::vec3(worldTransform*glm::vec4(box?box->centerMeters:cylinder->centerMeters,1));
    if(box) candidate.halfExtents=box->halfExtentsMeters*scale;
    else { candidate.radius=cylinder->radiusMeters*scale.x; candidate.height=cylinder->heightMeters*scale.y; }
    candidate.yawRadians=std::atan2(-x.z,x.x);
    if(!ValidateNavigationObstacleGeometry(candidate,error)) return false;
    output=std::move(candidate); error.clear(); return true;
}
}
