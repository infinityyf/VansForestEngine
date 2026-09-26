#include "VansEditorShellCommandController.h"

namespace VansGraphics
{
	VansEditorShellShortcutResolution VansEditorShellCommandController::ResolveShortcut(
		const VansEditorShellShortcutState& state)
	{
		if (!state.editingMode || state.wantTextInput || !state.controlDown)
			return {};
		if (state.shiftDown && state.savePressed)
			return { true, { VansEditorShellCommandType::SaveAll } };
		if (state.sceneDocumentReady && state.savePressed)
			return { true, { VansEditorShellCommandType::SaveScene } };
		if (state.canUndo && state.undoPressed)
			return { true, { VansEditorShellCommandType::Undo } };
		if (state.canRedo && state.redoPressed)
			return { true, { VansEditorShellCommandType::Redo } };
		return {};
	}

	bool VansEditorShellCommandController::Execute(
		const VansEditorShellCommand& command,
		const VansEditorShellCommandOperations& operations)
	{
		switch (command.type)
		{
		case VansEditorShellCommandType::SaveScene:
			if (!operations.executeAuthoringCommand) return false;
			operations.executeAuthoringCommand(VansEditorAuthoringCommand::SaveScene);
			return true;
		case VansEditorShellCommandType::SaveAsset:
			if (!operations.executeAuthoringCommand) return false;
			operations.executeAuthoringCommand(VansEditorAuthoringCommand::SaveAsset);
			return true;
		case VansEditorShellCommandType::SaveProjectDocuments:
			if (!operations.executeAuthoringCommand) return false;
			operations.executeAuthoringCommand(VansEditorAuthoringCommand::SaveProjectDocuments);
			return true;
		case VansEditorShellCommandType::SaveAll:
			if (!operations.executeAuthoringCommand) return false;
			operations.executeAuthoringCommand(VansEditorAuthoringCommand::SaveAll);
			return true;
		case VansEditorShellCommandType::Exit:
			if (!operations.executeAuthoringCommand) return false;
			operations.executeAuthoringCommand(VansEditorAuthoringCommand::Exit);
			return true;
		case VansEditorShellCommandType::CreateAsset:
			if (!operations.requestAssetCreation) return false;
			operations.requestAssetCreation(command.assetCreationKind);
			return true;
		case VansEditorShellCommandType::Undo:
			if (!operations.undo) return false;
			operations.undo();
			return true;
		case VansEditorShellCommandType::Redo:
			if (!operations.redo) return false;
			operations.redo();
			return true;
		case VansEditorShellCommandType::SetSceneAnimationPreviewOpen:
			if (!operations.setSceneAnimationPreviewOpen) return false;
			operations.setSceneAnimationPreviewOpen(command.open);
			return true;
		case VansEditorShellCommandType::OpenSelectedAnimationGraph:
			if (!operations.openSelectedAnimationGraph) return false;
			operations.openSelectedAnimationGraph();
			return true;
		}
		return false;
	}
}
