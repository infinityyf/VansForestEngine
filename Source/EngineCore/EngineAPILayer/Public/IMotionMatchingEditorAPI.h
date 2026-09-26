#pragma once

#include "EngineDTOs.h"

namespace Vans::EditorAPI
{
	class IMotionMatchingEditorAPI
	{
	public:
		virtual ~IMotionMatchingEditorAPI() = default;
		virtual MotionMatchingDebugSnapshot GetMotionMatchingDebugSnapshot() const = 0;
	};
}
