#pragma once

#include "EngineDTOs.h"

namespace Vans::EditorAPI
{
	class IAIEditorAPI
	{
	public:
		virtual ~IAIEditorAPI() = default;
		virtual VansAIDiagnosticsSnapshot GetAIDiagnosticsSnapshot() const = 0;
	};
}
