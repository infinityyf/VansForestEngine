#pragma once

#include "VansBaseWindowComponent.h"

namespace VansGraphics
{
	class VansReflectionProbeWindow : public VansBaseWindowComponent
	{
	public:

	private:
		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;
	};
}
