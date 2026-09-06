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

VansGameplayPropertySchema TypedRecordArrayChild(
	std::string stablePath,
	std::string memberName,
	std::string displayName)
{
	const std::string typePath = stablePath + "/*/type";
	const std::string inputsPath = stablePath + "/*/inputs";
	auto field = Child(std::move(stablePath), std::move(memberName),
		std::move(displayName), VansGameplayPropertyKind::Array,
		VansSerializedValue::Array({}));
	ObjectArrayElement(field, VansSerializedValue::Object({
		{ "type", VansSerializedValue::String("") },
		{ "inputs", VansSerializedValue::Object({}) }
	}), {
		Child(typePath, "type", "Type",
			VansGameplayPropertyKind::String, VansSerializedValue::String(""), true),
		Child(inputsPath, "inputs", "Inputs",
			VansGameplayPropertyKind::Payload, VansSerializedValue::Object({}), true)
	});
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
	std::unordered_set<std::string> allowedMembers;
	for (const VansGameplayPropertySchema& child : field.children)
		allowedMembers.insert(child.path);
	for (const auto& [member, childValue] : value.objectFields)
	{
		(void)childValue;
		if (allowedMembers.find(member) == allowedMembers.end())
			diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error,
				"GAF-FIELD-UNKNOWN", "Object contains an unregistered field", {},
				path + "/" + member });
	}
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
	if (descriptor.assetType == VansAssetType::Unknown || descriptor.assetKind.empty() ||
		descriptor.extension.empty() || descriptor.fields.empty())
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
	std::unordered_set<std::string> allowedRootFields;
	for (const VansGameplayPropertySchema& field : descriptor->fields)
	{
		const std::vector<std::string> path = SplitSerializedPointer(field.path);
		if (!path.empty()) allowedRootFields.insert(path.front());
	}
	for (const auto& [name, value] : root.objectFields)
	{
		(void)value;
		if (allowedRootFields.find(name) == allowedRootFields.end())
			diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error,
				"GAF-FIELD-UNKNOWN", "Gameplay asset contains an unregistered root field",
				{}, "/" + name });
	}
	const VansSerializedValue* kind = FindObjectField(root, "assetKind");
	if (!kind || kind->kind != VansSerializedValue::Kind::String || kind->stringValue != descriptor->assetKind)
		diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error, "GAF-ASSET-KIND",
			"assetKind does not match the file extension", {}, "/assetKind" });
	if (type == VansAssetType::ActionDefinition)
	{
		if (!HasNonEmptyString(root, "/actionId"))
			diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error, "GAF-ACTION-ID",
				"ActionId must be non-empty", {}, "/actionId" });
		const VansSerializedValue* drivers = FindSerializedPointer(
			root, "/phases/execute/drivers");
		if (!drivers || drivers->kind != VansSerializedValue::Kind::Array ||
			drivers->arrayItems.empty())
			diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error, "GAF-ACTION-EXECUTION",
				"Action requires at least one registered execution Driver", {},
				"/phases/execute/drivers" });
	}
	if (type == VansAssetType::GameplayEffect)
	{
		const VansSerializedValue* stacks = FindSerializedPointer(root, "/stacking/maximumStacks");
		if (stacks && stacks->kind == VansSerializedValue::Kind::Int && stacks->intValue <= 0)
			diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error, "GAF-EFFECT-STACK",
				"maximumStacks must be positive", {}, "/stacking/maximumStacks" });
		const VansSerializedValue* extensions = FindSerializedPointer(root, "/extensions");
		if (extensions && extensions->kind == VansSerializedValue::Kind::Array)
			for (std::size_t index = 0; index < extensions->arrayItems.size(); ++index)
			{
				const VansSerializedValue& record = extensions->arrayItems[index];
				if (record.kind != VansSerializedValue::Kind::Object ||
					ReadSerializedStringField(record, "type") !=
						"Gameplay.Effect.AttributeModifier") continue;
				const VansSerializedValue* modifier = FindObjectField(record, "inputs");
				if (!modifier || modifier->kind != VansSerializedValue::Kind::Object) continue;
				const std::string path = "/extensions/" + std::to_string(index) + "/inputs";
				const std::string source = ReadSerializedStringField(
					*modifier, "magnitudeSource", "Fixed");
				bool validSource = true;
				if (source == "SetByCaller")
					validSource = !ReadSerializedStringField(*modifier, "setByCaller").empty();
				else if (source == "CapturedAttribute")
				{
					const std::string captured = ReadSerializedStringField(*modifier, "capturedAttribute");
					validSource = !captured.empty() &&
						(ReadSerializedStringField(*modifier, "capture", "Snapshot") != "Dynamic" ||
							captured != ReadSerializedStringField(*modifier, "attribute"));
				}
				else if (source == "ContextPayload")
				{
					const std::string contextPath = ReadSerializedStringField(*modifier, "contextPath");
					validSource = !contextPath.empty() && contextPath.front() == '/';
				}
				if (!validSource)
					diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error,
						"GAF-EFFECT-MAGNITUDE-SOURCE",
						"Effect magnitude source is missing required data or is self-referential",
						{}, path + "/magnitudeSource" });
				const VansSerializedValue* minimum = FindObjectField(*modifier, "randomMinimum");
				const VansSerializedValue* maximum = FindObjectField(*modifier, "randomMaximum");
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

bool RegisterGameplayAssetSchemas(
	VansGameplayAssetSchemaRegistry& value,
	std::string& error,
	bool includeCore,
	bool includeGameplayPrimitives)
{
		const auto addTypedRecords = [](VansGameplayAssetSchemaDescriptor& descriptor,
			const char* path, const char* displayName, const char* group, bool required = false)
		{
			auto field = Field(path, displayName, group, VansGameplayPropertyKind::Array,
				VansSerializedValue::Array({}), required);
			ObjectArrayElement(field, VansSerializedValue::Object({
				{ "type", VansSerializedValue::String("") },
				{ "inputs", VansSerializedValue::Object({}) }
			}), {
				Child(std::string(path) + "/*/type", "type", "Registered Type",
					VansGameplayPropertyKind::String, VansSerializedValue::String(""), true),
				Child(std::string(path) + "/*/inputs", "inputs", "Typed Inputs",
					VansGameplayPropertyKind::Payload, VansSerializedValue::Object({}), true)
			});
			descriptor.fields.push_back(std::move(field));
		};

	if (includeCore)
	{
		auto action = Base(VansAssetType::ActionDefinition, "ActionDefinition", ".vaction");
		action.fields.push_back(Field("/actionId", "Action Id", "Identity",
			VansGameplayPropertyKind::String, VansSerializedValue::String(""), true));
		action.fields.push_back(Field("/metadata", "Metadata", "Identity",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({
				{ "displayName", VansSerializedValue::String("New Action") },
				{ "category", VansSerializedValue::String("Gameplay") },
				{ "labels", VansSerializedValue::Array({}) },
				{ "priority", VansSerializedValue::Int(0) }
			}), true));
		action.fields.back().children = {
			Child("/metadata/displayName", "displayName", "Display Name",
				VansGameplayPropertyKind::String, VansSerializedValue::String("New Action"), true),
			Child("/metadata/category", "category", "Category",
				VansGameplayPropertyKind::String, VansSerializedValue::String("Gameplay"), true),
			StringArrayChild("/metadata/labels", "labels", "Labels"),
			Child("/metadata/priority", "priority", "Priority",
				VansGameplayPropertyKind::Int, VansSerializedValue::Int(0))
		};
		action.fields.back().children[2].arrayElementKind = VansGameplayPropertyKind::Tag;
		action.fields.push_back(Field("/context", "Context", "Context",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({
				{ "schema", VansSerializedValue::Array({}) },
				{ "defaults", VansSerializedValue::Object({}) }
			}), true));
		action.fields.back().children = {
			Child("/context/schema", "schema", "Slots", VansGameplayPropertyKind::Array,
				VansSerializedValue::Array({}), true),
			Child("/context/defaults", "defaults", "Defaults", VansGameplayPropertyKind::Payload,
				VansSerializedValue::Object({}), true)
		};
		ObjectArrayElement(action.fields.back().children[0], VansSerializedValue::Object({
			{ "name", VansSerializedValue::String("") },
			{ "type", VansSerializedValue::String("") },
			{ "required", VansSerializedValue::Bool(false) }
		}), {
			Child("/context/schema/*/name", "name", "Slot Name",
				VansGameplayPropertyKind::String, VansSerializedValue::String(""), true),
			Child("/context/schema/*/type", "type", "Value Type",
				VansGameplayPropertyKind::String, VansSerializedValue::String(""), true),
			Child("/context/schema/*/required", "required", "Required",
				VansGameplayPropertyKind::Bool, VansSerializedValue::Bool(false))
		});
		addTypedRecords(action, "/policies", "Policies", "Policy", true);
		action.fields.push_back(Field("/phases", "Phases", "Lifecycle",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({
				{ "activate", VansSerializedValue::Object({}) },
				{ "commit", VansSerializedValue::Object({}) },
				{ "execute", VansSerializedValue::Object({}) },
				{ "finish", VansSerializedValue::Object({}) },
				{ "cancel", VansSerializedValue::Object({}) }
			}), true));
		for (const char* phase : { "activate", "commit", "execute", "finish", "cancel" })
			action.fields.push_back(Field(std::string("/phases/") + phase,
				phase, "Lifecycle", VansGameplayPropertyKind::Object,
				VansSerializedValue::Object({}), true));
		addTypedRecords(action, "/phases/activate/guards", "Activate Guards", "Activate", true);
		addTypedRecords(action, "/phases/activate/operations", "Activate Operations", "Activate", true);
		addTypedRecords(action, "/phases/commit/guards", "Commit Guards", "Commit", true);
		addTypedRecords(action, "/phases/commit/operations", "Commit Operations", "Commit", true);
		addTypedRecords(action, "/phases/execute/drivers", "Execution Drivers", "Execute", true);
		addTypedRecords(action, "/phases/execute/operations", "Execute Operations", "Execute", true);
		addTypedRecords(action, "/phases/finish/operations", "Finish Operations", "Finish", true);
		addTypedRecords(action, "/phases/cancel/operations", "Cancel Operations", "Cancel", true);
		action.fields.push_back(Field("/variables", "Variables", "Data",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({}), true));
		ObjectArrayElement(action.fields.back(), VansSerializedValue::Object({
			{ "name", VansSerializedValue::String("") },
			{ "type", VansSerializedValue::String("Core.Value.Any") },
			{ "default", VansSerializedValue::Null() }
		}), {
			Child("/variables/*/name", "name", "Name",
				VansGameplayPropertyKind::String, VansSerializedValue::String(""), true),
			Child("/variables/*/type", "type", "Value Type",
				VansGameplayPropertyKind::String, VansSerializedValue::String("Core.Value.Any"), true),
			Child("/variables/*/default", "default", "Default",
				VansGameplayPropertyKind::Payload, VansSerializedValue::Null(), true)
		});
		addTypedRecords(action, "/transitions", "Transitions", "Transition", true);
		action.fields.push_back(Field("/dependencies", "Dependencies", "Dependency",
			VansGameplayPropertyKind::Object, VansSerializedValue::Object({
				{ "capabilities", VansSerializedValue::Array({}) },
				{ "modules", VansSerializedValue::Array({}) }
			}), true));
		action.fields.back().children = {
			StringArrayChild("/dependencies/capabilities", "capabilities", "Capabilities"),
			StringArrayChild("/dependencies/modules", "modules", "Modules")
		};
		addTypedRecords(action, "/extensions", "Extensions", "Extension", true);
		value.Register(std::move(action), error);

		auto actionSet = Base(VansAssetType::ActionSet, "ActionSet", ".vactionset");
		AddIdentity(actionSet, "/actionSetId");
		actionSet.fields.push_back(Field("/grants", "Action Grants", "Grants",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({}), true));
		ObjectArrayElement(actionSet.fields.back(), VansSerializedValue::Object({
			{ "action", VansSerializedValue::Object({}) },
			{ "extensions", VansSerializedValue::Array({}) }
		}), {
			Child("/grants/*/action", "action", "Action",
				VansGameplayPropertyKind::AssetReference, VansSerializedValue::Object({}), true),
			TypedRecordArrayChild("/grants/*/extensions", "extensions", "Grant Extensions")
		});
		actionSet.fields.back().children[0].allowedAssetTypes = { VansAssetType::ActionDefinition };
		addTypedRecords(actionSet, "/initializers", "Initializers", "Initialization", true);
		addTypedRecords(actionSet, "/policies", "Grant Policies", "Lifecycle", true);
		actionSet.fields.back().defaultValue = VansSerializedValue::Array({
			VansSerializedValue::Object({
				{ "type", VansSerializedValue::String("Core.Grant.Revoke") },
				{ "inputs", VansSerializedValue::Object({
					{ "mode", VansSerializedValue::String("CancelRunning") }
				}) }
			})
		});
		value.Register(std::move(actionSet), error);
	}

	if (includeGameplayPrimitives)
	{
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
		addTypedRecords(effect, "/extensions", "Effect Records", "Effect", true);
		value.Register(std::move(effect), error);

		auto cue = Base(VansAssetType::GameplayCue, "GameplayCue", ".vcue");
		AddIdentity(cue, "/cueId");
		cue.fields.push_back(Field("/scope", "Scope", "Cue",
			VansGameplayPropertyKind::Enum, VansSerializedValue::String("Target"), true));
		cue.fields.back().enumValues = { "Owner", "Target", "Observers", "World", "LocalOnly" };
		cue.fields.push_back(Field("/payloadSchema", "Payload Schema", "Cue",
			VansGameplayPropertyKind::AssetReference, VansSerializedValue::Object({})));
		cue.fields.back().allowedAssetTypes = { VansAssetType::PayloadSchema };
		addTypedRecords(cue, "/bindings", "Invoke Bindings", "Cue", true);
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
			{ "maximum", VansSerializedValue::Float(100.0) }
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
				VansGameplayPropertyKind::Float, VansSerializedValue::Float(100.0))
		});
		value.Register(std::move(attributes), error);

		auto targeting = Base(VansAssetType::TargetingPolicy, "TargetingPolicy", ".vtargeting");
		AddIdentity(targeting, "/targetingId");
		targeting.fields.push_back(Field("/steps", "Pipeline", "Targeting",
			VansGameplayPropertyKind::Array, VansSerializedValue::Array({}), true));
		ObjectArrayElement(targeting.fields.back(), VansSerializedValue::Object({
			{ "type", VansSerializedValue::String("Targeting.Acquire.Owner") },
			{ "inputs", VansSerializedValue::Object({}) }
		}), {
			Child("/steps/*/type", "type", "Operation Type",
				VansGameplayPropertyKind::String, VansSerializedValue::String("Targeting.Acquire.Owner"), true),
			Child("/steps/*/inputs", "inputs", "Inputs",
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
	}

	if (includeCore)
	{
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
				{ "Pure", "Command", "Latent", "State", "Flow", "Bridge", "SubAction" }),
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
	}

		return error.empty();
}

bool VansRegisterCoreGameplayAssetSchemas(
	VansGameplayAssetSchemaRegistry& registry,
	std::string& error)
{
	return RegisterGameplayAssetSchemas(registry, error, true, false);
}

bool VansRegisterGameplayPrimitiveAssetSchemas(
	VansGameplayAssetSchemaRegistry& registry,
	std::string& error)
{
	return RegisterGameplayAssetSchemas(registry, error, false, true);
}

bool VansRegisterDefaultGameplayAssetSchemas(
	VansGameplayAssetSchemaRegistry& registry,
	std::string& error)
{
	return VansRegisterCoreGameplayAssetSchemas(registry, error) &&
		VansRegisterGameplayPrimitiveAssetSchemas(registry, error);
}

bool VansGameplayAssetSchemaRegistry::IsGameplayAssetType(VansAssetType type)
{
	return BuiltIns().Resolve(type) != nullptr;
}
}
