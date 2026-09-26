#pragma once

namespace Vans
{
	class VansSceneDocument;
	class VansSceneEditService;
}

namespace Vans::EditorAPI
{
	class IEngineEditorAPI;
}

namespace Vans
{
	class IVansEditorAPIHost
	{
	public:
		virtual ~IVansEditorAPIHost() = default;

		virtual EditorAPI::IEngineEditorAPI& AccessEditorAPI(
			VansSceneDocument* sceneDocument,
			VansSceneEditService* sceneEditService) = 0;
	};
}
