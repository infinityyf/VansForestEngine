#pragma once

#include <cmath>
#include <string>
#include <vector>
#include <variant>
#include <array>
#include <string_view>
#include <utility>
#include <glm/glm.hpp>

namespace Vans
{
enum class VansNavigationCollection : unsigned char { All, Hierarchy, ExplicitSources };
enum class VansNavigationSourceMode : unsigned char { Include, Exclude, Area };
enum class VansNavigationModifierShape : unsigned char { Box, Convex };
enum class VansNavigationObstacleGeometryKind : unsigned char { Box, Cylinder };
enum class VansNavigationObstacleMode : unsigned char { Carve, Avoidance, Both };
inline constexpr std::array<std::pair<VansNavigationObstacleMode,std::string_view>,3> kNavigationObstacleModes{{
    {VansNavigationObstacleMode::Carve,"Carve"},
    {VansNavigationObstacleMode::Avoidance,"Avoidance"},
    {VansNavigationObstacleMode::Both,"Both"}
}};
inline std::string_view NavigationObstacleModeName(VansNavigationObstacleMode mode)
{
    for(const auto& entry:kNavigationObstacleModes) if(entry.first==mode) return entry.second;
    return {};
}
inline bool ParseNavigationObstacleMode(std::string_view name,VansNavigationObstacleMode& mode)
{
    for(const auto& entry:kNavigationObstacleModes) if(entry.second==name) { mode=entry.first; return true; }
    return false;
}
inline bool NavigationObstacleCarves(VansNavigationObstacleMode mode)
{ return mode==VansNavigationObstacleMode::Carve || mode==VansNavigationObstacleMode::Both; }
inline bool NavigationObstacleAvoids(VansNavigationObstacleMode mode)
{ return mode==VansNavigationObstacleMode::Avoidance || mode==VansNavigationObstacleMode::Both; }

struct VansNavigationSourceReference
{
	std::string entityGuid;
	std::string componentGuid;
};

struct VansRuntimeNavigationSurfaceComponent
{
	std::string mapId = "World";
	std::vector<std::string> profiles{ "Humanoid" };
	glm::vec3 boundsCenterMeters{ 0.0f };
	glm::vec3 boundsHalfExtentsMeters{ 10.0f, 5.0f, 10.0f };
	VansNavigationCollection collection = VansNavigationCollection::All;
	std::vector<VansNavigationSourceReference> sources;
	std::vector<std::string> collisionLayers;
	std::string navigationMeshGuid;
};

inline bool ReferencesNavigationSource(const VansRuntimeNavigationSurfaceComponent& surface,
	const std::string& entityGuid,const std::string& componentGuid)
{
	for(const auto& source:surface.sources)
		if(source.entityGuid==entityGuid && source.componentGuid==componentGuid) return true;
	return false;
}

struct VansRuntimeNavigationSourceComponent
{
	VansNavigationSourceMode mode = VansNavigationSourceMode::Include;
	std::string area;
	std::string cpuMeshGuid;
};

struct VansRuntimeNavigationModifierComponent
{
	std::string mapId = "World";
	std::vector<std::string> profiles{ "Humanoid" };
	VansNavigationModifierShape shape = VansNavigationModifierShape::Box;
	glm::vec3 centerMeters{ 0.0f };
	glm::vec3 halfExtentsMeters{ 1.0f };
	std::vector<glm::vec2> convexVerticesMeters;
	float minimumHeightMeters = -1.0f;
	float maximumHeightMeters = 1.0f;
	std::string area = "Walkable";
	bool exclude = false;
	int priority = 0;
};

// Endpoints use owner-local metres; radius is a world-space admission/projection
// tolerance rather than a shape transformed with the owner.
struct VansRuntimeNavigationLinkComponent
{
    std::string mapId="World";
    std::vector<std::string> profiles{"Humanoid"};
    glm::vec3 startMeters{0},endMeters{2,0,0};
    float radiusMeters=.4f;
    std::string area="Walkable";
    bool bidirectional=true;
};

struct VansNavigationObstacleBox
{
    glm::vec3 centerMeters{0},halfExtentsMeters{.5f,1,.5f};
};
struct VansNavigationObstacleCylinder
{
    glm::vec3 centerMeters{0};
    float radiusMeters=.5f,heightMeters=2;
};
struct VansNavigationObstacleCollider
{
    std::string entityGuid,componentGuid;
};
using VansNavigationObstacleShape=std::variant<VansNavigationObstacleBox,VansNavigationObstacleCylinder,VansNavigationObstacleCollider>;
// 自有形状和显式组件绑定互斥；组合层冻结世界形状，核心不读取物理对象。
struct VansRuntimeNavigationObstacleComponent
{
    VansNavigationObstacleMode mode=VansNavigationObstacleMode::Carve;
    std::string mapId="World";
    std::vector<std::string> profiles{"Humanoid"};
    VansNavigationObstacleShape shape{VansNavigationObstacleCylinder{}};
};

struct VansRuntimeNavigationAgentComponent
{
	std::string mapId = "World";
	std::string profileId = "Humanoid";
	std::string queryFilter = "Default";
	float maxSpeed = 3.2f;
	float acceleration = 12.0f;
	bool avoidance = false;
	float repathInterval = 0.25f;
	float targetMoveThreshold = 0.75f;
	float forceRepathDistance = 6.0f;
};

inline bool ValidateNavigationAgentTiming(
	const VansRuntimeNavigationAgentComponent& navigation, std::string& error)
{
	if (navigation.mapId.empty() || navigation.profileId.empty() || navigation.queryFilter.empty() || !std::isfinite(navigation.repathInterval) ||
		navigation.repathInterval <= 0.0f ||
		!std::isfinite(navigation.targetMoveThreshold) ||
		navigation.targetMoveThreshold < 0.0f ||
		!std::isfinite(navigation.forceRepathDistance) ||
		navigation.forceRepathDistance <= navigation.targetMoveThreshold)
	{
		error = "NavigationAgent requires a query filter ID, positive repath interval and "
			"forceRepathDistance greater than targetMoveThreshold";
		return false;
	}
	error.clear();
	return true;
}
}
