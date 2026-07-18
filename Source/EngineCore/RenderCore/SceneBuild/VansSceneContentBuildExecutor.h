#pragma once

#include "../VansScene.h"

namespace VansGraphics
{
	class VansSceneContentBuildExecutor
	{
	public:
		static bool BuildFromFile(VansScene& scene, const char* path);

	private:
		static void ApplyVolumetricCloudSettings(VansMaterialManager& materialManager, const json& sceneData);
		static void ApplyGISettings(VansScene& scene, const json& sceneData);
		static std::string ResolveProjectRootFromScenePath(const char* path);
	};
}
