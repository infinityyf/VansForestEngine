#pragma once

#include "VansBaseWindowComponent.h"

namespace VansGraphics
{
	class VansShadowDebuggerWindow final : public VansBaseWindowComponent
	{
	public:
		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;

	private:
		Vans::EditorAPI::PunctualScreenSpaceShadowSettingsSnapshot m_DraftSettings;
		bool m_DraftInitialized = false;
		int m_SelectedLight = -1;
		bool m_ShowAtlasLabels = true;
	};
}
