#pragma once

#include "../EngineAPILayer/Public/EngineDTOs.h"

#include <string>

namespace Vans::EditorAPI
{
	class IEngineEditorAPI;
}

namespace Vans
{
	struct ScenePickResult
	{
		bool hit = false;
		std::string entityGuid;
	};

	class VansScenePickingService
	{
	public:
		static ScenePickResult PickRuntimeEntity(
			EditorAPI::IEngineEditorAPI& editorAPI,
			const EditorAPI::Ray& ray,
			const std::string& source);
	};
}
