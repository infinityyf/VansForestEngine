#pragma once

#include "../EngineAPILayer/Public/EngineDTOs.h"

#include <functional>
#include <string>

namespace VansGraphics
{
	enum class VansEditorPlayCommand
	{
		Play,
		Pause,
		Resume,
		Stop
	};

	enum class VansEditorPlayCommandOutcome
	{
		Executed,
		IgnoredWrongState,
		RejectedPrefabSession,
		RejectedMissingScene,
		RejectedDirtyScene,
		InvalidOperations
	};

	struct VansEditorPlayCommandContext
	{
		Vans::EditorAPI::EnginePlayState playState =
			Vans::EditorAPI::EnginePlayState::Edit;
		bool hasPrefabSession = false;
		bool sceneDirty = false;
		std::string currentScenePath;
	};

	struct VansEditorPlayCommandOperations
	{
		std::function<void(bool)> setTimePaused;
		std::function<void()> pauseRuntimePhysics;
		std::function<void()> resumeRuntimePhysics;
		std::function<void(Vans::EditorAPI::EnginePlayState)> setPlayState;
		std::function<void(Vans::EditorAPI::RuntimeSceneLoadMode, const std::string&)>
			requestSceneLoad;
		std::function<void(const std::string&)> logInfo;
		std::function<void(const std::string&)> logWarning;
	};

	class VansEditorPlayCommandController final
	{
	public:
		static VansEditorPlayCommandOutcome Execute(
			VansEditorPlayCommand command,
			const VansEditorPlayCommandContext& context,
			const VansEditorPlayCommandOperations& operations);
	};
}
