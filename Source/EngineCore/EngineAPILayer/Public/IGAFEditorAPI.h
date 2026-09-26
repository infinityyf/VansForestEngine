#pragma once

#include "EngineDTOs.h"

#include <string>
#include <vector>

namespace Vans::EditorAPI
{
	class IGAFEditorAPI
	{
	public:
		virtual ~IGAFEditorAPI() = default;
		virtual GAFEditorDocumentSnapshot OpenGAFAsset(const std::string& sourcePath) = 0;
		virtual GAFEditorOperationResult SetGAFAssetField(
			const GAFEditorFieldEditRequest& request) = 0;
		virtual GAFEditorOperationResult ResetGAFAssetField(
			const std::string& sourcePath, const std::string& fieldPath) = 0;
		virtual GAFEditorOperationResult EditGAFAssetArray(
			const GAFEditorArrayEditRequest& request) = 0;
		virtual std::vector<GAFGraphNodeTypeSnapshot> GetGAFGraphNodeCatalog() const = 0;
		virtual GAFEditorOperationResult EditGAFGraph(const GAFGraphEditRequest& request) = 0;
		virtual GAFEditorOperationResult UndoGAFAsset(const std::string& sourcePath) = 0;
		virtual GAFEditorOperationResult RedoGAFAsset(const std::string& sourcePath) = 0;
		virtual GAFEditorOperationResult RevertGAFAsset(const std::string& sourcePath) = 0;
		virtual GAFEditorOperationResult SaveGAFAsset(const std::string& sourcePath) = 0;
		virtual GAFSemanticDiffResult DiffGAFAsset(
			const std::string& sourcePath, const std::string& baselineCanonicalJson) = 0;
		virtual GAFProjectConfigurationSnapshot GetGAFProjectConfiguration() const = 0;
		virtual std::vector<std::string> GetGAFTagCatalog() const = 0;
		virtual GAFProjectConfigurationResult ApplyGAFProjectConfiguration(
			const GAFProjectConfigurationSnapshot& configuration) = 0;
		virtual GAFRuntimeDebugSnapshot GetGAFRuntimeDebugSnapshot() = 0;
		virtual GAFCombatDebugSnapshot GetGAFCombatDebugSnapshot() const = 0;
		virtual GAFDebugCommandResult ControlGAFDebugger(const GAFDebugCommand& command) = 0;
		virtual GAFTraceCommandResult ControlGAFTrace(const GAFTraceCommand& command) = 0;
		virtual GAFSimulationResult SimulateGAFAction(const GAFSimulationRequest& request) = 0;
	};
}
