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
		{ "initialTags", Value::Array({}) },
		{ "initialAttributes", Value::Array({}) },
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
			{ "level", Value::Float(1.0) },
			{ "inputBinding", Value::String("") },
			{ "dynamicTags", Value::Array({}) },
			{ "charges", Value::Int(-1) },
			{ "persistence", Value::String("OwnerLifetime") }
		});
	if (fieldName == "initialTags")
		return Value::Object({
			{ "tag", Value::String("") },
			{ "count", Value::Int(1) }
		});
	if (fieldName == "initialAttributes")
		return Value::Object({
			{ "attribute", Value::String("") },
			{ "value", Value::Float(0.0) }
		});
	if (fieldName == "dynamicTags") return Value::String("");
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
			const double level = FindObjectField(grant, "level")
				? ReadSerializedNumber(*FindObjectField(grant, "level"), 1.0) : 1.0;
			if (!std::isfinite(level) || level <= 0.0)
				AddError(diagnostics, "GAF-HOST-GRANT-LEVEL", "Action grant level must be positive",
					path + "/level");
			const std::int64_t charges = ReadSerializedIntField(grant, "charges", -1);
			if (charges < -1)
				AddError(diagnostics, "GAF-HOST-GRANT-CHARGES", "Charges must be -1 or non-negative",
					path + "/charges");
			const std::string persistence = ReadSerializedStringField(grant, "persistence", "OwnerLifetime");
			if (persistence != "Transient" && persistence != "OwnerLifetime" && persistence != "Persistent")
				AddError(diagnostics, "GAF-HOST-GRANT-PERSISTENCE", "Grant persistence is invalid",
					path + "/persistence");
			if (const Value* tags = FindObjectField(grant, "dynamicTags");
				!tags || tags->kind != Value::Kind::Array)
				AddError(diagnostics, "GAF-HOST-GRANT-TAGS", "Dynamic Tags must be an array",
					path + "/dynamicTags");
		}
	if (const Value* tags = RequireArray(data, "initialTags", diagnostics))
		for (std::size_t index = 0; index < tags->arrayItems.size(); ++index)
		{
			const Value& item = tags->arrayItems[index];
			const std::string path = "/initialTags/" + std::to_string(index);
			const std::string tag = item.kind == Value::Kind::String
				? item.stringValue : ReadSerializedStringField(item, "tag");
			if (tag.empty()) AddError(diagnostics, "GAF-HOST-TAG", "Initial Tag is empty", path);
			if (item.kind == Value::Kind::Object && ReadSerializedIntField(item, "count", 1) <= 0)
				AddError(diagnostics, "GAF-HOST-TAG-COUNT", "Initial Tag count must be positive",
					path + "/count");
		}
	if (const Value* attributes = RequireArray(data, "initialAttributes", diagnostics))
		for (std::size_t index = 0; index < attributes->arrayItems.size(); ++index)
		{
			const Value& item = attributes->arrayItems[index];
			const std::string path = "/initialAttributes/" + std::to_string(index);
			if (item.kind != Value::Kind::Object || ReadSerializedStringField(item, "attribute").empty())
				AddError(diagnostics, "GAF-HOST-ATTRIBUTE", "Initial Attribute requires a name", path);
			if (const Value* value = FindObjectField(item, "value"); value &&
				!std::isfinite(ReadSerializedNumber(*value)))
				AddError(diagnostics, "GAF-HOST-ATTRIBUTE-VALUE", "Initial Attribute value must be finite",
					path + "/value");
		}
	return diagnostics;
}
}
