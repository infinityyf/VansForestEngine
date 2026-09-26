#pragma once

#include "VansBaseWindowComponent.h"

namespace VansGraphics
{
	struct VansEditorDebugViewState;

	class VansSkeletonDebugWindow final : public VansBaseWindowComponent
	{
	public:
		explicit VansSkeletonDebugWindow(VansEditorDebugViewState& debugViewState)
			: m_DebugViewState(debugViewState)
		{
		}

		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;

	private:
		VansEditorDebugViewState& m_DebugViewState;
	};
}
