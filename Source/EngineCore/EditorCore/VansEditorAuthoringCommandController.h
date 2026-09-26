#pragma once

#include <functional>
#include <string>

namespace VansGraphics
{
	enum class VansEditorAuthoringCommand
	{
		SaveScene,
		SaveAsset,
		SaveProjectDocuments,
		SaveAll,
		Exit
	};

	enum class VansEditorAuthoringCommandOutcome
	{
		Executed,
		SaveFailed,
		ExitRejectedDirtyScene,
		ExitRejectedDirtyAssets,
		ExitRejectedDirtyProjectDocuments,
		InvalidOperations
	};

	struct VansEditorAuthoringCommandContext
	{
		bool hasPrefabSession = false;
		bool sceneDirty = false;
		bool assetsDirty = false;
		bool projectDocumentsDirty = false;
	};

	struct VansEditorAuthoringSaveStatus
	{
		bool success = false;
		bool wroteFile = false;
	};

	struct VansEditorAuthoringCommandOperations
	{
		std::function<bool()> savePrefab;
		std::function<bool()> saveSceneAndOwnedAssets;
		std::function<VansEditorAuthoringSaveStatus()> saveSelectedAsset;
		std::function<VansEditorAuthoringSaveStatus()> saveAllDirtyAssets;
		std::function<bool()> saveProjectDocuments;
		std::function<void()> reloadCurrentSceneForEditing;
		std::function<void()> requestExit;
		std::function<void(const std::string&)> logWarning;
	};

	class VansEditorAuthoringCommandController final
	{
	public:
		static VansEditorAuthoringCommandOutcome Execute(
			VansEditorAuthoringCommand command,
			const VansEditorAuthoringCommandContext& context,
			const VansEditorAuthoringCommandOperations& operations);
	};
}
