#include "VansGameplayActionHostAuthoring.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cmath>

namespace Vans
{
namespace
{
using Value = VansSerializedValue;

Value AssetReference()
{
	return Value::Object({ { "guid", Value::String("") } });
}

std::string ReferenceString(const Value& value)
{
	if (value.kind == Value::Kind::String) return value.stringValue;
	if (value.kind != Value::Kind::Object) return {};
	for (const char* field : { "guid", "assetGuid", "path", "assetPath", "id", "stableId" })
	{
		const std::string reference = ReadSerializedStringField(value, field);
		if (!reference.empty()) return reference;
	}
	return {};
}

void AddError(VansGameplayDiagnostics& diagnostics,
	std::string code, std::string message, std::string path)
{
	diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error,
		std::move(code), std::move(message), {}, std::move(path) });
}

const Value* RequireArray(
	const Value& data,
	const char* field,
	VansGameplayDiagnostics& diagnostics)
{
	const Value* value = FindObjectField(data, field);
	if (!value || value->kind != Value::Kind::Array)
	{
		AddError(diagnostics, "GAF-HOST-FIELD", std::string(field) + " must be an array",
			"/" + std::string(field));
		return nullptr;
	}
	return value;
}
}

VansSerializedValue VansGameplayActionHostAuthoring::CreateDefaultData()
{
	return Value::Object({
		{ "actionSets", Value::Array({}) },
		{ "grants", Value::Array({}) },
		{ "initializers", Value::Array({}) },
		{ "autoActivate", Value::Array({}) }
	});
}

std::optional<VansSerializedValue> VansGameplayActionHostAuthoring::CreateDefaultArrayElement(
	std::string_view fieldName)
{
	if (fieldName == "actionSets" || fieldName == "autoActivate") return AssetReference();
	if (fieldName == "grants")
		return Value::Object({
			{ "action", AssetReference() },
			{ "extensions", Value::Array({}) }
		});
	if (fieldName == "initializers" || fieldName == "extensions")
		return Value::Object({
			{ "type", Value::String("") },
			{ "inputs", Value::Object({}) }
		});
	return std::nullopt;
}

VansGameplayDiagnostics VansGameplayActionHostAuthoring::Validate(const VansSerializedValue& data)
{
	VansGameplayDiagnostics diagnostics;
	if (data.kind != Value::Kind::Object)
	{
		AddError(diagnostics, "GAF-HOST-ROOT", "ActionHost data must be an object", "/data");
		return diagnostics;
	}
	for (const auto& [name, value] : data.objectFields)
	{
		(void)value;
		if (name != "actionSets" && name != "grants" && name != "initializers" &&
			name != "autoActivate")
			AddError(diagnostics, "GAF-HOST-UNKNOWN-FIELD",
				"ActionHost data contains an unknown field", "/" + name);
	}
	for (const char* field : { "actionSets", "autoActivate" })
		if (const Value* references = RequireArray(data, field, diagnostics))
			for (std::size_t index = 0; index < references->arrayItems.size(); ++index)
				if (ReferenceString(references->arrayItems[index]).empty())
					AddError(diagnostics, "GAF-HOST-ASSET", "Asset reference is empty",
						"/" + std::string(field) + "/" + std::to_string(index));
	if (const Value* grants = RequireArray(data, "grants", diagnostics))
		for (std::size_t index = 0; index < grants->arrayItems.size(); ++index)
		{
			const Value& grant = grants->arrayItems[index];
			const std::string path = "/grants/" + std::to_string(index);
			if (grant.kind != Value::Kind::Object)
			{
				AddError(diagnostics, "GAF-HOST-GRANT", "Action grant must be an object", path);
				continue;
			}
			const Value* action = FindObjectField(grant, "action");
			if (!action || ReferenceString(*action).empty())
				AddError(diagnostics, "GAF-HOST-GRANT-ACTION", "Action grant requires an Action asset",
					path + "/action");
			for (const auto& [name, value] : grant.objectFields)
			{
				(void)value;
				if (name != "action" && name != "extensions")
					AddError(diagnostics, "GAF-HOST-GRANT-FIELD",
						"Action grant contains an unknown field", path + "/" + name);
			}
			const Value* extensions = FindObjectField(grant, "extensions");
			if (!extensions || extensions->kind != Value::Kind::Array)
			{
				AddError(diagnostics, "GAF-HOST-GRANT-EXTENSIONS",
					"Action grant extensions must be an array", path + "/extensions");
				continue;
			}
			for (std::size_t extensionIndex = 0;
				extensionIndex < extensions->arrayItems.size(); ++extensionIndex)
			{
				const Value& extension = extensions->arrayItems[extensionIndex];
				const std::string extensionPath =
					path + "/extensions/" + std::to_string(extensionIndex);
				const Value* inputs = extension.kind == Value::Kind::Object
					? FindObjectField(extension, "inputs") : nullptr;
				if (extension.kind != Value::Kind::Object ||
					ReadSerializedStringField(extension, "type").empty() || !inputs ||
					inputs->kind != Value::Kind::Object)
					AddError(diagnostics, "GAF-HOST-GRANT-EXTENSION",
						"Grant extension requires a TypeId and object inputs",
						extensionPath);
				if (extension.kind == Value::Kind::Object)
					for (const auto& [name, value] : extension.objectFields)
					{
						(void)value;
						if (name != "type" && name != "inputs")
							AddError(diagnostics, "GAF-HOST-GRANT-EXTENSION-FIELD",
								"Grant extension contains an unknown field",
								extensionPath + "/" + name);
					}
			}
		}
	if (const Value* initializers = RequireArray(data, "initializers", diagnostics))
		for (std::size_t index = 0; index < initializers->arrayItems.size(); ++index)
		{
			const Value& item = initializers->arrayItems[index];
			const std::string path = "/initializers/" + std::to_string(index);
			const Value* inputs = item.kind == Value::Kind::Object
				? FindObjectField(item, "inputs") : nullptr;
			if (item.kind != Value::Kind::Object ||
				ReadSerializedStringField(item, "type").empty() || !inputs ||
				inputs->kind != Value::Kind::Object)
				AddError(diagnostics, "GAF-HOST-INITIALIZER",
					"Host initializer requires a TypeId and object inputs", path);
			if (item.kind == Value::Kind::Object)
				for (const auto& [name, value] : item.objectFields)
				{
					(void)value;
					if (name != "type" && name != "inputs")
						AddError(diagnostics, "GAF-HOST-INITIALIZER-FIELD",
							"Host initializer contains an unknown field", path + "/" + name);
				}
		}
	return diagnostics;
}
}
