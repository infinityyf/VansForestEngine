#pragma once

#include "VansUIRuntimeHandles.h"
#include "VansUIVariant.h"

#include <string>

namespace VansRuntime
{
	struct VansUIActionEvent
	{
		std::string name;
		VansUIVariantMap params;
		VansUIHandleId sourceScreen = kInvalidUIHandle;
		std::string sourceElement;
	};
}
