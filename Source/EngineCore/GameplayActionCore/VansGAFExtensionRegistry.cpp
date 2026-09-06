#include "VansGAFExtensionRegistry.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace Vans
{
namespace
{
void HashText(std::uint64_t& hash, std::string_view text)
{
	for (unsigned char character : text)
	{
		hash ^= character;
		hash *= 1099511628211ull;
	}
	hash ^= 0xffu;
	hash *= 1099511628211ull;
}

VansGAFInputFieldDescriptor Input(
	std::string name,
	std::string valueType,
	bool required = false,
	VansSerializedValue defaultValue = {})
{
	return { std::move(name), std::move(valueType), required, std::move(defaultValue) };
}

VansGAFInputSchemaDescriptor Schema(
	std::string typeId,
	std::vector<VansGAFInputFieldDescriptor> fields)
{
	return { std::move(typeId), std::move(fields) };
}

bool IsReference(const VansSerializedValue& value)
{
	if (value.kind == VansSerializedValue::Kind::String) return !value.stringValue.empty();
	if (value.kind != VansSerializedValue::Kind::Object) return false;
	return !ReadSerializedStringField(value, "assetGuid").empty() ||
		!ReadSerializedStringField(value, "pathHint").empty() ||
		!ReadSerializedStringField(value, "stableId").empty();
}

bool RegisterType(VansGAFTypeRegistry& registry,
	VansGAFExtensionKind kind, const char* typeId, std::string& error)
{
	return registry.RegisterType({ typeId, typeId, kind }, error);
}
}

bool VansGAFTypeRegistry::RegisterType(VansGAFTypeDescriptor descriptor, std::string& error)
{
	if (m_Sealed)
	{
		error = "GAF Type registry is sealed";
		return false;
	}
	if (descriptor.typeId.empty() || descriptor.displayName.empty() ||
		descriptor.kind == VansGAFExtensionKind::ValueType)
	{
		error = "GAF extension Type descriptor is invalid";
		return false;
	}
	if (!m_Types.emplace(descriptor.typeId, std::move(descriptor)).second)
	{
		error = "duplicate GAF extension TypeId";
		return false;
	}
	return true;
}

bool VansGAFTypeRegistry::RegisterValueType(
	VansGAFValueTypeDescriptor descriptor,
	std::string& error)
{
	if (m_Sealed)
	{
		error = "GAF Type registry is sealed";
		return false;
	}
	if (descriptor.typeId.empty() || descriptor.displayName.empty() || !descriptor.validate)
	{
		error = "GAF ValueType descriptor is invalid";
		return false;
	}
	if (!m_ValueTypes.emplace(descriptor.typeId, std::move(descriptor)).second)
	{
		error = "duplicate GAF ValueType TypeId";
		return false;
	}
	return true;
}

bool VansGAFTypeRegistry::Seal(std::string& error)
{
	if (m_Types.empty() || m_ValueTypes.empty())
	{
		error = "GAF Type registry is incomplete";
		return false;
	}
	m_Sealed = true;
	return true;
}

const VansGAFTypeDescriptor* VansGAFTypeRegistry::ResolveType(std::string_view typeId) const
{
	const auto found = m_Types.find(std::string(typeId));
	return found == m_Types.end() ? nullptr : &found->second;
}

const VansGAFValueTypeDescriptor* VansGAFTypeRegistry::ResolveValueType(
	std::string_view typeId) const
{
	const auto found = m_ValueTypes.find(std::string(typeId));
	return found == m_ValueTypes.end() ? nullptr : &found->second;
}

std::uint64_t VansGAFTypeRegistry::Fingerprint() const
{
	std::vector<std::string> entries;
	for (const auto& [id, descriptor] : m_Types)
		entries.push_back(id + ":" + std::to_string(static_cast<int>(descriptor.kind)));
	for (const auto& [id, descriptor] : m_ValueTypes)
	{
		(void)descriptor;
		entries.push_back(id + ":value");
	}
	std::sort(entries.begin(), entries.end());
	std::uint64_t hash = 1469598103934665603ull;
	for (const std::string& entry : entries) HashText(hash, entry);
	return hash;
}

bool VansGAFSchemaRegistry::Register(
	VansGAFInputSchemaDescriptor descriptor,
	std::string& error)
{
	if (m_Sealed)
	{
		error = "GAF Schema registry is sealed";
		return false;
	}
	if (!m_Types || !m_Types->IsSealed() || descriptor.typeId.empty() ||
		!m_Types->ResolveType(descriptor.typeId))
	{
		error = "GAF input Schema has no registered extension Type";
		return false;
	}
	std::set<std::string> fields;
	for (const VansGAFInputFieldDescriptor& field : descriptor.fields)
		if (field.name.empty() || !fields.insert(field.name).second ||
			!m_Types->ResolveValueType(field.valueType))
		{
			error = "GAF input Schema contains an invalid field";
			return false;
		}
	if (!m_Schemas.emplace(descriptor.typeId, std::move(descriptor)).second)
	{
		error = "duplicate GAF input Schema TypeId";
		return false;
	}
	return true;
}

bool VansGAFSchemaRegistry::Seal(std::string& error)
{
	if (!m_Types || !m_Types->IsSealed() || m_Schemas.empty())
	{
		error = "GAF Schema registry has no sealed Type registry";
		return false;
	}
	for (const auto& [typeId, descriptor] : m_Schemas)
	{
		(void)descriptor;
		if (!m_Types->ResolveType(typeId))
		{
			error = "GAF Schema registry contains an unresolved TypeId";
			return false;
		}
	}
	m_Sealed = true;
	return true;
}

const VansGAFInputSchemaDescriptor* VansGAFSchemaRegistry::Resolve(
	std::string_view typeId) const
{
	const auto found = m_Schemas.find(std::string(typeId));
	return found == m_Schemas.end() ? nullptr : &found->second;
}

VansGameplayDiagnostics VansGAFSchemaRegistry::Validate(
	VansGAFExtensionKind expectedKind,
	std::string_view typeId,
	const VansSerializedValue& inputs,
	std::string_view fieldPath) const
{
	VansGameplayDiagnostics diagnostics;
	const std::string base(fieldPath);
	const VansGAFTypeDescriptor* type = m_Types ? m_Types->ResolveType(typeId) : nullptr;
	const VansGAFInputSchemaDescriptor* schema = Resolve(typeId);
	if (!m_Sealed || !type || type->kind != expectedKind || !schema)
	{
		diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error,
			"GAF-TYPE-UNREGISTERED", "GAF record TypeId is not registered for this slot",
			{}, base + "/type" });
		return diagnostics;
	}
	if (inputs.kind != VansSerializedValue::Kind::Object)
	{
		diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error,
			"GAF-INPUT-OBJECT", "GAF record inputs must be an object", {}, base + "/inputs" });
		return diagnostics;
	}
	std::unordered_map<std::string, const VansGAFInputFieldDescriptor*> fields;
	for (const VansGAFInputFieldDescriptor& field : schema->fields)
		fields.emplace(field.name, &field);
	for (const auto& [name, value] : inputs.objectFields)
	{
		const auto found = fields.find(name);
		if (found == fields.end())
		{
			diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error,
				"GAF-INPUT-FIELD", "GAF record contains an unregistered input field", {},
				base + "/inputs/" + name });
			continue;
		}
		const VansGAFValueTypeDescriptor* valueType =
			m_Types->ResolveValueType(found->second->valueType);
		std::string validationError;
		if (!valueType || !valueType->validate(value, validationError))
			diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error,
				"GAF-INPUT-TYPE", validationError.empty()
					? "GAF record input has the wrong value type" : validationError,
				{}, base + "/inputs/" + name });
	}
	for (const VansGAFInputFieldDescriptor& field : schema->fields)
		if (field.required && !FindObjectField(inputs, field.name))
			diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error,
				"GAF-INPUT-REQUIRED", "Required GAF record input is missing", {},
				base + "/inputs/" + field.name });
	return diagnostics;
}

std::uint64_t VansGAFSchemaRegistry::Fingerprint() const
{
	std::vector<std::string> entries;
	for (const auto& [id, schema] : m_Schemas)
	{
		std::string entry = id;
		for (const VansGAFInputFieldDescriptor& field : schema.fields)
			entry += ":" + field.name + ":" + field.valueType +
				(field.required ? ":required" : ":optional");
		entries.push_back(std::move(entry));
	}
	std::sort(entries.begin(), entries.end());
	std::uint64_t hash = 1469598103934665603ull;
	for (const std::string& entry : entries) HashText(hash, entry);
	return hash;
}

bool VansRegisterCoreGAFTypes(VansGAFTypeRegistry& registry, std::string& error)
{
	const auto kind = [](VansSerializedValue::Kind expected)
	{
		return [expected](const VansSerializedValue& value, std::string&)
		{
			return value.kind == expected;
		};
	};
	const auto number = [](const VansSerializedValue& value, std::string&)
	{
		return value.kind == VansSerializedValue::Kind::Int ||
			value.kind == VansSerializedValue::Kind::Float;
	};
	if (!registry.RegisterValueType({ "Core.Value.Any", "Any",
		[](const VansSerializedValue&, std::string&) { return true; } }, error) ||
		!registry.RegisterValueType({ "Core.Value.Bool", "Bool",
			kind(VansSerializedValue::Kind::Bool) }, error) ||
		!registry.RegisterValueType({ "Core.Value.Int", "Int",
			kind(VansSerializedValue::Kind::Int) }, error) ||
		!registry.RegisterValueType({ "Core.Value.Float", "Float", number }, error) ||
		!registry.RegisterValueType({ "Core.Value.String", "String",
			kind(VansSerializedValue::Kind::String) }, error) ||
		!registry.RegisterValueType({ "Core.Value.Object", "Object",
			kind(VansSerializedValue::Kind::Object) }, error) ||
		!registry.RegisterValueType({ "Core.Value.Array", "Array",
			kind(VansSerializedValue::Kind::Array) }, error) ||
		!registry.RegisterValueType({ "Core.Value.Reference", "Reference",
			[](const VansSerializedValue& value, std::string&) { return IsReference(value); } }, error) ||
		!registry.RegisterValueType({ "Core.Value.Entity", "Entity",
			[](const VansSerializedValue& value, std::string&)
			{
				return value.kind == VansSerializedValue::Kind::Int ||
					value.kind == VansSerializedValue::Kind::Object;
			} }, error) ||
		!registry.RegisterValueType({ "Core.Value.TargetData", "Target Data",
			kind(VansSerializedValue::Kind::Object) }, error) ||
		!registry.RegisterValueType({ "Core.Value.Resource", "Resource",
			kind(VansSerializedValue::Kind::Object) }, error) ||
		!registry.RegisterValueType({ "Core.Value.StringOrArray", "String Or Array",
			[](const VansSerializedValue& value, std::string&)
			{
				return value.kind == VansSerializedValue::Kind::String ||
					value.kind == VansSerializedValue::Kind::Array;
			} }, error)) return false;

	for (const char* typeId : {
		"Core.Policy.Concurrency", "Core.Policy.Cancellation", "Core.Policy.Interruption",
		"Core.Policy.Completion", "Core.Policy.Clock", "Core.Policy.Trigger",
		"Core.Policy.InputBuffer", "Core.Policy.Failure", "Core.Policy.Budget" })
		if (!RegisterType(registry, VansGAFExtensionKind::Policy, typeId, error)) return false;
	for (const char* typeId : { "Core.Capability.Available" })
		if (!RegisterType(registry, VansGAFExtensionKind::Guard, typeId, error)) return false;
	for (const char* typeId : { "Core.ExternalCost.Commit" })
		if (!RegisterType(registry, VansGAFExtensionKind::Operation, typeId, error)) return false;
	for (const char* typeId : { "Core.Driver.Graph", "Core.Driver.Immediate" })
		if (!RegisterType(registry, VansGAFExtensionKind::Driver, typeId, error)) return false;
	for (const char* typeId : { "Core.Transition.Rule" })
		if (!RegisterType(registry, VansGAFExtensionKind::Transition, typeId, error)) return false;
	for (const char* typeId : {
		"Core.Level", "Core.Grant.Lifetime", "Core.Grant.Revoke" })
		if (!RegisterType(registry, VansGAFExtensionKind::Extension, typeId, error)) return false;
	return true;
}

bool VansRegisterCoreGAFSchemas(VansGAFSchemaRegistry& registry, std::string& error)
{
	using F = VansSerializedValue;
	for (VansGAFInputSchemaDescriptor descriptor : {
		Schema("Core.Policy.Concurrency", { Input("group", "Core.Value.String"),
			Input("mode", "Core.Value.String", false, F::String("Allow")),
			Input("limit", "Core.Value.Int", false, F::Int(1)),
			Input("queueTimeout", "Core.Value.Float", false, F::Float(0.0)) }),
		Schema("Core.Policy.Cancellation", { Input("cancellable", "Core.Value.Bool", false, F::Bool(true)) }),
		Schema("Core.Policy.Interruption", { Input("interruptible", "Core.Value.Bool", false, F::Bool(true)),
			Input("blockedActions", "Core.Value.Array", false, F::Array({})),
			Input("cancelActions", "Core.Value.Array", false, F::Array({})) }),
		Schema("Core.Policy.Completion", { Input("mode", "Core.Value.String", false, F::String("ExecutorResult")) }),
		Schema("Core.Policy.Clock", { Input("domain", "Core.Value.String", false, F::String("Game")) }),
		Schema("Core.Policy.Trigger", { Input("bindings", "Core.Value.Array", false, F::Array({})) }),
		Schema("Core.Policy.InputBuffer", { Input("enabled", "Core.Value.Bool", false, F::Bool(false)),
			Input("duration", "Core.Value.Float", false, F::Float(0.0)),
			Input("maximumEntries", "Core.Value.Int", false, F::Int(1)) }),
		Schema("Core.Policy.Failure", { Input("action", "Core.Value.Reference"),
			Input("errors", "Core.Value.Array", false, F::Array({})),
			Input("contextPatch", "Core.Value.Object", false, F::Object({})) }),
		Schema("Core.Policy.Budget", { Input("maximumTasks", "Core.Value.Int") }),
		Schema("Core.Capability.Available", { Input("capability", "Core.Value.String", true) }),
		Schema("Core.ExternalCost.Commit", {
			Input("operation", "Core.Value.String", true),
			Input("resource", "Core.Value.String", true),
			Input("amount", "Core.Value.Float", true),
			Input("payload", "Core.Value.Object") }),
		Schema("Core.Driver.Graph", { Input("graph", "Core.Value.Reference", true) }),
		Schema("Core.Driver.Immediate", {}),
		Schema("Core.Transition.Rule", { Input("name", "Core.Value.String", true),
			Input("trigger", "Core.Value.String", false, F::String("Event")),
			Input("event", "Core.Value.String"), Input("input", "Core.Value.String"),
			Input("target", "Core.Value.Reference", true), Input("minimumTime", "Core.Value.Float"),
			Input("maximumTime", "Core.Value.Float"), Input("priority", "Core.Value.Int"),
			Input("consume", "Core.Value.Bool"), Input("cancelSource", "Core.Value.Bool"),
			Input("requirements", "Core.Value.Object"),
			Input("contextPatch", "Core.Value.Object") }),
		Schema("Core.Level", { Input("value", "Core.Value.Float", true) }),
		Schema("Core.Grant.Lifetime", {
			Input("policy", "Core.Value.String", false, F::String("OwnerLifetime")) }),
		Schema("Core.Grant.Revoke", {
			Input("mode", "Core.Value.String", false, F::String("CancelRunning")) })
	})
		if (!registry.Register(std::move(descriptor), error)) return false;
	return true;
}

bool VansRegisterGameplayPrimitiveGAFTypes(
	VansGAFTypeRegistry& registry,
	std::string& error)
{
	for (const char* typeId : {
		"Gameplay.Tags.Require", "Gameplay.Tags.Block", "Gameplay.Attributes.Compare",
		"Core.Target.MinimumCount", "Core.Target.PrimaryRequired" })
		if (!RegisterType(registry, VansGAFExtensionKind::Guard, typeId, error)) return false;
	for (const char* typeId : {
		"Gameplay.Targeting.Resolve", "Gameplay.Attributes.Consume",
		"Gameplay.Cooldown.Apply", "Gameplay.Tags.Grant",
		"Gameplay.Effects.Apply", "Gameplay.Cue.Emit",
		"Targeting.Acquire.Owner", "Targeting.Acquire.PrimaryTarget",
		"Targeting.Filter.ValidEntity", "Targeting.Limit.Count",
		"Targeting.Lock.Entity" })
		if (!RegisterType(registry, VansGAFExtensionKind::Operation, typeId, error)) return false;
	for (const char* typeId : {
		"Gameplay.Input.Binding", "Gameplay.Tags.Dynamic", "Gameplay.Charges",
		"Gameplay.Effects.Initialize", "Gameplay.Attributes.Initialize",
		"Gameplay.Effect.AttributeModifier", "Gameplay.Effect.CueBinding",
		"Gameplay.Cue.Invoke" })
		if (!RegisterType(registry, VansGAFExtensionKind::Extension, typeId, error)) return false;
	return RegisterType(registry, VansGAFExtensionKind::Transition,
		"Core.Transition.Combo", error);
}

bool VansRegisterGameplayPrimitiveGAFSchemas(
	VansGAFSchemaRegistry& registry,
	std::string& error)
{
	using F = VansSerializedValue;
	for (VansGAFInputSchemaDescriptor descriptor : {
		Schema("Gameplay.Tags.Require", { Input("query", "Core.Value.Object", true) }),
		Schema("Gameplay.Tags.Block", { Input("query", "Core.Value.Object", true) }),
		Schema("Gameplay.Attributes.Compare", { Input("attribute", "Core.Value.String", true),
			Input("comparison", "Core.Value.String", false, F::String("GreaterOrEqual")),
			Input("value", "Core.Value.Float", true) }),
		Schema("Core.Target.MinimumCount", { Input("minimumTargets", "Core.Value.Int", false, F::Int(1)) }),
		Schema("Core.Target.PrimaryRequired", {}),
		Schema("Gameplay.Targeting.Resolve", { Input("asset", "Core.Value.Reference", true) }),
		Schema("Gameplay.Attributes.Consume", { Input("attribute", "Core.Value.String", true),
			Input("amount", "Core.Value.Float", true) }),
		Schema("Gameplay.Cooldown.Apply", { Input("duration", "Core.Value.Float", true),
			Input("tag", "Core.Value.String", true) }),
		Schema("Gameplay.Tags.Grant", { Input("tags", "Core.Value.Array", true) }),
		Schema("Gameplay.Effects.Apply", { Input("asset", "Core.Value.Reference", true),
			Input("removeOnEnd", "Core.Value.Bool", false, F::Bool(false)) }),
		Schema("Gameplay.Cue.Emit", { Input("assets", "Core.Value.Array", true) }),
		Schema("Targeting.Acquire.Owner", {}),
		Schema("Targeting.Acquire.PrimaryTarget", {}),
		Schema("Targeting.Filter.ValidEntity", {}),
		Schema("Targeting.Limit.Count", {
			Input("count", "Core.Value.Int", false, F::Int(1)) }),
		Schema("Targeting.Lock.Entity", {}),
		Schema("Gameplay.Input.Binding", {
			Input("binding", "Core.Value.String", true) }),
		Schema("Gameplay.Tags.Dynamic", {
			Input("tags", "Core.Value.Array", true) }),
		Schema("Gameplay.Charges", {
			Input("count", "Core.Value.Int", true) }),
		Schema("Gameplay.Effects.Initialize", {
			Input("asset", "Core.Value.Reference", true),
			Input("releasePolicy", "Core.Value.String", false, F::String("OnRevoke")) }),
		Schema("Gameplay.Attributes.Initialize", {
			Input("attribute", "Core.Value.String", true),
			Input("value", "Core.Value.Float", true),
			Input("releasePolicy", "Core.Value.String", false, F::String("OnRevoke")) }),
		Schema("Gameplay.Effect.AttributeModifier", {
			Input("attribute", "Core.Value.String", true),
			Input("operation", "Core.Value.String", false, F::String("Additive")),
			Input("magnitudeSource", "Core.Value.String", false, F::String("Fixed")),
			Input("magnitude", "Core.Value.Float", false, F::Float(0.0)),
			Input("priority", "Core.Value.Int", false, F::Int(0)),
			Input("setByCaller", "Core.Value.String"),
			Input("capturedAttribute", "Core.Value.String"),
			Input("capture", "Core.Value.String", false, F::String("Snapshot")),
			Input("contextPath", "Core.Value.String"),
			Input("targetMetric", "Core.Value.String", false, F::String("Count")),
			Input("randomMinimum", "Core.Value.Float", false, F::Float(0.0)),
			Input("randomMaximum", "Core.Value.Float", false, F::Float(1.0)),
			Input("coefficient", "Core.Value.Float", false, F::Float(1.0)),
			Input("preAdd", "Core.Value.Float", false, F::Float(0.0)),
			Input("postAdd", "Core.Value.Float", false, F::Float(0.0)) }),
		Schema("Gameplay.Effect.CueBinding", {
			Input("phase", "Core.Value.String", true),
			Input("assets", "Core.Value.Array", true) }),
		Schema("Gameplay.Cue.Invoke", {
			Input("capability", "Core.Value.String", true),
			Input("invoke", "Core.Value.String", true),
			Input("update", "Core.Value.String"),
			Input("release", "Core.Value.String"),
			Input("asset", "Core.Value.Reference"),
			Input("parameters", "Core.Value.Object", false, F::Object({})) }),
		Schema("Core.Transition.Combo", { Input("name", "Core.Value.String", true),
			Input("input", "Core.Value.String", true), Input("target", "Core.Value.Reference", true),
			Input("openTime", "Core.Value.Float"), Input("closeTime", "Core.Value.Float"),
			Input("priority", "Core.Value.Int"), Input("consume", "Core.Value.Bool"),
			Input("cancelSource", "Core.Value.Bool"),
			Input("requirements", "Core.Value.Object"), Input("contextPatch", "Core.Value.Object") })
	})
		if (!registry.Register(std::move(descriptor), error)) return false;
	return true;
}

}
