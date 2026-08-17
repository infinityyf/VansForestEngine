#pragma once

#include "../GameplayActionExecution/VansActionExecutionGraph.h"

namespace Vans
{
const std::vector<VansActionGraphNodeDescriptor>& VansCameraActionGraphNodeDescriptors();
bool VansRegisterCameraActionGraphNodes(
	VansActionGraphNodeRegistry& registry,
	std::string& error);
}
