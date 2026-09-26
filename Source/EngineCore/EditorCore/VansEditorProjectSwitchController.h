#pragma once

#include "VansEditorProjectSession.h"
#include "../EngineAPILayer/Public/EngineDTOs.h"

#include <functional>
#include <string>

namespace VansGraphics
{
	enum class VansEditorProjectSwitchOutcome
	{
		Opened,
		RejectedPrefabSession,
		RejectedDirtyScene,
		RejectedDirtyAssets,
		RejectedDirtyProjectDocuments,
		OpenFailed,
		InvalidOperations
	};

	struct VansEditorProjectSwitchContext
	{
		VansEditorPendingProjectRequest request;
		bool hasPrefabSession = false;
		bool sceneDirty = false;
		bool assetsDirty = false;
		bool projectDocumentsDirty = false;
	};

	struct VansEditorProjectSwitchOperations
	{
		std::function<void()> clearPendingRequest;
		std::function<void(bool)> setTimePaused;
		std::function<void()> pauseRuntimePhysics;
		std::function<void()> unloadRuntimeScene;
		std::function<void()> unloadRuntimeProjectResources;
		std::function<void()> closeProject;
		std::function<void()> clearAssetHistories;
		std::function<void()> clearAssetDocuments;
		std::function<void(bool)> markProjectLoaded;
		std::function<void()> clearSceneSession;
		std::function<Vans::EditorAPI::ProjectOpenResult(
			const Vans::EditorAPI::ProjectOpenRequest&)> openProject;
		std::function<void(const std::string&)> logInfo;
		std::function<void(const std::string&)> logWarning;
		std::function<void(const std::string&)> logError;
	};

	struct VansEditorProjectSwitchResult
	{
		VansEditorProjectSwitchOutcome outcome =
			VansEditorProjectSwitchOutcome::InvalidOperations;
		Vans::EditorAPI::ProjectOpenResult projectOpenResult;

		bool Opened() const { return outcome == VansEditorProjectSwitchOutcome::Opened; }
	};

	class VansEditorProjectSwitchController final
	{
	public:
		static VansEditorProjectSwitchResult Execute(
			const VansEditorProjectSwitchContext& context,
			const VansEditorProjectSwitchOperations& operations);
	};
}
