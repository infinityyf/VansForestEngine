#include "VansEditorSceneLoadController.h"

#include <filesystem>
#include <utility>

namespace
{
bool IsDifferentScene(
	const std::string& currentScenePath,
	const std::string& pendingScenePath)
{
	return !currentScenePath.empty() &&
		std::filesystem::path(currentScenePath).lexically_normal() !=
		std::filesystem::path(pendingScenePath).lexically_normal();
}

bool HasRequiredOperations(
	const VansGraphics::VansEditorSceneLoadContext& context,
	const VansGraphics::VansEditorSceneLoadOperations& operations)
{
	const bool common = operations.clearPendingRequest &&
		operations.refreshPrefabView &&
		operations.loadRuntimeScene &&
		operations.markLoaded &&
		operations.setTimePaused &&
		operations.setPlayState &&
		operations.setCurrentProjectScenePath &&
		operations.logInfo &&
		operations.logWarning &&
		operations.logError;
	if (!common)
		return false;
	if (!context.canReuseCurrentDocument &&
		(!operations.prepareDocument || !operations.commitPreparedDocument))
	{
		return false;
	}
	if (context.mode == Vans::EditorAPI::RuntimeSceneLoadMode::Editor)
		return static_cast<bool>(operations.detachEditorViewportCameras);
	return operations.installRuntimeVehiclePhysicsStepCallback &&
		operations.startRuntimePhysicsIfNeeded;
}
}

VansGraphics::VansEditorSceneLoadOutcome
VansGraphics::VansEditorSceneLoadController::Execute(
	const VansEditorSceneLoadContext& context,
	const VansEditorSceneLoadOperations& operations)
{
	if (context.pendingScenePath.empty())
		return VansEditorSceneLoadOutcome::NoRequest;

	if (context.hasPrefabSession)
	{
		if (!operations.logWarning || !operations.clearPendingRequest)
			return VansEditorSceneLoadOutcome::InvalidOperations;
		operations.logWarning("[Prefab] Close Prefab mode before switching scenes or playing");
		operations.clearPendingRequest();
		return VansEditorSceneLoadOutcome::RejectedPrefabSession;
	}

	if (context.sceneDirty &&
		IsDifferentScene(context.currentScenePath, context.pendingScenePath))
	{
		if (!operations.logWarning || !operations.clearPendingRequest)
			return VansEditorSceneLoadOutcome::InvalidOperations;
		operations.logWarning(
			"[Editor] Scene switch cancelled: save or undo current scene changes first");
		operations.clearPendingRequest();
		return VansEditorSceneLoadOutcome::RejectedDirtyScene;
	}

	if (!HasRequiredOperations(context, operations))
		return VansEditorSceneLoadOutcome::InvalidOperations;

	operations.logInfo(
		"[Editor] Loading deferred scene: " + context.pendingScenePath +
		" [mode=" +
		(context.mode == Vans::EditorAPI::RuntimeSceneLoadMode::Editor
			? "Editor" : "Runtime") + "]");

	if (!context.canReuseCurrentDocument &&
		!operations.prepareDocument(context.pendingScenePath))
	{
		operations.logError(
			"[Editor] Scene document validation failed before runtime scene switch: " +
			context.pendingScenePath);
		operations.clearPendingRequest();
		return VansEditorSceneLoadOutcome::DocumentLoadFailed;
	}

	if (!operations.refreshPrefabView())
	{
		operations.clearPendingRequest();
		return VansEditorSceneLoadOutcome::PrefabRefreshFailed;
	}

	const VansEditorRuntimeSceneLoadStatus runtimeLoad =
		operations.loadRuntimeScene(context.mode);
	if (!runtimeLoad.success)
	{
		operations.logError(
			"[Editor] Runtime scene load request failed: " +
			context.pendingScenePath);
		operations.clearPendingRequest();
		return VansEditorSceneLoadOutcome::RuntimeLoadFailed;
	}

	operations.markLoaded(context.pendingScenePath);
	if (!context.canReuseCurrentDocument)
		operations.commitPreparedDocument();

	if (context.mode == Vans::EditorAPI::RuntimeSceneLoadMode::Editor)
	{
		operations.logInfo(
			"[SceneDocument] Document ready: " + context.pendingScenePath +
			" [revision=" + std::to_string(runtimeLoad.contentRevision) + "]");
		operations.detachEditorViewportCameras();
		operations.setTimePaused(true);
		operations.setPlayState(Vans::EditorAPI::EnginePlayState::Edit);
	}
	else
	{
		operations.setTimePaused(false);
		operations.installRuntimeVehiclePhysicsStepCallback();
		operations.startRuntimePhysicsIfNeeded();
		operations.setPlayState(Vans::EditorAPI::EnginePlayState::Play);
		operations.logInfo("[Editor] Scene started playing (Runtime mode)");
	}

	operations.setCurrentProjectScenePath(context.pendingScenePath);
	operations.clearPendingRequest();
	return context.mode == Vans::EditorAPI::RuntimeSceneLoadMode::Editor
		? VansEditorSceneLoadOutcome::LoadedEditor
		: VansEditorSceneLoadOutcome::LoadedRuntime;
}
