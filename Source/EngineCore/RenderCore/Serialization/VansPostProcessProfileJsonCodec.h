#pragma once

#include "../VansPostProcessProfile.h"
#include "../../AssetCore/Serialization/VansSerializedValue.h"

#include <filesystem>
#include <string>

namespace VansGraphics
{
class VansPostProcessProfileJsonCodec
{
public:
	static Vans::VansSerializedValue Encode(const VansPostProcessProfile& profile);
	static bool Decode(
		const Vans::VansSerializedValue& root,
		const std::filesystem::path& filePath,
		VansPostProcessProfile& profile,
		std::string& error);
};
}
