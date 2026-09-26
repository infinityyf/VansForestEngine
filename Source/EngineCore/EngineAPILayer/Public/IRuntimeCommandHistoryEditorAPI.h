#pragma once

#include "EngineDTOs.h"

namespace Vans::EditorAPI
{
	class IRuntimeCommandHistoryEditorAPI
	{
	public:
		virtual ~IRuntimeCommandHistoryEditorAPI() = default;
		virtual void BreakCommandMergeGroup() = 0;
		virtual EditorCommandHistorySnapshot GetRuntimeCommandHistory() const = 0;
		virtual void Undo() = 0;
		virtual void Redo() = 0;
	};
}
