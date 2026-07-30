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
		Vans::EditorAPI::PunctualShadowDebugSnapshot m_CachedSnapshot;
		bool m_DraftInitialized = false;
		bool m_HasCachedSnapshot = false;
		bool m_RequestPreviewNextFrame = true;
		double m_LastSnapshotTime = -1.0;
		int m_SelectedLight = -1;
		bool m_ShowAtlasLabels = true;
	};
}
