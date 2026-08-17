#pragma once

#include "VansGameplaySchemaTypes.h"
#include "../AssetCore/Serialization/VansSerializedValue.h"

#include <optional>
#include <string_view>

namespace Vans
{
class VansGameplayActionHostAuthoring
{
public:
	static VansSerializedValue CreateDefaultData();
	static std::optional<VansSerializedValue> CreateDefaultArrayElement(std::string_view fieldName);
	static VansGameplayDiagnostics Validate(const VansSerializedValue& data);
};
}
