#include "VansGameplayAssetEditorModel.h"

#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../VansAssetDocumentEditService.h"

#include <algorithm>
#include <unordered_map>

namespace Vans
{
namespace
{
bool HasBlockingDiagnosticAt(
	const VansGameplayDiagnostics& diagnostics,
	const std::string& editedPath,
	std::string& message)
{
	for (const VansGameplayDiagnostic& diagnostic : diagnostics)
	{
		if (diagnostic.severity != VansGameplayDiagnosticSeverity::Error &&
			diagnostic.severity != VansGameplayDiagnosticSeverity::Fatal) continue;
		if (!diagnostic.fieldPath.empty() && diagnostic.fieldPath != editedPath &&
			diagnostic.fieldPath.find(editedPath + "/") != 0) continue;
		message = diagnostic.code + ": " + diagnostic.message;
		return true;
	}
	return false;
}

std::string EscapePointerToken(std::string_view token)
{
	std::string result;
	result.reserve(token.size());
	for (const char character : token)
	{
		if (character == '~') result += "~0";
		else if (character == '/') result += "~1";
		else result.push_back(character);
	}
	return result;
}

void BuildDiff(
	const VansSerializedValue* before,
	const VansSerializedValue* after,
	const std::string& path,
	std::vector<VansGameplaySemanticDiffEntry>& result)
{
	if (!before)
	{
		result.push_back({ path, VansGameplaySemanticChangeKind::Added,
			VansSerializedValue::Null(), after ? *after : VansSerializedValue::Null() });
		return;
	}
	if (!after)
	{
		result.push_back({ path, VansGameplaySemanticChangeKind::Removed,
			*before, VansSerializedValue::Null() });
		return;
	}
	if (before->kind != after->kind)
	{
		result.push_back({ path, VansGameplaySemanticChangeKind::Modified, *before, *after });
		return;
	}
	if (before->kind == VansSerializedValue::Kind::Object)
	{
		std::unordered_map<std::string, const VansSerializedValue*> beforeFields;
		std::unordered_map<std::string, const VansSerializedValue*> afterFields;
		for (const auto& field : before->objectFields) beforeFields[field.first] = &field.second;
		for (const auto& field : after->objectFields) afterFields[field.first] = &field.second;
		std::vector<std::string> names;
		for (const auto& field : beforeFields) names.push_back(field.first);
		for (const auto& field : afterFields)
			if (beforeFields.find(field.first) == beforeFields.end()) names.push_back(field.first);
		std::sort(names.begin(), names.end());
		for (const std::string& name : names)
		{
			const auto left = beforeFields.find(name);
			const auto right = afterFields.find(name);
			BuildDiff(left == beforeFields.end() ? nullptr : left->second,
				right == afterFields.end() ? nullptr : right->second,
				path + "/" + EscapePointerToken(name), result);
		}
		return;
	}
	if (before->kind == VansSerializedValue::Kind::Array)
	{
		const std::size_t count = (std::max)(before->arrayItems.size(), after->arrayItems.size());
		for (std::size_t index = 0; index < count; ++index)
			BuildDiff(index < before->arrayItems.size() ? &before->arrayItems[index] : nullptr,
				index < after->arrayItems.size() ? &after->arrayItems[index] : nullptr,
				path + "/" + std::to_string(index), result);
		return;
	}
	if (!SerializedValuesEqual(*before, *after))
		result.push_back({ path, VansGameplaySemanticChangeKind::Modified, *before, *after });
}

const VansGameplayPropertySchema* FindNestedSchema(
	const VansGameplayPropertySchema& schema,
	const std::vector<std::string>& tokens,
	std::size_t index)
{
	if (index >= tokens.size()) return &schema;
	if (schema.kind == VansGameplayPropertyKind::Array)
	{
		std::size_t arrayIndex = 0;
		if (!TryParseSerializedArrayIndex(tokens[index], arrayIndex)) return nullptr;
		++index;
		if (index >= tokens.size()) return nullptr;
	}
	if (schema.kind != VansGameplayPropertyKind::Object &&
		schema.kind != VansGameplayPropertyKind::TagQuery &&
		schema.kind != VansGameplayPropertyKind::Payload &&
		schema.kind != VansGameplayPropertyKind::Graph &&
		schema.kind != VansGameplayPropertyKind::Array) return nullptr;
	for (const VansGameplayPropertySchema& child : schema.children)
		if (child.path == tokens[index])
			return FindNestedSchema(child, tokens, index + 1);
	return nullptr;
}
}

bool VansGameplayAssetEditorModel::Open(const std::filesystem::path& sourcePath, std::string& error)
{
	error.clear();
	const VansAssetType type = VansAssetDatabase::Classify(sourcePath);
	const VansGameplayAssetSchemaDescriptor* schema =
		VansGameplayAssetSchemaRegistry::BuiltIns().Resolve(type);
	if (!schema)
	{
		error = "Asset is not a registered GAF document type";
		return false;
	}
	std::shared_ptr<VansOpenAssetDocument> document =
		VansAssetDocumentRegistry::Get().GetOrOpen(sourcePath);
	if (!document || !document->sourceDocument.IsLoaded())
	{
		error = document && !document->lastError.empty()
			? document->lastError : "GAF source document could not be opened";
		return false;
	}
	m_Document = std::move(document);
	m_AssetType = type;
	m_Schema = schema;
	return true;
}

bool VansGameplayAssetEditorModel::CreateFromTemplate(
	VansAssetType assetType,
	const std::filesystem::path& sourcePath,
	const VansGAFProjectConfiguration& configuration,
	std::string& error)
{
	const VansGameplayAssetSchemaDescriptor* schema =
		VansGameplayAssetSchemaRegistry::BuiltIns().Resolve(assetType);
	if (!schema || VansAssetDatabase::Classify(sourcePath) != assetType)
	{
		error = "GAF template target extension does not match its asset type";
		return false;
	}
	const auto found = configuration.templates.find(schema->assetKind);
	if (found == configuration.templates.end())
	{
		error = "GAF project configuration has no template for " + schema->assetKind;
		return false;
	}
	if (std::filesystem::exists(sourcePath))
	{
		error = "GAF asset already exists";
		return false;
	}
	std::error_code directoryError;
	if (!sourcePath.parent_path().empty())
		std::filesystem::create_directories(sourcePath.parent_path(), directoryError);
	if (directoryError)
	{
		error = "GAF asset directory could not be created: " + directoryError.message();
		return false;
	}
	VansSerializedValue document = found->second;
	if (assetType == VansAssetType::GameplayTagTree)
	{
		std::vector<VansSerializedValue> roots;
		roots.reserve(configuration.settings.defaultTagRoots.size());
		for (const std::string& root : configuration.settings.defaultTagRoots)
			roots.push_back(VansSerializedValue::Object({
				{ "name", VansSerializedValue::String(root) },
				{ "description", VansSerializedValue::String("Project tag root") }
			}));
		if (!SetSerializedPointer(document, "/tags",
			VansSerializedValue::Array(std::move(roots)), &error)) return false;
	}
	if (!VansGameplayAssetStorage::SaveSourceAtomic(
		sourcePath, document, error, &configuration)) return false;
	return Open(sourcePath, error);
}

void VansGameplayAssetEditorModel::Close()
{
	m_Document.reset();
	m_AssetType = VansAssetType::Unknown;
	m_Schema = nullptr;
}

const std::filesystem::path& VansGameplayAssetEditorModel::SourcePath() const
{
	static const std::filesystem::path empty;
	return m_Document ? m_Document->sourcePath : empty;
}

VansSerializedValue VansGameplayAssetEditorModel::Snapshot() const
{
	return IsOpen() ? m_Document->sourceDocument.SerializedRootSnapshot()
		: VansSerializedValue::Object({});
}

std::vector<VansGameplayEditorFieldView> VansGameplayAssetEditorModel::Fields() const
{
	std::vector<VansGameplayEditorFieldView> result;
	if (!IsOpen() || !m_Schema) return result;
	const VansSerializedValue root = Snapshot();
	const VansGameplayDiagnostics diagnostics = Validate();
	result.reserve(m_Schema->fields.size());
	for (const VansGameplayPropertySchema& schema : m_Schema->fields)
	{
		VansGameplayEditorFieldView view;
		view.schema = schema;
		if (const VansSerializedValue* value = FindSerializedPointer(root, schema.path))
		{
			view.value = *value;
			view.exists = true;
		}
		else view.value = schema.defaultValue;
		if (!schema.visibleWhenPath.empty())
		{
			const VansSerializedValue* condition =
				FindSerializedPointer(root, schema.visibleWhenPath);
			view.visible = condition && SerializedValuesEqual(*condition, schema.visibleWhenValue);
		}
		view.enabled = !schema.deprecated && !schema.readOnly;
		for (const VansGameplayDiagnostic& diagnostic : diagnostics)
			if (diagnostic.fieldPath == schema.path) view.diagnostics.push_back(diagnostic);
		result.push_back(std::move(view));
	}
	return result;
}

VansGameplayDiagnostics VansGameplayAssetEditorModel::Validate() const
{
	return IsOpen() && m_Schema
		? VansGameplayAssetSchemaRegistry::BuiltIns().Validate(m_AssetType, Snapshot())
		: VansGameplayDiagnostics{ { VansGameplayDiagnosticSeverity::Error,
			"GAF-EDITOR-CLOSED", "No GAF asset is open" } };
}

VansGameplayCookResult VansGameplayAssetEditorModel::PreviewCook() const
{
	if (!IsOpen())
	{
		VansGameplayCookResult result;
		result.error = "No GAF asset is open";
		return result;
	}
	return VansGameplayAssetStorage::Cook(m_AssetType, Snapshot());
}

const VansGameplayPropertySchema* VansGameplayAssetEditorModel::FindField(const std::string& path) const
{
	if (!m_Schema) return nullptr;
	const auto found = std::find_if(m_Schema->fields.begin(), m_Schema->fields.end(),
		[&](const VansGameplayPropertySchema& field) { return field.path == path; });
	if (found != m_Schema->fields.end()) return &*found;
	const std::vector<std::string> targetTokens = SplitSerializedPointer(path);
	for (const VansGameplayPropertySchema& field : m_Schema->fields)
	{
		const std::vector<std::string> fieldTokens = SplitSerializedPointer(field.path);
		if (targetTokens.size() <= fieldTokens.size() ||
			!std::equal(fieldTokens.begin(), fieldTokens.end(), targetTokens.begin())) continue;
		if (const VansGameplayPropertySchema* nested =
			FindNestedSchema(field, targetTokens, fieldTokens.size())) return nested;
	}
	return nullptr;
}

AssetDocumentEditResult VansGameplayAssetEditorModel::ValidateCandidate(
	const std::string& editedPath,
	const VansSerializedValue& candidate) const
{
	if (!IsOpen()) return { false, "No GAF asset is open" };
	std::string message;
	if (HasBlockingDiagnosticAt(
		VansGameplayAssetSchemaRegistry::BuiltIns().Validate(m_AssetType, candidate),
		editedPath, message)) return { false, std::move(message) };
	return { true, {} };
}

AssetDocumentEditResult VansGameplayAssetEditorModel::SetValue(
	const std::string& path,
	VansSerializedValue value)
{
	if (!IsOpen()) return { false, "No GAF asset is open" };
	if (path.empty() || path.front() != '/') return { false, "GAF field path must be a JSON Pointer" };
	const VansGameplayPropertySchema* field = FindField(path);
	if (field && (field->deprecated || field->readOnly))
		return { false, "GAF field is read-only" };
	VansSerializedValue candidate = Snapshot();
	std::string error;
	if (!SetSerializedPointer(candidate, path, value, &error)) return { false, error };
	if (AssetDocumentEditResult validation = ValidateCandidate(path, candidate); !validation)
		return validation;
	return VansAssetDocumentEditService::Set(m_Document->sourceDocument,
		{ DocumentPropertySpace::AssetSource, path }, std::move(value));
}

AssetDocumentEditResult VansGameplayAssetEditorModel::ReplaceDocument(VansSerializedValue value)
{
	if (!IsOpen()) return { false, "No GAF asset is open" };
	if (value.kind != VansSerializedValue::Kind::Object)
		return { false, "GAF document root must be an object" };
	if (AssetDocumentEditResult validation = ValidateCandidate({}, value); !validation)
		return validation;
	return VansAssetDocumentEditService::ReplaceRoot(
		m_Document->sourceDocument, std::move(value));
}

AssetDocumentEditResult VansGameplayAssetEditorModel::ResetField(const std::string& path)
{
	const VansGameplayPropertySchema* field = FindField(path);
	return field ? SetValue(path, field->defaultValue)
		: AssetDocumentEditResult{ false, "GAF field has no registered default" };
}

AssetDocumentEditResult VansGameplayAssetEditorModel::ReplaceArray(
	const std::string& path,
	std::vector<VansSerializedValue> items)
{
	return SetValue(path, VansSerializedValue::Array(std::move(items)));
}

AssetDocumentEditResult VansGameplayAssetEditorModel::AppendArrayItem(
	const std::string& path,
	VansSerializedValue value)
{
	const VansSerializedValue snapshot = Snapshot();
	const VansSerializedValue* array = FindSerializedPointer(snapshot, path);
	if (!array || array->kind != VansSerializedValue::Kind::Array)
		return { false, "GAF field is not an array" };
	std::vector<VansSerializedValue> items = array->arrayItems;
	items.push_back(std::move(value));
	return ReplaceArray(path, std::move(items));
}

AssetDocumentEditResult VansGameplayAssetEditorModel::InsertArrayItem(
	const std::string& path,
	std::size_t index,
	VansSerializedValue value)
{
	const VansSerializedValue snapshot = Snapshot();
	const VansSerializedValue* array = FindSerializedPointer(snapshot, path);
	if (!array || array->kind != VansSerializedValue::Kind::Array || index > array->arrayItems.size())
		return { false, "GAF array insertion index is invalid" };
	std::vector<VansSerializedValue> items = array->arrayItems;
	items.insert(items.begin() + static_cast<std::ptrdiff_t>(index), std::move(value));
	return ReplaceArray(path, std::move(items));
}

AssetDocumentEditResult VansGameplayAssetEditorModel::DuplicateArrayItem(
	const std::string& path,
	std::size_t index)
{
	const VansSerializedValue snapshot = Snapshot();
	const VansSerializedValue* array = FindSerializedPointer(snapshot, path);
	if (!array || array->kind != VansSerializedValue::Kind::Array || index >= array->arrayItems.size())
		return { false, "GAF array duplication index is invalid" };
	return InsertArrayItem(path, index + 1, array->arrayItems[index]);
}

AssetDocumentEditResult VansGameplayAssetEditorModel::RemoveArrayItem(
	const std::string& path,
	std::size_t index)
{
	const VansSerializedValue snapshot = Snapshot();
	const VansSerializedValue* array = FindSerializedPointer(snapshot, path);
	if (!array || array->kind != VansSerializedValue::Kind::Array || index >= array->arrayItems.size())
		return { false, "GAF array removal index is invalid" };
	std::vector<VansSerializedValue> items = array->arrayItems;
	items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
	return ReplaceArray(path, std::move(items));
}

AssetDocumentEditResult VansGameplayAssetEditorModel::MoveArrayItem(
	const std::string& path,
	std::size_t from,
	std::size_t to)
{
	const VansSerializedValue snapshot = Snapshot();
	const VansSerializedValue* array = FindSerializedPointer(snapshot, path);
	if (!array || array->kind != VansSerializedValue::Kind::Array || from >= array->arrayItems.size() ||
		to >= array->arrayItems.size()) return { false, "GAF array move index is invalid" };
	if (from == to) return { false, "GAF array item is already at the requested index" };
	std::vector<VansSerializedValue> items = array->arrayItems;
	VansSerializedValue value = std::move(items[from]);
	items.erase(items.begin() + static_cast<std::ptrdiff_t>(from));
	items.insert(items.begin() + static_cast<std::ptrdiff_t>(to), std::move(value));
	return ReplaceArray(path, std::move(items));
}

AssetDocumentEditResult VansGameplayAssetEditorModel::Undo()
{
	return IsOpen() ? VansAssetDocumentEditService::Undo(m_Document->sourceDocument)
		: AssetDocumentEditResult{ false, "No GAF asset is open" };
}

AssetDocumentEditResult VansGameplayAssetEditorModel::Redo()
{
	return IsOpen() ? VansAssetDocumentEditService::Redo(m_Document->sourceDocument)
		: AssetDocumentEditResult{ false, "No GAF asset is open" };
}

std::vector<VansGameplaySemanticDiffEntry> VansGameplayAssetEditorModel::DiffAgainst(
	const VansSerializedValue& other) const
{
	std::vector<VansGameplaySemanticDiffEntry> result;
	if (!IsOpen()) return result;
	const VansSerializedValue current = Snapshot();
	BuildDiff(&other, &current, std::string(), result);
	return result;
}
}
