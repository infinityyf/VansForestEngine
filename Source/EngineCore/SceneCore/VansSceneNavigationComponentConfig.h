#pragma once

#include "../NavigationCore/VansNavigationRuntimeComponents.h"

namespace Vans
{
struct VansSceneNavigationSurfaceConfig
{
	bool enabled = true;
	VansRuntimeNavigationSurfaceComponent runtime;
};
struct VansSceneNavigationSourceConfig
{
	bool enabled = true;
	VansRuntimeNavigationSourceComponent runtime;
};
struct VansSceneNavigationModifierConfig
{
	bool enabled = true;
	VansRuntimeNavigationModifierComponent runtime;
};
struct VansSceneNavigationAgentConfig
{
	bool enabled = true;
	VansRuntimeNavigationAgentComponent runtime;
};
struct VansSceneNavigationObstacleConfig
{
    bool enabled=true;
    VansRuntimeNavigationObstacleComponent runtime;
};
struct VansSceneNavigationLinkConfig
{
    bool enabled=true;
    VansRuntimeNavigationLinkComponent runtime;
};
}
