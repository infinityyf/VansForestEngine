#include "VansEditorPlayCommandController.h"

VansGraphics::VansEditorPlayCommandOutcome
VansGraphics::VansEditorPlayCommandController::Execute(
	VansEditorPlayCommand command,
	const VansEditorPlayCommandContext& context,
	const VansEditorPlayCommandOperations& operations)
{
	using Vans::EditorAPI::EnginePlayState;
	using Vans::EditorAPI::RuntimeSceneLoadMode;

	switch (command)
	{
	case VansEditorPlayCommand::Play:
		if (context.playState != EnginePlayState::Edit)
			return VansEditorPlayCommandOutcome::IgnoredWrongState;
		if (context.hasPrefabSession)
		{
			if (!operations.logWarning)
				return VansEditorPlayCommandOutcome::InvalidOperations;
			operations.logWarning("[Prefab] Close Prefab mode before playing the scene");
			return VansEditorPlayCommandOutcome::RejectedPrefabSession;
		}
		if (context.currentScenePath.empty())
		{
			if (!operations.logWarning)
				return VansEditorPlayCommandOutcome::InvalidOperations;
			operations.logWarning("[Editor] OnPlay: no scene loaded, cannot start");
			return VansEditorPlayCommandOutcome::RejectedMissingScene;
		}
		if (context.sceneDirty)
		{
			if (!operations.logWarning)
				return VansEditorPlayCommandOutcome::InvalidOperations;
			operations.logWarning("[Editor] Save or undo scene changes before entering Play mode");
			return VansEditorPlayCommandOutcome::RejectedDirtyScene;
		}
		if (!operations.logInfo || !operations.requestSceneLoad)
			return VansEditorPlayCommandOutcome::InvalidOperations;
		operations.logInfo("[Editor] Play: reloading scene in Runtime mode: " +
			context.currentScenePath);
		operations.requestSceneLoad(RuntimeSceneLoadMode::Runtime, context.currentScenePath);
		return VansEditorPlayCommandOutcome::Executed;

	case VansEditorPlayCommand::Pause:
		if (context.playState != EnginePlayState::Play)
			return VansEditorPlayCommandOutcome::IgnoredWrongState;
		if (!operations.setTimePaused || !operations.pauseRuntimePhysics ||
			!operations.setPlayState || !operations.logInfo)
			return VansEditorPlayCommandOutcome::InvalidOperations;
		operations.setTimePaused(true);
		operations.pauseRuntimePhysics();
		operations.setPlayState(EnginePlayState::Pause);
		operations.logInfo("[Editor] Scene paused");
		return VansEditorPlayCommandOutcome::Executed;

	case VansEditorPlayCommand::Resume:
		if (context.playState != EnginePlayState::Pause)
			return VansEditorPlayCommandOutcome::IgnoredWrongState;
		if (!operations.setTimePaused || !operations.resumeRuntimePhysics ||
			!operations.setPlayState || !operations.logInfo)
			return VansEditorPlayCommandOutcome::InvalidOperations;
		operations.setTimePaused(false);
		operations.resumeRuntimePhysics();
		operations.setPlayState(EnginePlayState::Play);
		operations.logInfo("[Editor] Scene resumed");
		return VansEditorPlayCommandOutcome::Executed;

	case VansEditorPlayCommand::Stop:
		if (context.playState == EnginePlayState::Edit)
			return VansEditorPlayCommandOutcome::IgnoredWrongState;
		if (!operations.setTimePaused || !operations.pauseRuntimePhysics ||
			!operations.setPlayState || !operations.logInfo ||
			!operations.requestSceneLoad)
			return VansEditorPlayCommandOutcome::InvalidOperations;
		operations.setTimePaused(true);
		operations.pauseRuntimePhysics();
		operations.setPlayState(EnginePlayState::Edit);
		operations.logInfo("[Editor] Stop: reloading scene in Editor mode: " +
			context.currentScenePath);
		operations.requestSceneLoad(RuntimeSceneLoadMode::Editor, context.currentScenePath);
		return VansEditorPlayCommandOutcome::Executed;
	}

	return VansEditorPlayCommandOutcome::IgnoredWrongState;
}
