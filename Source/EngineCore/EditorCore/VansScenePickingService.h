#pragma once

#include "../EngineAPILayer/Public/EngineDTOs.h"

#include <string>

namespace Vans::EditorAPI
{
	class IEngineEditorAPI;
}

namespace Vans
{
	class VansScenePickingService
	{
	public:
		static EditorAPI::EditorScenePickResult Pick(
			EditorAPI::IEngineEditorAPI& editorAPI,
			const EditorAPI::Ray& ray,
			float maxDistance, bool toggle, bool additive);
	};
}
