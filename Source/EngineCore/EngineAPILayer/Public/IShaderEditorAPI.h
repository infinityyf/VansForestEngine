#pragma once

#include "EngineDTOs.h"

#include <vector>

namespace Vans::EditorAPI
{
	class IShaderEditorAPI
	{
	public:
		virtual ~IShaderEditorAPI() = default;
		virtual std::vector<ShaderProgramSourceSnapshot> QueryShaderProgramSources() const = 0;
		virtual ShaderCandidateApplyResult ApplyShaderCandidateAtRenderSafePoint(
			const ShaderCandidatePackage& package) = 0;
	};
}
