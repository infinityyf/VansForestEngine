#pragma once

#include "../../EngineAPILayer/Public/IEngineEditorAPI.h"

namespace VansGraphics
{
	class VansBaseWindowComponent
	{
	public:
		virtual ~VansBaseWindowComponent() = default;

		virtual void ShowWindow(Vans::EditorAPI::IEngineEditorAPI&)
		{
		}
	};
}

