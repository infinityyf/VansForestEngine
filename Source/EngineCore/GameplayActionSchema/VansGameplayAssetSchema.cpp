#include "VansGameplayAssetSchema.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../AssetCore/VansAssetGuid.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Vans
{
namespace
{
VansGameplayPropertySchema Field(
	std::string path,
	std::string displayName,
	std::string group,
	VansGameplayPropertyKind kind,
	VansSerializedValue defaultValue,
	bool required = false)
{
	VansGameplayPropertySchema field;
	field.fieldId = VansMakeStableId<VansActionFieldIdTag>(path);
	field.path = std::move(path);
	field.displayName = std::move(displayName);
	field.group = std::move(group);
	field.kind = kind;
	field.defaultValue = std::move(defaultValue);
	field.required = required;
	if (kind == VansGameplayPropertyKind::Array)
	{
		field.hasArrayElement = true;
		field.arrayElementKind = VansGameplayPropertyKind::Object;
		field.arrayElementDefault = VansSerializedValue::Object({});
	}
	return field;
}

void StringArrayElement(VansGameplayPropertySchema& field)
{
	field.hasArrayElement = true;
	field.arrayElementKind = VansGameplayPropertyKind::String;
	field.arrayElementDefault = VansSerializedValue::String("");
}

VansGameplayPropertySchema Child(
	std::string stablePath,
	std::string memberName,
	std::string displayName,
	VansGameplayPropertyKind kind,
	VansSerializedValue defaultValue,
	bool required = false)
{
	VansGameplayPropertySchema field = Field(
		std::move(stablePath), std::move(displayName), {}, kind, std::move(defaultValue), required);
	field.path = std::move(memberName);
	return field;
}

void ObjectArrayElement(
	VansGameplayPropertySchema& field,
	VansSerializedValue defaultValue,
	std::vector<VansGameplayPropertySchema> children)
{
	field.hasArrayElement = true;
	field.arrayElementKind = VansGameplayPropertyKind::Object;
	field.arrayElementDefault = std::move(defaultValue);
	field.children = std::move(children);
}

VansGameplayPropertySchema EnumChild(
	std::string stablePath,
	std::string memberName,
	std::string displayName,
	std::string defaultValue,
	std::vector<std::string> values,
	bool required = false)
{
	auto field = Child(std::move(stablePath), std::move(memberName), std::move(displayName),
		VansGameplayPropertyKind::Enum, VansSerializedValue::String(std::move(defaultValue)), required);
	field.enumValues = std::move(values);
	return field;
}

VansGameplayPropertySchema StringArrayChild(
	std::string stablePath,
	std::string memberName,
	std::string displayName)
{
	auto field = Child(std::move(stablePath), std::move(memberName), std::move(displayName),
		VansGameplayPropertyKind::Array, VansSerializedValue::Array({}));
	StringArrayElement(field);
	return field;
}

std::vector<VansGameplayPropertySchema> TagQueryChildren(const std::string& stablePath)
{
	auto all = StringArrayChild(stablePath + "/all", "all", "All Tags");
	auto any = StringArrayChild(stablePath + "/any", "any", "Any Tags");
	auto none = StringArrayChild(stablePath + "/none", "none", "Excluded Tags");
	for (VansGameplayPropertySchema* query : { &all, &any, &none })
		query->arrayElementKind = VansGameplayPropertyKind::Tag;
	return { std::move(all), std::move(any), std::move(none),
		Child(stablePath + "/exact", "exact", "Exact Match",
			VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(false)) };
}

VansGameplayPropertySchema Vector3Child(
	std::string stablePath,
	std::string memberName,
	std::string displayName,
	double x,
	double y,
	double z)
{
	auto field = Child(stablePath, std::move(memberName), std::move(displayName),
		VansGameplayPropertyKind::Vec3, VansSerializedValue::Object({
			{ "x", VansSerializedValue::Float(x) },
			{ "y", VansSerializedValue::Float(y) },
			{ "z", VansSerializedValue::Float(z) }
		}));
	field.children = {
		Child(stablePath + "/x", "x", "X", VansGameplayPropertyKind::Float,
			VansSerializedValue::Float(x)),
		Child(stablePath + "/y", "y", "Y", VansGameplayPropertyKind::Float,
			VansSerializedValue::Float(y)),
		Child(stablePath + "/z", "z", "Z", VansGameplayPropertyKind::Float,
			VansSerializedValue::Float(z))
	};
	return field;
}

bool KindMatches(VansGameplayPropertyKind kind, const VansSerializedValue& value)
{
	switch (kind)
	{
	case VansGameplayPropertyKind::Bool: return value.kind == VansSerializedValue::Kind::Bool;
	case VansGameplayPropertyKind::Int: return value.kind == VansSerializedValue::Kind::Int;
	case VansGameplayPropertyKind::Float:
		return value.kind == VansSerializedValue::Kind::Float || value.kind == VansSerializedValue::Kind::Int;
	case VansGameplayPropertyKind::String:
	case VansGameplayPropertyKind::Enum:
	case VansGameplayPropertyKind::Tag:
		return value.kind == VansSerializedValue::Kind::String;
	case VansGameplayPropertyKind::Object:
	case VansGameplayPropertyKind::TagQuery:
	case VansGameplayPropertyKind::Vec2:
	case VansGameplayPropertyKind::Vec3:
	case VansGameplayPropertyKind::Vec4:
	case VansGameplayPropertyKind::Quaternion:
	case VansGameplayPropertyKind::Color:
	case VansGameplayPropertyKind::Map:
	case VansGameplayPropertyKind::EntityBinding:
	case VansGameplayPropertyKind::ComponentBinding:
	case VansGameplayPropertyKind::AssetReference:
	case VansGameplayPropertyKind::Graph:
		return value.kind == VansSerializedValue::Kind::Object ||
			(kind == VansGameplayPropertyKind::AssetReference && value.kind == VansSerializedValue::Kind::String);
	case VansGameplayPropertyKind::Payload:
		return true;
	case VansGameplayPropertyKind::Array: return value.kind == VansSerializedValue::Kind::Array;
	}
	return false;
}

double NumericValue(const VansSerializedValue& value)
{
	return value.kind == VansSerializedValue::Kind::Int ?
		static_cast<double>(value.intValue) : value.floatValue;
}

void ValidateStructuredValue(
	const VansGameplayPropertySchema& field,
	const VansSerializedValue& value,
	const std::string& path,
	VansGameplayDiagnostics& diagnostics)
{
	if (!KindMatches(field.kind, value))
	{
		diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error, "GAF-FIELD-TYPE",
			"Field value has the wrong type", {}, path });
		return;
	}
	if ((field.kind == VansGameplayPropertyKind::Int ||
		field.kind == VansGameplayPropertyKind::Float) &&
		(field.hasMinimum || field.hasMaximum))
	{
		const double number = NumericValue(value);
		if (!std::isfinite(number) || (field.hasMinimum && number < field.minimum) ||
			(field.hasMaximum && number > field.maximum))
			diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error, "GAF-FIELD-RANGE",
				"Field value is outside the allowed range", {}, path });
	}
	if (field.kind == VansGameplayPropertyKind::Enum &&
		std::find(field.enumValues.begin(), field.enumValues.end(), value.stringValue) ==
			field.enumValues.end())
		diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error, "GAF-FIELD-ENUM",
			"Field value is not a registered enum option", {}, path });
	if (field.deprecated)
		diagnostics.push_back({ VansGameplayDiagnosticSeverity::Warning, "GAF-FIELD-DEPRECATED",
			"Deprecated field is preserved but should be migrated", {}, path });

	if (field.kind == VansGameplayPropertyKind::Array)
	{
		if (!field.hasArrayElement) return;
		for (std::size_t index = 0; index < value.arrayItems.size(); ++index)
		{
			VansGameplayPropertySchema element;
			element.kind = field.arrayElementKind;
			element.children = field.children;
			ValidateStructuredValue(element, value.arrayItems[index],
				path + "/" + std::to_string(index), diagnostics);
		}
		return;
	}
	if (value.kind != VansSerializedValue::Kind::Object || field.children.empty()) return;
	for (const VansGameplayPropertySchema& child : field.children)
	{
		const VansSerializedValue* childValue = FindObjectField(value, child.path);
		const std::string childPath = path + "/" + child.path;
		if (!childValue)
		{
			if (child.required)
				diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error, "GAF-FIELD-REQUIRED",
					"Required field is missing", {}, childPath });
			continue;
		}
		ValidateStructuredValue(child, *childValue, childPath, diagnostics);
	}
}

void SetPath(VansSerializedValue& root, const std::string& path, VansSerializedValue value)
{
	const std::vector<std::string> tokens = SplitSerializedPointer(path);
	if (tokens.empty()) return;
	VansSerializedValue* current = &root;
	for (std::size_t index = 0; index + 1 < tokens.size(); ++index)
		current = &EnsureSerializedObjectField(*current, tokens[index]);
	SetSerializedObjectField(*current, tokens.back(), std::move(value));
}

void CollectReferenceValue(const VansSerializedValue& value, std::vector<std::string>& result)
{
	if (value.kind == VansSerializedValue::Kind::String)
	{
		if (!value.stringValue.empty()) result.push_back(value.stringValue);
		return;
	}
	if (value.kind == VansSerializedValue::Kind::Array)
	{
		for (const VansSerializedValue& item : value.arrayItems) CollectReferenceValue(item, result);
		return;
	}
	if (value.kind != VansSerializedValue::Kind::Object) return;
	for (const char* name : { "guid", "path", "assetGuid", "assetPath" })
	{
		const VansSerializedValue* field = FindObjectField(value, name);
		if (field && field->kind == VansSerializedValue::Kind::String && !field->stringValue.empty())
			result.push_back(field->stringValue);
	}
}

void CollectConventionalReferences(const VansSerializedValue& value, std::vector<std::string>& result)
{
	const auto isGuidReferenceField = [](std::string_view name)
	{
		return name == "guid" || name == "assetGuid" || name == "rig" || name == "shake" ||
			name == "clip" || name == "sound" || name == "effect" ||
			name == "damageProfile" || name == "projectile" || name == "indicator";
	};
	if (value.kind == VansSerializedValue::Kind::Array)
	{
		for (const VansSerializedValue& item : value.arrayItems)
			CollectConventionalReferences(item, result);
		return;
	}
	if (value.kind != VansSerializedValue::Kind::Object) return;
	for (const auto& field : value.objectFields)
	{
		if (field.second.kind == VansSerializedValue::Kind::String)
		{
			if (isGuidReferenceField(field.first))
			{
				VansAssetGuid guid;
				if (VansAssetGuid::TryParse(field.second.stringValue, guid))
					result.push_back(field.second.stringValue);
			}
			else if (field.first == "assetPath" &&
				VansAssetDatabase::Classify(field.second.stringValue) != VansAssetType::Unknown)
				result.push_back(field.second.stringValue);
			else if (field.first == "path" &&
				VansAssetDatabase::Classify(field.second.stringValue) != VansAssetType::Unknown)
				result.push_back(field.second.stringValue);
		}
		CollectConventionalReferences(field.second, result);
	}
}

void CollectSchemaReferences(
	const VansGameplayPropertySchema& field,
	const VansSerializedValue& value,
	std::vector<std::string>& result)
{
	if (field.kind == VansGameplayPropertyKind::AssetReference)
		CollectReferenceValue(value, result);
	if (value.kind == VansSerializedValue::Kind::Array)
	{
		for (const VansSerializedValue& item : value.arrayItems)
		{
			if (field.arrayElementKind == VansGameplayPropertyKind::AssetReference)
				CollectReferenceValue(item, result);
			if (item.kind != VansSerializedValue::Kind::Object) continue;
			for (const VansGameplayPropertySchema& child : field.children)
				if (const VansSerializedValue* childValue = FindObjectField(item, child.path))
					CollectSchemaReferences(child, *childValue, result);
		}
		return;
	}
	if (value.kind != VansSerializedValue::Kind::Object) return;
	for (const VansGameplayPropertySchema& child : field.children)
		if (const VansSerializedValue* childValue = FindObjectField(value, child.path))
			CollectSchemaReferences(child, *childValue, result);
}

VansGameplayAssetSchemaDescriptor Base(
	VansAssetType type,
	std::string kind,
	std::string extension,
	bool editorOnly = false)
{
	VansGameplayAssetSchemaDescriptor descriptor;
	descriptor.assetType = type;
	descriptor.assetKind = std::move(kind);
	descriptor.extension = std::move(extension);
	descriptor.editorOnly = editorOnly;
	descriptor.fields.push_back(Field("/assetKind", "Asset Kind", "Identity",
		VansGameplayPropertyKind::String, VansSerializedValue::String(descriptor.assetKind), true));
	descriptor.fields.back().cook = !editorOnly;
	descriptor.fields.back().readOnly = true;
	descriptor.fields.push_back(Field("/schemaVersion", "Schema Version", "Identity",
		VansGameplayPropertyKind::Int, VansSerializedValue::Int(1), true));
	descriptor.fields.back().cook = !editorOnly;
	descriptor.fields.back().readOnly = true;
	return descriptor;
}

void AddIdentity(VansGameplayAssetSchemaDescriptor& descriptor, const char* idPath)
{
	descriptor.fields.push_back(Field(idPath, "Stable Id", "Identity",
		VansGameplayPropertyKind::String, VansSerializedValue::String(""), true));
	descriptor.fields.push_back(Field("/displayName", "Display Name", "Identity",
		VansGameplayPropertyKind::String, VansSerializedValue::String("New Asset"), true));
	descriptor.fields.push_back(Field("/authoringGuid", "Authoring Guid", "Identity",
		VansGameplayPropertyKind::String, VansSerializedValue::String("")));
}

bool HasNonEmptyString(const VansSerializedValue& root, const char* path)
{
	const VansSerializedValue* value = FindSerializedPointer(root, path);
	return value && value->kind == VansSerializedValue::Kind::String && !value->stringValue.empty();
}
}

bool VansGameplayAssetSchemaRegistry::Register(
	VansGameplayAssetSchemaDescriptor descriptor,
	std::string& error)
{
	if (m_Sealed)
	{
		error = "Gameplay Asset Schema registry is sealed";
		return false;
	}
	if (!IsGameplayAssetType(descriptor.assetType) || descriptor.assetKind.empty() ||
		descriptor.extension.empty() || descriptor.schemaVersion == 0 || descriptor.fields.empty())
	{
		error = "Gameplay Asset Schema descriptor is invalid";
		return false;
	}
	std::unordered_set<VansActionFieldId> ids;
	std::unordered_set<std::string> paths;
	const auto validateField = [&](const auto& self, const VansGameplayPropertySchema& field,
		bool topLevel) -> bool
	{
		if (!field.fieldId || field.path.empty() ||
			(topLevel ? field.path.front() != '/' : field.path.find('/') != std::string::npos) ||
			!ids.insert(field.fieldId).second)
		{
			error = "Gameplay Asset Schema contains an invalid or duplicate field";
			return false;
		}
		if (topLevel && !paths.insert(field.path).second)
		{
			error = "Gameplay Asset Schema contains an invalid or duplicate field";
			return false;
		}
		for (const VansGameplayPropertySchema& child : field.children)
			if (!self(self, child, false)) return false;
		return true;
	};
	for (const VansGameplayPropertySchema& field : descriptor.fields)
		if (!validateField(validateField, field, true)) return false;
	if (m_ByKind.find(descriptor.assetKind) != m_ByKind.end() ||
		m_ByType.find(descriptor.assetType) != m_ByType.end())
	{
		error = "duplicate Gameplay Asset Schema";
		return false;
	}
	m_ByKind.emplace(descriptor.assetKind, descriptor.assetType);
	m_ByType.emplace(descriptor.assetType, std::move(descriptor));
	return true;
}

bool VansGameplayAssetSchemaRegistry::Seal(std::string& error)
{
	if (m_ByType.empty())
	{
		error = "Gameplay Asset Schema registry is empty";
		return false;
	}
	m_Sealed = true;
	return true;
}

const VansGameplayAssetSchemaDescriptor* VansGameplayAssetSchemaRegistry::Resolve(VansAssetType type) const
{
	const auto found = m_ByType.find(type);
	return found == m_ByType.end() ? nullptr : &found->second;
}

const VansGameplayAssetSchemaDescriptor* VansGameplayAssetSchemaRegistry::ResolveKind(
	std::string_view assetKind) const
{
	const auto found = m_ByKind.find(std::string(assetKind));
	return found == m_ByKind.end() ? nullptr : Resolve(found->second);
}

VansGameplayDiagnostics VansGameplayAssetSchemaRegistry::Validate(
	VansAssetType type,
	const VansSerializedValue& root) const
{
	VansGameplayDiagnostics diagnostics;
	const VansGameplayAssetSchemaDescriptor* descriptor = Resolve(type);
	if (!descriptor)
	{
		diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error, "GAF-SCHEMA-MISSING",
			"Gameplay Asset Schema is not registered" });
		return diagnostics;
	}
	if (root.kind != VansSerializedValue::Kind::Object)
	{
		diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error, "GAF-ASSET-ROOT",
			"Gameplay asset root must be an object" });
		return diagnostics;
	}
	for (const VansGameplayPropertySchema& field : descriptor->fields)
	{
		const VansSerializedValue* value = FindSerializedPointer(root, field.path);
		if (!value)
		{
			if (field.required) diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error,
				"GAF-FIELD-REQUIRED", "Required field is missing", {}, field.path });
			continue;
		}
		ValidateStructuredValue(field, *value, field.path, diagnostics);
	}
	const VansSerializedValue* kind = FindObjectField(root, "assetKind");
	if (!kind || kind->kind != VansSerializedValue::Kind::String || kind->stringValue != descriptor->assetKind)
		diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error, "GAF-ASSET-KIND",
			"assetKind does not match the file extension", {}, "/assetKind" });
	const VansSerializedValue* version = FindObjectField(root, "schemaVersion");
	if (!version || version->kind != VansSerializedValue::Kind::Int ||
		version->intValue != descriptor->schemaVersion)
		diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error, "GAF-SCHEMA-VERSION",
			"schemaVersion is not the current project schema", {}, "/schemaVersion" });
	if (type == VansAssetType::ActionDefinition)
	{
		if (!HasNonEmptyString(root, "/actionId"))
			diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error, "GAF-ACTION-ID",
				"ActionId must be non-empty", {}, "/actionId" });
		if (!HasNonEmptyString(root, "/execution/executor") &&
			!FindSerializedPointer(root, "/execution/graph"))
			diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error, "GAF-ACTION-EXECUTION",
				"Action needs an Executor or Graph", {}, "/execution" });
	}
	if (type == VansAssetType::GameplayEffect)
	{
		const VansSerializedValue* stacks = FindSerializedPointer(root, "/stacking/maximumStacks");
		if (stacks && stacks->kind == VansSerializedValue::Kind::Int && stacks->intValue <= 0)
			diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error, "GAF-EFFECT-STACK",
				"maximumStacks must be positive", {}, "/stacking/maximumStacks" });
		const VansSerializedValue* modifiers = FindSerializedPointer(root, "/modifiers");
		if (modifiers && modifiers->kind == VansSerializedValue::Kind::Array)
			for (std::size_t index = 0; index < modifiers->arrayItems.size(); ++index)
			{
				const VansSerializedValue& modifier = modifiers->arrayItems[index];
				if (modifier.kind != VansSerializedValue::Kind::Object) continue;
				const std::string path = "/modifiers/" + std::to_string(index);
				const std::string source = ReadSerializedStringField(
					modifier, "magnitudeSource", "Fixed");
				bool validSource = true;
				if (source == "SetByCaller")
					validSource = !ReadSerializedStringField(modifier, "setByCaller").empty();
				else if (source == "CapturedAttribute")
				{
					const std::string captured = ReadSerializedStringField(modifier, "capturedAttribute");
					validSource = !captured.empty() &&
						(ReadSerializedStringField(modifier, "capture", "Snapshot") != "Dynamic" ||
							captured != ReadSerializedStringField(modifier, "attribute"));
				}
				else if (source == "ContextPayload")
				{
					const std::string contextPath = ReadSerializedStringField(modifier, "contextPath");
					validSource = !contextPath.empty() && contextPath.front() == '/';
				}
				if (!validSource)
					diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error,
						"GAF-EFFECT-MAGNITUDE-SOURCE",
						"Effect magnitude source is missing required data or is self-referential",
						{}, path + "/magnitudeSource" });
				const VansSerializedValue* minimum = FindObjectField(modifier, "randomMinimum");
				const VansSerializedValue* maximum = FindObjectField(modifier, "randomMaximum");
				if (source == "RandomRange" && minimum && maximum &&
					ReadSerializedNumber(*minimum) > ReadSerializedNumber(*maximum))
					diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error,
						"GAF-EFFECT-RANDOM-RANGE", "Effect random range minimum exceeds maximum",
						{}, path + "/randomMinimum" });
			}
	}
	if (type == VansAssetType::ActionGraph)
	{
		const VansSerializedValue* nodes = FindSerializedPointer(root, "/nodes");
		if (!nodes || nodes->kind != VansSerializedValue::Kind::Array || nodes->arrayItems.empty())
			diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error, "GAF-GRAPH-NODES",
				"ActionGraph must contain at least one node", {}, "/nodes" });
	}
	return diagnostics;
}

std::vector<std::string> VansGameplayAssetSchemaRegistry::CollectDependencies(
	VansAssetType type,
	const VansSerializedValue& root) const
{
	std::vector<std::string> result;
	const VansGameplayAssetSchemaDescriptor* descriptor = Resolve(type);
	if (!descriptor) return result;
	for (const VansGameplayPropertySchema& field : descriptor->fields)
	{
		if (const VansSerializedValue* value = FindSerializedPointer(root, field.path))
			CollectSchemaReferences(field, *value, result);
	}
	CollectConventionalReferences(root, result);
	std::sort(result.begin(), result.end());
	result.erase(std::unique(result.begin(), result.end()), result.end());
	return result;
}

VansSerializedValue VansGameplayAssetSchemaRegistry::CreateDefault(VansAssetType type) const
{
	VansSerializedValue root = VansSerializedValue::Object({});
	const VansGameplayAssetSchemaDescriptor* descriptor = Resolve(type);
	if (!descriptor) return root;
	for (const VansGameplayPropertySchema& field : descriptor->fields)
		SetPath(root, field.path, field.defaultValue);
	return root;
}

const VansGameplayAssetSchemaRegistry& VansGameplayAssetSchemaRegistry::BuiltIns()
{
	static const VansGameplayAssetSchemaRegistry registry = []
	{
		VansGameplayAssetSchemaRegistry value;
		std::string error;

		auto action = Base(VansAssetType::ActionDefinition, "ActionDefinition", ".vaction");
		AddIdentity(action, "/actionId");
		action.fields.push_back(Field("/namespace", "Namespace", "Identity",
			VansGameplayPropertyKind::String, VansSerializedValue::String("")));
		action.fields.push_back(Field("/definitionVersion", "Definition Version", "Identity",
			VansGameplayPropertyKind::Int, VansSerializedValue::Int(1)));
		action.fields.back().hasMinimum = true;
		action.fields.back().minimum = 1.0;
		action.fields.push_back(Field("/tags", "Ability Tags", "Classification",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		StringArrayElement(action.fields.back());
		action.fields.push_back(Field("/category", "Category", "Classification",
			VansGameplayPropertyKind::String, VansSerializedValue::String("Gameplay")));
		action.fields.push_back(Field("/priority", "Priority", "Classification",
			VansGameplayPropertyKind::Int, VansSerializedValue::Int(0)));
		action.fields.push_back(Field("/replication", "Replication Policy", "Network",
			VansGameplayPropertyKind::Enum, VansSerializedValue::String("LocalOnly")));
		action.fields.back().enumValues = {
			"LocalOnly", "OwnerPredicted", "ServerAuthoritative", "Replicated" };
		action.fields.push_back(Field("/activation", "Activation", "Activation",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({}), true));
		action.fields.push_back(Field("/activation/authority", "Authority", "Activation",
			VansGameplayPropertyKind::Enum, VansSerializedValue::String("Any")));
		action.fields.back().enumValues = { "Any", "LocalOwner", "AuthorityOnly" };
		action.fields.push_back(Field("/activation/requirements", "Required Tags", "Activation",
			VansGameplayPropertyKind::TagQuery, VansSerializedValue::Object({})));
		action.fields.back().children = TagQueryChildren("/activation/requirements");
		action.fields.push_back(Field("/activation/blockedTags", "Blocked Tags", "Activation",
			VansGameplayPropertyKind::TagQuery, VansSerializedValue::Object({})));
		action.fields.back().children = TagQueryChildren("/activation/blockedTags");
		action.fields.push_back(Field("/activation/triggers", "Triggers", "Activation",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		StringArrayElement(action.fields.back());
		action.fields.push_back(Field("/activation/targeting/asset", "Targeting Policy", "Activation",
			VansGameplayPropertyKind::AssetReference, VansSerializedValue::Object({})));
		action.fields.back().allowedAssetTypes = { VansAssetType::TargetingPolicy };
		action.fields.push_back(Field("/commit", "Commit", "Commit",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({}), true));
		action.fields.push_back(Field("/commit/requirements", "Requirements", "Commit",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		ObjectArrayElement(action.fields.back(), VansSerializedValue::Object({
			{ "kind", VansSerializedValue::String("Attribute") },
			{ "attribute", VansSerializedValue::String("") },
			{ "comparison", VansSerializedValue::String("GreaterOrEqual") },
			{ "value", VansSerializedValue::Float(0.0) },
			{ "minimumTargets", VansSerializedValue::Int(1) },
			{ "service", VansSerializedValue::String("") }
		}), {
			EnumChild("/commit/requirements/*/kind", "kind", "Requirement Kind", "Attribute",
				{ "Attribute", "PrimaryTarget", "TargetData", "Service" }, true),
			Child("/commit/requirements/*/attribute", "attribute", "Attribute",
				VansGameplayPropertyKind::String, VansSerializedValue::String("")),
			EnumChild("/commit/requirements/*/comparison", "comparison", "Comparison",
				"GreaterOrEqual", { "Less", "LessOrEqual", "Equal", "NotEqual",
					"GreaterOrEqual", "Greater" }),
			Child("/commit/requirements/*/value", "value", "Value",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.0)),
			Child("/commit/requirements/*/minimumTargets", "minimumTargets", "Minimum Targets",
				VansGameplayPropertyKind::Int, VansSerializedValue::Int(1)),
			Child("/commit/requirements/*/service", "service", "Service",
				VansGameplayPropertyKind::String, VansSerializedValue::String(""))
		});
		action.fields.back().children[4].hasMinimum = true;
		action.fields.back().children[4].minimum = 1.0;
		action.fields.push_back(Field("/commit/costs", "Costs", "Commit",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		ObjectArrayElement(action.fields.back(), VansSerializedValue::Object({
			{ "kind", VansSerializedValue::String("Attribute") },
			{ "attribute", VansSerializedValue::String("") },
			{ "amount", VansSerializedValue::Float(0.0) },
			{ "refund", VansSerializedValue::String("Never") },
			{ "resource", VansSerializedValue::String("") },
			{ "payload", VansSerializedValue::Object({}) }
		}), {
			EnumChild("/commit/costs/*/kind", "kind", "Cost Kind", "Attribute",
				{ "Attribute", "Inventory", "Reservation" }),
			Child("/commit/costs/*/attribute", "attribute", "Attribute",
				VansGameplayPropertyKind::String, VansSerializedValue::String("")),
			Child("/commit/costs/*/amount", "amount", "Amount",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.0), true),
			EnumChild("/commit/costs/*/refund", "refund", "Refund Policy", "Never",
				{ "Never", "OnCommitFailure", "OnCancel", "Always" }),
			Child("/commit/costs/*/resource", "resource", "External Resource",
				VansGameplayPropertyKind::String, VansSerializedValue::String("")),
			Child("/commit/costs/*/payload", "payload", "Provider Payload",
				VansGameplayPropertyKind::Payload, VansSerializedValue::Object({}))
		});
		action.fields.push_back(Field("/commit/cooldowns", "Cooldowns", "Commit",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		ObjectArrayElement(action.fields.back(), VansSerializedValue::Object({
			{ "duration", VansSerializedValue::Float(0.0) },
			{ "tag", VansSerializedValue::String("") }
		}), {
			Child("/commit/cooldowns/*/duration", "duration", "Duration",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.0), true),
			Child("/commit/cooldowns/*/tag", "tag", "Cooldown Tag",
				VansGameplayPropertyKind::Tag, VansSerializedValue::String(""), true)
		});
		action.fields.push_back(Field("/commit/grantedWhileRunning", "Running Tags", "Commit",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		StringArrayElement(action.fields.back());
		action.fields.push_back(Field("/commit/effects", "Commit Effects", "Commit",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		ObjectArrayElement(action.fields.back(), VansSerializedValue::Object({
			{ "assetGuid", VansSerializedValue::String("") },
			{ "removeOnEnd", VansSerializedValue::Bool(false) }
		}), {
			Child("/commit/effects/*/assetGuid", "assetGuid", "Effect Asset",
				VansGameplayPropertyKind::AssetReference, VansSerializedValue::String(""), true),
			Child("/commit/effects/*/removeOnEnd", "removeOnEnd", "Remove On End",
				VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(false))
		});
		action.fields.back().arrayElementKind = VansGameplayPropertyKind::AssetReference;
		action.fields.back().children.front().allowedAssetTypes = { VansAssetType::GameplayEffect };
		action.fields.push_back(Field("/commit/concurrency", "Concurrency", "Commit",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({})));
		action.fields.push_back(Field("/commit/concurrency/group", "Group", "Commit",
			VansGameplayPropertyKind::String, VansSerializedValue::String("")));
		action.fields.push_back(Field("/commit/concurrency/mode", "Policy", "Commit",
			VansGameplayPropertyKind::Enum, VansSerializedValue::String("Allow")));
		action.fields.back().enumValues = { "Allow", "RejectNew", "CancelExisting", "QueueNew" };
		action.fields.push_back(Field("/commit/concurrency/limit", "Concurrent Limit", "Commit",
			VansGameplayPropertyKind::Int, VansSerializedValue::Int(1)));
		action.fields.back().hasMinimum = true;
		action.fields.back().minimum = 1.0;
		action.fields.push_back(Field("/commit/concurrency/queueTimeout", "Queue Timeout", "Commit",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.0)));
		action.fields.back().hasMinimum = true;
		action.fields.back().minimum = 0.0;
		action.fields.push_back(Field("/execution", "Execution", "Execution",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({}), true));
		action.fields.push_back(Field("/execution/executor", "Executor", "Execution",
			VansGameplayPropertyKind::String, VansSerializedValue::String("Action.Executor.Immediate")));
		action.fields.push_back(Field("/execution/graph", "Action Graph", "Execution",
			VansGameplayPropertyKind::AssetReference, VansSerializedValue::Object({})));
		action.fields.back().allowedAssetTypes = { VansAssetType::ActionGraph };
		action.fields.push_back(Field("/execution/timeline", "Timeline", "Execution",
			VansGameplayPropertyKind::AssetReference, VansSerializedValue::Object({})));
		action.fields.back().allowedAssetTypes = { VansAssetType::Timeline };
		action.fields.push_back(Field("/execution/timelines", "Additional Timelines", "Execution",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		StringArrayElement(action.fields.back());
		action.fields.push_back(Field("/execution/variables", "Variables", "Execution",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		ObjectArrayElement(action.fields.back(), VansSerializedValue::Object({
			{ "name", VansSerializedValue::String("") },
			{ "default", VansSerializedValue::Null() }
		}), {
			Child("/execution/variables/*/name", "name", "Name",
				VansGameplayPropertyKind::String, VansSerializedValue::String(""), true),
			Child("/execution/variables/*/default", "default", "Default Value",
				VansGameplayPropertyKind::Payload, VansSerializedValue::Null())
		});
		action.fields.push_back(Field("/execution/timeDomain", "Time Domain", "Execution",
			VansGameplayPropertyKind::Enum, VansSerializedValue::String("Game")));
		action.fields.back().enumValues = { "Game", "Unscaled", "Fixed", "Timeline" };
		action.fields.push_back(Field("/execution/endPolicy", "End Policy", "Execution",
			VansGameplayPropertyKind::Enum, VansSerializedValue::String("ExecutorResult")));
		action.fields.back().enumValues = { "ExecutorResult", "Explicit", "TimelineEnd", "FirstTerminal" };
		action.fields.push_back(Field("/presentation", "Presentation", "Presentation",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({})));
		action.fields.push_back(Field("/presentation/cues", "Cues", "Presentation",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		action.fields.back().hasArrayElement = true;
		action.fields.back().arrayElementKind = VansGameplayPropertyKind::AssetReference;
		action.fields.back().arrayElementDefault = VansSerializedValue::Object({});
		action.fields.back().allowedAssetTypes = { VansAssetType::GameplayCue };
		action.fields.push_back(Field("/transitions", "Transitions", "Transition",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({})));
		action.fields.push_back(Field("/transitions/cancellable", "Cancellable", "Transition",
			VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(true)));
		action.fields.push_back(Field("/transitions/interruptible", "Interruptible", "Transition",
			VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(true)));
		action.fields.push_back(Field("/transitions/blockedActions", "Blocked Actions", "Transition",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		action.fields.back().hasArrayElement = true;
		action.fields.back().arrayElementKind = VansGameplayPropertyKind::AssetReference;
		action.fields.back().arrayElementDefault = VansSerializedValue::Object({});
		action.fields.back().allowedAssetTypes = { VansAssetType::ActionDefinition };
		action.fields.push_back(Field("/transitions/cancelActions", "Cancelled Actions", "Transition",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		action.fields.back().hasArrayElement = true;
		action.fields.back().arrayElementKind = VansGameplayPropertyKind::AssetReference;
		action.fields.back().arrayElementDefault = VansSerializedValue::Object({});
		action.fields.back().allowedAssetTypes = { VansAssetType::ActionDefinition };
		action.fields.push_back(Field("/transitions/rules", "Transition Rules", "Transition",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		ObjectArrayElement(action.fields.back(), VansSerializedValue::Object({
			{ "name", VansSerializedValue::String("New Transition") },
			{ "trigger", VansSerializedValue::String("Event") },
			{ "event", VansSerializedValue::String("") },
			{ "input", VansSerializedValue::String("") },
			{ "target", VansSerializedValue::Object({}) },
			{ "minimumTime", VansSerializedValue::Float(0.0) },
			{ "maximumTime", VansSerializedValue::Float(-1.0) },
			{ "priority", VansSerializedValue::Int(0) },
			{ "consume", VansSerializedValue::Bool(false) },
			{ "cancelSource", VansSerializedValue::Bool(true) },
			{ "inheritPrimaryTarget", VansSerializedValue::Bool(true) },
			{ "requirements", VansSerializedValue::Object({
				{ "all", VansSerializedValue::Array({}) },
				{ "any", VansSerializedValue::Array({}) },
				{ "none", VansSerializedValue::Array({}) },
				{ "exact", VansSerializedValue::Bool(false) } }) },
			{ "contextPatch", VansSerializedValue::Object({}) }
		}), {
			Child("/transitions/rules/*/name", "name", "Name",
				VansGameplayPropertyKind::String, VansSerializedValue::String("New Transition"), true),
			EnumChild("/transitions/rules/*/trigger", "trigger", "Trigger", "Event",
				{ "Event", "Input", "Completed", "Failed" }, true),
			Child("/transitions/rules/*/event", "event", "Event",
				VansGameplayPropertyKind::String, VansSerializedValue::String("")),
			Child("/transitions/rules/*/input", "input", "Input Binding",
				VansGameplayPropertyKind::String, VansSerializedValue::String("")),
			Child("/transitions/rules/*/target", "target", "Target Action",
				VansGameplayPropertyKind::AssetReference, VansSerializedValue::Object({}), true),
			Child("/transitions/rules/*/minimumTime", "minimumTime", "Minimum Time",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.0)),
			Child("/transitions/rules/*/maximumTime", "maximumTime", "Maximum Time",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(-1.0)),
			Child("/transitions/rules/*/priority", "priority", "Priority",
				VansGameplayPropertyKind::Int, VansSerializedValue::Int(0)),
			Child("/transitions/rules/*/consume", "consume", "Consume Trigger",
				VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(false)),
			Child("/transitions/rules/*/cancelSource", "cancelSource", "Cancel Source",
				VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(true)),
			Child("/transitions/rules/*/inheritPrimaryTarget", "inheritPrimaryTarget",
				"Inherit Primary Target", VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(true)),
			Child("/transitions/rules/*/requirements", "requirements", "Tag Requirements",
				VansGameplayPropertyKind::TagQuery, VansSerializedValue::Object({
					{ "all", VansSerializedValue::Array({}) },
					{ "any", VansSerializedValue::Array({}) },
					{ "none", VansSerializedValue::Array({}) },
					{ "exact", VansSerializedValue::Bool(false) } })),
			Child("/transitions/rules/*/contextPatch", "contextPatch", "Context Patch",
				VansGameplayPropertyKind::Payload, VansSerializedValue::Object({}))
		});
		action.fields.back().children[4].allowedAssetTypes = { VansAssetType::ActionDefinition };
		action.fields.back().children[5].hasMinimum = true;
		action.fields.back().children[5].minimum = 0.0;
		action.fields.back().children[11].children = TagQueryChildren(
			"/transitions/rules/*/requirements");
		action.fields.push_back(Field("/transitions/comboWindows", "Combo Windows", "Transition",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		ObjectArrayElement(action.fields.back(), VansSerializedValue::Object({
			{ "name", VansSerializedValue::String("New Combo") },
			{ "input", VansSerializedValue::String("") },
			{ "target", VansSerializedValue::Object({}) },
			{ "openTime", VansSerializedValue::Float(0.0) },
			{ "closeTime", VansSerializedValue::Float(1.0) },
			{ "priority", VansSerializedValue::Int(0) },
			{ "consume", VansSerializedValue::Bool(true) },
			{ "cancelSource", VansSerializedValue::Bool(true) },
			{ "inheritPrimaryTarget", VansSerializedValue::Bool(true) },
			{ "requirements", VansSerializedValue::Object({
				{ "all", VansSerializedValue::Array({}) },
				{ "any", VansSerializedValue::Array({}) },
				{ "none", VansSerializedValue::Array({}) },
				{ "exact", VansSerializedValue::Bool(false) } }) },
			{ "contextPatch", VansSerializedValue::Object({}) }
		}), {
			Child("/transitions/comboWindows/*/name", "name", "Name",
				VansGameplayPropertyKind::String, VansSerializedValue::String("New Combo"), true),
			Child("/transitions/comboWindows/*/input", "input", "Input Binding",
				VansGameplayPropertyKind::String, VansSerializedValue::String(""), true),
			Child("/transitions/comboWindows/*/target", "target", "Target Action",
				VansGameplayPropertyKind::AssetReference, VansSerializedValue::Object({}), true),
			Child("/transitions/comboWindows/*/openTime", "openTime", "Open Time",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.0)),
			Child("/transitions/comboWindows/*/closeTime", "closeTime", "Close Time",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(1.0)),
			Child("/transitions/comboWindows/*/priority", "priority", "Priority",
				VansGameplayPropertyKind::Int, VansSerializedValue::Int(0)),
			Child("/transitions/comboWindows/*/consume", "consume", "Consume Input",
				VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(true)),
			Child("/transitions/comboWindows/*/cancelSource", "cancelSource", "Cancel Source",
				VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(true)),
			Child("/transitions/comboWindows/*/inheritPrimaryTarget", "inheritPrimaryTarget",
				"Inherit Primary Target", VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(true)),
			Child("/transitions/comboWindows/*/requirements", "requirements", "Tag Requirements",
				VansGameplayPropertyKind::TagQuery, VansSerializedValue::Object({
					{ "all", VansSerializedValue::Array({}) },
					{ "any", VansSerializedValue::Array({}) },
					{ "none", VansSerializedValue::Array({}) },
					{ "exact", VansSerializedValue::Bool(false) } })),
			Child("/transitions/comboWindows/*/contextPatch", "contextPatch", "Context Patch",
				VansGameplayPropertyKind::Payload, VansSerializedValue::Object({}))
		});
		action.fields.back().children[2].allowedAssetTypes = { VansAssetType::ActionDefinition };
		action.fields.back().children[3].hasMinimum = true;
		action.fields.back().children[3].minimum = 0.0;
		action.fields.back().children[4].hasMinimum = true;
		action.fields.back().children[4].minimum = 0.0;
		action.fields.back().children[9].children = TagQueryChildren(
			"/transitions/comboWindows/*/requirements");
		action.fields.push_back(Field("/transitions/inputBuffer", "Input Buffer", "Transition",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({
				{ "enabled", VansSerializedValue::Bool(false) },
				{ "duration", VansSerializedValue::Float(0.2) },
				{ "maximumEntries", VansSerializedValue::Int(1) }
			})));
		action.fields.back().children = {
			Child("/transitions/inputBuffer/enabled", "enabled", "Enabled",
				VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(false)),
			Child("/transitions/inputBuffer/duration", "duration", "Duration",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.2)),
			Child("/transitions/inputBuffer/maximumEntries", "maximumEntries", "Maximum Entries",
				VansGameplayPropertyKind::Int, VansSerializedValue::Int(1))
		};
		action.fields.back().children[1].hasMinimum = true;
		action.fields.back().children[1].minimum = 0.001;
		action.fields.back().children[2].hasMinimum = true;
		action.fields.back().children[2].minimum = 1.0;
		action.fields.push_back(Field("/transitions/failureFallback", "Failure Fallback", "Transition",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({
				{ "action", VansSerializedValue::Object({}) },
				{ "errors", VansSerializedValue::Array({}) },
				{ "inheritPrimaryTarget", VansSerializedValue::Bool(true) },
				{ "contextPatch", VansSerializedValue::Object({}) }
			})));
		action.fields.back().children = {
			Child("/transitions/failureFallback/action", "action", "Fallback Action",
				VansGameplayPropertyKind::AssetReference, VansSerializedValue::Object({})),
			StringArrayChild("/transitions/failureFallback/errors", "errors", "Handled Errors"),
			Child("/transitions/failureFallback/inheritPrimaryTarget", "inheritPrimaryTarget",
				"Inherit Primary Target", VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(true)),
			Child("/transitions/failureFallback/contextPatch", "contextPatch", "Context Patch",
				VansGameplayPropertyKind::Payload, VansSerializedValue::Object({}))
		};
		action.fields.back().children[0].allowedAssetTypes = { VansAssetType::ActionDefinition };
		action.fields.push_back(Field("/dependencies/services", "Required Services", "Dependency",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		StringArrayElement(action.fields.back());
		action.fields.push_back(Field("/extensions", "Extensions", "Extension",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		ObjectArrayElement(action.fields.back(), VansSerializedValue::Object({
			{ "extensionId", VansSerializedValue::String("") },
			{ "bridge", VansSerializedValue::String("") },
			{ "version", VansSerializedValue::Int(1) },
			{ "properties", VansSerializedValue::Object({}) }
		}), {
			Child("/extensions/*/extensionId", "extensionId", "Extension Id",
				VansGameplayPropertyKind::String, VansSerializedValue::String(""), true),
			Child("/extensions/*/bridge", "bridge", "Bridge",
				VansGameplayPropertyKind::String, VansSerializedValue::String(""), true),
			Child("/extensions/*/version", "version", "Version",
				VansGameplayPropertyKind::Int, VansSerializedValue::Int(1), true),
			Child("/extensions/*/properties", "properties", "Properties",
				VansGameplayPropertyKind::Payload, VansSerializedValue::Object({}))
		});
		value.Register(std::move(action), error);

		auto actionSet = Base(VansAssetType::ActionSet, "ActionSet", ".vactionset");
		AddIdentity(actionSet, "/actionSetId");
		actionSet.fields.push_back(Field("/grants", "Action Grants", "Grants",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({}), true));
		ObjectArrayElement(actionSet.fields.back(), VansSerializedValue::Object({
			{ "action", VansSerializedValue::Object({}) },
			{ "level", VansSerializedValue::Float(1.0) },
			{ "inputBinding", VansSerializedValue::String("") },
			{ "dynamicTags", VansSerializedValue::Array({}) },
			{ "charges", VansSerializedValue::Int(-1) },
			{ "persistence", VansSerializedValue::String("OwnerLifetime") }
		}), {
			Child("/grants/*/action", "action", "Action",
				VansGameplayPropertyKind::AssetReference, VansSerializedValue::Object({}), true),
			Child("/grants/*/level", "level", "Level",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(1.0)),
			Child("/grants/*/inputBinding", "inputBinding", "Input Binding",
				VansGameplayPropertyKind::String, VansSerializedValue::String("")),
			StringArrayChild("/grants/*/dynamicTags", "dynamicTags", "Dynamic Tags"),
			Child("/grants/*/charges", "charges", "Charges",
				VansGameplayPropertyKind::Int, VansSerializedValue::Int(-1)),
			EnumChild("/grants/*/persistence", "persistence", "Persistence", "OwnerLifetime",
				{ "Transient", "OwnerLifetime", "Persistent" })
		});
		actionSet.fields.back().children[0].allowedAssetTypes = { VansAssetType::ActionDefinition };
		actionSet.fields.push_back(Field("/initialEffects", "Initial Effects", "Effects",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		actionSet.fields.back().hasArrayElement = true;
		actionSet.fields.back().arrayElementKind = VansGameplayPropertyKind::AssetReference;
		actionSet.fields.back().arrayElementDefault = VansSerializedValue::Object({});
		actionSet.fields.back().allowedAssetTypes = { VansAssetType::GameplayEffect };
		actionSet.fields.push_back(Field("/attributeOverrides", "Attribute Overrides", "Attributes",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		ObjectArrayElement(actionSet.fields.back(), VansSerializedValue::Object({
			{ "attribute", VansSerializedValue::String("") },
			{ "value", VansSerializedValue::Float(0.0) }
		}), {
			Child("/attributeOverrides/*/attribute", "attribute", "Attribute",
				VansGameplayPropertyKind::String, VansSerializedValue::String(""), true),
			Child("/attributeOverrides/*/value", "value", "Value",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.0), true)
		});
		actionSet.fields.push_back(Field("/revokePolicy", "Revoke Policy", "Lifecycle",
			VansGameplayPropertyKind::Enum, VansSerializedValue::String("CancelRunning")));
		actionSet.fields.back().enumValues = { "KeepRunning", "CancelRunning", "DeferUntilIdle" };
		actionSet.fields.push_back(Field("/removeInitialEffectsOnRevoke",
			"Remove Initial Effects", "Lifecycle", VansGameplayPropertyKind::Bool,
			VansSerializedValue::Bool(true)));
		value.Register(std::move(actionSet), error);

		auto effect = Base(VansAssetType::GameplayEffect, "GameplayEffect", ".veffect");
		AddIdentity(effect, "/effectId");
		effect.fields.push_back(Field("/duration/policy", "Duration Policy", "Duration",
			VansGameplayPropertyKind::Enum, VansSerializedValue::String("Instant"), true));
		effect.fields.back().enumValues = { "Instant", "Duration", "Infinite" };
		effect.fields.push_back(Field("/duration/seconds", "Duration", "Duration",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.0)));
		effect.fields.back().minimum = 0.0; effect.fields.back().hasMinimum = true;
		effect.fields.push_back(Field("/duration/period", "Period", "Duration",
			VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.0)));
		effect.fields.back().minimum = 0.0; effect.fields.back().hasMinimum = true;
		effect.fields.push_back(Field("/duration/executePeriodicOnApply", "Execute On Apply", "Duration",
			VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(false)));
		effect.fields.push_back(Field("/requirements", "Application Requirements", "Requirements",
			VansGameplayPropertyKind::TagQuery, VansSerializedValue::Object({})));
		effect.fields.back().children = TagQueryChildren("/requirements");
		effect.fields.push_back(Field("/effectTags", "Effect Tags", "Tags",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		StringArrayElement(effect.fields.back());
		effect.fields.push_back(Field("/grantedTags", "Granted Tags", "Tags",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		StringArrayElement(effect.fields.back());
		effect.fields.push_back(Field("/modifiers", "Modifiers", "Modifiers",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		ObjectArrayElement(effect.fields.back(), VansSerializedValue::Object({
			{ "attribute", VansSerializedValue::String("") },
			{ "operation", VansSerializedValue::String("Additive") },
			{ "magnitudeSource", VansSerializedValue::String("Fixed") },
			{ "magnitude", VansSerializedValue::Float(0.0) },
			{ "priority", VansSerializedValue::Int(0) },
			{ "setByCaller", VansSerializedValue::String("") },
			{ "capturedAttribute", VansSerializedValue::String("") },
			{ "capture", VansSerializedValue::String("Snapshot") },
			{ "contextPath", VansSerializedValue::String("") },
			{ "targetMetric", VansSerializedValue::String("Count") },
			{ "randomMinimum", VansSerializedValue::Float(0.0) },
			{ "randomMaximum", VansSerializedValue::Float(1.0) },
			{ "coefficient", VansSerializedValue::Float(1.0) },
			{ "preAdd", VansSerializedValue::Float(0.0) },
			{ "postAdd", VansSerializedValue::Float(0.0) }
		}), {
			Child("/modifiers/*/attribute", "attribute", "Attribute",
				VansGameplayPropertyKind::String, VansSerializedValue::String(""), true),
			EnumChild("/modifiers/*/operation", "operation", "Operation", "Additive",
				{ "Additive", "Multiplicative", "Override" }),
			EnumChild("/modifiers/*/magnitudeSource", "magnitudeSource", "Magnitude Source", "Fixed",
				{ "Fixed", "SetByCaller", "CapturedAttribute", "ContextPayload", "TargetData", "RandomRange" }),
			Child("/modifiers/*/magnitude", "magnitude", "Magnitude",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.0), true),
			Child("/modifiers/*/priority", "priority", "Priority",
				VansGameplayPropertyKind::Int, VansSerializedValue::Int(0)),
			Child("/modifiers/*/setByCaller", "setByCaller", "Set By Caller Field",
				VansGameplayPropertyKind::String, VansSerializedValue::String("")),
			Child("/modifiers/*/capturedAttribute", "capturedAttribute", "Captured Attribute",
				VansGameplayPropertyKind::String, VansSerializedValue::String("")),
			EnumChild("/modifiers/*/capture", "capture", "Capture Policy", "Snapshot",
				{ "Snapshot", "Dynamic" }),
			Child("/modifiers/*/contextPath", "contextPath", "Context Payload Path",
				VansGameplayPropertyKind::String, VansSerializedValue::String("")),
			EnumChild("/modifiers/*/targetMetric", "targetMetric", "Target Data Metric", "Count",
				{ "Count", "HitDistance", "AreaRadius", "RayLength" }),
			Child("/modifiers/*/randomMinimum", "randomMinimum", "Random Minimum",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.0)),
			Child("/modifiers/*/randomMaximum", "randomMaximum", "Random Maximum",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(1.0)),
			Child("/modifiers/*/coefficient", "coefficient", "Coefficient",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(1.0)),
			Child("/modifiers/*/preAdd", "preAdd", "Pre Add",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.0)),
			Child("/modifiers/*/postAdd", "postAdd", "Post Add",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.0))
		});
		effect.fields.push_back(Field("/stacking", "Stacking", "Stacking",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({})));
		effect.fields.push_back(Field("/stacking/policy", "Stacking Policy", "Stacking",
			VansGameplayPropertyKind::Enum, VansSerializedValue::String("None")));
		effect.fields.back().enumValues = { "None", "AggregateBySource", "AggregateByTarget" };
		effect.fields.push_back(Field("/stacking/overflow", "Overflow Policy", "Stacking",
			VansGameplayPropertyKind::Enum, VansSerializedValue::String("Reject")));
		effect.fields.back().enumValues = { "Reject", "RefreshOnly", "ReplaceOldest" };
		effect.fields.push_back(Field("/stacking/maximumStacks", "Maximum Stacks", "Stacking",
			VansGameplayPropertyKind::Int, VansSerializedValue::Int(1)));
		effect.fields.back().minimum = 1.0; effect.fields.back().hasMinimum = true;
		effect.fields.push_back(Field("/stacking/refreshDuration", "Refresh Duration", "Stacking",
			VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(true)));
		effect.fields.push_back(Field("/stacking/resetPeriod", "Reset Period", "Stacking",
			VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(false)));
		effect.fields.push_back(Field("/immunity", "Immunity", "Requirements",
			VansGameplayPropertyKind::TagQuery, VansSerializedValue::Object({})));
		effect.fields.back().children = TagQueryChildren("/immunity");
		effect.fields.push_back(Field("/cues", "Cues", "Presentation",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({})));
		for (const auto& cueList : std::vector<std::pair<const char*, const char*>>{
			{ "execute", "Execute Cues" }, { "persistent", "Persistent Cues" },
			{ "periodic", "Periodic Cues" }, { "remove", "Remove Cues" } })
		{
			effect.fields.push_back(Field(std::string("/cues/") + cueList.first,
				cueList.second, "Presentation", VansGameplayPropertyKind::Array,
				VansSerializedValue::Array({})));
			effect.fields.back().hasArrayElement = true;
			effect.fields.back().arrayElementKind = VansGameplayPropertyKind::AssetReference;
			effect.fields.back().arrayElementDefault = VansSerializedValue::Object({});
			effect.fields.back().allowedAssetTypes = { VansAssetType::GameplayCue };
		}
		value.Register(std::move(effect), error);

		auto cue = Base(VansAssetType::GameplayCue, "GameplayCue", ".vcue");
		AddIdentity(cue, "/cueId");
		cue.fields.push_back(Field("/scope", "Scope", "Cue",
			VansGameplayPropertyKind::Enum, VansSerializedValue::String("Target"), true));
		cue.fields.back().enumValues = { "Owner", "Target", "Observers", "World", "LocalOnly" };
		cue.fields.push_back(Field("/payloadSchema", "Payload Schema", "Cue",
			VansGameplayPropertyKind::AssetReference, VansSerializedValue::Object({})));
		cue.fields.back().allowedAssetTypes = { VansAssetType::PayloadSchema };
		cue.fields.push_back(Field("/adapters", "Adapter Mappings", "Cue",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({}), true));
		ObjectArrayElement(cue.fields.back(), VansSerializedValue::Object({
			{ "service", VansSerializedValue::String("") },
			{ "command", VansSerializedValue::String("") },
			{ "updateCommand", VansSerializedValue::String("") },
			{ "removeCommand", VansSerializedValue::String("") },
			{ "asset", VansSerializedValue::Object({}) },
			{ "parameters", VansSerializedValue::Object({}) }
		}), {
			Child("/adapters/*/service", "service", "Service",
				VansGameplayPropertyKind::String, VansSerializedValue::String(""), true),
			Child("/adapters/*/command", "command", "Command",
				VansGameplayPropertyKind::String, VansSerializedValue::String(""), true),
			Child("/adapters/*/updateCommand", "updateCommand", "Update Command",
				VansGameplayPropertyKind::String, VansSerializedValue::String("")),
			Child("/adapters/*/removeCommand", "removeCommand", "Remove Command",
				VansGameplayPropertyKind::String, VansSerializedValue::String("")),
			Child("/adapters/*/asset", "asset", "Asset",
				VansGameplayPropertyKind::AssetReference, VansSerializedValue::Object({})),
			Child("/adapters/*/parameters", "parameters", "Parameters",
				VansGameplayPropertyKind::Payload, VansSerializedValue::Object({}))
		});
		value.Register(std::move(cue), error);

		auto attributes = Base(VansAssetType::AttributeSet, "AttributeSet", ".vattributeset");
		AddIdentity(attributes, "/attributeSetId");
		attributes.fields.push_back(Field("/attributes", "Attributes", "Attributes",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({}), true));
		ObjectArrayElement(attributes.fields.back(), VansSerializedValue::Object({
			{ "name", VansSerializedValue::String("Attribute.NewValue") },
			{ "fieldId", VansSerializedValue::String("Attribute.NewValue") },
			{ "default", VansSerializedValue::Float(0.0) },
			{ "minimum", VansSerializedValue::Float(0.0) },
			{ "maximum", VansSerializedValue::Float(100.0) },
			{ "replicated", VansSerializedValue::Bool(false) }
		}), {
			Child("/attributes/*/name", "name", "Attribute Name",
				VansGameplayPropertyKind::String, VansSerializedValue::String("Attribute.NewValue"), true),
			Child("/attributes/*/fieldId", "fieldId", "Stable Field Id",
				VansGameplayPropertyKind::String, VansSerializedValue::String("Attribute.NewValue"), true),
			Child("/attributes/*/default", "default", "Default Value",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.0), true),
			Child("/attributes/*/minimum", "minimum", "Minimum",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.0)),
			Child("/attributes/*/maximum", "maximum", "Maximum",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(100.0)),
			Child("/attributes/*/replicated", "replicated", "Replicated",
				VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(false))
		});
		value.Register(std::move(attributes), error);

		auto targeting = Base(VansAssetType::TargetingPolicy, "TargetingPolicy", ".vtargeting");
		AddIdentity(targeting, "/targetingId");
		targeting.fields.push_back(Field("/steps", "Pipeline", "Targeting",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({}), true));
		ObjectArrayElement(targeting.fields.back(), VansSerializedValue::Object({
			{ "kind", VansSerializedValue::String("Acquire") },
			{ "handler", VansSerializedValue::String("Targeting.Acquire.Owner") },
			{ "parameters", VansSerializedValue::Object({}) }
		}), {
			EnumChild("/steps/*/kind", "kind", "Step Kind", "Acquire",
				{ "Acquire", "Filter", "Sort", "Limit", "Lock" }, true),
			Child("/steps/*/handler", "handler", "Handler",
				VansGameplayPropertyKind::String, VansSerializedValue::String("Targeting.Acquire.Owner"), true),
			Child("/steps/*/parameters", "parameters", "Parameters",
				VansGameplayPropertyKind::Payload, VansSerializedValue::Object({}))
		});
		value.Register(std::move(targeting), error);

		auto tags = Base(VansAssetType::GameplayTagTree, "GameplayTagTree", ".vtagtree");
		AddIdentity(tags, "/tagTreeId");
		tags.fields.push_back(Field("/tags", "Tags", "Tags",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({}), true));
		ObjectArrayElement(tags.fields.back(), VansSerializedValue::Object({
			{ "name", VansSerializedValue::String("") },
			{ "description", VansSerializedValue::String("") },
			{ "deprecated", VansSerializedValue::Bool(false) },
			{ "replacement", VansSerializedValue::String("") }
		}), {
			Child("/tags/*/name", "name", "Tag",
				VansGameplayPropertyKind::Tag, VansSerializedValue::String(""), true),
			Child("/tags/*/description", "description", "Description",
				VansGameplayPropertyKind::String, VansSerializedValue::String("")),
			Child("/tags/*/deprecated", "deprecated", "Deprecated",
				VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(false)),
			Child("/tags/*/replacement", "replacement", "Replacement",
				VansGameplayPropertyKind::Tag, VansSerializedValue::String(""))
		});
		tags.fields.back().arrayElementKind = VansGameplayPropertyKind::Payload;
		value.Register(std::move(tags), error);

		auto payload = Base(VansAssetType::PayloadSchema, "PayloadSchema", ".vpayloadschema");
		AddIdentity(payload, "/payloadTypeId");
		payload.fields.push_back(Field("/maximumBytes", "Maximum Bytes", "Payload",
			VansGameplayPropertyKind::Int, VansSerializedValue::Int(4096), true));
		payload.fields.back().minimum = 1.0; payload.fields.back().hasMinimum = true;
		payload.fields.push_back(Field("/editorSafe", "Editor Safe", "Payload",
			VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(false)));
		payload.fields.push_back(Field("/allowAdditionalFields", "Allow Additional Fields", "Payload",
			VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(false)));
		payload.fields.push_back(Field("/fields", "Fields", "Payload",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({}), true));
		ObjectArrayElement(payload.fields.back(), VansSerializedValue::Object({
			{ "name", VansSerializedValue::String("") },
			{ "type", VansSerializedValue::String("String") },
			{ "required", VansSerializedValue::Bool(false) },
			{ "sensitive", VansSerializedValue::Bool(false) }
		}), {
			Child("/fields/*/name", "name", "Field Name",
				VansGameplayPropertyKind::String, VansSerializedValue::String(""), true),
			EnumChild("/fields/*/type", "type", "Value Type", "String",
				{ "Bool", "Int32", "Int64", "Float", "Double", "Enum", "String", "Vec2",
					"Vec3", "Vec4", "Quaternion", "ColorLinear", "ColorSrgb",
					"ObjectReference", "Struct" }, true),
			Child("/fields/*/required", "required", "Required",
				VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(false)),
			Child("/fields/*/sensitive", "sensitive", "Sensitive",
				VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(false))
		});
		value.Register(std::move(payload), error);

		auto graph = Base(VansAssetType::ActionGraph, "ActionGraph", ".vactiongraph");
		AddIdentity(graph, "/graphId");
		graph.fields.push_back(Field("/entryNode", "Entry Node", "Graph",
			VansGameplayPropertyKind::String, VansSerializedValue::String(""), true));
		graph.fields.push_back(Field("/nodes", "Nodes", "Graph",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({}), true));
		ObjectArrayElement(graph.fields.back(), VansSerializedValue::Object({
			{ "guid", VansSerializedValue::String("") },
			{ "type", VansSerializedValue::String("Action.Graph.Complete") },
			{ "kind", VansSerializedValue::String("Flow") },
			{ "predictable", VansSerializedValue::Bool(false) },
			{ "rollbackPlan", VansSerializedValue::String("None") },
			{ "properties", VansSerializedValue::Object({}) },
			{ "editor", VansSerializedValue::Object({
				{ "x", VansSerializedValue::Float(0.0) },
				{ "y", VansSerializedValue::Float(0.0) }
			}) }
		}), {
			Child("/nodes/*/guid", "guid", "Node Guid",
				VansGameplayPropertyKind::String, VansSerializedValue::String(""), true),
			Child("/nodes/*/type", "type", "Node Type",
				VansGameplayPropertyKind::String, VansSerializedValue::String("Action.Graph.Complete"), true),
			EnumChild("/nodes/*/kind", "kind", "Node Kind", "Flow",
				{ "Pure", "Command", "Latent", "State", "Flow", "Transaction", "Bridge", "SubAction" }),
			Child("/nodes/*/predictable", "predictable", "Predictable",
				VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(false)),
			EnumChild("/nodes/*/rollbackPlan", "rollbackPlan", "Rollback Plan", "None",
				{ "None", "Automatic", "Compensate" }),
			Child("/nodes/*/properties", "properties", "Properties",
				VansGameplayPropertyKind::Payload, VansSerializedValue::Object({})),
			Child("/nodes/*/editor", "editor", "Editor Position",
				VansGameplayPropertyKind::Object, VansSerializedValue::Object({
					{ "x", VansSerializedValue::Float(0.0) },
					{ "y", VansSerializedValue::Float(0.0) }
				}))
		});
		graph.fields.back().children.back().cook = false;
		graph.fields.back().children.back().children = {
			Child("/nodes/*/editor/x", "x", "X", VansGameplayPropertyKind::Float,
				VansSerializedValue::Float(0.0)),
			Child("/nodes/*/editor/y", "y", "Y", VansGameplayPropertyKind::Float,
				VansSerializedValue::Float(0.0))
		};
		graph.fields.push_back(Field("/edges", "Edges", "Graph",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({}), true));
		ObjectArrayElement(graph.fields.back(), VansSerializedValue::Object({
			{ "from", VansSerializedValue::String("") },
			{ "output", VansSerializedValue::String("Success") },
			{ "to", VansSerializedValue::String("") },
			{ "order", VansSerializedValue::Int(0) }
		}), {
			Child("/edges/*/from", "from", "From Node",
				VansGameplayPropertyKind::String, VansSerializedValue::String(""), true),
			Child("/edges/*/output", "output", "Output Pin",
				VansGameplayPropertyKind::String, VansSerializedValue::String("Success"), true),
			Child("/edges/*/to", "to", "To Node",
				VansGameplayPropertyKind::String, VansSerializedValue::String(""), true),
			Child("/edges/*/order", "order", "Order",
				VansGameplayPropertyKind::Int, VansSerializedValue::Int(0))
		});
		graph.fields.push_back(Field("/variables", "Variables", "Graph",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		ObjectArrayElement(graph.fields.back(), VansSerializedValue::Object({
			{ "name", VansSerializedValue::String("") },
			{ "type", VansSerializedValue::String("String") },
			{ "default", VansSerializedValue::Null() }
		}), {
			Child("/variables/*/name", "name", "Name",
				VansGameplayPropertyKind::String, VansSerializedValue::String(""), true),
			EnumChild("/variables/*/type", "type", "Type", "String",
				{ "Bool", "Int", "Float", "String", "Tag", "Entity", "TargetData", "Payload" }),
			Child("/variables/*/default", "default", "Default Value",
				VansGameplayPropertyKind::Payload, VansSerializedValue::Null())
		});
		value.Register(std::move(graph), error);

		auto rig = Base(VansAssetType::CameraRigProfile, "CameraRigProfile", ".vcamerarig");
		AddIdentity(rig, "/cameraRigId");
		rig.fields.push_back(Field("/follow", "Follow", "Rig",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({
				{ "mode", VansSerializedValue::String("SpringArm") },
				{ "targetBinding", VansSerializedValue::String("Avatar") },
				{ "offset", VansSerializedValue::Object({
					{ "x", VansSerializedValue::Float(0.0) },
					{ "y", VansSerializedValue::Float(1.6) },
					{ "z", VansSerializedValue::Float(-3.0) } }) },
				{ "damping", VansSerializedValue::Float(0.15) }
			}), true));
		rig.fields.back().children = {
			EnumChild("/follow/mode", "mode", "Mode", "SpringArm",
				{ "Fixed", "SpringArm", "Orbit", "Rail" }),
			Child("/follow/targetBinding", "targetBinding", "Target Binding",
				VansGameplayPropertyKind::String, VansSerializedValue::String("Avatar")),
			Vector3Child("/follow/offset", "offset", "Offset", 0.0, 1.6, -3.0),
			Child("/follow/damping", "damping", "Damping",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.15))
		};
		rig.fields.back().children[3].hasMinimum = true;
		rig.fields.back().children[3].minimum = 0.0;
		rig.fields.back().children[3].hasMaximum = true;
		rig.fields.back().children[3].maximum = 60.0;
		rig.fields.push_back(Field("/lookAt", "Look At", "Rig",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({
				{ "enabled", VansSerializedValue::Bool(true) },
				{ "targetBinding", VansSerializedValue::String("Avatar") },
				{ "offset", VansSerializedValue::Object({
					{ "x", VansSerializedValue::Float(0.0) },
					{ "y", VansSerializedValue::Float(1.4) },
					{ "z", VansSerializedValue::Float(0.0) } }) },
				{ "damping", VansSerializedValue::Float(0.1) }
			}), true));
		rig.fields.back().children = {
			Child("/lookAt/enabled", "enabled", "Enabled",
				VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(true)),
			Child("/lookAt/targetBinding", "targetBinding", "Target Binding",
				VansGameplayPropertyKind::String, VansSerializedValue::String("Avatar")),
			Vector3Child("/lookAt/offset", "offset", "Offset", 0.0, 1.4, 0.0),
			Child("/lookAt/damping", "damping", "Damping",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.1))
		};
		rig.fields.back().children[3].hasMinimum = true;
		rig.fields.back().children[3].minimum = 0.0;
		rig.fields.back().children[3].hasMaximum = true;
		rig.fields.back().children[3].maximum = 60.0;
		rig.fields.push_back(Field("/collision", "Collision", "Rig",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({
				{ "enabled", VansSerializedValue::Bool(true) },
				{ "probeRadius", VansSerializedValue::Float(0.2) },
				{ "minimumDistance", VansSerializedValue::Float(0.1) },
				{ "padding", VansSerializedValue::Float(0.05) },
				{ "recoverySeconds", VansSerializedValue::Float(0.2) },
				{ "layers", VansSerializedValue::Array({}) }
			}), true));
		rig.fields.back().children = {
			Child("/collision/enabled", "enabled", "Enabled",
				VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(true)),
			Child("/collision/probeRadius", "probeRadius", "Probe Radius",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.2)),
			Child("/collision/minimumDistance", "minimumDistance", "Minimum Distance",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.1)),
			Child("/collision/padding", "padding", "Padding",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.05)),
			Child("/collision/recoverySeconds", "recoverySeconds", "Recovery",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.2)),
			StringArrayChild("/collision/layers", "layers", "Collision Layers")
		};
		for (std::size_t index : { std::size_t(1), std::size_t(2),
			std::size_t(3), std::size_t(4) })
		{
			rig.fields.back().children[index].hasMinimum = true;
			rig.fields.back().children[index].minimum = 0.0;
		}
		rig.fields.push_back(Field("/lens", "Lens", "Rig",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({
				{ "fieldOfView", VansSerializedValue::Float(60.0) },
				{ "nearPlane", VansSerializedValue::Float(0.1) },
				{ "farPlane", VansSerializedValue::Float(1000.0) },
				{ "focusDistance", VansSerializedValue::Float(10.0) }
			}), true));
		rig.fields.back().children = {
			Child("/lens/fieldOfView", "fieldOfView", "Field Of View",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(60.0)),
			Child("/lens/nearPlane", "nearPlane", "Near Plane",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.1)),
			Child("/lens/farPlane", "farPlane", "Far Plane",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(1000.0)),
			Child("/lens/focusDistance", "focusDistance", "Focus Distance",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(10.0))
		};
		rig.fields.back().children[0].hasMinimum = true;
		rig.fields.back().children[0].minimum = 1.0;
		rig.fields.back().children[0].hasMaximum = true;
		rig.fields.back().children[0].maximum = 179.0;
		rig.fields.back().children[1].hasMinimum = true;
		rig.fields.back().children[1].minimum = 0.001;
		rig.fields.back().children[2].hasMinimum = true;
		rig.fields.back().children[2].minimum = 0.002;
		rig.fields.back().children[2].hasMaximum = true;
		rig.fields.back().children[2].maximum = 1000000.0;
		rig.fields.back().children[3].hasMinimum = true;
		rig.fields.back().children[3].minimum = 0.0;
		rig.fields.push_back(Field("/composition", "Composition", "Rig",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({
				{ "screenX", VansSerializedValue::Float(0.5) },
				{ "screenY", VansSerializedValue::Float(0.5) },
				{ "deadZoneX", VansSerializedValue::Float(0.1) },
				{ "deadZoneY", VansSerializedValue::Float(0.1) }
			})));
		rig.fields.back().children = {
			Child("/composition/screenX", "screenX", "Screen X",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.5)),
			Child("/composition/screenY", "screenY", "Screen Y",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.5)),
			Child("/composition/deadZoneX", "deadZoneX", "Dead Zone X",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.1)),
			Child("/composition/deadZoneY", "deadZoneY", "Dead Zone Y",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.1))
		};
		for (VansGameplayPropertySchema& field : rig.fields.back().children)
		{
			field.hasMinimum = true;
			field.minimum = 0.0;
			field.hasMaximum = true;
			field.maximum = 1.0;
		}
		value.Register(std::move(rig), error);

		auto shake = Base(VansAssetType::CameraShakeProfile, "CameraShakeProfile", ".vcamerashake");
		AddIdentity(shake, "/cameraShakeId");
		shake.fields.push_back(Field("/noise", "Noise", "Shake",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({
				{ "translationAmplitude", VansSerializedValue::Object({
					{ "x", VansSerializedValue::Float(0.05) }, { "y", VansSerializedValue::Float(0.05) },
					{ "z", VansSerializedValue::Float(0.05) } }) },
				{ "rotationAmplitude", VansSerializedValue::Object({
					{ "x", VansSerializedValue::Float(0.5) }, { "y", VansSerializedValue::Float(0.5) },
					{ "z", VansSerializedValue::Float(0.5) } }) },
				{ "frequency", VansSerializedValue::Float(12.0) }
			}), true));
		shake.fields.back().children = {
			Vector3Child("/noise/translationAmplitude", "translationAmplitude", "Translation", 0.05, 0.05, 0.05),
			Vector3Child("/noise/rotationAmplitude", "rotationAmplitude", "Rotation", 0.5, 0.5, 0.5),
			Child("/noise/frequency", "frequency", "Frequency",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(12.0))
		};
		for (std::size_t vectorIndex : { std::size_t(0), std::size_t(1) })
			for (VansGameplayPropertySchema& axis : shake.fields.back().children[vectorIndex].children)
			{
				axis.hasMinimum = true;
				axis.minimum = 0.0;
			}
		shake.fields.back().children[2].hasMinimum = true;
		shake.fields.back().children[2].minimum = 0.001;
		shake.fields.back().children[2].hasMaximum = true;
		shake.fields.back().children[2].maximum = 1000.0;
		shake.fields.push_back(Field("/envelope", "Envelope", "Shake",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({
				{ "attack", VansSerializedValue::Float(0.05) },
				{ "sustain", VansSerializedValue::Float(0.1) },
				{ "release", VansSerializedValue::Float(0.15) }
			}), true));
		shake.fields.back().children = {
			Child("/envelope/attack", "attack", "Attack",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.05)),
			Child("/envelope/sustain", "sustain", "Sustain",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.1)),
			Child("/envelope/release", "release", "Release",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.15))
		};
		for (VansGameplayPropertySchema& field : shake.fields.back().children)
		{
			field.hasMinimum = true;
			field.minimum = 0.0;
			field.hasMaximum = true;
			field.maximum = 3600.0;
		}
		shake.fields.push_back(Field("/falloff", "Falloff", "Shake",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({
				{ "minimumDistance", VansSerializedValue::Float(0.0) },
				{ "maximumDistance", VansSerializedValue::Float(25.0) },
				{ "exponent", VansSerializedValue::Float(1.0) }
			})));
		shake.fields.back().children = {
			Child("/falloff/minimumDistance", "minimumDistance", "Minimum Distance",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(0.0)),
			Child("/falloff/maximumDistance", "maximumDistance", "Maximum Distance",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(25.0)),
			Child("/falloff/exponent", "exponent", "Exponent",
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(1.0))
		};
		for (VansGameplayPropertySchema& field : shake.fields.back().children)
		{
			field.hasMinimum = true;
			field.minimum = 0.0;
		}
		shake.fields.back().children[2].minimum = 0.001;
		shake.fields.push_back(Field("/seed", "Seed", "Shake",
			VansGameplayPropertyKind::Int, VansSerializedValue::Int(0)));
		shake.fields.back().hasMinimum = true;
		shake.fields.back().minimum = 0.0;
		value.Register(std::move(shake), error);

		auto layout = Base(VansAssetType::GAFEditorLayout, "GAFEditorLayout", ".gafeditorlayout", true);
		layout.fields.push_back(Field("/assetGuid", "Asset Guid", "Identity",
			VansGameplayPropertyKind::String, VansSerializedValue::String("")));
		layout.fields.push_back(Field("/panels", "Panels", "Layout",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({})));
		layout.fields.back().cook = false;
		layout.fields.push_back(Field("/graphView", "Graph View", "Layout",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({})));
		layout.fields.back().cook = false;
		layout.fields.push_back(Field("/expandedPaths", "Expanded Paths", "Layout",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({})));
		StringArrayElement(layout.fields.back());
		layout.fields.back().cook = false;
		value.Register(std::move(layout), error);

		value.Seal(error);
		return value;
	}();
	return registry;
}

bool VansGameplayAssetSchemaRegistry::IsGameplayAssetType(VansAssetType type)
{
	switch (type)
	{
	case VansAssetType::ActionDefinition:
	case VansAssetType::ActionSet:
	case VansAssetType::GameplayEffect:
	case VansAssetType::GameplayCue:
	case VansAssetType::AttributeSet:
	case VansAssetType::TargetingPolicy:
	case VansAssetType::GameplayTagTree:
	case VansAssetType::PayloadSchema:
	case VansAssetType::ActionGraph:
	case VansAssetType::CameraRigProfile:
	case VansAssetType::CameraShakeProfile:
	case VansAssetType::GAFEditorLayout:
		return true;
	default:
		return false;
	}
}
}
