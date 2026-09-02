#pragma once

#include "../SceneRuntime/VansRuntimeHandle.h"

namespace Vans
{
struct VansAIActivationRequested
{
	VansEntityHandle target;
};

struct VansAIGameplayReleased
{
	VansEntityHandle target;
};
}
