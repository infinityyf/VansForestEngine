#pragma once

#include "VansBaseWindowComponent.h"

namespace VansGraphics
{
	struct VansEditorDebugViewState;

	class VansHiZCullWindow final : public VansBaseWindowComponent
	{
	public:
		explicit VansHiZCullWindow(VansEditorDebugViewState& debugViewState)
			: m_DebugViewState(debugViewState)
		{
		}

		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;

	private:
		VansEditorDebugViewState& m_DebugViewState;
	};
}
