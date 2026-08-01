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
	class VansSceneResourceLoadContext;
}

namespace VansGraphics
{
	class VansSceneResourceBatchExecutor
	{
	public:
		static bool Execute(VansScene& scene, const Vans::VansSceneResourceBuildPlan& resourcePlan);
		static bool Execute(
			VansScene& scene,
			const Vans::VansSceneResourceBuildPlan& resourcePlan,
			const Vans::VansSceneResourceLoadContext& loadContext);

	private:
		static void FinalizeResourceBatch(VansScene& scene);
	};
}
