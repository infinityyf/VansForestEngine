#pragma once

#include "../../SceneCore/VansSceneLodGroupComponentConfig.h"

#include <string>
#include <vector>

class VansScriptLodGroupComponent;
class VansScriptObject;
class VansScriptRenderComponent;

namespace VansGraphics
{
	class VansMesh;
	class VansScene;

	struct VansSceneLodGroupDependencies
	{
		bool success = true;
		std::string error;
		std::vector<std::vector<VansMesh*>> levelMeshes;
	};

	struct VansSceneLodGroupBuildResult
	{
		bool success = false;
		std::string error;
		VansScriptLodGroupComponent* component = nullptr;
	};

	class VansSceneLodGroupComponentBuilder
	{
	public:
		static VansSceneLodGroupDependencies ResolveDependencies(
			VansScene& scene,
			const Vans::VansSceneLodGroupComponentConfig& config);

		static VansSceneLodGroupBuildResult Build(
			VansScriptObject& object,
			const Vans::VansSceneLodGroupComponentConfig& config,
			const VansSceneLodGroupDependencies& dependencies,
			VansScriptRenderComponent* renderComponent,
			const std::string& componentGuid);
	};
}
