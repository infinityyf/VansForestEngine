#pragma once

#include "VansBaseWindowComponent.h"

namespace VansGraphics
{
	class VansGIWindow : public VansBaseWindowComponent
	{
	private:
		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;
	};
}
