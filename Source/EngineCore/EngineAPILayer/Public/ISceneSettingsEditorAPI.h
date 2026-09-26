#pragma once

#include "EngineDTOs.h"

namespace Vans::EditorAPI
{
	class ISceneSettingsEditorAPI
	{
	public:
		virtual ~ISceneSettingsEditorAPI() = default;
		virtual LightingSettingsSnapshot GetLightingSettings() const = 0;
		virtual void ApplyLightingSettings(const LightingSettingsSnapshot& settings) = 0;
		virtual PostProcessSettingsSnapshot GetPostProcessSettings() const = 0;
		virtual void ApplyPostProcessSettings(const PostProcessSettingsSnapshot& settings) = 0;
		virtual void CommitPostProcessSettings() = 0;
		virtual EnvironmentSettings GetEnvironmentSettings() const = 0;
		virtual void ApplyEnvironmentSettings(const EnvironmentSettings& settings) = 0;
		virtual void CommitEnvironmentSettings() = 0;
	};
}
