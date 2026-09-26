#pragma once

#include <string>

namespace VansGraphics
{
class VansScene;
}

namespace Vans
{
class VansSceneGameplayComposition
{
public:
	static bool InitializeActionRuntime(VansGraphics::VansScene& scene, std::string& error);
	static bool ConfigureTimelineRuntime(VansGraphics::VansScene& scene, std::string& error);
};
}
