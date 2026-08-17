#pragma once

#include "../RuntimeCore/VansStableIdentity.h"
#include "../TimelineCore/VansTimelineTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
enum class VansPayloadFieldFlags : std::uint8_t { None = 0, Sensitive = 1 };
struct VansPayloadFieldSchema
{
	VansTimelineFieldId id;
	std::string name;
	VansTimelineValueType type = VansTimelineValueType::Null;
	VansTimelineValue defaultValue;
	VansPayloadFieldFlags flags = VansPayloadFieldFlags::None;
	bool required = false;
};
struct VansPayloadSchema
{
	VansTimelinePayloadTypeId typeId;
	std::string stableName;
	std::uint32_t maximumBytes = 4096;
	bool editorSafe = false;
	bool allowAdditionalFields = false;
	std::vector<VansPayloadFieldSchema> fields;
};
class VansPayloadSchemaRegistry
{
public:
	bool Register(VansPayloadSchema schema, std::string& error);
	bool Seal(std::string& error);
	const VansPayloadSchema* Resolve(VansTimelinePayloadTypeId typeId) const;
	bool Validate(VansTimelinePayloadTypeId typeId, const VansSerializedValue& payload,
		std::string& error) const;
	bool IsSealed() const { return m_Sealed; }
private:
	bool m_Sealed = false;
	std::vector<VansPayloadSchema> m_Schemas;
	std::unordered_map<VansTimelinePayloadTypeId, std::uint32_t> m_ByType;
};
}
