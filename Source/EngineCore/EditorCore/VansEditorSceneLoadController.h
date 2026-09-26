#pragma once

#include "../EngineAPILayer/Public/EngineDTOs.h"

#include <cstdint>
#include <functional>
#include <string>

namespace VansGraphics
{
	enum class VansEditorSceneLoadOutcome
	{
		NoRequest,
		InvalidOperations,
		RejectedPrefabSession,
		RejectedDirtyScene,
		DocumentLoadFailed,
		PrefabRefreshFailed,
		RuntimeLoadFailed,
		LoadedEditor,
		LoadedRuntime
	};

	struct VansEditorSceneLoadContext
	{
		std::string pendingScenePath;
		std::string currentScenePath;
		Vans::EditorAPI::RuntimeSceneLoadMode mode =
			Vans::EditorAPI::RuntimeSceneLoadMode::Editor;
		bool hasPrefabSession = false;
		bool sceneDirty = false;
		bool canReuseCurrentDocument = false;
	};

	struct VansEditorRuntimeSceneLoadStatus
	{
		bool success = false;
		std::uint64_t contentRevision = 0;
	};

	struct VansEditorSceneLoadOperations
	{
		std::function<void()> clearPendingRequest;
		std::function<bool(const std::string&)> prepareDocument;
		std::function<bool()> refreshPrefabView;
		std::function<VansEditorRuntimeSceneLoadStatus(
			Vans::EditorAPI::RuntimeSceneLoadMode)> loadRuntimeScene;
		std::function<void(const std::string&)> markLoaded;
		std::function<void()> commitPreparedDocument;
		std::function<void()> detachEditorViewportCameras;
		std::function<void(bool)> setTimePaused;
		std::function<void()> installRuntimeVehiclePhysicsStepCallback;
		std::function<void()> startRuntimePhysicsIfNeeded;
		std::function<void(Vans::EditorAPI::EnginePlayState)> setPlayState;
		std::function<void(const std::string&)> setCurrentProjectScenePath;
		std::function<void(const std::string&)> logInfo;
		std::function<void(const std::string&)> logWarning;
		std::function<void(const std::string&)> logError;
	};

	class VansEditorSceneLoadController final
	{
	public:
		static VansEditorSceneLoadOutcome Execute(
			const VansEditorSceneLoadContext& context,
			const VansEditorSceneLoadOperations& operations);
	};
}
