#pragma once

#include "../AICore/VansAIRuntimeComponents.h"

namespace Vans
{
struct VansSceneNavigationAgentConfig
{
	bool enabled = true;
	VansRuntimeNavigationAgentComponent runtime;
};

struct VansSceneAIAgentConfig
{
	bool enabled = true;
	VansRuntimeAIAgentComponent runtime;
};
}
