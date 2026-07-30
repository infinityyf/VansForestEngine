#pragma once

#include "EngineDTOs.h"

namespace Vans::EditorAPI
{
	struct VansEditorPlayStateChangedEvent
	{
		EnginePlayState previousState = EnginePlayState::Edit;
		EnginePlayState state = EnginePlayState::Edit;
	};
}
