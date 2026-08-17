#pragma once

#include "../VansSkinProfile.h"
#include "../VansSkinProfileJson.h"

#include <filesystem>
#include <string>

namespace Vans
{
class VansSkinProfileJsonCodec
{
public:
	static SkinProfileJson Encode(const VansSkinProfile& profile);
	static bool Decode(
		const SkinProfileJson& root,
		const std::filesystem::path& filePath,
		VansSkinProfile& profile,
		std::string& error);
};
}
