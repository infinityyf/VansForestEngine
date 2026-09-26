#pragma once

#include "VansEditorAuthoringCommandController.h"
#include "VansEditorShellMenu.h"

#include <functional>

namespace VansGraphics
{
	struct VansEditorShellShortcutState final
	{
		bool editingMode = false;
		bool wantTextInput = false;
		bool controlDown = false;
		bool shiftDown = false;
		bool sceneDocumentReady = false;
		bool canUndo = false;
		bool canRedo = false;
		bool savePressed = false;
		bool undoPressed = false;
		bool redoPressed = false;
	};

	struct VansEditorShellShortcutResolution final
	{
		bool available = false;
		VansEditorShellCommand command;
	};

	struct VansEditorShellCommandOperations final
	{
		std::function<void(VansEditorAuthoringCommand)> executeAuthoringCommand;
		std::function<void(Vans::EditorAPI::ProjectAssetCreationKind)> requestAssetCreation;
		std::function<void()> undo;
		std::function<void()> redo;
		std::function<void(bool)> setSceneAnimationPreviewOpen;
		std::function<void()> openSelectedAnimationGraph;
	};

	class VansEditorShellCommandController final
	{
	public:
		static VansEditorShellShortcutResolution ResolveShortcut(
			const VansEditorShellShortcutState& state);
		static bool Execute(
			const VansEditorShellCommand& command,
			const VansEditorShellCommandOperations& operations);
	};
}
