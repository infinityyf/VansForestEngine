#pragma once

#include "../VansPostProcessProfile.h"
#include "../VansPostProcessProfileJson.h"

#include <filesystem>
#include <string>

namespace VansGraphics
{
class VansPostProcessProfileJsonCodec
{
public:
	static PostProcessProfileJson Encode(const VansPostProcessProfile& profile);
	static bool Decode(
		const PostProcessProfileJson& root,
		const std::filesystem::path& filePath,
		VansPostProcessProfile& profile,
		std::string& error);
};
}
