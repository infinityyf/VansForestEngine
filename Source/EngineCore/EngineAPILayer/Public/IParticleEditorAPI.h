#pragma once

#include "EngineDTOs.h"

namespace Vans::EditorAPI
{
	class IParticleEditorAPI
	{
	public:
		virtual ~IParticleEditorAPI() = default;
		virtual ParticleAuthoringSchemaSnapshot GetParticleAuthoringSchema() const = 0;
		virtual ParticleDebugSnapshot GetParticleDebugSnapshot() const = 0;
	};
}
