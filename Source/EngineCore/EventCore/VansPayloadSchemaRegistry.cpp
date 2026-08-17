#include "VansPayloadSchemaRegistry.h"

#include "../TimelineCore/VansTimelineSourceSchema.h"

#include <unordered_set>

namespace Vans
{
bool VansPayloadSchemaRegistry::Register(VansPayloadSchema schema, std::string& error)
{
	error.clear();
	if (m_Sealed) { error = "Event.PayloadRegistrySealed"; return false; }
	if (schema.stableName.empty() || schema.maximumBytes == 0)
	{ error = "Event.PayloadSchemaInvalid"; return false; }
	const VansTimelinePayloadTypeId expected = VansMakeStableId<VansTimelinePayloadTypeTag>(schema.stableName);
	if (!schema.typeId) schema.typeId = expected;
	if (schema.typeId != expected) { error = "Event.PayloadTypeHashMismatch"; return false; }
	if (m_ByType.find(schema.typeId) != m_ByType.end()) { error = "Event.PayloadTypeDuplicate"; return false; }
	std::unordered_set<VansTimelineFieldId> fieldIds;
	std::unordered_set<std::string> fieldNames;
	for (const VansPayloadFieldSchema& field : schema.fields)
	{
		if (!field.id || field.name.empty() ||
			field.id != VansMakeStableId<VansTimelineFieldTag>(field.name) ||
			!fieldIds.insert(field.id).second || !fieldNames.insert(field.name).second)
		{ error = "Event.PayloadFieldSchemaInvalid"; return false; }
	}
	m_ByType.emplace(schema.typeId, static_cast<std::uint32_t>(m_Schemas.size()));
	m_Schemas.push_back(std::move(schema));
	return true;
}
bool VansPayloadSchemaRegistry::Seal(std::string& error) { error.clear(); m_Sealed = true; return true; }
const VansPayloadSchema* VansPayloadSchemaRegistry::Resolve(VansTimelinePayloadTypeId typeId) const
{
	const auto found = m_ByType.find(typeId); return found == m_ByType.end() ? nullptr : &m_Schemas[found->second];
}
bool VansPayloadSchemaRegistry::Validate(
	VansTimelinePayloadTypeId typeId, const VansSerializedValue& payload, std::string& error) const
{
	error.clear();
	const VansPayloadSchema* schema = Resolve(typeId);
	if (!schema) { error = "Event.PayloadSchemaMissing"; return false; }
	if (payload.kind != VansSerializedValue::Kind::Object) { error = "Event.PayloadMustBeObject"; return false; }
	std::size_t payloadBytes = 0;
	auto measure = [&](auto&& self, const VansSerializedValue& value) -> void
	{
		payloadBytes += sizeof(value.kind) + value.stringValue.size();
		for (const auto& item : value.arrayItems) self(self, item);
		for (const auto& [name, field] : value.objectFields)
		{ payloadBytes += name.size(); self(self, field); }
	};
	measure(measure, payload);
	if (payloadBytes > schema->maximumBytes) { error = "Event.PayloadTooLarge"; return false; }
	for (const VansPayloadFieldSchema& field : schema->fields)
	{
		const VansSerializedValue* encoded = VansTimelineFindSourceField(payload, field.name);
		if (!encoded)
		{
			if (field.required) { error = "Event.PayloadFieldMissing:" + field.name; return false; }
			continue;
		}
		VansTimelineValue decoded;
		if (!VansTimelineDecodeSourceValue(*encoded, field.type, decoded))
		{ error = "Event.PayloadFieldTypeMismatch:" + field.name; return false; }
	}
	if (!schema->allowAdditionalFields)
		for (const auto& [name, value] : payload.objectFields)
		{
			(void)value;
			bool declared = false;
			for (const VansPayloadFieldSchema& field : schema->fields)
				if (field.name == name) { declared = true; break; }
			if (!declared) { error = "Event.PayloadFieldUnknown:" + name; return false; }
		}
	return true;
}
}
