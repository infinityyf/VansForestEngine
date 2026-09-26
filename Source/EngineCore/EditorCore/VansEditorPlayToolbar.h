#pragma once

#include "VansEditorPlayCommandController.h"

#include <functional>

namespace VansGraphics
{
	struct VansEditorToolbarConfiguration;

	struct VansEditorPlayToolbarState
	{
		bool playEnabled = false;
		bool pauseResumeEnabled = false;
		bool stopEnabled = false;
		bool showResume = false;
		VansEditorPlayCommand pauseResumeCommand = VansEditorPlayCommand::Pause;
	};

	class VansEditorPlayToolbar final
	{
	public:
		using CommandHandler = std::function<void(VansEditorPlayCommand)>;

		static VansEditorPlayToolbarState Resolve(
			Vans::EditorAPI::EnginePlayState playState,
			bool sceneReady);
		static void Draw(
			const VansEditorPlayToolbarState& state,
			const VansEditorToolbarConfiguration& configuration,
			const CommandHandler& execute);
	};
}
