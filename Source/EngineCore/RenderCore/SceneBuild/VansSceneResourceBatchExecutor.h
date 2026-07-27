#pragma once

#include <string>

namespace VansGraphics
{
	class VansScene;
	class VansVKDevice;
}

namespace Vans
{
	struct VansSceneResourceBuildPlan;
}

namespace VansGraphics
{
	class VansSceneResourceBatchExecutor
	{
	public:
		static void Execute(VansScene& scene, const Vans::VansSceneResourceBuildPlan& resourcePlan);

	private:
		static void LoadEngineMeshes(VansScene& scene, const std::string& enginePrefix, VansVKDevice* vkDevice);
		static void FinalizeResourceBatch(VansScene& scene);
	};
}
