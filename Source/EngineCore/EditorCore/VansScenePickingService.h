#pragma once

#include "../EngineAPILayer/Public/EngineDTOs.h"

#include <string>

namespace Vans::EditorAPI
{
	class ISceneInteractionEditorAPI;
}

namespace Vans
{
	class VansScenePickingService
	{
	public:
		static EditorAPI::EditorScenePickResult Pick(
			EditorAPI::ISceneInteractionEditorAPI& editorAPI,
			const EditorAPI::Ray& ray,
			float maxDistance, bool toggle, bool additive);
	};
}
