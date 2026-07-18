#pragma once
#include "VansBaseWindowComponent.h"

namespace VansGraphics
{
	class VansProjectWindow : public VansBaseWindowComponent
	{
	public:
		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI&) override;

	private:
		void DrawProjectContents(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
	};
}
