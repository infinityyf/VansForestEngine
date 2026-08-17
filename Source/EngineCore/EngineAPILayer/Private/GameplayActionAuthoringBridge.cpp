#include "GameplayActionAuthoringBridge.h"

#include "../Public/IEngineEditorAPI.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../EditorCore/GameplayAction/VansGameplayAssetEditorModel.h"
#include "../../EditorCore/VansAssetDocumentEditService.h"
#include "../../EditorCore/VansEditorAssetSaveService.h"
#include "../../CameraGameplayAction/VansCameraActionGraphNodes.h"
#include "../../GameplayActionExecution/VansActionExecutionGraph.h"
#include "../../ProjectSystem/VansProjectManager.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace Vans::EditorAPI
{
namespace
{
AssetType ToEditorAssetType(VansAssetType type)
{
	switch (type)
	{
	case VansAssetType::ActionDefinition: return AssetType::ActionDefinition;
	case VansAssetType::ActionSet: return AssetType::ActionSet;
	case VansAssetType::GameplayEffect: return AssetType::GameplayEffect;
	case VansAssetType::GameplayCue: return AssetType::GameplayCue;
	case VansAssetType::AttributeSet: return AssetType::AttributeSet;
	case VansAssetType::TargetingPolicy: return AssetType::TargetingPolicy;
	case VansAssetType::GameplayTagTree: return AssetType::GameplayTagTree;
	case VansAssetType::PayloadSchema: return AssetType::PayloadSchema;
	case VansAssetType::ActionGraph: return AssetType::ActionGraph;
	case VansAssetType::CameraRigProfile: return AssetType::CameraRigProfile;
	case VansAssetType::CameraShakeProfile: return AssetType::CameraShakeProfile;
	case VansAssetType::GAFEditorLayout: return AssetType::GAFEditorLayout;
	default: return AssetType::Unknown;
	}
}

GAFEditorPropertyKind ToEditorPropertyKind(VansGameplayPropertyKind kind)
{
	switch (kind)
	{
	case VansGameplayPropertyKind::Bool: return GAFEditorPropertyKind::Bool;
	case VansGameplayPropertyKind::Int: return GAFEditorPropertyKind::Int;
	case VansGameplayPropertyKind::Float: return GAFEditorPropertyKind::Float;
	case VansGameplayPropertyKind::String: return GAFEditorPropertyKind::String;
	case VansGameplayPropertyKind::Enum: return GAFEditorPropertyKind::Enum;
	case VansGameplayPropertyKind::Object: return GAFEditorPropertyKind::Object;
	case VansGameplayPropertyKind::Array: return GAFEditorPropertyKind::Array;
	case VansGameplayPropertyKind::Tag: return GAFEditorPropertyKind::Tag;
	case VansGameplayPropertyKind::TagQuery: return GAFEditorPropertyKind::TagQuery;
	case VansGameplayPropertyKind::AssetReference: return GAFEditorPropertyKind::AssetReference;
	case VansGameplayPropertyKind::Payload: return GAFEditorPropertyKind::Payload;
	case VansGameplayPropertyKind::Graph: return GAFEditorPropertyKind::Graph;
	case VansGameplayPropertyKind::Vec2: return GAFEditorPropertyKind::Vec2;
	case VansGameplayPropertyKind::Vec3: return GAFEditorPropertyKind::Vec3;
	case VansGameplayPropertyKind::Vec4: return GAFEditorPropertyKind::Vec4;
	case VansGameplayPropertyKind::Quaternion: return GAFEditorPropertyKind::Quaternion;
	case VansGameplayPropertyKind::Color: return GAFEditorPropertyKind::Color;
	case VansGameplayPropertyKind::Map: return GAFEditorPropertyKind::Map;
	case VansGameplayPropertyKind::EntityBinding: return GAFEditorPropertyKind::EntityBinding;
	case VansGameplayPropertyKind::ComponentBinding: return GAFEditorPropertyKind::ComponentBinding;
	}
	return GAFEditorPropertyKind::String;
}

GAFEditorPropertyKind ToEditorPropertyKind(VansActionGraphPropertyKind kind)
{
	switch (kind)
	{
	case VansActionGraphPropertyKind::Bool: return GAFEditorPropertyKind::Bool;
	case VansActionGraphPropertyKind::Int: return GAFEditorPropertyKind::Int;
	case VansActionGraphPropertyKind::Float: return GAFEditorPropertyKind::Float;
	case VansActionGraphPropertyKind::String: return GAFEditorPropertyKind::String;
	case VansActionGraphPropertyKind::AssetReference: return GAFEditorPropertyKind::AssetReference;
	case VansActionGraphPropertyKind::StringArray: return GAFEditorPropertyKind::Array;
	case VansActionGraphPropertyKind::Payload: return GAFEditorPropertyKind::Payload;
	}
	return GAFEditorPropertyKind::Payload;
}

const std::vector<VansActionGraphNodeDescriptor>& AuthoringGraphNodeDescriptors()
{
	static const std::vector<VansActionGraphNodeDescriptor> descriptors = []
	{
		std::vector<VansActionGraphNodeDescriptor> result =
			VansBuiltInActionGraphNodeDescriptors();
		const auto& camera = VansCameraActionGraphNodeDescriptors();
		result.insert(result.end(), camera.begin(), camera.end());
		return result;
	}();
	return descriptors;
}

bool LoadActiveGAFConfiguration(VansGAFProjectConfiguration& configuration)
{
	auto& projectManager = VansProjectManager::Get();
	if (!projectManager.IsProjectLoaded()) return false;
	std::string ignored;
	return VansGAFProjectConfiguration::LoadForProject(
		projectManager.GetProjectRootPath(),
		projectManager.GetPathResolver().GetEngineRoot(), configuration, ignored);
}

std::string GraphNodeKindName(VansActionGraphNodeKind kind)
{
	switch (kind)
	{
	case VansActionGraphNodeKind::Command: return "Command";
	case VansActionGraphNodeKind::Latent: return "Latent";
	case VansActionGraphNodeKind::State: return "State";
	case VansActionGraphNodeKind::Flow: return "Flow";
	case VansActionGraphNodeKind::Transaction: return "Transaction";
	case VansActionGraphNodeKind::Bridge: return "Bridge";
	case VansActionGraphNodeKind::SubAction: return "SubAction";
	default: return "Pure";
	}
}

const VansActionGraphNodeDescriptor* FindGraphNodeDescriptor(std::string_view stableName)
{
	const auto& descriptors = AuthoringGraphNodeDescriptors();
	const auto found = std::find_if(descriptors.begin(), descriptors.end(),
		[&](const VansActionGraphNodeDescriptor& descriptor)
		{ return descriptor.stableName == stableName; });
	return found == descriptors.end() ? nullptr : &*found;
}

GAFEditorDiagnostic ToEditorDiagnostic(const VansGameplayDiagnostic& diagnostic)
{
	GAFEditorDiagnostic result;
	switch (diagnostic.severity)
	{
	case VansGameplayDiagnosticSeverity::Warning:
		result.severity = GAFEditorDiagnosticSeverity::Warning; break;
	case VansGameplayDiagnosticSeverity::Error:
		result.severity = GAFEditorDiagnosticSeverity::Error; break;
	case VansGameplayDiagnosticSeverity::Fatal:
		result.severity = GAFEditorDiagnosticSeverity::Fatal; break;
	default:
		result.severity = GAFEditorDiagnosticSeverity::Info; break;
	}
	result.code = diagnostic.code;
	result.message = diagnostic.message;
	result.fieldPath = diagnostic.fieldPath;
	return result;
}

GAFEditorValue ToEditorValue(const VansSerializedValue& value)
{
	GAFEditorValue result;
	switch (value.kind)
	{
	case VansSerializedValue::Kind::Bool:
		result.kind = GAFEditorValueKind::Bool;
		result.boolValue = value.boolValue;
		break;
	case VansSerializedValue::Kind::Int:
		result.kind = GAFEditorValueKind::Int;
		result.intValue = value.intValue;
		break;
	case VansSerializedValue::Kind::Float:
		result.kind = GAFEditorValueKind::Float;
		result.floatValue = value.floatValue;
		break;
	case VansSerializedValue::Kind::String:
		result.kind = GAFEditorValueKind::String;
		result.stringValue = value.stringValue;
		break;
	case VansSerializedValue::Kind::Array:
		result.kind = GAFEditorValueKind::Array;
		break;
	case VansSerializedValue::Kind::Object:
		result.kind = GAFEditorValueKind::Object;
		break;
	default:
		result.kind = GAFEditorValueKind::Null;
		break;
	}
	result.canonicalJson = EncodeSerializedValueJson<nlohmann::ordered_json>(value).dump();
	return result;
}

GAFGraphSnapshot BuildGraphSnapshot(const VansSerializedValue& root)
{
	GAFGraphSnapshot graph;
	const VansSerializedValue* nodes = FindObjectField(root, "nodes");
	const VansSerializedValue* edges = FindObjectField(root, "edges");
	if (!nodes || nodes->kind != VansSerializedValue::Kind::Array ||
		!edges || edges->kind != VansSerializedValue::Kind::Array) return graph;
	graph.available = true;
	graph.entryNode = ReadSerializedStringField(root, "entryNode");
	graph.nodes.reserve(nodes->arrayItems.size());
	for (std::size_t index = 0; index < nodes->arrayItems.size(); ++index)
	{
		const VansSerializedValue& source = nodes->arrayItems[index];
		if (source.kind != VansSerializedValue::Kind::Object) continue;
		GAFGraphNodeSnapshot node;
		node.index = index;
		node.guid = ReadSerializedStringField(source, "guid");
		node.type = ReadSerializedStringField(source, "type");
		node.nodeKind = ReadSerializedStringField(source, "kind", "Pure");
		node.predictable = ReadSerializedBoolField(source, "predictable", false);
		if (const VansSerializedValue* editor = FindObjectField(source, "editor"))
		{
			if (const VansSerializedValue* x = FindObjectField(*editor, "x"))
				node.x = ReadSerializedNumber(*x, 0.0);
			if (const VansSerializedValue* y = FindObjectField(*editor, "y"))
				node.y = ReadSerializedNumber(*y, 0.0);
		}
		if (const VansSerializedValue* properties = FindObjectField(source, "properties"))
		{
			node.properties = ToEditorValue(*properties);
			for (const auto& [name, value] : properties->objectFields)
				node.propertyValues.push_back({ name, ToEditorValue(value) });
		}
		else node.properties = ToEditorValue(VansSerializedValue::Object({}));
		graph.nodes.push_back(std::move(node));
	}
	graph.edges.reserve(edges->arrayItems.size());
	for (std::size_t index = 0; index < edges->arrayItems.size(); ++index)
	{
		const VansSerializedValue& source = edges->arrayItems[index];
		if (source.kind != VansSerializedValue::Kind::Object) continue;
		graph.edges.push_back({ index, ReadSerializedStringField(source, "from"),
			ReadSerializedStringField(source, "output", "Success"),
			ReadSerializedStringField(source, "to"),
			static_cast<std::int32_t>(ReadSerializedIntField(source, "order", 0)) });
	}
	return graph;
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

void AppendDiagnostics(
	const VansGameplayDiagnostics& diagnostics,
	const std::string& path,
	std::vector<GAFEditorDiagnostic>& output)
{
	for (const VansGameplayDiagnostic& diagnostic : diagnostics)
		if (diagnostic.fieldPath == path)
			output.push_back(ToEditorDiagnostic(diagnostic));
}

GAFEditorFieldSnapshot BuildStructuredField(
	const VansGameplayPropertySchema& schema,
	const VansSerializedValue& value,
	bool exists,
	std::string path,
	std::string group,
	const VansGameplayDiagnostics& diagnostics,
	bool isArrayElement = false,
	std::size_t arrayIndex = 0);

void AppendStructuredChildren(
	GAFEditorFieldSnapshot& result,
	const VansGameplayPropertySchema& schema,
	const VansSerializedValue& value,
	const VansGameplayDiagnostics& diagnostics)
{
	if (value.kind == VansSerializedValue::Kind::Array && schema.hasArrayElement)
	{
		result.children.reserve(value.arrayItems.size());
		for (std::size_t index = 0; index < value.arrayItems.size(); ++index)
		{
			VansGameplayPropertySchema element;
			element.fieldId = schema.fieldId;
			element.path = std::to_string(index);
			element.displayName = "Item " + std::to_string(index + 1);
			element.kind = schema.arrayElementKind;
			element.defaultValue = schema.arrayElementDefault;
			element.cook = schema.cook;
			element.deprecated = schema.deprecated;
			element.readOnly = schema.readOnly;
			element.allowedAssetTypes = schema.allowedAssetTypes;
			element.children = schema.children;
			result.children.push_back(BuildStructuredField(element, value.arrayItems[index], true,
				result.path + "/" + std::to_string(index), result.group, diagnostics, true, index));
		}
		return;
	}
	if (value.kind != VansSerializedValue::Kind::Object) return;
	result.children.reserve(result.children.size() + schema.children.size());
	for (const VansGameplayPropertySchema& child : schema.children)
	{
		const VansSerializedValue* childValue = FindObjectField(value, child.path);
		result.children.push_back(BuildStructuredField(child,
			childValue ? *childValue : child.defaultValue,
			childValue != nullptr,
			result.path + "/" + EscapePointerToken(child.path),
			result.group, diagnostics));
	}
}

GAFEditorFieldSnapshot BuildStructuredField(
	const VansGameplayPropertySchema& schema,
	const VansSerializedValue& value,
	bool exists,
	std::string path,
	std::string group,
	const VansGameplayDiagnostics& diagnostics,
	bool isArrayElement,
	std::size_t arrayIndex)
{
	GAFEditorFieldSnapshot field;
	field.fieldId = schema.fieldId.value;
	field.path = std::move(path);
	field.displayName = schema.displayName;
	field.group = group.empty() ? schema.group : std::move(group);
	field.description = schema.description;
	field.unit = schema.unit;
	field.kind = ToEditorPropertyKind(schema.kind);
	field.value = ToEditorValue(value);
	if (schema.kind == VansGameplayPropertyKind::AssetReference &&
		value.kind == VansSerializedValue::Kind::Object)
	{
		for (const char* name : { "assetGuid", "guid", "assetPath", "path", "stableId", "id" })
			if (const VansSerializedValue* reference = FindObjectField(value, name);
				reference && reference->kind == VansSerializedValue::Kind::String &&
				!reference->stringValue.empty())
			{
				field.value.stringValue = reference->stringValue;
				break;
			}
	}
	field.exists = exists;
	field.enabled = !schema.deprecated && !schema.readOnly;
	field.required = schema.required;
	field.deprecated = schema.deprecated;
	field.readOnly = schema.readOnly;
	field.arraySize = value.kind == VansSerializedValue::Kind::Array
		? value.arrayItems.size() : 0;
	field.hasMinimum = schema.hasMinimum;
	field.hasMaximum = schema.hasMaximum;
	field.minimum = schema.minimum;
	field.maximum = schema.maximum;
	field.step = schema.step;
	field.hasStep = schema.hasStep;
	field.enumValues = schema.enumValues;
	field.hasArrayElement = schema.hasArrayElement;
	field.arrayElementKind = ToEditorPropertyKind(schema.arrayElementKind);
	field.arrayElementDefault = ToEditorValue(schema.arrayElementDefault);
	field.isArrayElement = isArrayElement;
	field.arrayIndex = arrayIndex;
	for (const VansAssetType type : schema.allowedAssetTypes)
		field.allowedAssetTypes.push_back(ToEditorAssetType(type));
	AppendDiagnostics(diagnostics, field.path, field.diagnostics);
	AppendStructuredChildren(field, schema, value, diagnostics);
	return field;
}

bool FromEditorValue(const GAFEditorValue& value, VansSerializedValue& result, std::string& error)
{
	error.clear();
	switch (value.kind)
	{
	case GAFEditorValueKind::Null:
		result = VansSerializedValue::Null(); return true;
	case GAFEditorValueKind::Bool:
		result = VansSerializedValue::Bool(value.boolValue); return true;
	case GAFEditorValueKind::Int:
		result = VansSerializedValue::Int(value.intValue); return true;
	case GAFEditorValueKind::Float:
		result = VansSerializedValue::Float(value.floatValue); return true;
	case GAFEditorValueKind::String:
		result = VansSerializedValue::String(value.stringValue); return true;
	case GAFEditorValueKind::Array:
	case GAFEditorValueKind::Object:
	case GAFEditorValueKind::Json:
		try
		{
			const nlohmann::ordered_json json = nlohmann::ordered_json::parse(value.canonicalJson);
			result = DecodeSerializedValueJson(json);
			if (value.kind == GAFEditorValueKind::Json) return true;
			const VansSerializedValue::Kind expected = value.kind == GAFEditorValueKind::Array
				? VansSerializedValue::Kind::Array : VansSerializedValue::Kind::Object;
			if (result.kind != expected)
			{
				error = "GAF structured value has the wrong JSON root type";
				return false;
			}
			return true;
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
			return false;
		}
	}
	error = "GAF editor value kind is unsupported";
	return false;
}

GAFEditorDocumentSnapshot BuildSnapshot(VansGameplayAssetEditorModel& model)
{
	GAFEditorDocumentSnapshot result;
	if (!model.IsOpen())
	{
		result.message = "No GAF asset is open";
		return result;
	}
	result.success = true;
	result.sourcePath = model.SourcePath().string();
	result.assetType = ToEditorAssetType(model.AssetType());
	if (const VansGameplayAssetSchemaDescriptor* schema =
		VansGameplayAssetSchemaRegistry::BuiltIns().Resolve(model.AssetType()))
	{
		result.assetKind = schema->assetKind;
		result.schemaVersion = schema->schemaVersion;
	}
	result.dirty = model.Document()->sourceDocument.IsDirty();
	result.canUndo = VansAssetDocumentEditService::CanUndo(model.Document()->sourceDocument);
	result.canRedo = VansAssetDocumentEditService::CanRedo(model.Document()->sourceDocument);
	const VansSerializedValue root = model.Snapshot();
	result.canonicalJson = EncodeSerializedValueJson<nlohmann::ordered_json>(root).dump();
	if (model.AssetType() == VansAssetType::ActionGraph)
		result.graph = BuildGraphSnapshot(root);
	VansGameplayDiagnostics diagnostics = model.Validate();
	VansGAFProjectConfiguration configuration;
	const bool hasConfiguration = LoadActiveGAFConfiguration(configuration);
	if (hasConfiguration)
	{
		VansGameplayAssetStorage::AppendProjectDiagnostics(
			model.AssetType(), root, configuration, diagnostics);
		configuration.ApplyValidationPolicy(diagnostics);
	}
	for (const VansGameplayDiagnostic& diagnostic : diagnostics)
		result.diagnostics.push_back(ToEditorDiagnostic(diagnostic));
	const std::vector<VansGameplayEditorFieldView> sourceFields = model.Fields();
	std::vector<GAFEditorFieldSnapshot> snapshots;
	snapshots.reserve(sourceFields.size());
	for (const VansGameplayEditorFieldView& source : sourceFields)
	{
		GAFEditorFieldSnapshot field = BuildStructuredField(source.schema, source.value,
			source.exists, source.schema.path, source.schema.group, diagnostics);
		field.visible = source.visible;
		field.enabled = source.enabled;
		snapshots.push_back(std::move(field));
	}
	std::vector<int> parents(snapshots.size(), -1);
	for (std::size_t child = 0; child < snapshots.size(); ++child)
	{
		std::size_t bestLength = 0;
		for (std::size_t candidate = 0; candidate < snapshots.size(); ++candidate)
		{
			if (candidate == child || snapshots[candidate].kind != GAFEditorPropertyKind::Object &&
				snapshots[candidate].kind != GAFEditorPropertyKind::TagQuery &&
				snapshots[candidate].kind != GAFEditorPropertyKind::Payload &&
				snapshots[candidate].kind != GAFEditorPropertyKind::Graph &&
				snapshots[candidate].kind != GAFEditorPropertyKind::Vec2 &&
				snapshots[candidate].kind != GAFEditorPropertyKind::Vec3 &&
				snapshots[candidate].kind != GAFEditorPropertyKind::Vec4 &&
				snapshots[candidate].kind != GAFEditorPropertyKind::Quaternion &&
				snapshots[candidate].kind != GAFEditorPropertyKind::Color &&
				snapshots[candidate].kind != GAFEditorPropertyKind::Map &&
				snapshots[candidate].kind != GAFEditorPropertyKind::EntityBinding &&
				snapshots[candidate].kind != GAFEditorPropertyKind::ComponentBinding) continue;
			const std::string prefix = snapshots[candidate].path + "/";
			if (snapshots[child].path.rfind(prefix, 0) == 0 && prefix.size() > bestLength)
			{
				parents[child] = static_cast<int>(candidate);
				bestLength = prefix.size();
			}
		}
	}
	const auto appendTree = [&](const auto& self, std::size_t index) -> GAFEditorFieldSnapshot
	{
		GAFEditorFieldSnapshot field = std::move(snapshots[index]);
		for (std::size_t child = 0; child < snapshots.size(); ++child)
			if (parents[child] == static_cast<int>(index))
				field.children.push_back(self(self, child));
		return field;
	};
	for (std::size_t index = 0; index < snapshots.size(); ++index)
		if (parents[index] < 0) result.fields.push_back(appendTree(appendTree, index));
	const VansGameplayCookResult cook = VansGameplayAssetStorage::Cook(
		model.AssetType(), root, VansGameplayAssetSchemaRegistry::BuiltIns(),
		hasConfiguration ? &configuration : nullptr);
	if (cook)
	{
		result.cookable = true;
		result.contentHash = cook.asset.contentHash;
		result.dependencies = cook.asset.dependencies;
	}
	return result;
}

GAFEditorOperationResult OperationResult(
	VansGameplayAssetEditorModel& model,
	const AssetDocumentEditResult& operation)
{
	GAFEditorOperationResult result;
	result.success = operation.success;
	result.message = operation.message;
	result.document = BuildSnapshot(model);
	return result;
}

bool OpenModel(const std::string& sourcePath, VansGameplayAssetEditorModel& model, std::string& error)
{
	return model.Open(sourcePath, error);
}

VansSerializedValue DefaultGraphNodeProperties(const VansActionGraphNodeDescriptor& descriptor)
{
	VansSerializedValue properties = VansSerializedValue::Object({});
	for (const VansActionGraphPropertyDescriptor& property : descriptor.properties)
		SetSerializedObjectField(properties, property.name, property.defaultValue);
	return properties;
}

bool GraphOutputExists(const VansActionGraphNodeDescriptor& descriptor, std::string_view output)
{
	for (const VansActionGraphPinDescriptor& pin : descriptor.pins)
	{
		if (pin.input) continue;
		if (pin.name == output || pin.name == "*") return true;
		if (pin.name.size() > 2 && pin.name.compare(pin.name.size() - 2, 2, ".*") == 0 &&
			output.rfind(pin.name.substr(0, pin.name.size() - 1), 0) == 0) return true;
	}
	return false;
}

bool GraphPropertyValueIsValid(
	const VansActionGraphPropertyDescriptor& property,
	const VansSerializedValue& value,
	std::string& error)
{
	bool compatible = false;
	switch (property.kind)
	{
	case VansActionGraphPropertyKind::Bool:
		compatible = value.kind == VansSerializedValue::Kind::Bool; break;
	case VansActionGraphPropertyKind::Int:
		compatible = value.kind == VansSerializedValue::Kind::Int; break;
	case VansActionGraphPropertyKind::Float:
		compatible = value.kind == VansSerializedValue::Kind::Float ||
			value.kind == VansSerializedValue::Kind::Int; break;
	case VansActionGraphPropertyKind::String:
	case VansActionGraphPropertyKind::AssetReference:
		compatible = value.kind == VansSerializedValue::Kind::String; break;
	case VansActionGraphPropertyKind::StringArray:
		compatible = value.kind == VansSerializedValue::Kind::Array &&
			std::all_of(value.arrayItems.begin(), value.arrayItems.end(), [](const auto& item)
			{ return item.kind == VansSerializedValue::Kind::String; });
		break;
	case VansActionGraphPropertyKind::Payload:
		compatible = true; break;
	}
	if (!compatible)
	{
		error = "Graph node property value has the wrong type";
		return false;
	}
	if (property.hasMinimum || property.hasMaximum)
	{
		const double number = ReadSerializedNumber(value);
		if ((property.hasMinimum && number < property.minimum) ||
			(property.hasMaximum && number > property.maximum))
		{
			error = "Graph node property value is outside its allowed range";
			return false;
		}
	}
	return true;
}

std::size_t FindGraphNodeIndex(const VansSerializedValue& nodes, std::string_view guid)
{
	if (nodes.kind != VansSerializedValue::Kind::Array) return static_cast<std::size_t>(-1);
	for (std::size_t index = 0; index < nodes.arrayItems.size(); ++index)
		if (ReadSerializedStringField(nodes.arrayItems[index], "guid") == guid) return index;
	return static_cast<std::size_t>(-1);
}

std::vector<std::string> Sorted(const std::unordered_set<std::string>& values)
{
	std::vector<std::string> result(values.begin(), values.end());
	std::sort(result.begin(), result.end());
	return result;
}

template <typename Range>
bool HasEmptyOrDuplicate(const Range& values)
{
	std::unordered_set<std::string> unique;
	for (const std::string& value : values)
		if (value.empty() || !unique.insert(value).second) return true;
	return false;
}
}

GAFEditorDocumentSnapshot GameplayActionAuthoringBridge::Open(const std::string& sourcePath)
{
	VansGameplayAssetEditorModel model;
	std::string error;
	if (!OpenModel(sourcePath, model, error))
	{
		GAFEditorDocumentSnapshot result;
		result.message = std::move(error);
		return result;
	}
	return BuildSnapshot(model);
}

GAFEditorOperationResult GameplayActionAuthoringBridge::SetField(
	const GAFEditorFieldEditRequest& request)
{
	VansGameplayAssetEditorModel model;
	std::string error;
	if (!OpenModel(request.sourcePath, model, error)) return { false, std::move(error), {} };
	VansSerializedValue value;
	if (!FromEditorValue(request.value, value, error))
		return { false, std::move(error), BuildSnapshot(model) };
	return OperationResult(model, model.SetValue(request.fieldPath, std::move(value)));
}

GAFEditorOperationResult GameplayActionAuthoringBridge::ResetField(
	const std::string& sourcePath,
	const std::string& fieldPath)
{
	VansGameplayAssetEditorModel model;
	std::string error;
	if (!OpenModel(sourcePath, model, error)) return { false, std::move(error), {} };
	return OperationResult(model, model.ResetField(fieldPath));
}

GAFEditorOperationResult GameplayActionAuthoringBridge::EditArray(
	const GAFEditorArrayEditRequest& request)
{
	VansGameplayAssetEditorModel model;
	std::string error;
	if (!OpenModel(request.sourcePath, model, error)) return { false, std::move(error), {} };
	AssetDocumentEditResult operation;
	switch (request.operation)
	{
	case GAFEditorArrayOperation::Append:
	case GAFEditorArrayOperation::Insert:
	{
		VansSerializedValue value;
		if (!FromEditorValue(request.value, value, error))
			return { false, std::move(error), BuildSnapshot(model) };
		operation = request.operation == GAFEditorArrayOperation::Append
			? model.AppendArrayItem(request.fieldPath, std::move(value))
			: model.InsertArrayItem(request.fieldPath, request.index, std::move(value));
		break;
	}
	case GAFEditorArrayOperation::Duplicate:
		operation = model.DuplicateArrayItem(request.fieldPath, request.index); break;
	case GAFEditorArrayOperation::Remove:
		operation = model.RemoveArrayItem(request.fieldPath, request.index); break;
	case GAFEditorArrayOperation::Move:
		operation = model.MoveArrayItem(
			request.fieldPath, request.index, request.destinationIndex); break;
	}
	return OperationResult(model, operation);
}

std::vector<GAFGraphNodeTypeSnapshot> GameplayActionAuthoringBridge::GetGraphNodeCatalog()
{
	std::unordered_set<std::string> allowedNodeTypes;
	VansGAFProjectConfiguration configuration;
	const bool hasProjectAllowlist = LoadActiveGAFConfiguration(configuration);
	if (hasProjectAllowlist) allowedNodeTypes = configuration.allowlist.nodeTypes;
	std::vector<GAFGraphNodeTypeSnapshot> result;
	for (const VansActionGraphNodeDescriptor& descriptor : AuthoringGraphNodeDescriptors())
	{
		GAFGraphNodeTypeSnapshot node;
		node.type = descriptor.stableName;
		node.displayName = descriptor.displayName;
		node.category = descriptor.category;
		node.nodeKind = GraphNodeKindName(descriptor.kind);
		node.predictable = descriptor.predictable;
		node.authorityOnly = descriptor.authorityOnly;
		node.allowed = !hasProjectAllowlist ||
			allowedNodeTypes.find(descriptor.stableName) != allowedNodeTypes.end();
		for (const VansActionGraphPinDescriptor& source : descriptor.pins)
			node.pins.push_back({ source.name, source.dataType, source.input, source.multiple });
		for (const VansActionGraphPropertyDescriptor& source : descriptor.properties)
		{
			GAFGraphPropertySnapshot property;
			property.name = source.name;
			property.displayName = source.displayName;
			property.kind = ToEditorPropertyKind(source.kind);
			property.defaultValue = ToEditorValue(source.defaultValue);
			property.required = source.required;
			property.hasMinimum = source.hasMinimum;
			property.hasMaximum = source.hasMaximum;
			property.minimum = source.minimum;
			property.maximum = source.maximum;
			for (const VansAssetType type : source.allowedAssetTypes)
				property.allowedAssetTypes.push_back(ToEditorAssetType(type));
			node.properties.push_back(std::move(property));
		}
		result.push_back(std::move(node));
	}
	return result;
}

GAFEditorOperationResult GameplayActionAuthoringBridge::EditGraph(
	const GAFGraphEditRequest& request)
{
	VansGameplayAssetEditorModel model;
	std::string error;
	if (!OpenModel(request.sourcePath, model, error)) return { false, std::move(error), {} };
	if (model.AssetType() != VansAssetType::ActionGraph)
		return { false, "GAF Graph operations require an ActionGraph asset", BuildSnapshot(model) };
	auto fail = [&](std::string message)
	{
		return GAFEditorOperationResult{ false, std::move(message), BuildSnapshot(model) };
	};
	VansSerializedValue root = model.Snapshot();
	VansSerializedValue* nodes = FindObjectField(root, "nodes");
	VansSerializedValue* edges = FindObjectField(root, "edges");
	if (!nodes || nodes->kind != VansSerializedValue::Kind::Array ||
		!edges || edges->kind != VansSerializedValue::Kind::Array)
		return fail("Action Graph nodes or edges collection is invalid");
	const std::size_t missing = static_cast<std::size_t>(-1);
	switch (request.operation)
	{
	case GAFGraphEditOperation::AddNode:
	{
		const VansActionGraphNodeDescriptor* descriptor = FindGraphNodeDescriptor(request.nodeType);
		if (!descriptor) return fail("Action Graph node type is not registered");
		for (const GAFGraphNodeTypeSnapshot& candidate : GetGraphNodeCatalog())
			if (candidate.type == request.nodeType && !candidate.allowed)
				return fail("Action Graph node type is blocked by the project allowlist");
		if (!std::isfinite(request.x) || !std::isfinite(request.y))
			return fail("Action Graph node position is invalid");
		std::string guid = request.nodeGuid;
		if (guid.empty())
		{
			std::size_t suffix = nodes->arrayItems.size() + 1;
			do guid = "node-" + std::to_string(suffix++);
			while (FindGraphNodeIndex(*nodes, guid) != missing);
		}
		if (FindGraphNodeIndex(*nodes, guid) != missing)
			return fail("Action Graph node GUID is duplicated");
		nodes->arrayItems.push_back(VansSerializedValue::Object({
			{ "guid", VansSerializedValue::String(guid) },
			{ "type", VansSerializedValue::String(descriptor->stableName) },
			{ "kind", VansSerializedValue::String(GraphNodeKindName(descriptor->kind)) },
			{ "predictable", VansSerializedValue::Bool(descriptor->predictable) },
			{ "properties", DefaultGraphNodeProperties(*descriptor) },
			{ "editor", VansSerializedValue::Object({
				{ "x", VansSerializedValue::Float(request.x) },
				{ "y", VansSerializedValue::Float(request.y) } }) }
		}));
		if (ReadSerializedStringField(root, "entryNode").empty())
			SetSerializedObjectField(root, "entryNode", VansSerializedValue::String(guid));
		break;
	}
	case GAFGraphEditOperation::RemoveNode:
	{
		if (nodes->arrayItems.size() <= 1)
			return fail("Action Graph must retain at least one node");
		const std::size_t index = FindGraphNodeIndex(*nodes, request.nodeGuid);
		if (index == missing) return fail("Action Graph node GUID does not exist");
		nodes->arrayItems.erase(nodes->arrayItems.begin() + static_cast<std::ptrdiff_t>(index));
		edges->arrayItems.erase(std::remove_if(edges->arrayItems.begin(), edges->arrayItems.end(),
			[&](const VansSerializedValue& edge)
			{
				return ReadSerializedStringField(edge, "from") == request.nodeGuid ||
					ReadSerializedStringField(edge, "to") == request.nodeGuid;
			}), edges->arrayItems.end());
		if (ReadSerializedStringField(root, "entryNode") == request.nodeGuid)
			SetSerializedObjectField(root, "entryNode", VansSerializedValue::String(
				ReadSerializedStringField(nodes->arrayItems.front(), "guid")));
		break;
	}
	case GAFGraphEditOperation::MoveNode:
	{
		if (!std::isfinite(request.x) || !std::isfinite(request.y))
			return fail("Action Graph node position is invalid");
		const std::size_t index = FindGraphNodeIndex(*nodes, request.nodeGuid);
		if (index == missing) return fail("Action Graph node GUID does not exist");
		VansSerializedValue& editor = EnsureSerializedObjectField(nodes->arrayItems[index], "editor");
		SetSerializedObjectField(editor, "x", VansSerializedValue::Float(request.x));
		SetSerializedObjectField(editor, "y", VansSerializedValue::Float(request.y));
		break;
	}
	case GAFGraphEditOperation::Connect:
	{
		const std::size_t from = FindGraphNodeIndex(*nodes, request.fromNode);
		const std::size_t to = FindGraphNodeIndex(*nodes, request.toNode);
		if (from == missing || to == missing || request.outputPin.empty())
			return fail("Action Graph connection endpoint or output pin is invalid");
		const std::string fromType = ReadSerializedStringField(nodes->arrayItems[from], "type");
		const VansActionGraphNodeDescriptor* descriptor = FindGraphNodeDescriptor(fromType);
		if (!descriptor || !GraphOutputExists(*descriptor, request.outputPin))
			return fail("Action Graph output pin is not declared by its node type");
		for (const VansSerializedValue& edge : edges->arrayItems)
			if (ReadSerializedStringField(edge, "from") == request.fromNode &&
				ReadSerializedStringField(edge, "output") == request.outputPin &&
				ReadSerializedStringField(edge, "to") == request.toNode)
				return fail("Action Graph connection is duplicated");
		edges->arrayItems.push_back(VansSerializedValue::Object({
			{ "from", VansSerializedValue::String(request.fromNode) },
			{ "output", VansSerializedValue::String(request.outputPin) },
			{ "to", VansSerializedValue::String(request.toNode) },
			{ "order", VansSerializedValue::Int(request.order) }
		}));
		break;
	}
	case GAFGraphEditOperation::Disconnect:
	{
		const auto found = std::find_if(edges->arrayItems.begin(), edges->arrayItems.end(),
			[&](const VansSerializedValue& edge)
			{
				return ReadSerializedStringField(edge, "from") == request.fromNode &&
					ReadSerializedStringField(edge, "output") == request.outputPin &&
					ReadSerializedStringField(edge, "to") == request.toNode;
			});
		if (found == edges->arrayItems.end()) return fail("Action Graph connection does not exist");
		edges->arrayItems.erase(found);
		break;
	}
	case GAFGraphEditOperation::SetEntryNode:
		if (FindGraphNodeIndex(*nodes, request.nodeGuid) == missing)
			return fail("Action Graph entry node does not exist");
		SetSerializedObjectField(root, "entryNode", VansSerializedValue::String(request.nodeGuid));
		break;
	case GAFGraphEditOperation::SetNodeProperty:
	{
		const std::size_t index = FindGraphNodeIndex(*nodes, request.nodeGuid);
		if (index == missing) return fail("Action Graph node GUID does not exist");
		const VansActionGraphNodeDescriptor* descriptor = FindGraphNodeDescriptor(
			ReadSerializedStringField(nodes->arrayItems[index], "type"));
		if (!descriptor) return fail("Action Graph node type is not registered");
		const auto property = std::find_if(descriptor->properties.begin(), descriptor->properties.end(),
			[&](const VansActionGraphPropertyDescriptor& candidate)
			{ return candidate.name == request.propertyName; });
		if (property == descriptor->properties.end())
			return fail("Action Graph node property is not declared by its node type");
		VansSerializedValue value;
		if (!FromEditorValue(request.value, value, error) ||
			!GraphPropertyValueIsValid(*property, value, error)) return fail(error);
		VansSerializedValue& properties = EnsureSerializedObjectField(
			nodes->arrayItems[index], "properties");
		SetSerializedObjectField(properties, property->name, std::move(value));
		break;
	}
	}
	return OperationResult(model, model.ReplaceDocument(std::move(root)));
}

GAFEditorOperationResult GameplayActionAuthoringBridge::Undo(const std::string& sourcePath)
{
	VansGameplayAssetEditorModel model;
	std::string error;
	if (!OpenModel(sourcePath, model, error)) return { false, std::move(error), {} };
	return OperationResult(model, model.Undo());
}

GAFEditorOperationResult GameplayActionAuthoringBridge::Redo(const std::string& sourcePath)
{
	VansGameplayAssetEditorModel model;
	std::string error;
	if (!OpenModel(sourcePath, model, error)) return { false, std::move(error), {} };
	return OperationResult(model, model.Redo());
}

GAFEditorOperationResult GameplayActionAuthoringBridge::Revert(const std::string& sourcePath)
{
	VansGameplayAssetEditorModel model;
	std::string error;
	if (!OpenModel(sourcePath, model, error)) return { false, std::move(error), {} };
	return OperationResult(model, VansAssetDocumentEditService::RevertToSaved(
		model.Document()->sourceDocument));
}

GAFEditorOperationResult GameplayActionAuthoringBridge::Save(
	IEngineEditorAPI& editorAPI,
	const std::string& sourcePath)
{
	VansGameplayAssetEditorModel model;
	std::string error;
	if (!OpenModel(sourcePath, model, error)) return { false, std::move(error), {} };
	const VansAssetSaveResult save =
		VansEditorAssetSaveService::Get().SaveAsset(editorAPI, model.Document());
	GAFEditorOperationResult result;
	result.success = static_cast<bool>(save);
	result.message = save.message;
	result.document = BuildSnapshot(model);
	return result;
}

GAFSemanticDiffResult GameplayActionAuthoringBridge::Diff(
	const std::string& sourcePath,
	const std::string& baselineCanonicalJson)
{
	GAFSemanticDiffResult result;
	VansGameplayAssetEditorModel model;
	std::string error;
	if (!OpenModel(sourcePath, model, error))
	{
		result.message = std::move(error);
		return result;
	}
	try
	{
		const VansSerializedValue baseline = DecodeSerializedValueJson(
			nlohmann::ordered_json::parse(baselineCanonicalJson));
		for (const VansGameplaySemanticDiffEntry& source : model.DiffAgainst(baseline))
		{
			GAFSemanticDiffEntry entry;
			entry.fieldPath = source.path;
			if (source.kind == VansGameplaySemanticChangeKind::Added)
				entry.kind = GAFSemanticChangeKind::Added;
			else if (source.kind == VansGameplaySemanticChangeKind::Removed)
				entry.kind = GAFSemanticChangeKind::Removed;
			entry.before = ToEditorValue(source.before);
			entry.after = ToEditorValue(source.after);
			result.entries.push_back(std::move(entry));
		}
		result.success = true;
	}
	catch (const std::exception& exception)
	{
		result.message = exception.what();
	}
	return result;
}

GAFProjectConfigurationSnapshot GameplayActionAuthoringBridge::GetProjectConfiguration()
{
	GAFProjectConfigurationSnapshot result;
	auto& projectManager = VansProjectManager::Get();
	if (!projectManager.IsProjectLoaded())
	{
		result.message = "Open a project before editing GAF configuration";
		return result;
	}
	const std::filesystem::path projectRoot = projectManager.GetProjectRootPath();
	const std::filesystem::path engineRoot = projectManager.GetPathResolver().GetEngineRoot();
	VansGAFProjectConfiguration configuration;
	std::string error;
	if (!VansGAFProjectConfiguration::LoadForProject(
		projectRoot, engineRoot, configuration, error))
	{
		result.message = std::move(error);
		return result;
	}
	result.available = true;
	result.settingsDirectory = (projectRoot / "ProjectSettings").string();
	result.schemaVersion = configuration.settings.schemaVersion;
	result.defaultTagRoots = configuration.settings.defaultTagRoots;
	result.networkMode = configuration.settings.networkMode == VansGAFNetworkMode::Loopback
		? "Loopback" : configuration.settings.networkMode == VansGAFNetworkMode::ExternalTransport
		? "ExternalTransport" : "Disabled";
	result.predictionEnabled = configuration.settings.predictionEnabled;
	result.requireRollbackPlan = configuration.settings.requireRollbackPlan;
	result.failWithoutTransport = configuration.settings.failWithoutTransport;
	result.deterministicCook = configuration.settings.deterministicCook;
	result.stripEditorMetadata = configuration.settings.stripEditorMetadata;
	result.treatCookWarningsAsErrors = configuration.settings.treatCookWarningsAsErrors;
	result.templateDirectory = configuration.settings.templateDirectory;
	result.maximumActiveActionsPerHost = configuration.settings.performance.maximumActiveActionsPerHost;
	result.maximumTasksPerAction = configuration.settings.performance.maximumTasksPerAction;
	result.maximumGraphTransitionsPerTick =
		configuration.settings.performance.maximumGraphTransitionsPerTick;
	result.maximumEffectsPerHost = configuration.settings.performance.maximumEffectsPerHost;
	result.maximumPayloadBytes = configuration.settings.performance.maximumPayloadBytes;
	result.allowedNodeTypes = Sorted(configuration.allowlist.nodeTypes);
	result.allowedServices = Sorted(configuration.allowlist.services);
	result.allowedHandlers = Sorted(configuration.allowlist.handlers);
	result.bridgeAllowlist = Sorted(configuration.allowlist.bridges);
	std::vector<std::pair<std::string, std::string>> overrides(
		configuration.validation.severityOverrides.begin(),
		configuration.validation.severityOverrides.end());
	std::sort(overrides.begin(), overrides.end());
	for (const auto& entry : overrides) result.severityOverrides.push_back({ entry.first, entry.second });
	result.saveBlockingCodes = Sorted(configuration.validation.saveBlockingCodes);
	result.cookBlockingCodes = Sorted(configuration.validation.cookBlockingCodes);
	result.ciBlockingCodes = Sorted(configuration.validation.ciBlockingCodes);
	std::vector<std::string> templateNames;
	for (const auto& entry : configuration.templates) templateNames.push_back(entry.first);
	std::sort(templateNames.begin(), templateNames.end());
	for (const std::string& name : templateNames)
		result.templates.push_back({ name, ToEditorValue(configuration.templates.at(name)) });
	return result;
}

std::vector<std::string> GameplayActionAuthoringBridge::GetTagCatalog()
{
	std::unordered_set<std::string> unique;
	for (const VansAssetRecord& record : VansProjectManager::Get().EnumerateAssetRecords())
	{
		if (record.type != VansAssetType::GameplayTagTree ||
			record.state == VansAssetState::Missing) continue;
		const std::filesystem::path path = !record.authoringPath.empty()
			? record.authoringPath : record.sourcePath;
		if (path.empty()) continue;
		VansGameplayAssetEditorModel model;
		std::string error;
		if (!model.Open(path, error)) continue;
		const VansSerializedValue root = model.Snapshot();
		const VansSerializedValue* tags = FindObjectField(root, "tags");
		if (!tags || tags->kind != VansSerializedValue::Kind::Array) continue;
		for (const VansSerializedValue& tag : tags->arrayItems)
		{
			const std::string name = ReadSerializedStringField(tag, "name");
			if (!name.empty()) unique.insert(name);
		}
	}
	std::vector<std::string> result(unique.begin(), unique.end());
	std::sort(result.begin(), result.end());
	return result;
}

GAFProjectConfigurationResult GameplayActionAuthoringBridge::ApplyProjectConfiguration(
	const GAFProjectConfigurationSnapshot& source)
{
	GAFProjectConfigurationResult result;
	auto& projectManager = VansProjectManager::Get();
	if (!projectManager.IsProjectLoaded())
	{
		result.message = "Open a project before editing GAF configuration";
		return result;
	}
	if (source.schemaVersion != 1 || HasEmptyOrDuplicate(source.defaultTagRoots) ||
		HasEmptyOrDuplicate(source.allowedNodeTypes) || HasEmptyOrDuplicate(source.allowedServices) ||
		HasEmptyOrDuplicate(source.allowedHandlers) || HasEmptyOrDuplicate(source.bridgeAllowlist) ||
		HasEmptyOrDuplicate(source.saveBlockingCodes) || HasEmptyOrDuplicate(source.cookBlockingCodes) ||
		HasEmptyOrDuplicate(source.ciBlockingCodes))
	{
		result.message = "GAF project lists contain an empty or duplicate entry";
		return result;
	}
	VansGAFProjectConfiguration configuration;
	configuration.settings.schemaVersion = source.schemaVersion;
	configuration.settings.defaultTagRoots = source.defaultTagRoots;
	if (source.networkMode == "Disabled")
		configuration.settings.networkMode = VansGAFNetworkMode::Disabled;
	else if (source.networkMode == "Loopback")
		configuration.settings.networkMode = VansGAFNetworkMode::Loopback;
	else if (source.networkMode == "ExternalTransport")
		configuration.settings.networkMode = VansGAFNetworkMode::ExternalTransport;
	else
	{
		result.message = "GAF network mode is invalid";
		return result;
	}
	configuration.settings.predictionEnabled = source.predictionEnabled;
	configuration.settings.requireRollbackPlan = source.requireRollbackPlan;
	configuration.settings.failWithoutTransport = source.failWithoutTransport;
	configuration.settings.deterministicCook = source.deterministicCook;
	configuration.settings.stripEditorMetadata = source.stripEditorMetadata;
	configuration.settings.treatCookWarningsAsErrors = source.treatCookWarningsAsErrors;
	configuration.settings.templateDirectory = source.templateDirectory;
	configuration.settings.performance.maximumActiveActionsPerHost = source.maximumActiveActionsPerHost;
	configuration.settings.performance.maximumTasksPerAction = source.maximumTasksPerAction;
	configuration.settings.performance.maximumGraphTransitionsPerTick =
		source.maximumGraphTransitionsPerTick;
	configuration.settings.performance.maximumEffectsPerHost = source.maximumEffectsPerHost;
	configuration.settings.performance.maximumPayloadBytes = source.maximumPayloadBytes;
	configuration.allowlist.nodeTypes.insert(source.allowedNodeTypes.begin(), source.allowedNodeTypes.end());
	configuration.allowlist.services.insert(source.allowedServices.begin(), source.allowedServices.end());
	configuration.allowlist.handlers.insert(source.allowedHandlers.begin(), source.allowedHandlers.end());
	configuration.allowlist.bridges.insert(source.bridgeAllowlist.begin(), source.bridgeAllowlist.end());
	std::unordered_set<std::string> overrideCodes;
	for (const GAFNamedString& entry : source.severityOverrides)
	{
		if (entry.name.empty() || !overrideCodes.insert(entry.name).second)
		{
			result.message = "GAF severity overrides contain an empty or duplicate code";
			return result;
		}
		configuration.validation.severityOverrides.emplace(entry.name, entry.value);
	}
	configuration.validation.saveBlockingCodes.insert(
		source.saveBlockingCodes.begin(), source.saveBlockingCodes.end());
	configuration.validation.cookBlockingCodes.insert(
		source.cookBlockingCodes.begin(), source.cookBlockingCodes.end());
	configuration.validation.ciBlockingCodes.insert(
		source.ciBlockingCodes.begin(), source.ciBlockingCodes.end());
	std::unordered_set<std::string> templateKinds;
	for (const GAFProjectTemplateSnapshot& sourceTemplate : source.templates)
	{
		if (sourceTemplate.assetKind.empty() || !templateKinds.insert(sourceTemplate.assetKind).second)
		{
			result.message = "GAF templates contain an empty or duplicate asset kind";
			return result;
		}
		VansSerializedValue document;
		std::string error;
		if (!FromEditorValue(sourceTemplate.document, document, error) ||
			document.kind != VansSerializedValue::Kind::Object)
		{
			result.message = "GAF template " + sourceTemplate.assetKind + " is invalid: " + error;
			return result;
		}
		configuration.templates.emplace(sourceTemplate.assetKind, std::move(document));
	}
	const std::filesystem::path projectRoot = projectManager.GetProjectRootPath();
	const std::filesystem::path engineRoot = projectManager.GetPathResolver().GetEngineRoot();
	const std::filesystem::path directory = projectRoot / "ProjectSettings";
	std::string error;
	if (!VansGAFProjectConfiguration::EnsureProjectFiles(
		directory, engineRoot / "EngineAssets/GAF/ProjectSettings", error) ||
		!VansGAFProjectConfiguration::Save(directory, configuration, error))
	{
		result.message = std::move(error);
		return result;
	}
	result.configuration = GetProjectConfiguration();
	result.success = result.configuration.available;
	result.message = result.success ? "GAF project configuration saved" : result.configuration.message;
	return result;
}
}
