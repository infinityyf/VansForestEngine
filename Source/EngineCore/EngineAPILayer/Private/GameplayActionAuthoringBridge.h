#pragma once

#include "../Public/EngineDTOs.h"

#include <string>

namespace Vans
{
class IVansAuthoringSaveHost;
}

namespace Vans::EditorAPI
{

class GameplayActionAuthoringBridge
{
public:
	static GAFEditorDocumentSnapshot Open(const std::string& sourcePath);
	static GAFEditorOperationResult SetField(const GAFEditorFieldEditRequest& request);
	static GAFEditorOperationResult ResetField(
		const std::string& sourcePath,
		const std::string& fieldPath);
	static GAFEditorOperationResult EditArray(const GAFEditorArrayEditRequest& request);
	static std::vector<GAFGraphNodeTypeSnapshot> GetGraphNodeCatalog();
	static GAFEditorOperationResult EditGraph(const GAFGraphEditRequest& request);
	static GAFEditorOperationResult Undo(const std::string& sourcePath);
	static GAFEditorOperationResult Redo(const std::string& sourcePath);
	static GAFEditorOperationResult Revert(const std::string& sourcePath);
	static GAFEditorOperationResult Save(
		Vans::IVansAuthoringSaveHost& saveHost,
		const std::string& sourcePath);
	static GAFSemanticDiffResult Diff(
		const std::string& sourcePath,
		const std::string& baselineCanonicalJson);
	static GAFProjectConfigurationSnapshot GetProjectConfiguration();
	static std::vector<std::string> GetTagCatalog();
	static GAFProjectConfigurationResult ApplyProjectConfiguration(
		const GAFProjectConfigurationSnapshot& configuration);
};
}
