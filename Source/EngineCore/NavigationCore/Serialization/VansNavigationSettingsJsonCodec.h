#pragma once

#include "../VansNavigationTypes.h"
#include "../../AssetCore/Serialization/VansSerializedValue.h"

namespace Vans
{
class VansNavigationSettingsJsonCodec
{
public:
	static bool Decode(const VansSerializedValue& root,
		VansNavigationSettings& settings, std::string& error);
	static VansSerializedValue Encode(const VansNavigationSettings& settings);
};
}
