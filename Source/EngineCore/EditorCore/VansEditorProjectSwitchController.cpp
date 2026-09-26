#include "VansEditorProjectSwitchController.h"

namespace
{
using VansGraphics::VansEditorProjectSwitchOperations;

bool HasCoreOperations(const VansEditorProjectSwitchOperations& operations)
{
	return operations.clearPendingRequest && operations.setTimePaused &&
		operations.pauseRuntimePhysics && operations.unloadRuntimeScene &&
		operations.unloadRuntimeProjectResources && operations.closeProject &&
		operations.clearAssetHistories && operations.clearAssetDocuments &&
		operations.markProjectLoaded && operations.clearSceneSession &&
		operations.openProject && operations.logInfo && operations.logError;
}
}

VansGraphics::VansEditorProjectSwitchResult
VansGraphics::VansEditorProjectSwitchController::Execute(
	const VansEditorProjectSwitchContext& context,
	const VansEditorProjectSwitchOperations& operations)
{
	auto reject = [&](VansEditorProjectSwitchOutcome outcome, const char* message)
	{
		VansEditorProjectSwitchResult result;
		if (!operations.logWarning || !operations.clearPendingRequest)
			return result;
		operations.logWarning(message);
		operations.clearPendingRequest();
		result.outcome = outcome;
		return result;
	};

	if (context.hasPrefabSession)
		return reject(VansEditorProjectSwitchOutcome::RejectedPrefabSession,
			"[Prefab] Close Prefab mode before switching projects");
	if (context.sceneDirty)
		return reject(VansEditorProjectSwitchOutcome::RejectedDirtyScene,
			"[Editor] Project switch cancelled: save or undo current scene changes first");
	if (context.assetsDirty)
		return reject(VansEditorProjectSwitchOutcome::RejectedDirtyAssets,
			"[Editor] Project switch cancelled: save or revert dirty asset changes first");
	if (context.projectDocumentsDirty)
		return reject(VansEditorProjectSwitchOutcome::RejectedDirtyProjectDocuments,
			"[Editor] Project switch cancelled: save or revert dirty project documents first");

	VansEditorProjectSwitchResult result;
	if (!HasCoreOperations(operations))
		return result;

	operations.clearPendingRequest();
	operations.logInfo("[Editor] Processing pending project load: " + context.request.projectPath);
	operations.setTimePaused(true);
	operations.pauseRuntimePhysics();
	operations.unloadRuntimeScene();
	operations.unloadRuntimeProjectResources();
	operations.closeProject();
	operations.clearAssetHistories();
	operations.clearAssetDocuments();
	operations.markProjectLoaded(false);
	operations.clearSceneSession();

	Vans::EditorAPI::ProjectOpenRequest request;
	request.projectPath = context.request.projectPath;
	request.projectName = context.request.projectName;
	request.createNew = context.request.createNew;
	if (request.createNew)
	{
		operations.logInfo("[Editor] Creating project '" + request.projectName +
			"' at " + request.projectPath);
	}
	else
	{
		operations.logInfo("[Editor] Opening project: " + request.projectPath);
	}

	result.projectOpenResult = operations.openProject(request);
	if (!result.projectOpenResult.success)
	{
		operations.logError("[Editor] Pending project load failed: " + request.projectPath);
		result.outcome = VansEditorProjectSwitchOutcome::OpenFailed;
		return result;
	}

	operations.markProjectLoaded(true);
	operations.logInfo("[Editor] Project load completed");
	result.outcome = VansEditorProjectSwitchOutcome::Opened;
	return result;
}
