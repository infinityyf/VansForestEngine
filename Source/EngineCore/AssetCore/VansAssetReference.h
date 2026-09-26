#pragma once

#include "Serialization/VansSerializedValue.h"
#include "VansAssetGuid.h"

#include <optional>

namespace Vans
{
inline bool TryReadOptionalAssetGuidReference(
	const VansSerializedValue& reference,
	std::optional<VansAssetGuid>& guid)
{
	guid.reset();
	if (reference.kind != VansSerializedValue::Kind::Object ||
		reference.objectFields.size() != 1u)
	{
		return false;
	}

	const auto& [fieldName, fieldValue] = reference.objectFields.front();
	if (fieldName != "guid" || fieldValue.kind != VansSerializedValue::Kind::String)
		return false;
	if (fieldValue.stringValue.empty())
		return true;

	VansAssetGuid parsed;
	if (!VansAssetGuid::TryParse(fieldValue.stringValue, parsed))
		return false;
	guid = parsed;
	return true;
}

inline bool TryReadAssetGuidReference(
	const VansSerializedValue& reference,
	VansAssetGuid& guid)
{
	std::optional<VansAssetGuid> optionalGuid;
	if (!TryReadOptionalAssetGuidReference(reference, optionalGuid) || !optionalGuid)
		return false;
	guid = *optionalGuid;
	return true;
}
}
