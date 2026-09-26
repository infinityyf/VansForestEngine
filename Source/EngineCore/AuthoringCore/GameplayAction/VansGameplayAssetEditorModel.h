#pragma once

#include "../../GameplayActionSchema/VansGAFProjectConfiguration.h"
#include "../../GameplayActionSchema/VansGameplayAssetStorage.h"
#include "../VansAssetDocumentEditService.h"
#include "../VansAssetDocumentRegistry.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Vans
{
struct VansGameplayEditorFieldView
{
	VansGameplayPropertySchema schema;
	VansSerializedValue value;
	bool exists = false;
	bool visible = true;
	bool enabled = true;
	std::vector<VansGameplayDiagnostic> diagnostics;
};

enum class VansGameplaySemanticChangeKind : std::uint8_t
{
	Added,
	Removed,
	Modified
};

struct VansGameplaySemanticDiffEntry
{
	std::string path;
	VansGameplaySemanticChangeKind kind = VansGameplaySemanticChangeKind::Modified;
	VansSerializedValue before;
	VansSerializedValue after;
};

class VansGameplayAssetEditorModel
{
public:
	bool Open(const std::filesystem::path& sourcePath, std::string& error);
	bool CreateFromTemplate(
		VansAssetType assetType,
		const std::filesystem::path& sourcePath,
		const VansGAFProjectConfiguration& configuration,
		std::string& error);
	void Close();

	bool IsOpen() const { return m_Document && m_Document->sourceDocument.IsLoaded(); }
	VansAssetType AssetType() const { return m_AssetType; }
	const std::filesystem::path& SourcePath() const;
	std::shared_ptr<VansOpenAssetDocument> Document() const { return m_Document; }
	VansSerializedValue Snapshot() const;
	std::vector<VansGameplayEditorFieldView> Fields() const;
	VansGameplayDiagnostics Validate() const;
	VansGameplayCookResult PreviewCook() const;

	AssetDocumentEditResult SetValue(const std::string& path, VansSerializedValue value);
	AssetDocumentEditResult ReplaceDocument(VansSerializedValue value);
	AssetDocumentEditResult ResetField(const std::string& path);
	AssetDocumentEditResult AppendArrayItem(const std::string& path, VansSerializedValue value);
	AssetDocumentEditResult InsertArrayItem(
		const std::string& path,
		std::size_t index,
		VansSerializedValue value);
	AssetDocumentEditResult DuplicateArrayItem(const std::string& path, std::size_t index);
	AssetDocumentEditResult RemoveArrayItem(const std::string& path, std::size_t index);
	AssetDocumentEditResult MoveArrayItem(
		const std::string& path,
		std::size_t from,
		std::size_t to);
	AssetDocumentEditResult Undo();
	AssetDocumentEditResult Redo();
	std::vector<VansGameplaySemanticDiffEntry> DiffAgainst(
		const VansSerializedValue& other) const;

private:
	const VansGameplayPropertySchema* FindField(const std::string& path) const;
	AssetDocumentEditResult ReplaceArray(
		const std::string& path,
		std::vector<VansSerializedValue> items);
	AssetDocumentEditResult ValidateCandidate(
		const std::string& editedPath,
		const VansSerializedValue& candidate) const;

	std::shared_ptr<VansOpenAssetDocument> m_Document;
	VansAssetType m_AssetType = VansAssetType::Unknown;
	const VansGameplayAssetSchemaDescriptor* m_Schema = nullptr;
};
}
