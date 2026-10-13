#pragma once

#include "../AICore/VansAIRuntimeComponents.h"

namespace Vans
{
struct VansSceneAIAgentConfig
{
	bool enabled = true;
	VansRuntimeAIAgentComponent runtime;
};
}
