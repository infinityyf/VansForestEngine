#pragma once

#include "VansTimelineTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
struct VansTimelinePayloadFieldSchema
{
	VansTimelineFieldId id;
	std::string name;
	VansTimelineValueType type = VansTimelineValueType::Null;
	bool required = false;
};

struct VansTimelinePayloadSchema
{
	VansTimelinePayloadTypeId typeId;
	std::string stableName;
	std::uint32_t maximumBytes = 4096;
	bool allowAdditionalFields = false;
	std::vector<VansTimelinePayloadFieldSchema> fields;
};

class VansTimelinePayloadSchemaRegistry
{
public:
	bool Register(VansTimelinePayloadSchema schema, std::string& error);
	bool Seal(bool allowEmpty, std::string& error);
	const VansTimelinePayloadSchema* Resolve(VansTimelinePayloadTypeId typeId) const;
	bool Validate(VansTimelinePayloadTypeId typeId, const VansSerializedValue& payload,
		std::string& error) const;
	bool IsSealed() const { return m_Sealed; }

private:
	bool m_Sealed = false;
	std::vector<VansTimelinePayloadSchema> m_Schemas;
	std::unordered_map<VansTimelinePayloadTypeId, std::uint32_t> m_ByType;
};
}
