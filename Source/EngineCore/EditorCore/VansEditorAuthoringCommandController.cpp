#include "VansEditorAuthoringCommandController.h"

namespace
{
using namespace VansGraphics;

bool HasSceneSaveOperation(
	const VansEditorAuthoringCommandContext& context,
	const VansEditorAuthoringCommandOperations& operations)
{
	return context.hasPrefabSession
		? static_cast<bool>(operations.savePrefab)
		: static_cast<bool>(operations.saveSceneAndOwnedAssets);
}

bool SaveScene(
	const VansEditorAuthoringCommandContext& context,
	const VansEditorAuthoringCommandOperations& operations)
{
	return context.hasPrefabSession
		? operations.savePrefab()
		: operations.saveSceneAndOwnedAssets();
}
}

VansGraphics::VansEditorAuthoringCommandOutcome
VansGraphics::VansEditorAuthoringCommandController::Execute(
	VansEditorAuthoringCommand command,
	const VansEditorAuthoringCommandContext& context,
	const VansEditorAuthoringCommandOperations& operations)
{
	switch (command)
	{
	case VansEditorAuthoringCommand::SaveScene:
		if (!HasSceneSaveOperation(context, operations))
			return VansEditorAuthoringCommandOutcome::InvalidOperations;
		return SaveScene(context, operations)
			? VansEditorAuthoringCommandOutcome::Executed
			: VansEditorAuthoringCommandOutcome::SaveFailed;

	case VansEditorAuthoringCommand::SaveAsset:
		if (!operations.saveSelectedAsset ||
			!operations.reloadCurrentSceneForEditing)
		{
			return VansEditorAuthoringCommandOutcome::InvalidOperations;
		}
		{
			const VansEditorAuthoringSaveStatus result =
				operations.saveSelectedAsset();
			if (result.success && result.wroteFile)
				operations.reloadCurrentSceneForEditing();
			return result.success
				? VansEditorAuthoringCommandOutcome::Executed
				: VansEditorAuthoringCommandOutcome::SaveFailed;
		}

	case VansEditorAuthoringCommand::SaveProjectDocuments:
		if (!operations.saveProjectDocuments)
			return VansEditorAuthoringCommandOutcome::InvalidOperations;
		return operations.saveProjectDocuments()
			? VansEditorAuthoringCommandOutcome::Executed
			: VansEditorAuthoringCommandOutcome::SaveFailed;

	case VansEditorAuthoringCommand::SaveAll:
		if (!HasSceneSaveOperation(context, operations) ||
			(context.assetsDirty &&
				(!operations.saveAllDirtyAssets ||
				 !operations.reloadCurrentSceneForEditing)) ||
			(context.projectDocumentsDirty && !operations.saveProjectDocuments))
		{
			return VansEditorAuthoringCommandOutcome::InvalidOperations;
		}
		if (!SaveScene(context, operations))
			return VansEditorAuthoringCommandOutcome::SaveFailed;
		{
			bool success = true;
			if (context.assetsDirty)
			{
				const VansEditorAuthoringSaveStatus result =
					operations.saveAllDirtyAssets();
				if (result.success && result.wroteFile)
					operations.reloadCurrentSceneForEditing();
				success = result.success;
			}
			if (context.projectDocumentsDirty &&
				!operations.saveProjectDocuments())
			{
				success = false;
			}
			return success
				? VansEditorAuthoringCommandOutcome::Executed
				: VansEditorAuthoringCommandOutcome::SaveFailed;
		}

	case VansEditorAuthoringCommand::Exit:
		if (!operations.requestExit || !operations.logWarning)
			return VansEditorAuthoringCommandOutcome::InvalidOperations;
		if (context.sceneDirty)
		{
			operations.logWarning(
				"[Editor] Exit cancelled: save or undo current scene changes first");
			return VansEditorAuthoringCommandOutcome::ExitRejectedDirtyScene;
		}
		if (context.assetsDirty)
		{
			operations.logWarning(
				"[Editor] Exit cancelled: save or revert dirty asset changes first");
			return VansEditorAuthoringCommandOutcome::ExitRejectedDirtyAssets;
		}
		if (context.projectDocumentsDirty)
		{
			operations.logWarning(
				"[Editor] Exit cancelled: save or revert dirty project documents first");
			return VansEditorAuthoringCommandOutcome::ExitRejectedDirtyProjectDocuments;
		}
		operations.requestExit();
		return VansEditorAuthoringCommandOutcome::Executed;
	}

	return VansEditorAuthoringCommandOutcome::InvalidOperations;
}
