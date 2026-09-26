#pragma once

#include "../SceneRuntime/VansRuntimeHandle.h"

#include <string>

namespace Vans
{
struct VansAIActivationRequested
{
	VansEntityHandle target;
	std::string sourceGuid;
};

struct VansAIGameplayReleased
{
	VansEntityHandle target;
	std::string sourceGuid;
};
}
