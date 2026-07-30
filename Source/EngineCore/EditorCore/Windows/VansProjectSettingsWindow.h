#pragma once

#include "VansBaseWindowComponent.h"

namespace VansGraphics
{
	class VansProjectSettingsWindow final : public VansBaseWindowComponent
	{
	public:
		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;
	};
}

